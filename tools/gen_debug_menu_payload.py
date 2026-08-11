#!/usr/bin/env python3
"""Generate the Tomba 2 debug-menu guest payload header from its cheat source.

The debug menu ships as a block of MIPS R3000A code + string data that is
installed into free kernel RAM at 0x8000C000, plus a handful of guarded
call-site detours in the game's runtime-loaded overlays. The authoritative
source is the GameShark/DuckStation-extension cheat list in
`mods/sources/tomba2_debug_menu.cht`; this script turns it into a C header so
the trusted mod plugin can install it without a cheat interpreter.

Supported cheat opcodes (DuckStation `InstructionCode`):
  0x90  ExtConstantWrite32  -- payload word / detour word
  0xA4  ExtSkipIfNotEqual32 -- whole-list guard (module-loaded check)

Any other opcode is an error rather than a silent skip: the header must be a
complete, faithful transcription or the installed menu is not the menu that
was authored.

Usage:
  py -3 tools/gen_debug_menu_payload.py \
      mods/sources/tomba2_debug_menu.cht \
      src/mods/tomba2_debug_menu_payload.h
"""

from __future__ import annotations

import re
import sys

LINE = re.compile(r"^([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s*$")

# Whose work the payload is. Emitted into the generated header so the credit
# travels with the bytes rather than living only in a README.
PAYLOAD_AUTHOR = ("Discord user unicorngoulash, on behalf of the "
                  "Tomba Club community")

# The payload proper is one contiguous run in free kernel RAM. Everything else
# the cheat writes is a detour into already-loaded game code.
PAYLOAD_BASE = 0x8000C000

# Split point between executable words and the string/format-literal table.
# Determined by disassembly: the last instruction is at 0x8000D764.
PAYLOAD_CODE_END = 0x8000D768

# Stock words at each detour site, derived from the trampolines the payload
# itself executes before/after the game's original code:
#
#   0x80050CB0  the trampoline at 0x8000C000 opens with `jal 0x800788AC`
#   0x80050CC0  the trampoline at 0x8000C02C ends with `jal 0x80080F6C`
#   0x8007A904  the bypass at 0x8000C098 replays `lui $v0,0x8010`
#   0x8007A908  ... and `lw $a0,-0x4E98($v0)` before rejoining at 0x8007A90C
#   0x80108B60  the trampoline at 0x8000CA40 tail-jumps to 0x8007A904
#
# The plugin refuses to patch a site whose live word is neither the stock nor
# the patched value, so a wrong derivation fails closed instead of corrupting
# an overlay.
STOCK_WORDS = {
    0x80050CB0: 0x0C01E22B,
    0x80050CC0: 0x0C0203DB,
    0x8007A904: 0x3C028010,
    0x8007A908: 0x8C44B168,
    0x80108B60: 0x0C01EA41,
}

# Resident-module signature: the first word of every game routine the payload
# calls or jumps into. Measured live in gameplay (2026-08-10, SCUS-94454,
# psxrecomp a155eef).
#
# This REPLACES the cheat list's own 0xA4 guard, which tested a game function
# pointer parked in BIOS kernel RAM at 0x8000B080. That address is a property of
# the retail SCPH1001 kernel's data layout, not of the game: under the OpenBIOS
# kernel psxrecomp ships, the same pointer lands at 0x80008558 and 0x8000B080
# reads zero forever, so the verbatim guard never fires. Signing the callees
# themselves is BIOS-independent and strictly stronger — it proves the exact
# code the payload will branch into is resident, not merely that some kernel
# slot was populated.
CALLEE_SIGNATURE = {
    0x800788AC: 0x27BDFFE8,  # per-frame routine the 0x80050CB0 trampoline wraps
    0x80080F6C: 0x3C02800A,  # tail call from the 0x80050CC0 trampoline
    0x8009B0C0: 0xAFA50004,  # string formatter used by the menu renderer
    0x800896E0: 0x3C02800C,  # the routine the cheat's kernel-pointer guard named
}


