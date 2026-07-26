# Adaptive widescreen - object/model participation

> **Superseded 2026-07-26:** the thirteen `widescreen.cull.keep` records
> described below are no longer enabled for Tomba 2. After rebasing onto
> current master, a cache-off run with the records enabled reached the
> black/softlock signature, while removing them completed repeated attract-demo
> transitions. Cached DLLs compiled with the records were also semantically
> stale because the cache tag did not include the game config. Treat the
> earlier validation claims below as historical leads, not accepted evidence.
> The guarded framework mechanism remains available for future sites that are
> independently proven safe.

Status as of **2026-07-25** on branch
`feat/tomba2-adaptive-spawn-ranges`.

The beach-road pop-in is fixed in 16:9, 21:9, and adaptive widescreen. The fix
does not widen the renderer or terrain margin. It bypasses two proven
object/model distance-and-camera-cone predicates while a widened world view is
configured. True 4:3 evaluates the original comparisons.

## Reproduction and result

The reliable reproduction is the Whoopee Camp beach attract demo:

- At the starting wide camera, the Evil Pig pedestal is visible on the right.
- In the original build the pig is absent, then appears only after Tomba moves
  toward it.
- With the guarded participation sites, the pig is already on the pedestal at
  the initial camera.
- Distant fence and uphill model groups are also submitted before reaching the
  old 4:3 boundary.
- Terrain remains complete. This avoids the terrain-removal regression caused
  by the discarded `ws_margin = 600` global diagnostic.

Captured validation images are under `build-spawn-dbg/`:

| mode | image | observed |
|---|---|---|
| 21:9 adaptive | `maximal_seq_1500ms.png` | pig/model groups present at initial camera; terrain intact |
| 21:9 adaptive | `maximal_seq_4000ms.png` | uphill structures participate before the old edge |
| 16:9 fixed | `maximal_16x9_1500ms.png` | pig is present at the revealed right edge |
| 16:9 fixed | `maximal_16x9_2500ms.png` | pig clearly present; terrain intact |
| true 4:3 | `beach_control_true4x3_2500ms.png` | runtime reports `x_margin = 0`; vanilla comparisons execute |

The real memory-card save currently loads into the lava area. A manual lava
pass remains useful because the attract demo ignores debug-pad injection; do
not mutate live guest code to force navigation.

## Predicate 1: per-object visibility

`FUN_8002B278` takes one object, computes its position relative to the camera
scratch state at `1F8000D2/D6/DA`, then performs:

```c
distance = sqrt(dx*dx + dy*dy + dz*dz);
if (distance < 0x200) return false;
if (distance >= 0x1C01) return false;
if (dot(camera_forward, delta) < distance * 0xD60) return false;
object[1] = 1;
return true;
```

It has thirteen captured callers spanning model/effect submission wrappers:
`8002C3EC`, `8002918C`, `80032918`, `80030990`, `800293F4`, `800275D4`,
`800292B8`, `8002B8F4`, `8002F230`, `80029530`, `8002BAFC`, `8002C548`, and
`80030A3C`.

The guarded sites are:

| address | expected word | wide result | meaning |
|---|---:|---:|---|
| `8002B310` | `28A21C01` | 1 | keep the far-distance test passing |
| `8002B368` | `0082202A` | 0 | suppress the forward-cone reject |

Across the overlay capture set, 218 bodies contain the distance site and all
have the exact expected word. The cone site occurs in the same function body.

## Predicate 2: engine model-list builder

`FUN_8007712C` is the broader engine predicate. It receives an object and a
camera-relative vector, clears `object[1]`, chooses distance/cone limits by
model type and renderer mode, then:

- sets `object[1] = 1`;
- returns visible;
- queues types 2/9, 4, and 5 into bounded scratch lists.

The nine direct wrappers at `8007778C`, `80077958`, `80077F3C`, `80077A4C`,
`800777FC`, `80077870`, `800779D0`, `80077ACC`, and `800778E4` make it a
general model-participation path.

The fix bypasses only far-distance and forward-cone rejects. It deliberately
keeps:

- near-plane limits (`0x200`, `0x300`, `0x400`);
- type dispatch;
- bounded queue capacities (24, 28, and 40 entries).

Removing the queue-capacity checks would write past fixed scratch storage and
is not a valid "maximal objects" implementation.

| address | expected word | wide result | role |
|---|---:|---:|---|
| `80077248` | `28821401` | 1 | far limit |
| `800772D4` | `28620370` | 0 | cone reject |
| `800772E4` | `28821C01` | 1 | far limit |
| `80077368` | `28620358` | 0 | cone reject |
| `80077380` | `28821801` | 1 | far limit |
| `80077390` | `28821C01` | 1 | far limit |
| `80077414` | `28620358` | 0 | cone reject |
| `80077424` | `28821001` | 1 | far limit |
| `800774A8` | `28620370` | 0 | cone reject |
| `8007754C` | `28821A01` | 1 | far limit |
| `800775D0` | `28620368` | 0 | cone reject |

Every site is one distinct word across all 235 captured copies of this overlay
body. Most were executed in 218 capture sets; the less common type/mode arms
were executed in 14 or 32 sets. `overlay_xref.py word` is the authority because
the Ghidra RAM image is a composite of mutually exclusive overlays.

## Framework mechanism

The framework now accepts:

```toml
[[widescreen.cull.keep]]
address = "0x8002B310"
expected = "0x28A21C01"
result = 1
```

Properties:

- supports `SLT`, `SLTU`, `SLTI`, and `SLTIU`;
- identifies a site by physical address plus the complete 32-bit instruction;
- leaves a nonmatching same-VA overlay variant unchanged;
- returns the vanilla comparison result at true 4:3;
- forces the configured 0/1 verdict only when `psx_ws_x_margin() > 0`;
- works in both native generated code and the dirty-RAM interpreter.

All thirteen Tomba sites are present in `game.toml`, `game_aot.toml`, and
`game_autocompile.toml`.

## Validation performed

- `recompiler_patch_test.exe`: parser, invalid-opcode rejection, native true and
  false emission, and same-VA overlay mismatch all pass.
- `psxrecomp-game.exe --config game.toml`: 2726 functions regenerated.
- `build-spawn-dbg/Tomba2Recomp.exe`: runtime rebuilt successfully.
- Native overlay preflights for regions `80022000` / `80073000`, forced
  interiors `8002B278` / `8007712C`: both DLL shards built successfully
  against overlay ABI v19 / codegen v9 (hash `3213685b`).
- 21:9 adaptive beach sequence: pig and distant models present; terrain intact;
  attract demo returns normally.
- 16:9 fixed beach sequence: pig present at revealed edge; terrain intact.
- true 4:3 control: `x_margin = 0`; helper returns the original comparisons.

## Related findings that must not be conflated

`80069B6C` is a symmetric 3-axis proximity window used by behavior state
machines. Existing `bias_sites`/`range_sites` around this function therefore
affect behavior reach, not only draw culling. Do not add more sites of that
shape without reading their callers.

Centered 12-bit angle windows in `FUN_8001FAE0` and overlay variants around
`8010E2FC` are combat/hit cones. Widening them changes gameplay and is unrelated
to model participation.

The actor structure offsets recovered during the earlier investigation remain
useful for later AI/lifecycle work, but the Evil Pig visual repro did not
require a write trace of `+0x00`, `+0x06`, or `+0x145`: both actual rejection
paths write the model-visible byte at `+0x01`.
