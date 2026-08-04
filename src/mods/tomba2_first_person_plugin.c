#include "mod_plugins.h"
#include "sio.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Limited first-person camera experiment for the stock SCUS-94454 executable.
 *
 * Tomba 2's normal camera controller lives at 0x800E8008. Controller mode 7
 * (FUN_8006E3B0) preserves the eye in scratchpad, copies an arbitrary look-at
 * point from 0x800E8040, and then builds the stock GTE view matrix. Feeding
 * that existing path keeps terrain, actors, fog, projection, and ordering in
 * the original renderer.
 *
 * The game is digital-native and constrains Tomba to authored 2.5D paths.
 * First-person input therefore maps left-stick forward intent to the nearest
 * native path direction, treats left-stick reverse as an orientation-only
 * half-turn, and reserves the right stick for free look. L1/R1 expose stock
 * Up/Down for doors, ladders, and depth transitions.
 */

#define TOMBA2_PACKAGE_ID "tomba2.experimental.first-person"
#define TOMBA2_FEATURE_ID "first-person"
#define TOMBA2_PLUGIN_ID  "tomba2.camera.first-person"

#define CAMERA_STATE             0x800E8008u
#define CAMERA_MODE              0x800E806Cu
#define CAMERA_LOOK_X            0x800E8040u
#define CAMERA_LOOK_Y            0x800E8044u
#define CAMERA_LOOK_Z            0x800E8048u

#define PLAYER_X                 0x800E7EACu
#define PLAYER_Y                 0x800E7EB0u
#define PLAYER_Z                 0x800E7EB4u
#define PLAYER_RENDER_HEADING    0x800E7ED6u
#define PLAYER_PATH_HEADING      0x800E7FC0u
#define PLAYER_VISUAL_FACING     0x800E7FC7u
#define PLAYER_DIRECTION_FLAGS   0x800E7FCAu

/* FUN_80055E28 maps physical Left/Right through these current path-direction
 * masks and retains the result in PLAYER_DIRECTION_FLAGS bit 0. */
#define NATIVE_DIRECTION_ZERO    0x1F80016Cu
#define NATIVE_DIRECTION_ONE     0x1F80016Eu
#define SCRIPTED_INPUT_STATE     0x1F80019Au

#define CAMERA_EYE_X             0x1F8000D0u
#define CAMERA_EYE_Y             0x1F8000D4u
#define CAMERA_EYE_Z             0x1F8000D8u

#define INPUT_POLL_CALL           0x80078938u
#define INPUT_POLL_DELAY          0x8007893Cu
#define INPUT_OVERRIDE_WORD       0x1F8003F0u

/* FUN_800788AC normally calls FUN_800524B4 for the active-high pad word.
 * The feature's guarded overlay hook replaces that call and delay slot with:
 *
 *   lui v0, 0x1F80
 *   lhu v0, 0x03F0(v0)
 *
 * This runs after the frontend's host-pad sample, at the point Tomba consumes
 * input. A VBlank-time SIO write is too early: the frontend overwrites it
 * before the game's poll. */
#define INPUT_POLL_CALL_STOCK     0x0C01492Du
#define INPUT_POLL_DELAY_STOCK    0x00002021u
#define INPUT_POLL_CALL_PATCHED   0x3C021F80u
#define INPUT_POLL_DELAY_PATCHED  0x944203F0u

#define PSX_PAD_UP               0x0010u
#define PSX_PAD_RIGHT            0x0020u
#define PSX_PAD_DOWN             0x0040u
#define PSX_PAD_LEFT             0x0080u
#define PSX_PAD_SELECT           0x0001u
#define PSX_PAD_L1               0x0400u
#define PSX_PAD_R1               0x0800u
#define PSX_PAD_DPAD             0x00F0u
#define PSX_PAD_FACE             0xF000u

#define GUEST_ANGLE_MASK         0x0FFF
#define GUEST_HALF_TURN          0x0800
#define GUEST_QUARTER_TURN       0x0400
#define CAMERA_MODE_MASK         0x3Fu
#define CAMERA_MODE_FREE_TARGET  7u

