#include "mod_plugins.h"
#include "tomba2_debug_menu_payload.h"

#include <stdio.h>

/*
 * Tomba 2 developer debug menu.
 *
 * The menu is guest MIPS code: ~6.7 KB of R3000A instructions plus a string
 * table, installed into free kernel RAM at 0x8000C000, reached by five guarded
 * detour words inside the game's runtime-loaded gameplay overlay. It renders
 * through the game's own text routines, so it looks and behaves like a
 * first-party debug build rather than a host overlay.
 *
 * Why a guest payload instead of a native C reimplementation: the menu calls
 * the game's own print/format/transition routines (0x800788AC, 0x80080F6C,
 * 0x8009B0C0) and reads live overlay state. A trusted plugin cannot call guest
 * functions, so a native rewrite would have to reproduce Tomba 2's text
 * pipeline. Installing the authored payload keeps one implementation.
 *
 * Why this is not a cheat interpreter: the payload and its detours are baked
 * into the build by tools/gen_debug_menu_payload.py; the package archive
 * carries metadata only, exactly like every other trusted plugin here.
 *
 * Placement notes:
 *   0x8000C000..0x8000DAD7  payload (code, then string/format table)
 *   0x8000DAE0..0x8000DB07  menu state block ('TDBG' magic drives self-init)
 * Both lie in the BIOS kernel window, above the kernel's own data structures
 * and below the boot EXE at 0x80010000. The runtime treats that window as
 * dirty-tracked executable RAM, so psx_mod_write_code_word() is sufficient to
 * make the payload dispatchable (interpreted first, then natively compiled by
 * the overlay cache like any other runtime-loaded code).
 *
 * All four detour sites live above the boot EXE's text (0x80038800), i.e. in
 * overlay code that is re-streamed from disc on area transitions. Re-asserting
 * them on guest VBlank is therefore load-bearing, not belt-and-braces: a fresh
 * overlay load restores the stock words.
 */

#define PLUGIN_ID "tomba2.debug.menu"

/* Report state so a mismatch is loud once rather than silent forever. */
enum {
    REPORT_NONE = 0,
    REPORT_INSTALLED,
    REPORT_UNRECOGNISED_SITE,
    REPORT_PAYLOAD_REGION_BUSY
};

static int s_report = REPORT_NONE;
static int s_payload_written = 0;

static void report_once(int what, uint32_t address, uint32_t found,
                        uint32_t expected) {
    if (s_report == what) return;
    s_report = what;
    switch (what) {
        case REPORT_INSTALLED:
            fprintf(stderr,
                    "[" PLUGIN_ID "] installed: %u payload words at "
                    "0x%08X, %u detour words\n",
                    (unsigned)TOMBA2_DEBUG_MENU_PAYLOAD_WORDS,
                    (unsigned)TOMBA2_DEBUG_MENU_PAYLOAD_BASE,
                    (unsigned)TOMBA2_DEBUG_MENU_HOOK_COUNT);
            break;
        case REPORT_UNRECOGNISED_SITE:
            fprintf(stderr,
                    "[" PLUGIN_ID "] NOT installed: detour site 0x%08X holds "
                    "0x%08X, expected stock 0x%08X. Refusing to patch an "
                    "overlay this build does not recognise.\n",
                    (unsigned)address, (unsigned)found, (unsigned)expected);
            break;
        case REPORT_PAYLOAD_REGION_BUSY:
            fprintf(stderr,
                    "[" PLUGIN_ID "] NOT installed: kernel RAM 0x%08X holds "
                    "0x%08X before install; the payload region is in use.\n",
                    (unsigned)address, (unsigned)found);
            break;
        default:
            break;
    }
}

/*
 * Every detour site must read as either its stock word or its patched word
 * before anything is written. Patching only some of them would leave the
 * 0x8007A904/0x8007A908 jump and its delay slot inconsistent, and a site that
 * matches neither means the resident overlay is not the one this payload was
 * authored against.
 */
static int detour_sites_recognised(void) {
    for (unsigned i = 0; i < TOMBA2_DEBUG_MENU_HOOK_COUNT; i++) {
        const Tomba2DebugMenuHook* hook = &kTomba2DebugMenuHooks[i];
        const uint32_t live = psx_mod_read_word(hook->address);
        if (live == hook->stock || live == hook->patched) continue;
        report_once(REPORT_UNRECOGNISED_SITE, hook->address, live,
                    hook->stock);
        return 0;
    }
    return 1;
}