def parse(path: str):
    writes: dict[int, int] = {}
    guards: list[tuple[int, int]] = []
    title = ""
    for lineno, raw in enumerate(open(path, encoding="utf-8"), 1):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            if not title:
                title = line.lstrip("#").strip()
            continue
        match = LINE.match(line)
        if not match:
            raise SystemExit(f"{path}:{lineno}: not a cheat word pair: {line!r}")
        addr_field = int(match.group(1), 16)
        value = int(match.group(2), 16)
        opcode = addr_field >> 24
        address = 0x80000000 | (addr_field & 0x00FFFFFF)
        if opcode == 0x90:
            if address in writes:
                raise SystemExit(f"{path}:{lineno}: duplicate write to {address:#010x}")
            writes[address] = value
        elif opcode == 0xA4:
            guards.append((address, value))
        else:
            raise SystemExit(
                f"{path}:{lineno}: unsupported cheat opcode {opcode:#04x}")
    if len(guards) != 1:
        raise SystemExit(f"{path}: expected exactly one 0xA4 list guard")
    return title, writes, guards[0]


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        raise SystemExit(__doc__)
    source, out_path = argv[1], argv[2]
    title, writes, guard = parse(source)

    payload_end = PAYLOAD_BASE
    while payload_end in writes:
        payload_end += 4
    payload = [writes[a] for a in range(PAYLOAD_BASE, payload_end, 4)]

    hooks = sorted(a for a in writes if not PAYLOAD_BASE <= a < payload_end)
    missing = [f"{a:#010x}" for a in hooks if a not in STOCK_WORDS]
    if missing:
        raise SystemExit(
            "no stock word recorded for detour site(s): " + ", ".join(missing))
    if not payload:
        raise SystemExit("no payload words found at 0x8000C000")

    lines: list[str] = []
    add = lines.append
    add("/* Generated by tools/gen_debug_menu_payload.py -- do not edit.")
    add(f" * Source: {source}")
    add(f" * Title:  {title}")
    add(f" * Author: {PAYLOAD_AUTHOR}")
    add(" */")
    add("#pragma once")
    add("")
    add("#include <stdint.h>")
    add("")
    add(f"#define TOMBA2_DEBUG_MENU_PAYLOAD_BASE 0x{PAYLOAD_BASE:08X}u")
    add(f"#define TOMBA2_DEBUG_MENU_PAYLOAD_END 0x{payload_end:08X}u")
    add(f"#define TOMBA2_DEBUG_MENU_CODE_END 0x{PAYLOAD_CODE_END:08X}u")
    add(f"#define TOMBA2_DEBUG_MENU_PAYLOAD_WORDS {len(payload)}u")
    add("")
    add("/* The cheat list's own whole-list guard, recorded for provenance only.")
    add(" * It reads a game function pointer out of BIOS kernel RAM, which is a")
    add(" * retail-SCPH1001 layout detail: under OpenBIOS the same pointer lives")
    add(" * elsewhere and this address stays zero. Not used as a gate. */")
    add(f"#define TOMBA2_DEBUG_MENU_CHEAT_GUARD_ADDRESS 0x{guard[0]:08X}u")
    add(f"#define TOMBA2_DEBUG_MENU_CHEAT_GUARD_VALUE 0x{guard[1]:08X}u")
    add("")
    add("/* Resident-module signature used instead: the first word of every game")
    add(" * routine the payload branches into. */")
    add("typedef struct Tomba2DebugMenuSignature {")
    add("    uint32_t address;")
    add("    uint32_t word;")
    add("} Tomba2DebugMenuSignature;")
    add("")
    add(f"#define TOMBA2_DEBUG_MENU_SIGNATURE_COUNT {len(CALLEE_SIGNATURE)}u")
    add("static const Tomba2DebugMenuSignature kTomba2DebugMenuSignature[] = {")
    for address in sorted(CALLEE_SIGNATURE):
        add(f"    {{ 0x{address:08X}u, 0x{CALLEE_SIGNATURE[address]:08X}u }},")
    add("};")
    add("")
    add("/* Menu state block, immediately after the payload. Byte 0 is the")
    add(" * open/closed flag; the 'TDBG' magic at +0x0C drives self-init. */")
    add(f"#define TOMBA2_DEBUG_MENU_STATE_BASE 0x{payload_end + 8:08X}u")
    add("#define TOMBA2_DEBUG_MENU_STATE_SIZE 0x28u")
    add("#define TOMBA2_DEBUG_MENU_STATE_MAGIC_OFFSET 0x0Cu")
    add("#define TOMBA2_DEBUG_MENU_STATE_MAGIC 0x54444247u /* 'TDBG' */")
    add("")
    add("typedef struct Tomba2DebugMenuHook {")
    add("    uint32_t address;")
    add("    uint32_t stock;")
    add("    uint32_t patched;")
    add("} Tomba2DebugMenuHook;")
    add("")
    add(f"#define TOMBA2_DEBUG_MENU_HOOK_COUNT {len(hooks)}u")
    add("static const Tomba2DebugMenuHook kTomba2DebugMenuHooks[] = {")
    for address in hooks:
        add(f"    {{ 0x{address:08X}u, 0x{STOCK_WORDS[address]:08X}u, "
            f"0x{writes[address]:08X}u }},")
    add("};")
    add("")
    add("static const uint32_t kTomba2DebugMenuPayload[] = {")
    for index in range(0, len(payload), 6):
        chunk = payload[index:index + 6]
        add("    " + " ".join(f"0x{word:08X}u," for word in chunk))
    add("};")
    add("")

    with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))

    print(f"{out_path}: {len(payload)} payload words "
          f"(0x{PAYLOAD_BASE:08X}..0x{payload_end - 1:08X}), "
          f"{len(hooks)} detour words")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
