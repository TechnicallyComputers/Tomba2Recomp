#include "mod_plugins.h"

/*
 * Tomba 2 normally produces a new display image at roughly 30 Hz while its
 * guest VBlank, input, audio, and scheduler continue at their original rates.
 * These callbacks only select the OpenGL presentation blend cadence.
 */
static void tomba2_frame_rate_display_activate(void) {
    /* Zero follows the measured display refresh; it must never busy-loop. */
    (void)psx_mod_set_frame_interpolation(0u);
}

static void tomba2_frame_rate_60_activate(void) {
    (void)psx_mod_set_frame_interpolation(60u);
}

static void tomba2_frame_rate_90_activate(void) {
    (void)psx_mod_set_frame_interpolation(90u);
}

static void tomba2_frame_rate_120_activate(void) {
    (void)psx_mod_set_frame_interpolation(120u);
}

static void tomba2_frame_rate_144_activate(void) {
    (void)psx_mod_set_frame_interpolation(144u);
}

static void tomba2_frame_rate_165_activate(void) {
    (void)psx_mod_set_frame_interpolation(165u);
}

static void tomba2_frame_rate_240_activate(void) {
    (void)psx_mod_set_frame_interpolation(240u);
}

PSX_MOD_CONSTRUCTOR(tomba2_register_frame_rate_plugins) {
    (void)psx_mod_register_activation_plugin(
        "tomba2.framerate.display", tomba2_frame_rate_display_activate);
    (void)psx_mod_register_activation_plugin(
        "tomba2.framerate.60", tomba2_frame_rate_60_activate);
    (void)psx_mod_register_activation_plugin(
        "tomba2.framerate.90", tomba2_frame_rate_90_activate);
    (void)psx_mod_register_activation_plugin(
        "tomba2.framerate.120", tomba2_frame_rate_120_activate);
    (void)psx_mod_register_activation_plugin(
        "tomba2.framerate.144", tomba2_frame_rate_144_activate);
    (void)psx_mod_register_activation_plugin(
        "tomba2.framerate.165", tomba2_frame_rate_165_activate);
    (void)psx_mod_register_activation_plugin(
        "tomba2.framerate.240", tomba2_frame_rate_240_activate);
}