#define FIXED_ONE                65536
#define DEFAULT_EYE_HEIGHT       224
#define DEFAULT_FORWARD_OFFSET   32
#define DEFAULT_LOOK_DISTANCE    1024
#define DEFAULT_TURN_SPEED       32
#define SAFE_TRANSITION_FRAMES   10
#define SAFE_POSITION_RADIUS     (FIXED_ONE / 4)
#define MIN_TURNAROUND_STEP      128
#define STICK_CENTER             128
#define STICK_DEADZONE           24
#define MAX_LOOK_PITCH           512
#define TOMBA2_PI                3.14159265358979323846

static int s_enabled;
static int s_requested_enabled;
static int s_camera_owned;
static int s_heading_initialized;
static int s_heading;
static int s_look_pitch;
static int s_last_path_heading;
static int s_path_heading_valid;
static int s_eye_height = DEFAULT_EYE_HEIGHT;
static int s_forward_offset = DEFAULT_FORWARD_OFFSET;
static int s_turn_speed = DEFAULT_TURN_SPEED;
static int s_toggle_latched;
static int s_down_latched;
static int s_turnaround_remaining;
static int s_stable_frames;
static int32_t s_stable_x;
static int32_t s_stable_y;
static int32_t s_stable_z;
static int s_stable_position_valid;
static int s_transition_queued_announced;
static int s_announced;
static int s_input_hook_installed;
static int s_input_hook_announced;

static uint16_t read_u16(uint32_t address) {
    return (uint16_t)(
        (uint16_t)psx_mod_read_byte(address) |
        ((uint16_t)psx_mod_read_byte(address + 1u) << 8));
}

static int32_t read_s32(uint32_t address) {
    uint32_t value =
        (uint32_t)psx_mod_read_byte(address) |
        ((uint32_t)psx_mod_read_byte(address + 1u) << 8) |
        ((uint32_t)psx_mod_read_byte(address + 2u) << 16) |
        ((uint32_t)psx_mod_read_byte(address + 3u) << 24);
    return (int32_t)value;
}

static void write_s32(uint32_t address, int32_t value) {
    uint32_t bits = (uint32_t)value;
    psx_mod_write_byte(address, (uint8_t)bits);
    psx_mod_write_byte(address + 1u, (uint8_t)(bits >> 8));
    psx_mod_write_byte(address + 2u, (uint8_t)(bits >> 16));
    psx_mod_write_byte(address + 3u, (uint8_t)(bits >> 24));
}

static void write_u16(uint32_t address, uint16_t value) {
    psx_mod_write_byte(address, (uint8_t)value);
    psx_mod_write_byte(address + 1u, (uint8_t)(value >> 8));
}

static int read_option_int(const char* option, int fallback,
                           int minimum, int maximum) {
    char value[32];
    char* end = NULL;
    long parsed;
    if (!psx_mod_option_value(
            TOMBA2_PACKAGE_ID, TOMBA2_FEATURE_ID, option,
            value, (uint32_t)sizeof(value))) {
        return fallback;
    }
    parsed = strtol(value, &end, 10);
    if (!end || *end != '\0' || parsed < minimum || parsed > maximum)
        return fallback;
    return (int)parsed;
}

static int wrap_heading(int heading) {
    return heading & GUEST_ANGLE_MASK;
}

static int signed_angle_delta(int a, int b) {
    int delta = (a - b) & GUEST_ANGLE_MASK;
    if (delta >= GUEST_HALF_TURN)
        delta -= GUEST_ANGLE_MASK + 1;
    return delta;
}

static int gameplay_camera_available(void) {
    int32_t x;
    int32_t y;
    int32_t z;
    if (!psx_mod_game_started() ||
        psx_mod_read_byte(CAMERA_STATE) != 1u ||
        psx_mod_read_byte(SCRIPTED_INPUT_STATE) == 1u)
        return 0;
    x = read_s32(PLAYER_X);
    y = read_s32(PLAYER_Y);
    z = read_s32(PLAYER_Z);
    /* Reject uninitialized/title-screen coordinates without imposing a level
     * bound on valid signed 16.16 world positions. */
    return (x | y | z) != 0;
}

