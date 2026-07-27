# Tomba! 2 aspect-aware world participation

Status: validated implementation in progress on
`feat/tomba2-aspect-participation` (2026-07-26).

This document replaces the superseded experiment that forced thirteen
distance/cone comparisons. That experiment conflated several systems, changed
gameplay reach, admitted candidates without queue policy, and could reproduce
the black-screen/softlock signature. None of those thirteen
`widescreen.cull.keep` records is enabled.

## Desired behavior

At 16:9, 21:9, and during adaptive resizing, terrain and resident objects that
intersect the newly revealed horizontal field should participate before they
enter the old 4:3 field. Composite models must remain whole at the wide edges.
True 4:3 must execute the original comparisons exactly.

The implementation is deliberately not “infinite spawning.” It does not load
other rooms, expand actor pools, remove distance/near-plane checks, or activate
AI and combat outside their authored ranges.

## Fresh investigation

The investigation used the additive overlay-capture history plus the headless
Ghidra MCP program `psx/Tomba2`. Overlay addresses were accepted only when the
complete captured instruction matched; the composite RAM image was not treated
as proof for mutually exclusive overlay bodies.

The visible symptom separates into three relevant paths:

1. terrain-cell frustum selection;
2. engine model-list participation;
3. a lower-level per-model/per-child visibility bit.

Actor behavior, hit cones, room loading, and fixed queue capacity are separate
systems and remain unchanged.

### Terrain-cell selection

Six captured overlay constants are 12-bit angular half-extents used by the
terrain producers:

| address | expected instruction |
|---|---:|
| `8010E9A0` | `240301C7` |
| `8012EEB8` | `2403012C` |
| `8013F138` | `24020155` |
| `8013F190` | `240301C7` |
| `8013F224` | `2402013E` |
| `8013F244` | `240201C7` |

The runtime widens `tan(angle)` by the current horizontal reveal factor. It
does not add pixels to angle units. The complete instruction word guards every
site, so an unrelated overlay body at the same virtual address is untouched.

Measured values:

| client aspect | per-side extent (including 16px guard) | max vanilla | max widened |
|---|---:|---:|---:|
| 4:3 | 0 | 455 | 455 |
| 16:9 | 69 | 455 | 571 |
| 21:9 | 136 | 455 | 651 |

### Engine model-list participation

`FUN_8007712C` clears `model+1`, evaluates distance and a camera cone, marks an
accepted model visible, and inserts types 2/9, 4, and 5 into fixed scratch
queues. Their capacities are 24, 40, and 28 entries.

Only the six signed cosine-reject instructions are aspect-aware:

| address | expected instruction |
|---|---:|
| `800772D4` | `28620370` |
| `80077368` | `28620358` |
| `80077414` | `28620358` |
| `800774A8` | `28620370` |
| `8007753C` | `28620350` |
| `800775D0` | `28620368` |

Every vanilla acceptance is preserved. A vanilla rejection is reconsidered
with the horizontal cone scaled to the live aspect; the vertical cone and
distance checks stay vanilla.

The visible field has priority over the guard/hysteresis region. Guard and
hysteresis candidates are rejected when a relevant queue reaches
`capacity - 4`, reserving four slots for later visible candidates. The game’s
own capacity checks remain intact.

### Composite-child participation

The user’s new reproduction—an old man losing his body while his arms remain,
and a foreground barrel losing only some pieces—identified a lower-level
per-child gate.

Headless Ghidra shows `FUN_8002B278` computing:

```c
delta = object_position - camera_position;
distance = sqrt(delta.x² + delta.y² + delta.z²);
if (distance < 0x200)  reject;
if (distance >= 0x1C01) reject;
if (dot(camera_forward, delta) < distance * 0xD60) reject;
object[1] = 1;
```

The exact cone instruction is:

| address | expected instruction | threshold |
|---|---:|---:|
| `8002B368` | `0082202A` (`slt a0,a0,v0`) | `0x358` Q10 |

This site uses object register `s4` and camera-relative X/Z/Y registers
`s3/s2/s1`. It does not append to the three fixed model queues, so its
per-site `queue_guard` is false. Its near and far distance checks are not
changed.

This is the missing half-cull fix: individual children that vanilla accepts
remain accepted, while children in the aspect-derived horizontal fringe are
kept with the same vertical and distance envelope as 4:3.

## Runtime policy

- visible envelope: current client aspect, excluding the 16px guard;
- activation guard: 16px outside the visible horizontal edge;
- deactivation hysteresis: another 24px outside the guard;
- true 4:3: zero margin and the original comparison result;
- native generated, GCC overlay, and dirty-interpreter paths call the same
  helper;
