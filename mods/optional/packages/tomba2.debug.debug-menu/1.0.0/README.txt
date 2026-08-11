Tomba 2 Debug Menu
==================

DEVELOPER-ONLY, NOT SHIPPED. This package lives under mods/optional, which the
build does NOT copy beside the runtime, so the feature never appears on the Mods
page and cannot be enabled by accident. The trusted plugin is still compiled into
Tomba2Recomp; only the manifest that offers it is withheld.

To use it locally, copy this package directory into the runtime's mods/packages
folder beside the executable, then enable "Debug Menu (Experimental)" under
Debug on the Mods page. To expose it for real, move the directory to
mods/preloaded/packages and add its plugin id to the catalog test's expectations.

An in-game developer menu drawn with Tomba 2's own text routines. Press L3
during gameplay to open it; X selects, O goes back, Up/Down move the cursor,
L3 closes.

Pages
-----
  FREE POSITION    move Tomba freely in X/Y, with L2 held for Z; hide objects
  WARP TOOLS       pick a destination area/entry and launch the transition
  EVENT FLAGS      step through the event-flag table and edit values
  SYSTEM TOOLS     freeze Tomba, freeze actors/objects, disable the effect
                   list, change game speed and render path, restore defaults
  INVENTORY TOOLS  grant every item
  WORLD INFO       live area, sub-area, level variable, player mode and state
  CONTROLS / HELP  the button reference

How it is installed
-------------------
The menu is guest MIPS code. When the feature is enabled and a gameplay module
is resident, the runtime writes the payload into free BIOS kernel RAM at
0x8000C000 and re-points five call sites inside the loaded gameplay overlay at
it. Every site is checked against its expected stock instruction first; if any
site does not match, nothing is patched and the reason is written to the log.
Because those sites live in overlay code that is re-streamed from disc on area
transitions, the runtime re-asserts them each frame while the feature is on.

Disabling the feature removes the plugin from the resolved plan entirely, so no
guest memory is touched and the game runs stock.

Warnings
--------
This is a development tool, not a gameplay enhancement. Warping to a raw
destination, freeing the player position, and granting items can all put the
game into states the stock title never reaches, and those states can be saved.
Use a memory card you do not mind losing.

The menu's toggle is L3. Tomba 2 is pinned to a digital pad, and a real
SCPH-1080 digital controller has no L3 button; the runtime still delivers the
L3 bit, so the binding works here but does not correspond to anything you could
press on original hardware.

Provenance
----------
The payload is the community "Debug Menu (Press L3 to toggle)" code list for
Tomba! 2 (NTSC-U, SCUS-94454), transcribed from
mods/sources/tomba2_debug_menu.cht by tools/gen_debug_menu_payload.py.