static void release_camera(void) {
    uint8_t mode;
    if (!s_camera_owned)
        return;
    mode = psx_mod_read_byte(CAMERA_MODE);
    if ((mode & CAMERA_MODE_MASK) == CAMERA_MODE_FREE_TARGET)
        psx_mod_write_byte(CAMERA_MODE, (uint8_t)(mode & ~CAMERA_MODE_MASK));
    s_camera_owned = 0;
    s_heading_initialized = 0;
    s_path_heading_valid = 0;
}

static void update_input_hook(void) {
    uint32_t call_word = (uint32_t)read_s32(INPUT_POLL_CALL);
    uint32_t delay_word = (uint32_t)read_s32(INPUT_POLL_DELAY);

    if (!s_enabled) {
        if (call_word == INPUT_POLL_CALL_PATCHED &&
            delay_word == INPUT_POLL_DELAY_PATCHED) {
            /* A savestate may contain the live hook and free-target camera
             * while host-side plugin state correctly starts disabled. Treat
             * that paired signature as our stale ownership and recover the
             * stock camera before restoring the two input instructions. */
            s_camera_owned = 1;
            release_camera();
            write_s32(INPUT_POLL_CALL, (int32_t)INPUT_POLL_CALL_STOCK);
            write_s32(INPUT_POLL_DELAY, (int32_t)INPUT_POLL_DELAY_STOCK);
        }
        s_input_hook_installed = 0;
        return;
    }

    if (call_word == INPUT_POLL_CALL_PATCHED &&
        delay_word == INPUT_POLL_DELAY_PATCHED) {
        s_input_hook_installed = 1;
        return;
    }
    s_input_hook_installed = 0;
    if (call_word != INPUT_POLL_CALL_STOCK ||
        delay_word != INPUT_POLL_DELAY_STOCK) {
        return;
    }

    /* The input routine is a resident overlay. Exact full-word guards keep
     * this inert while another overlay occupies the same RAM addresses. Guest
     * writes pass through the runtime's executable-RAM invalidation path. */
    write_s32(INPUT_POLL_CALL, (int32_t)INPUT_POLL_CALL_PATCHED);
    write_s32(INPUT_POLL_DELAY, (int32_t)INPUT_POLL_DELAY_PATCHED);
    s_input_hook_installed = 1;
    if (!s_input_hook_announced) {
        printf("[Mods] Tomba 2 first-person input hook active\n");
        s_input_hook_announced = 1;
    }
}

static int native_forward_button(void) {
    int path_heading = (int)read_u16(PLAYER_PATH_HEADING) &
        GUEST_ANGLE_MASK;
    int delta = signed_angle_delta(s_heading, path_heading);
    return (delta >= -GUEST_QUARTER_TURN &&
            delta <= GUEST_QUARTER_TURN)
        ? PSX_PAD_RIGHT
        : PSX_PAD_LEFT;
}

static int native_button_for_facing(int facing) {
    uint16_t mask = read_u16(
        facing ? NATIVE_DIRECTION_ONE : NATIVE_DIRECTION_ZERO);
    mask &= (PSX_PAD_LEFT | PSX_PAD_RIGHT);
    if (mask == PSX_PAD_LEFT || mask == PSX_PAD_RIGHT)
        return (int)mask;
    /* The resident player input routine normally maintains the masks above.
     * Fall back deterministically during the first frame of a level load. */
    return facing ? PSX_PAD_LEFT : PSX_PAD_RIGHT;
}

static void orient_tomba_to_camera(void) {
    int native_button = native_forward_button();
    int facing =
        native_button == native_button_for_facing(1) ? 1 : 0;
    int path_heading =
        (int)read_u16(PLAYER_PATH_HEADING) & GUEST_ANGLE_MASK;
    uint8_t direction_flags =
        psx_mod_read_byte(PLAYER_DIRECTION_FLAGS);

    /* FUN_80055E28 normally copies desired facing (+0x14A bit 0) to visual
     * facing (+0x147), and FUN_80055284 derives render yaw (+0x56) from the
     * path basis (+0x140). Do that orientation-only portion directly: feeding
     * a native Left/Right frame would also make Tomba walk. */
    psx_mod_write_byte(PLAYER_VISUAL_FACING, (uint8_t)facing);
    psx_mod_write_byte(
        PLAYER_DIRECTION_FLAGS,
        (uint8_t)((direction_flags & ~1u) | (uint8_t)facing));
    write_u16(
        PLAYER_RENDER_HEADING,
        (uint16_t)wrap_heading(
            path_heading + (facing ? GUEST_HALF_TURN : 0)));
}

