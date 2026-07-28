#include "mod_plugins.h"

/*
 * Keep the host acceleration switch behind Tomba 2's trusted mod catalog.
 * The runtime still drives the game's normal MDEC/XA completion path, including
 * the silent Whoopee Camp logo detector configured in game.toml.
 */
static void tomba2_skip_fmvs_activate(void) {
    (void)psx_mod_set_auto_skip_fmv(1);
}

PSX_MOD_CONSTRUCTOR(tomba2_register_skip_fmv_plugin) {
    (void)psx_mod_register_activation_plugin(
        "tomba2.fmv.skip", tomba2_skip_fmvs_activate);
}
