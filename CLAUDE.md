# Tomba2Recomp Rules

Static recompilation of **Tomba! 2 - The Evil Swine Return (USA)** — serial
**SCUS-94454** — to native code, built with the shared **psxrecomp** framework.
The end goal is a binary that plays without an emulator behind it, exactly like
TombaRecomp.

## Inheritance

This project inherits, in order:

1. `F:/Projects/recomp-template/PRINCIPLES.md` — system-agnostic recomp/debug
   discipline (ground truth = original EXE + emulator oracle; generated C is
   evidence, not authority; first-divergence; no guessing).
2. The framework constitution at `psxrecomp-v4/CLAUDE.md` — a junction →
   a psxrecomp worktree. Read it first: no MIPS interpreter, no HLE BIOS shims,
   no stubs, recompiled-BIOS-first, fix the framework/runtime/config and
   **regenerate** — never hand-edit `generated/`.

## Issue tracking (Beads)

Work items live in the central Beads tracker at `F:\Software\beads\issues`,
never in a `.beads` database inside this repo. See the global rules for the
hierarchy and label conventions.

- **This game's epic:** `beads-eio.2` — *Game: Tomba! 2 - The Evil Swine
  Return (PlayStation)*, beneath `beads-eio` (*System: PlayStation*).
- **Framework work belongs elsewhere:** anything in `psxrecomp-v4/` or
  `recomp-ui/` that affects more than this title goes under `beads-eio.3`
  (*Meta: psxrecomp framework*), not under the game epic — the same rule
  that says a class fix belongs in the framework.

```powershell
bd -C F:\Software\beads\issues list
bd -C F:\Software\beads\issues show beads-eio.2
bd -C F:\Software\beads\issues create "<title>" --parent beads-eio.2 --type bug
```

## Project rules

- Game binaries (disc image, extracted boot EXE, the headerless Ghidra dump),
  Ghidra databases, memory cards, and build outputs are **local only** and must
  not be committed. See `.gitignore`.
- Tracked: `game.toml`, `seeds/`, `annotations/`, `ghidra/instructions.txt`,
  `ghidra/scripts/`, `ghidra/annotations/`, `CMakeLists.txt`, `tools/`, docs.
- Codegen/runtime fixes belong in the framework (`psxrecomp-v4/`) or in
  per-game `game.toml` config — never in `generated/*.c`. A fix that only this
  game needs is a smell; prefer a class fix that the next title inherits.
- After every run, resolve all dispatch misses before any other debugging.
- The framework version this project builds against is recorded as the
  `psxrecomp-v4` git submodule pointer (see `.gitmodules`); the former
  `psxrecomp-v4.pin` file was retired in favor of the submodule (its changelog
  is preserved in `docs/framework_pin_history.md`).
