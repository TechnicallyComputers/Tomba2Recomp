#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mod_plugins.h"
#include "sio.h"

static uint8_t s_ram[2 * 1024 * 1024];
static uint8_t s_scratch[1024];
static uint16_t s_raw_buttons = 0xFFFFu;
static uint8_t s_sticks[4] = {0x80u, 0x80u, 0x80u, 0x80u};
static uint32_t s_guest_hook_address;
static PSXModGuestFunctionCallback s_guest_hook;
static int s_game_started = 1;
static int s_failures;

static uint8_t* guest_pointer(uint32_t address) {
    uint32_t physical = address & 0x1FFFFFFFu;
    if (physical < (uint32_t)sizeof(s_ram))
        return &s_ram[physical];
    if (physical >= 0x1F800000u &&
        physical < 0x1F800000u + (uint32_t)sizeof(s_scratch)) {
        return &s_scratch[physical - 0x1F800000u];
    }
    return NULL;
}

int psx_mod_register_activation_plugin(
    const char* id, PSXModActivationCallback callback) {
    (void)id;
    (void)callback;
    return 1;
}

int psx_mod_register_vblank_plugin(
    const char* id, PSXModVBlankCallback callback) {
    (void)id;
    (void)callback;
    return 1;
}

int psx_mod_register_guest_function_hook(
    uint32_t function_address, PSXModGuestFunctionCallback callback) {
    s_guest_hook_address = function_address;
    s_guest_hook = callback;
    return function_address != 0 && callback != NULL;
}

int psx_mod_game_started(void) {
    return s_game_started;
}

uint8_t psx_mod_read_byte(uint32_t address) {
    uint8_t* byte = guest_pointer(address);
    return byte ? *byte : 0;
}

void psx_mod_write_byte(uint32_t address, uint8_t value) {
    uint8_t* byte = guest_pointer(address);
    if (byte)
        *byte = value;
}

int psx_mod_option_value(
    const char* package_id, const char* feature_id,
    const char* option_id, char* out, uint32_t out_size) {
    (void)package_id;
    (void)feature_id;
    (void)option_id;
    if (out && out_size)
        out[0] = '\0';
    return 0;
}

uint16_t sio_get_pad_buttons_slot(int slot) {
    return slot == 0 ? s_raw_buttons : 0xFFFFu;
}

void sio_get_pad_sticks(int slot, uint8_t out[4]) {
    if (slot == 0)
        memcpy(out, s_sticks, sizeof(s_sticks));
    else
        memset(out, 0x80, sizeof(s_sticks));
}

/* Keep the production implementation and this state-machine regression test in
 * one translation unit so the test exercises the actual internal transitions. */
#include "../src/mods/tomba2_first_person_plugin.c"

static void fail(const char* message) {
    fprintf(stderr, "FAIL: %s\n", message);
    ++s_failures;
}

static void check(int condition, const char* message) {
    if (!condition)
        fail(message);
}

