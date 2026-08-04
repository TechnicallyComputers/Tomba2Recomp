#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mod_plugins.h"
#include "sio.h"

static uint8_t s_ram[2 * 1024 * 1024];
static uint8_t s_scratch[1024];
static uint16_t s_raw_buttons = 0xFFFFu;
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

static void initialize_gameplay(void) {
    memset(s_ram, 0, sizeof(s_ram));
    memset(s_scratch, 0, sizeof(s_scratch));
    s_raw_buttons = 0xFFFFu;
    s_enabled = 0;
    s_requested_enabled = 0;
    s_camera_owned = 0;
    s_heading_initialized = 0;
    s_path_heading_valid = 0;
    s_heading = 0;
    s_toggle_latched = 0;
    s_down_latched = 0;
    s_turnaround_remaining = 0;
    s_stable_frames = 0;
    s_stable_position_valid = 0;
    s_transition_queued_announced = 0;
    s_input_hook_installed = 0;

    psx_mod_write_byte(CAMERA_STATE, 1);
    psx_mod_write_byte(PLAYER_DIRECTION_FLAGS, 0);
    psx_mod_write_byte(SCRIPTED_INPUT_STATE, 0);
    store_u32(PLAYER_X, 100u * FIXED_ONE);
    store_u32(PLAYER_Y, 1000u * FIXED_ONE);
    store_u32(PLAYER_Z, 200u * FIXED_ONE);
    store_u16(PLAYER_PATH_HEADING, 0);
    store_u16(NATIVE_DIRECTION_ZERO, PSX_PAD_RIGHT);
    store_u16(NATIVE_DIRECTION_ONE, PSX_PAD_LEFT);
    store_u32(INPUT_POLL_CALL, INPUT_POLL_CALL_STOCK);
    store_u32(INPUT_POLL_DELAY, INPUT_POLL_DELAY_STOCK);
}

int main(void) {
    int i;
    int32_t eye_x;
    int32_t look_x;

    initialize_gameplay();
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

    store_u32(
        PLAYER_X,
        load_u32(PLAYER_X) + (uint32_t)(SAFE_POSITION_RADIUS * 2));
    frame((uint16_t)~PSX_PAD_SELECT);
    check(s_enabled && s_camera_owned,
          "Select enters first-person without rejecting an active game state");
    check(load_u32(INPUT_POLL_CALL) == INPUT_POLL_CALL_PATCHED &&
              load_u32(INPUT_POLL_DELAY) == INPUT_POLL_DELAY_PATCHED,
          "entry installs the guarded post-poll input hook");
    check(load_u16(INPUT_OVERRIDE_WORD) == 0,
          "Select is consumed on first-person entry");
    eye_x = (int32_t)load_u32(CAMERA_EYE_X);
    look_x = (int32_t)load_u32(CAMERA_LOOK_X);
    check(look_x - eye_x == DEFAULT_LOOK_DISTANCE * FIXED_ONE,
          "entry looks along Tomba's +X authored facing, not off-path");

    frame(0xFFFFu);
    frame((uint16_t)~PSX_PAD_UP);
    check(load_u16(INPUT_OVERRIDE_WORD) == PSX_PAD_RIGHT,
          "Up maps to Tomba's native forward direction");

    frame((uint16_t)~PSX_PAD_LEFT);
    check(load_u16(INPUT_OVERRIDE_WORD) == 0,
          "Left rotates the camera without moving Tomba");

    {
        int heading_before = s_heading;
        frame((uint16_t)~(PSX_PAD_LEFT | 0x4000u));
        check(load_u16(INPUT_OVERRIDE_WORD) ==
                  (PSX_PAD_LEFT | 0x4000u) &&
                  s_heading == heading_before,
              "side-facing interaction chords stay stock without camera input");
        frame((uint16_t)~(PSX_PAD_UP | 0x4000u));
        check(load_u16(INPUT_OVERRIDE_WORD) ==
                  (PSX_PAD_RIGHT | 0x4000u) &&
                  s_heading == heading_before,
              "jump plus Up preserves the action while moving forward");
        frame((uint16_t)~(PSX_PAD_L1 | 0x4000u));
        check(load_u16(INPUT_OVERRIDE_WORD) ==
                  (PSX_PAD_UP | 0x4000u),
              "interaction chords retain explicit stock Up access on L1");
    }

    s_heading = 0;
    s_turnaround_remaining = 0;
    s_down_latched = 0;
    for (i = 0; i < 15; ++i) {
        frame((uint16_t)~PSX_PAD_DOWN);
        check(load_u16(INPUT_OVERRIDE_WORD) == 0,
              "Down does not move during the smooth turn-around");
    }
    frame((uint16_t)~PSX_PAD_DOWN);
    check(s_heading == GUEST_HALF_TURN &&
              load_u16(INPUT_OVERRIDE_WORD) == PSX_PAD_LEFT,
          "Down completes 180 degrees before advancing the opposite way");
    eye_x = (int32_t)load_u32(CAMERA_EYE_X);
    look_x = (int32_t)load_u32(CAMERA_LOOK_X);
    check(look_x - eye_x == -DEFAULT_LOOK_DISTANCE * FIXED_ONE,
          "turn-around reverses the rendered forward vector");

    frame(0xFFFFu);
    store_u32(
        PLAYER_X,
        load_u32(PLAYER_X) + (uint32_t)(SAFE_POSITION_RADIUS * 2));
    frame((uint16_t)~PSX_PAD_SELECT);
    check(s_enabled && load_u32(INPUT_POLL_CALL) == INPUT_POLL_CALL_PATCHED,
          "exit request initially remains first-person");
    frame(0xFFFFu);
    for (i = 0; i < 4; ++i) {
        store_u32(
            PLAYER_X,
            load_u32(PLAYER_X) + (uint32_t)(SAFE_POSITION_RADIUS * 2));
        frame(0xFFFFu);
    }
    check(s_enabled,
          "exit stays queued while Tomba or a supporting platform moves");
    for (i = 0; i < SAFE_TRANSITION_FRAMES; ++i)
        frame(0xFFFFu);
    check(!s_enabled && !s_camera_owned,
          "queued exit completes after Tomba stands safely");
    check(load_u32(INPUT_POLL_CALL) == INPUT_POLL_CALL_STOCK &&
              load_u32(INPUT_POLL_DELAY) == INPUT_POLL_DELAY_STOCK,
          "third-person exit restores the native input poll byte-for-byte");
    check((psx_mod_read_byte(CAMERA_MODE) & CAMERA_MODE_MASK) == 0,
          "third-person exit restores the stock camera mode");

    psx_mod_write_byte(SCRIPTED_INPUT_STATE, 1);
    frame((uint16_t)~PSX_PAD_SELECT);
    frame(0xFFFFu);
    for (i = 0; i < SAFE_TRANSITION_FRAMES + 2; ++i)
        frame(0xFFFFu);
    check(!s_enabled && load_u32(INPUT_POLL_CALL) == INPUT_POLL_CALL_STOCK,
          "attract demos and scripted input cannot enter first-person");

    if (s_failures)
        return 1;
    puts("Tomba 2 first-person control tests passed");
    return 0;
}