static void initialize_heading_from_tomba(void) {
    int path_heading =
        (int)read_u16(PLAYER_PATH_HEADING) & GUEST_ANGLE_MASK;
    /* +0x56 is Tomba's already-resolved render yaw. Unlike +0x140, it includes
     * his current left/right orientation, so no second facing offset belongs
     * here. */
    s_heading =
        (int)read_u16(PLAYER_RENDER_HEADING) & GUEST_ANGLE_MASK;
    s_heading_initialized = 1;
    s_last_path_heading = path_heading;
    s_path_heading_valid = 1;
    s_turnaround_remaining = 0;
    s_down_latched = 0;
}

static double stick_curve(uint8_t axis) {
    int delta = (int)axis - STICK_CENTER;
    int magnitude = delta < 0 ? -delta : delta;
    double normalized;
    double deadzone = (double)STICK_DEADZONE / 128.0;
    if (magnitude <= STICK_DEADZONE)
        return 0.0;
    normalized = (double)delta / (delta < 0 ? 128.0 : 127.0);
    normalized = (fabs(normalized) - deadzone) / (1.0 - deadzone);
    normalized *= normalized;
    return delta < 0 ? -normalized : normalized;
}

static int stick_forward(const uint8_t sticks[4]) {
    return sticks[1] < STICK_CENTER - STICK_DEADZONE;
}

static int stick_reverse(const uint8_t sticks[4]) {
    return sticks[1] > STICK_CENTER + STICK_DEADZONE;
}

static int stick_left(const uint8_t sticks[4]) {
    return sticks[0] < STICK_CENTER - STICK_DEADZONE;
}

static int stick_right(const uint8_t sticks[4]) {
    return sticks[0] > STICK_CENTER + STICK_DEADZONE;
}

static void follow_authored_path_heading(void) {
    int path_heading;
    int delta;
    if (!s_enabled || !s_heading_initialized)
        return;
    path_heading = (int)read_u16(PLAYER_PATH_HEADING) &
        GUEST_ANGLE_MASK;
    if (!s_path_heading_valid) {
        s_last_path_heading = path_heading;
        s_path_heading_valid = 1;
        return;
    }
    delta = signed_angle_delta(path_heading, s_last_path_heading);
    s_heading = wrap_heading(s_heading + delta);
    s_last_path_heading = path_heading;
}

static int position_delta_safe(int32_t a, int32_t b) {
    int64_t delta = (int64_t)a - (int64_t)b;
    if (delta < 0)
        delta = -delta;
    return delta <= SAFE_POSITION_RADIUS;
}

static void update_transition_stability(void) {
    int32_t x;
    int32_t y;
    int32_t z;
    if (!gameplay_camera_available()) {
        s_stable_frames = 0;
        s_stable_position_valid = 0;
        return;
    }

    x = read_s32(PLAYER_X);
    y = read_s32(PLAYER_Y);
    z = read_s32(PLAYER_Z);
    if (s_stable_position_valid &&
        position_delta_safe(x, s_stable_x) &&
        position_delta_safe(y, s_stable_y) &&
        position_delta_safe(z, s_stable_z)) {
        if (s_stable_frames < SAFE_TRANSITION_FRAMES)
            ++s_stable_frames;
    }
    else {
        s_stable_x = x;
        s_stable_y = y;
        s_stable_z = z;
        s_stable_position_valid = 1;
        s_stable_frames = 1;
    }
}

