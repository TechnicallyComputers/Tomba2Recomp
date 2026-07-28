#include "mod_plugins.h"

/*
 * Projection, backdrop, overlay-cull, and resident-object participation hooks
 * remain in game.toml. The mod only activates their presentation aspect before
 * renderer startup, leaving every hook inert at the authentic 4:3 baseline.
 */
static void tomba2_widescreen_16_9_activate(void) {
    (void)psx_mod_set_fixed_display_aspect(16u, 9u);
}

static void tomba2_widescreen_21_9_activate(void) {
    (void)psx_mod_set_fixed_display_aspect(21u, 9u);
}

static void tomba2_widescreen_adaptive_activate(void) {
    (void)psx_mod_set_fixed_display_aspect(16u, 9u);
    (void)psx_mod_set_adaptive_display_aspect(21u, 9u);
}

PSX_MOD_CONSTRUCTOR(tomba2_register_widescreen_plugins) {
    (void)psx_mod_register_activation_plugin(
        "tomba2.widescreen.16-9", tomba2_widescreen_16_9_activate);
    (void)psx_mod_register_activation_plugin(
        "tomba2.widescreen.21-9", tomba2_widescreen_21_9_activate);
    (void)psx_mod_register_activation_plugin(
        "tomba2.widescreen.adaptive", tomba2_widescreen_adaptive_activate);
}