- relevant aspect policy and every exact site contribute to overlay cache
  identity.

Changing window dimensions updates the envelope immediately. No recompilation
or cache rebuild is needed because the generated code calls a live runtime
helper.

## Safety exclusions

- `80069B84`–`80069BD8` are absent. `FUN_80069B6C` is a behavior/teleport
  proximity trigger, not a pure visibility test.
- The old far-distance sites (`8002B310`, `80077248`, `800772E4`,
  `80077380`, `80077390`, `80077424`, and `8007754C`) are not forced.
- Combat/attack angle windows in `FUN_8001FAE0` and overlay variants are not
  changed.
- Queue-capacity branches are not removed.
- Actor pools and unloaded rooms are not expanded.
- No address-only global comparison rewrite is used.

## Validation evidence

Compiler/runtime validation:

- guarded recompiler parser and code-generation suite: all tests pass;
- aspect-cone math suite: all tests pass;
- game regeneration: 2,726 functions, six shards;
- overlay ABI: 21;
- codegen hash: `4cc1a66b`;
- game-codegen config hash: `f8403a93`;
- bundled OpenBIOS regenerated after the final emitter change;
- fresh-cache GCC autocompile completed 18 observed runs with zero process
  failures, zero failed shards, and a final one-built/28-safely-skipped shard
  result.

Execution-path validation:

- hybrid/native cache loaded the new identity and executed native overlays;
- interpreter control (`overlay_native_off`) produced zero native dispatches
  during the sample and 107,523 interpreted dispatches;
- in that interpreter sample, exact site `8002B368` ran 4,033 times and made
  122 additional wide-visible keeps;
- in the 21:9 starting-area pass, exact site `8002B368` ran 17,015 times,
  with 5,556 additional visible keeps, 34 guard keeps, six hysteresis keeps,
  and zero queue rejects;
- model-queue high-water marks were 16/25/6 against capacities 24/40/28, with
  zero aggregate queue rejects.

Sustained 21:9 hybrid/native soak:

- 119 samples covered 595.859 seconds and frames 39,458 through 74,683;
- the guest frame counter advanced in every sample interval;
- median guest rate was 59.866 Hz and native dispatches increased by
  14,053,359 during the sampled intervals;
- candidate, range-index, lazy-manifest, and aspect-queue overflow counters
  remained zero;
- the three reported stale blocks were live overlay CRC transitions; every
  loaded library used the current `cg9_4cc1a66b_gcf8403a93` cache identity;
- the process remained responsive after the monitor exited, and the final
  host-window capture was 1280x548.

True 4:3 identity:

- live resize selected mode 0 and `x_margin = 0`;
- during a five-second gameplay sample, all 111 calls to `8002B368`
  incremented only its `identity_43` counter;
- visible, guard, hysteresis, outside-reject, and queue-reject counters did not
  change.

Dynamic sizing:

- 4:3 -> mode 0, margin 0;
- 16:9 -> native-wide mode 2, margin 69;
- 21:9 -> native-wide mode 2, margin 136;
- every transition occurred in the same running process.

Visual evidence in the local validation build:

- `build-t2/wincap_exact_user_route/cap_12.bmp`: old man is a complete model
  at the doorway;
- `build-t2/wincap_exact_user_route/cap_18.bmp`: foreground barrels remain
  complete through the wide view;
- `build-t2/next_demo_check.bmp`: starting-area Evil Pig and distant objects
  participate in the initial 21:9 view;
- `build-t2/wincap_half_cull/cap_00.bmp` and `cap_15.bmp`: lava/mine terrain
  and models remain complete across the 21:9 frame;
- `build-t2/soak_21x9_final/final_host_window.bmp`: actual presented window
  after the sustained hybrid/native soak;
- window-capture motion checks remained live on every gameplay pair.

The first clean-cache boot made before OpenBIOS regeneration correctly failed
validation: the build had warned of an emitter fingerprint mismatch and the
starvation dump stayed in OpenBIOS exception/SIO code (`BFC21xxx`) without
reaching the Tomba visibility sites. After canonical OpenBIOS regeneration,
the same empty-cache boot remained healthy and autocompiled successfully.

## Remaining scope

This solution covers candidates already resident in the loaded area. It does
not deliberately create actors from unloaded rooms or run distant AI early.
If a future scene proves that a visible enemy is not resident at all, that
scene needs separate allocation/lifecycle attribution before any pool change.
Literal maximal participation is intentionally not implemented: it is
unnecessary for the current reproductions and would require producer,
consumer, storage, and overflow analysis for each fixed pool.