static void request_mode_toggle(uint16_t pressed) {
    int toggle = (pressed & PSX_PAD_SELECT) != 0u;
    if (toggle && !s_toggle_latched) {
        s_requested_enabled = !s_requested_enabled;
        s_transition_queued_announced = 0;
    }
    s_toggle_latched = toggle;
}

static void apply_requested_mode(void) {
    int mode;
    if (s_requested_enabled == s_enabled)
        return;
    mode = psx_mod_read_byte(CAMERA_MODE) & CAMERA_MODE_MASK;
    if ((s_requested_enabled &&
         (!gameplay_camera_available() || mode != 0)) ||
        (!s_requested_enabled &&
         (s_stable_frames < SAFE_TRANSITION_FRAMES ||
          (s_camera_owned && mode != CAMERA_MODE_FREE_TARGET)))) {
        if (!s_transition_queued_announced) {
            printf("[Mods] Tomba 2 first-person %s queued until the "
                   "camera transition is safe\n",
                   s_requested_enabled ? "entry" : "exit");
            s_transition_queued_announced = 1;
        }
        return;
    }

    if (s_requested_enabled) {
        s_enabled = 1;
        s_heading_initialized = 0;
        s_look_pitch = 0;
        s_path_heading_valid = 0;
        s_turnaround_remaining = 0;
        s_down_latched = 0;
        printf("[Mods] Tomba 2 first-person enabled\n");
    }
    else {
        release_camera();
        s_enabled = 0;
        s_turnaround_remaining = 0;
        s_down_latched = 0;
        printf("[Mods] Tomba 2 first-person disabled\n");
    }
    s_transition_queued_announced = 0;
}

static void update_heading_controls(
    uint16_t pressed, const uint8_t sticks[4]) {
    double yaw_input;
    double pitch_input;
    int reverse_only;
    int step;
    if (!s_enabled || !s_camera_owned || !s_heading_initialized)
        return;

    /* Free look is wholly camera-owned and independent of Tomba's movement or
     * action state. Match the Zelda experiment's squared response curve: the
     * right stick gives fine control near center and full configured speed at
     * the rim. */
    yaw_input = stick_curve(sticks[2]);
    pitch_input = stick_curve(sticks[3]);
    s_heading = wrap_heading(
        s_heading + (int)(yaw_input * (double)s_turn_speed));
    s_look_pitch +=
        (int)(pitch_input * (double)(s_turn_speed * 3 / 4));
    if (s_look_pitch > MAX_LOOK_PITCH)
        s_look_pitch = MAX_LOOK_PITCH;
    else if (s_look_pitch < -MAX_LOOK_PITCH)
        s_look_pitch = -MAX_LOOK_PITCH;

    if (pressed & PSX_PAD_FACE) {
        /* Interaction chords remain game-owned. Free look above remains
         * available, but do not carry a pending locomotion turn through pig
         * capture/carry/throw input. */
        s_turnaround_remaining = 0;
        s_down_latched = 0;
        return;
    }

    reverse_only =
        (stick_reverse(sticks) || (pressed & PSX_PAD_DOWN)) &&
        !stick_forward(sticks) && !(pressed & PSX_PAD_UP);
    if (reverse_only && !s_down_latched &&
        s_turnaround_remaining == 0) {
        s_turnaround_remaining = GUEST_HALF_TURN;
    }
    if (s_turnaround_remaining > 0) {
        step = s_turn_speed * 4;
        if (step < MIN_TURNAROUND_STEP)
            step = MIN_TURNAROUND_STEP;
        if (step > s_turnaround_remaining)
            step = s_turnaround_remaining;
        s_heading = wrap_heading(s_heading + step);
        s_turnaround_remaining -= step;
        if (s_turnaround_remaining == 0)
            orient_tomba_to_camera();
    }
    s_down_latched = reverse_only;
}

