# Tomba2Recomp Rules

Static recompilation of **Tomba! 2 - The Evil Swine Return (USA)** — serial
**SCUS-94454** — to native code, built with the shared **psxrecomp** framework.
The end goal is a binary that plays without an emulator behind it, exactly like
TombaRecomp.

## Inheritance

This project inherits, in order:

1. **The shared doctrine repo `recomp-ai-rules`** — system-agnostic recomp/debug
   discipline (ground truth = original EXE + emulator oracle; generated C is
   evidence, not authority; first-divergence; no guessing). Read
   `PRINCIPLES.md`, new `CHANGELOG.md` entries, and `PSX/PRINCIPLES.md` for this
   platform. It is a sibling checkout — `../recomp-ai-rules/` in the standard
   workspace layout, where `CLAUDE.md` at the workspace root carries the
   precedence chain.
2. **The framework constitution at `psxrecomp/CLAUDE.md`** — the `psxrecomp`
   submodule vendored in this repo. Read it first: no MIPS interpreter, no HLE
   BIOS shims, no stubs, recompiled-BIOS-first, fix the framework/runtime/config
   and **regenerate** — never hand-edit `generated/`.

Root wins: where the framework or this file conflicts with the doctrine repo,
the lower file is the bug.

## Issue tracking

**The current in-repo ledger is `ISSUES.md`.**

Work items were previously kept in a central Beads store outside this repo. That
store is **not present on this host and `bd` is not installed**, so its IDs are
recorded here as identifiers rather than as something you can query today:

- **This game's epic:** `beads-eio.2` — *Game: Tomba! 2 - The Evil Swine Return
  (PlayStation)*, beneath `beads-eio` (*System: PlayStation*).
- **Framework work belongs elsewhere:** anything in `psxrecomp/` or
  `recomp-ui/` that affects more than this title goes under `beads-eio.3`
  (*Meta: psxrecomp framework*), not under the game epic — the same rule that
  says a class fix belongs in the framework. That rule holds regardless of which
  tracker is in use.

If the Beads store is restored, point this section at its actual location and
resume the `bd -C <store> …` workflow; see `recomp-ai-rules/TRACKING.md` for the
hierarchy and label conventions.

## Project rules

- Game binaries (disc image, extracted boot EXE, the headerless Ghidra dump),
  Ghidra databases, memory cards, and build outputs are **local only** and must
  not be committed. See `.gitignore`.
- Tracked: `game.toml`, `seeds/`, `annotations/`, `ghidra/instructions.txt`,
  `ghidra/scripts/`, `ghidra/annotations/`, `CMakeLists.txt`, `tools/`, docs.
- Codegen/runtime fixes belong in the framework (`psxrecomp/`) or in per-game
  `game.toml` config — never in `generated/*.c`. A fix that only this game needs
  is a smell; prefer a class fix that the next title inherits.
- After every run, resolve all dispatch misses before any other debugging.
- The framework version this project builds against is recorded as the
  `psxrecomp` git submodule pointer (see `.gitmodules`); the former
  `psxrecomp-v4.pin` file was retired in favor of the submodule (its changelog is
  preserved in `docs/framework_pin_history.md`). `framework_pins.txt` records the
  companion pins (`recomp-ui`, `recomp-net`, `retcomm-rbengine`) — when it
  disagrees with the submodule pointer, the submodule pointer is what the build
  actually used.