/*
 * The payload region must be free before the first write. The kernel leaves
 * 0x8000C000 upward untouched, but verifying it means a future BIOS or HLE
 * kernel that does use the region fails loudly instead of trading corruption
 * for a debug menu.
 */
static int payload_region_free(void) {
    for (uint32_t address = TOMBA2_DEBUG_MENU_PAYLOAD_BASE;
         address < TOMBA2_DEBUG_MENU_PAYLOAD_END + 0x30u; address += 4u) {
        const uint32_t live = psx_mod_read_word(address);
        if (live == 0u) continue;
        report_once(REPORT_PAYLOAD_REGION_BUSY, address, live, 0u);
        return 0;
    }
    return 1;
}

static int payload_resident(void) {
    return psx_mod_read_word(TOMBA2_DEBUG_MENU_PAYLOAD_BASE) ==
               kTomba2DebugMenuPayload[0] &&
           psx_mod_read_word(TOMBA2_DEBUG_MENU_PAYLOAD_END - 4u) ==
               kTomba2DebugMenuPayload[TOMBA2_DEBUG_MENU_PAYLOAD_WORDS - 1u];
}

static void write_payload(void) {
    uint32_t offset;

    /* Start from a known state block so the payload's 'TDBG' self-init runs
     * against zeroes rather than whatever the kernel left behind. */
    for (offset = 0; offset < TOMBA2_DEBUG_MENU_STATE_SIZE; offset += 4u)
        psx_mod_write_word(TOMBA2_DEBUG_MENU_STATE_BASE + offset, 0u);

    for (offset = 0; offset < TOMBA2_DEBUG_MENU_PAYLOAD_WORDS; offset++) {
        const uint32_t address =
            TOMBA2_DEBUG_MENU_PAYLOAD_BASE + (offset * 4u);
        if (address < TOMBA2_DEBUG_MENU_CODE_END) {
            /* Executable: routes the address through the runtime's
             * dirty-RAM/overlay path so a restored save state cannot leave a
             * stale compiled body behind it. */
            psx_mod_write_code_word(address, kTomba2DebugMenuPayload[offset]);
        } else {
            psx_mod_write_word(address, kTomba2DebugMenuPayload[offset]);
        }
    }
    s_payload_written = 1;
}

static void enforce_detours(void) {
    for (unsigned i = 0; i < TOMBA2_DEBUG_MENU_HOOK_COUNT; i++) {
        const Tomba2DebugMenuHook* hook = &kTomba2DebugMenuHooks[i];
        if (psx_mod_read_word(hook->address) == hook->patched) continue;
        psx_mod_write_code_word(hook->address, hook->patched);
    }
}

/*
 * The gameplay module that owns the detour sites must be fully resident before
 * anything is written: outside gameplay those addresses hold unrelated code, a
 * partially DMA'd overlay, or nothing at all.
 *
 * The cheat list gated on a game function pointer parked in BIOS kernel RAM at
 * 0x8000B080. That is a retail-SCPH1001 kernel layout detail rather than a
 * property of the game — under the OpenBIOS kernel this runtime ships, the same
 * pointer is stored at 0x80008558 and 0x8000B080 reads zero forever, so the
 * verbatim guard would never fire and the menu would never install. Signing the
 * callees is BIOS-independent and proves more: the exact routines the payload
 * branches into are the ones it was authored against.
 */
static int module_resident(void) {
    for (unsigned i = 0; i < TOMBA2_DEBUG_MENU_SIGNATURE_COUNT; i++) {
        const Tomba2DebugMenuSignature* entry = &kTomba2DebugMenuSignature[i];
        if (psx_mod_read_word(entry->address) != entry->word) return 0;
    }
    return 1;
}

static void tomba2_debug_menu_vblank(void) {
    if (!psx_mod_game_started()) return;
    if (!module_resident()) return;
    if (!detour_sites_recognised()) return;

    if (!payload_resident()) {
        if (!s_payload_written && !payload_region_free()) return;
        write_payload();
    }
    enforce_detours();
    report_once(REPORT_INSTALLED, 0u, 0u, 0u);
}

PSX_MOD_CONSTRUCTOR(tomba2_register_debug_menu_plugin) {
    (void)psx_mod_register_vblank_plugin(PLUGIN_ID,
                                         tomba2_debug_menu_vblank);
}