static void store_u16(uint32_t address, uint16_t value) {
    uint8_t* p = guest_pointer(address);
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void store_u32(uint32_t address, uint32_t value) {
    uint8_t* p = guest_pointer(address);
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint16_t load_u16(uint32_t address) {
    uint8_t* p = guest_pointer(address);
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t load_u32(uint32_t address) {
    uint8_t* p = guest_pointer(address);
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

static void frame(uint16_t active_low_buttons) {
    s_raw_buttons = active_low_buttons;
    tomba2_first_person_vblank();
}

static void set_sticks(uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry) {
    s_sticks[0] = lx;
    s_sticks[1] = ly;
    s_sticks[2] = rx;
    s_sticks[3] = ry;
}

static void initialize_gameplay(void) {
    memset(s_ram, 0, sizeof(s_ram));
    memset(s_scratch, 0, sizeof(s_scratch));
    s_raw_buttons = 0xFFFFu;
    set_sticks(0x80u, 0x80u, 0x80u, 0x80u);
    s_enabled = 0;
    s_requested_enabled = 0;
    s_render_override_ready = 0;
    s_heading_initialized = 0;
    s_look_pitch = 0;
    s_path_heading_valid = 0;
    s_heading = 0;
    s_toggle_latched = 0;
    s_down_latched = 0;
    s_reverse_turn_input_pending = 0;
    s_view_state_saved = 0;
    s_transition_queued_announced = 0;
    s_input_hook_installed = 0;

    psx_mod_write_byte(CAMERA_STATE, 1);
    psx_mod_write_byte(PLAYER_DIRECTION_FLAGS, 0);
    psx_mod_write_byte(SCRIPTED_INPUT_STATE, 0);
    store_u32(PLAYER_X, 100u * FIXED_ONE);
    store_u32(PLAYER_Y, 1000u * FIXED_ONE);
    store_u32(PLAYER_Z, 200u * FIXED_ONE);
    store_u16(PLAYER_RENDER_HEADING, 0);
    store_u16(PLAYER_PATH_HEADING, 0);
    store_u16(NATIVE_DIRECTION_ZERO, PSX_PAD_RIGHT);
    store_u16(NATIVE_DIRECTION_ONE, PSX_PAD_LEFT);
    store_u32(INPUT_POLL_CALL, INPUT_POLL_CALL_STOCK);
    store_u32(INPUT_POLL_DELAY, INPUT_POLL_DELAY_STOCK);
}

int main(void) {
    int i;
    int heading_before;
    int32_t player_x_before;
    uint8_t stock_view_first;

    initialize_gameplay();
    tomba2_first_person_activate();
    check(s_guest_hook_address == GAMEPLAY_RENDER_ENTRY &&
              s_guest_hook == install_render_view,
          "activation registers the render-only guest boundary");

    store_u32(INPUT_POLL_CALL, INPUT_POLL_CALL_PATCHED);
    store_u32(INPUT_POLL_DELAY, INPUT_POLL_DELAY_PATCHED);
    psx_mod_write_byte(CAMERA_MODE, CAMERA_MODE_FREE_TARGET);
    frame(0xFFFFu);
    check(load_u32(INPUT_POLL_CALL) == INPUT_POLL_CALL_STOCK &&
              load_u32(INPUT_POLL_DELAY) == INPUT_POLL_DELAY_STOCK &&
              (psx_mod_read_byte(CAMERA_MODE) & CAMERA_MODE_MASK) == 0,
          "disabled startup recovers stock state from a first-person savestate");

    check(!s_enabled, "feature starts in stock third-person mode");
    check(load_u32(INPUT_POLL_CALL) == INPUT_POLL_CALL_STOCK,
          "stock mode leaves Tomba's input poll untouched");

    frame((uint16_t)~PSX_PAD_SELECT);
    check(s_enabled && s_render_override_ready,
          "Select enters first-person without rejecting an active game state");
    check(load_u32(INPUT_POLL_CALL) == INPUT_POLL_CALL_PATCHED &&
              load_u32(INPUT_POLL_DELAY) == INPUT_POLL_DELAY_PATCHED,
          "entry installs the guarded post-poll input hook");
    check(load_u16(INPUT_OVERRIDE_WORD) == 0,
          "Select is consumed on first-person entry");
    check((psx_mod_read_byte(CAMERA_MODE) & CAMERA_MODE_MASK) == 0,
          "first-person leaves the stock gameplay camera mode untouched");
    check(s_render_target_x - s_render_eye_x ==
              DEFAULT_LOOK_DISTANCE * FIXED_ONE,
          "entry looks along Tomba's +X authored facing, not off-path");

    player_x_before = (int32_t)load_u32(PLAYER_X);
    for (i = 0; i < (int)VIEW_STATE_SIZE; ++i) {
        psx_mod_write_byte(
            VIEW_STATE_START + (uint32_t)i,
            (uint8_t)(i * 3 + 1));
    }
    stock_view_first = psx_mod_read_byte(VIEW_STATE_START);
    s_guest_hook();
    check(s_view_state_saved &&
              (int32_t)load_u32(VIEW_EYE_X) == s_render_eye_x &&
              load_u16(VIEW_MATRIX + 4u) != 0,
          "render entry installs the first-person eye and view matrix");
    check((int32_t)load_u32(PLAYER_X) == player_x_before &&
              (psx_mod_read_byte(CAMERA_MODE) & CAMERA_MODE_MASK) == 0,
          "render override does not alter player or gameplay camera state");
    frame(0xFFFFu);
    check(!s_view_state_saved &&
              psx_mod_read_byte(VIEW_STATE_START) == stock_view_first,
          "VBlank restores the exact stock camera state before gameplay");

    set_sticks(0x80u, 0x00u, 0x80u, 0x80u);
    frame(0xFFFFu);
    check(load_u16(INPUT_OVERRIDE_WORD) == PSX_PAD_RIGHT,
          "left-stick forward maps to Tomba's native path direction");
    set_sticks(0x80u, 0x80u, 0x80u, 0x80u);

    heading_before = s_heading;
    frame((uint16_t)~PSX_PAD_LEFT);
    check(load_u16(INPUT_OVERRIDE_WORD) == 0 &&
              s_heading == heading_before,
          "left direction neither moves Tomba nor rotates the camera");
    set_sticks(0x80u, 0x80u, 0xFFu, 0x80u);
    frame(0xFFFFu);
    check(load_u16(INPUT_OVERRIDE_WORD) == 0 &&
              s_heading > heading_before,
          "right-stick horizontal input owns camera yaw");
    set_sticks(0x80u, 0x80u, 0x80u, 0xFFu);
    frame(0xFFFFu);
    check(load_u16(INPUT_OVERRIDE_WORD) == 0 &&
              s_look_pitch > 0,
          "right-stick vertical input owns camera pitch");
    set_sticks(0x80u, 0x80u, 0x80u, 0x80u);

    {
        heading_before = s_heading;
        frame((uint16_t)~(PSX_PAD_LEFT | 0x4000u));
        check(load_u16(INPUT_OVERRIDE_WORD) ==
                  (PSX_PAD_LEFT | 0x4000u) &&
                  s_heading == heading_before,
              "side-facing interaction chords stay stock without camera input");
        set_sticks(0x80u, 0x00u, 0x80u, 0x80u);
        frame((uint16_t)~0x4000u);
        check(load_u16(INPUT_OVERRIDE_WORD) ==
                  (PSX_PAD_RIGHT | 0x4000u) &&
                  s_heading == heading_before,
              "jump plus left-stick forward preserves Tomba's action");
        set_sticks(0x00u, 0x80u, 0x80u, 0x80u);
        frame((uint16_t)~0x4000u);
        check(load_u16(INPUT_OVERRIDE_WORD) ==
                  (PSX_PAD_LEFT | 0x4000u) &&
                  s_heading == heading_before,
              "left-stick interaction direction remains game-owned");
        set_sticks(0x80u, 0x80u, 0x80u, 0x80u);
        frame((uint16_t)~(PSX_PAD_L1 | 0x4000u));
        check(load_u16(INPUT_OVERRIDE_WORD) ==
                  (PSX_PAD_UP | 0x4000u),
              "interaction chords retain explicit stock Up access on L1");
    }

    /* A complete right-stick camera turn must change subsequent forward
     * mapping even though Tomba has not moved or changed facing yet. */
    s_heading = 0;
    s_look_pitch = 0;
    s_down_latched = 0;
    set_sticks(0x80u, 0x80u, 0xFFu, 0x80u);
    for (i = 0; i < 64; ++i)
        frame(0xFFFFu);
    check(s_heading == GUEST_HALF_TURN,
          "right-stick free look reaches the opposite camera heading");
    set_sticks(0x80u, 0x00u, 0x80u, 0x80u);
    frame(0xFFFFu);
    check(load_u16(INPUT_OVERRIDE_WORD) == PSX_PAD_LEFT,
          "forward follows the new camera heading after free look");

    /* Reverse is one native opposite-direction input. The plugin does not
     * write any Tomba facing or render-yaw state itself. */
    set_sticks(0x80u, 0x80u, 0x80u, 0x80u);
    frame(0xFFFFu);
    s_heading = 0;
    s_reverse_turn_input_pending = 0;
    s_down_latched = 0;
    store_u16(PLAYER_RENDER_HEADING, 0);
    psx_mod_write_byte(PLAYER_DIRECTION_FLAGS, 0);
    set_sticks(0x80u, 0xFFu, 0x80u, 0x80u);
    frame(0xFFFFu);
    check(s_heading == GUEST_HALF_TURN &&
              load_u16(INPUT_OVERRIDE_WORD) == PSX_PAD_LEFT,
          "reverse sends one stock Left tap while Tomba faces Right");
    check((psx_mod_read_byte(PLAYER_DIRECTION_FLAGS) & 1u) == 0u &&
              load_u16(PLAYER_RENDER_HEADING) == 0,
          "plugin never writes Tomba's facing or render heading");
    frame(0xFFFFu);
    check(s_heading == GUEST_HALF_TURN &&
              load_u16(INPUT_OVERRIDE_WORD) == 0,
          "holding reverse emits no repeated walking input");

    set_sticks(0x80u, 0x80u, 0x80u, 0x80u);
    frame(0xFFFFu);
    psx_mod_write_byte(PLAYER_DIRECTION_FLAGS, 1);
    store_u16(PLAYER_RENDER_HEADING, GUEST_HALF_TURN);
    set_sticks(0x80u, 0x00u, 0x80u, 0x80u);
    frame(0xFFFFu);
    check(load_u16(INPUT_OVERRIDE_WORD) == PSX_PAD_LEFT,
          "forward after turning follows Tomba's reversed orientation");
    set_sticks(0x80u, 0x80u, 0x80u, 0x80u);

    s_guest_hook();
    frame((uint16_t)~PSX_PAD_SELECT);
    check(!s_enabled && !s_render_override_ready &&
              !s_view_state_saved,
          "Select exits immediately and restores the stock render view");
    check(load_u32(INPUT_POLL_CALL) == INPUT_POLL_CALL_STOCK &&
              load_u32(INPUT_POLL_DELAY) == INPUT_POLL_DELAY_STOCK,
          "third-person exit restores the native input poll byte-for-byte");
    check((psx_mod_read_byte(CAMERA_MODE) & CAMERA_MODE_MASK) == 0,
          "third-person exit restores the stock camera mode");

    psx_mod_write_byte(SCRIPTED_INPUT_STATE, 1);
    frame(0xFFFFu);
    frame((uint16_t)~PSX_PAD_SELECT);
    frame(0xFFFFu);
    check(!s_enabled && load_u32(INPUT_POLL_CALL) == INPUT_POLL_CALL_STOCK,
          "attract demos and scripted input cannot enter first-person");

    if (s_failures)
        return 1;
    puts("Tomba 2 first-person control tests passed");
    return 0;
}