static void write_guest_input(
    uint16_t pressed, const uint8_t sticks[4]) {
    int forward_intent =
        stick_forward(sticks) || (pressed & PSX_PAD_UP);
    int reverse_intent =
        stick_reverse(sticks) || (pressed & PSX_PAD_DOWN);
    uint16_t output_pressed = pressed & (uint16_t)~PSX_PAD_SELECT;

    /* Select is always host-only. When first-person is not actually active,
     * every other button passes through unchanged. Requested transitions do
     * not split camera and controls while Tomba is in an unstable state. */
    if (!s_enabled || !s_camera_owned || !s_input_hook_installed) {
        write_u16(INPUT_OVERRIDE_WORD, output_pressed);
        return;
    }
    if (pressed & PSX_PAD_FACE) {
        uint16_t interaction_dpad = pressed & PSX_PAD_DPAD;

        /* Action buttons are always game-owned. Preserve side/depth directions
         * exactly for aiming, carrying, and throwing, but translate a plain
         * Up+action chord to native horizontal movement. Otherwise jumping
         * forward at a pig would send stock Up (path depth) and Tomba would
         * never reach it. L1/R1 remain explicit access to stock Up/Down. */
        output_pressed &= (uint16_t)~(
            PSX_PAD_DPAD | PSX_PAD_L1 | PSX_PAD_R1);
        if (interaction_dpad == PSX_PAD_UP ||
            (interaction_dpad == 0 && forward_intent &&
             !reverse_intent)) {
            output_pressed |= (uint16_t)native_forward_button();
        }
        else if (interaction_dpad != 0) {
            output_pressed |= interaction_dpad;
        }
        else if (stick_reverse(sticks)) {
            output_pressed |= PSX_PAD_DOWN;
        }
        else if (stick_left(sticks) != stick_right(sticks)) {
            output_pressed |=
                stick_left(sticks) ? PSX_PAD_LEFT : PSX_PAD_RIGHT;
        }
        if (pressed & PSX_PAD_L1)
            output_pressed |= PSX_PAD_UP;
        if (pressed & PSX_PAD_R1)
            output_pressed |= PSX_PAD_DOWN;
        write_u16(INPUT_OVERRIDE_WORD, output_pressed);
        return;
    }

    output_pressed &= (uint16_t)~(
        PSX_PAD_DPAD | PSX_PAD_L1 | PSX_PAD_R1);
    /* The left stick's vertical axis owns locomotion. Forward advances along
     * the path; reverse turns Tomba and the camera around without ever
     * injecting a walking frame. Horizontal left-stick/D-pad intent is inert.
     * The right stick is consumed only by the camera path above. */
    if (forward_intent && !reverse_intent &&
        s_turnaround_remaining == 0) {
        output_pressed |= (uint16_t)native_forward_button();
    }
    if (pressed & PSX_PAD_L1)
        output_pressed |= PSX_PAD_UP;
    if (pressed & PSX_PAD_R1)
        output_pressed |= PSX_PAD_DOWN;

    write_u16(INPUT_OVERRIDE_WORD, output_pressed);
}

