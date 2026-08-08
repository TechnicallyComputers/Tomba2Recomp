# Tomba! 2 Recompiled — v0.0.8

Fixes memory cards, which could hang on "Checking MEMORY CARD…" in v0.0.7.

## Fixed

- **Memory cards work again.** In v0.0.7 the memory card screen could stall
  indefinitely — the card never finished being checked, and the game sat there
  burning CPU. The cause was in the recompiler: it was not preserving MIPS-I
  load-delay *value* semantics when translating certain code, and the BIOS's
  own card fast-path depends on that behaviour, so the card routine was
  miscompiled. Reported by a v0.0.7 player; thanks for flagging it.
  (Framework fix by Alexbeav, psxrecomp PR #93.)
- **Turbo loads is now owned by the Mods page.** Loading speed moved to the mod
  catalog ("Fast Loading (host pacing)" and "CD Speed", both off by default),
  but a `turbo_loads = true` left in an older build's `settings.toml` was still
  being applied — and the launcher no longer shows a control for it, so there
  was no way to switch it off. That stale value is now ignored.

## Changed

- **Framework and launcher updated to current.** Brings DualShock rumble
  output, mods re-applying executable patches after a save-state load,
  cross-package overlay predicates, and clearer reporting when the overlay
  compiler falls back to the interpreter.
- **The bundled overlay cache was fully rebuilt** with the corrected compiler
  (374 prebuilt overlays, up from 300 in v0.0.7). Caches are specific to the
  build that produced them, so any cache your v0.0.7 install accumulated will
  not carry over — this build ships its own and fills in the rest as you play.

## Known issues

- **`geometry_correction` in `settings.toml` has no launcher control.** If it
  is set to `true` it can produce visible seams along polygon edges. Leave it
  `false`; it is not offered in the UI and is not validated.
