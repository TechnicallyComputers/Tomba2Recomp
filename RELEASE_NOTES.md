# Tomba! 2 Recompiled — v0.0.8

A maintenance release: the loading-speed settings behave correctly again, and
the recompiler framework and launcher move up to current.

## Fixed

- **Turbo loads is now owned by the Mods page.** Loading speed moved to the
  mod catalog ("Fast Loading (host pacing)" and "CD Speed", both off by
  default), but a `turbo_loads = true` left in an older build's
  `settings.toml` was still being applied — and the launcher no longer shows a
  control for it, so there was no way to switch it back off. The game now
  ignores that stale value and says so on startup.

## Changed

- **Framework and launcher updated to current.** Brings DualShock rumble
  output, mods re-applying executable patches after a save-state load,
  cross-package overlay predicates, and clearer reporting when the overlay
  compiler falls back to the interpreter.
- **The bundled overlay cache was regenerated for this build.** Caches are
  specific to the build that produced them, so v0.0.8 ships its own. Any cache
  your v0.0.7 install had accumulated will not carry over, and the first
  session will fill in anything the bundled set does not cover.

## Known issues

- **A slow memory card screen has been reported on v0.0.7 and is not yet
  explained.** It has not been reproduced on a developer machine, and nothing
  in this release is known to fix it. If you saw it on v0.0.7, please try
  v0.0.8 and report whether it persists — that information is what we need to
  track it down.
- **`geometry_correction` in `settings.toml` has no launcher control.** If it
  is set to `true` it can produce visible seams along polygon edges. Leave it
  `false`; it is not offered in the UI and is not validated.