static void update_camera(void) {
    uint8_t mode;
    int32_t player_x;
    int32_t player_y;
    int32_t player_z;
    int32_t eye_x;
    int32_t eye_y;
    int32_t eye_z;
    int32_t look_x;
    int32_t look_z;
    double radians;
    double pitch_radians;
    double forward_x;
    double forward_z;
    double look_horizontal;
    double look_vertical;

    if (!s_enabled || !s_input_hook_installed ||
        !gameplay_camera_available()) {
        release_camera();
        return;
    }

    mode = psx_mod_read_byte(CAMERA_MODE);
    if (s_camera_owned &&
        (mode & CAMERA_MODE_MASK) != CAMERA_MODE_FREE_TARGET) {
        /* A cutscene or transition requested a special camera. Yield until the
         * stock controller returns to its ordinary follow mode. Retain the
         * user's heading so a one-frame camera request cannot reverse it. */
        s_camera_owned = 0;
        return;
    }
    if (!s_camera_owned) {
        if ((mode & CAMERA_MODE_MASK) != 0u)
            return;
        s_camera_owned = 1;
    }
    if (!s_heading_initialized) {
        initialize_heading_from_tomba();
    }

    player_x = read_s32(PLAYER_X);
    player_y = read_s32(PLAYER_Y);
    player_z = read_s32(PLAYER_Z);
    radians = (double)s_heading * (2.0 * TOMBA2_PI / 4096.0);
    pitch_radians =
        (double)s_look_pitch * (2.0 * TOMBA2_PI / 4096.0);
    /* Tomba's path heading 0 advances along +X. Keeping camera and native
     * movement in that same angle basis prevents entry from looking sideways
     * off the authored path (often straight out over the ocean). */
    forward_x = cos(radians);
    forward_z = sin(radians);
    look_horizontal = cos(pitch_radians);
    look_vertical = sin(pitch_radians);

    /* Move slightly through the front of Tomba's model so his head and hair
     * sit behind the near plane instead of filling the view. */
    eye_x = player_x +
        (int32_t)(forward_x * (double)(s_forward_offset * FIXED_ONE));
    eye_y = player_y - s_eye_height * FIXED_ONE;
    eye_z = player_z +
        (int32_t)(forward_z * (double)(s_forward_offset * FIXED_ONE));
    look_x = eye_x +
        (int32_t)(forward_x * look_horizontal *
                  (double)(DEFAULT_LOOK_DISTANCE * FIXED_ONE));
    look_z = eye_z +
        (int32_t)(forward_z * look_horizontal *
                  (double)(DEFAULT_LOOK_DISTANCE * FIXED_ONE));

    write_s32(CAMERA_EYE_X, eye_x);
    write_s32(CAMERA_EYE_Y, eye_y);
    write_s32(CAMERA_EYE_Z, eye_z);
    write_s32(CAMERA_LOOK_X, look_x);
    write_s32(
        CAMERA_LOOK_Y,
        eye_y + (int32_t)(
            look_vertical * (double)(DEFAULT_LOOK_DISTANCE * FIXED_ONE)));
    write_s32(CAMERA_LOOK_Z, look_z);
    psx_mod_write_byte(
        CAMERA_MODE,
        (uint8_t)((mode & ~CAMERA_MODE_MASK) | CAMERA_MODE_FREE_TARGET));

    if (!s_announced) {
        printf("[Mods] Tomba 2 first-person experiment active "
               "(left stick forward/turn-around, right stick free look, "
               "L1/R1 stock vertical, "
               "Select toggle)\n");
        s_announced = 1;
    }
}

static void tomba2_first_person_activate(void) {
    /* Host-side mode never inherits a prior run or savestate. The first Select
     * press is the only way to request first-person after activation. */
    s_enabled = 0;
    s_requested_enabled = 0;
    s_camera_owned = 0;
    s_heading_initialized = 0;
    s_look_pitch = 0;
    s_path_heading_valid = 0;
    s_turnaround_remaining = 0;
    s_toggle_latched = 0;
    s_down_latched = 0;
    s_stable_frames = 0;
    s_stable_position_valid = 0;
    s_transition_queued_announced = 0;
    s_input_hook_installed = 0;
    s_eye_height = read_option_int(
        "eye-height", DEFAULT_EYE_HEIGHT, 96, 320);
    s_forward_offset = read_option_int(
        "forward-offset", DEFAULT_FORWARD_OFFSET, 0, 160);
    s_turn_speed = read_option_int(
        "turn-speed", DEFAULT_TURN_SPEED, 8, 128);
}

static void tomba2_first_person_vblank(void) {
    uint8_t sticks[4];
    uint16_t raw_buttons = sio_get_pad_buttons_slot(0);
    uint16_t pressed = (uint16_t)~raw_buttons;
    sio_get_pad_sticks(0, sticks);
    update_transition_stability();
    request_mode_toggle(pressed);
    apply_requested_mode();
    update_input_hook();
    follow_authored_path_heading();
    update_heading_controls(pressed, sticks);
    update_camera();
    if (s_input_hook_installed)
        write_guest_input(pressed, sticks);
}

PSX_MOD_CONSTRUCTOR(tomba2_register_first_person_plugin) {
    (void)psx_mod_register_activation_plugin(
        TOMBA2_PLUGIN_ID, tomba2_first_person_activate);
    (void)psx_mod_register_vblank_plugin(
        TOMBA2_PLUGIN_ID, tomba2_first_person_vblank);
}
