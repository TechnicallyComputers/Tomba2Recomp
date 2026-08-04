# Tomba 2 Known Issues

## First-person camera experiment

Status: parked on `codex/tomba2-first-person-experiment`; not suitable for
merging or release. The accompanying runtime work is parked on the
`psxrecomp-v4` branch `codex/digital-pad-raw-stick-samples`.

### Intended behavior

The experiment adds a disabled-by-default mod that toggles first-person view
with Select. The final parked snapshot restores the earlier direct camera-mode
implementation from `1fc1959`, because it was visually closer to the intended
result than the later render-boundary implementation. The left stick maps
forward intent to the nearest authored path direction, a backward press requests
one stock turn input, and the right stick controls free look. Horizontal
movement input is intentionally neutral.

The branch also disables and hides Turbo Loads for Tomba 2. Its shared runtime
branch exposes raw stick samples to digital-pad mods and maps Select on
supported modern controllers. It also contains the trusted guest
render-boundary hook developed for the discarded follow-up approach; that hook
is retained for provenance but is not used by the final parked first-person
implementation.

### Unresolved failures

Manual gameplay testing still shows fundamental regressions:

- actors and geometry are culled incorrectly from the first-person view; evil
  pigs can disappear when Tomba approaches them;
- turning and reversing are unstable, including spinning, flicker, camera/player
  orientation disagreement, and forward input using a stale direction;
- normal interactions are unreliable, especially jumping onto, capturing,
  carrying, and throwing pigs;
- special traversal and interaction states such as seesaws can clip Tomba
  through the map or reveal scenery below the ground;
- switching views during active interactions does not consistently preserve
  the stock third-person behavior.

Two approaches were tested. Direct camera-mode control looked more visually
coherent but exhibited the failures above. A later render-boundary view
override, preserved in commits `e4b331f` and `f431141`, caused even more severe
problems and was rolled back before parking. Unit tests cover the host-side
toggle, input translation, and camera state machine, but they do not model the
game's actor, collision, culling, or scripted interaction systems and therefore
do not establish gameplay correctness.

### Technical assessment

Neither changing Tomba 2's camera mode directly nor substituting one late
scratchpad view matrix produced a safe separation between presentation and
gameplay. Tomba 2 appears to consume camera/view state at multiple stages, and
the exact consumers responsible for actor visibility have not been attributed.
The game's path-constrained movement, facing, action, and scripted state are
also tightly coupled; translating camera-relative intent back into digital
Left/Right inputs can leave the view, player animation, and interaction state
disagreeing.

Do not continue by adding more coarse state guards or by directly writing
Tomba's facing/action fields. A future attempt should start by:

1. tracing the per-actor and terrain culling decisions for a reproducible pig
   disappearance;
2. identifying every consumer and lifetime of the scratchpad/GTE view state;
3. locating a renderer-side transform point after game-owned culling and
   interaction decisions are complete;
4. validating stock input and interaction traces byte-for-byte before adding
   camera-relative movement; and
5. testing view entry/exit across pigs, seesaws, doors, ladders, path-depth
   changes, scripted cameras, and transitions.

### Parked revisions

- Tomba 2 project: `codex/tomba2-first-person-experiment`; final implementation
  restored from `1fc1959`
- Discarded render-boundary attempts retained in history: `e4b331f`, `f431141`
- Shared runtime: `b1fa93d` on `codex/digital-pad-raw-stick-samples`

## OpenGL renderer is substantially slower than software

Status: resolved on `feat/opengl-performance`. OpenGL is again the default.

### Resolution (2026-07-11)

The slowdown had three compounding causes in the shared GL backend:

- the always-on present history synchronously sampled both the backbuffer and
  VRAM with `glReadPixels` every frame;
- Tomba 2's alternating opaque/mode-0 transparent draw stream forced hundreds
  of isolated two-pass batches per gameplay frame;
- its 30 Hz double-buffered output was blitted/swapped twice at the 60 Hz guest
  vblank rate even though the displayed buffer was unchanged.

The renderer now keeps present metadata without readback (pixel probes are
opt-in through `PSX_GL_PRESENT_PROBE=1`), uses dual-source painter-ordered
batching for the safe blend modes, rebuilds the mask stencil lazily only when a
game enables destination-mask checking, and suppresses duplicate presents using
VRAM dirty tiles. The default present is a texture quad rather than the slow
scaled default-framebuffer blit path.

On the same NVIDIA test system and warm overlay cache, steady Beach Town work
dropped from roughly 110 textured batches to 25-43, while native-wide mirror
cost remained about 1 ms. Unskipped FMV pacing holds a 16.68 ms median. The
production build sustained 598 guest frames over a 10-second steady-gameplay
sample (59.8 FPS), up from the former 30-35 FPS collapse; debug GPU timer-query
builds remain intentionally slower than production.

Optional presentation-only temporal blending is available through **Tomba 2
Frame Blending** on the launcher's Mods page. A shared OpenGL presentation
context combines the two most recent completed display images at the selected
target while the guest, audio, and input continue at their original cadence.
It adds one source-frame of display latency but does not accelerate game logic.
**Display refresh** preserves the framework's zero sentinel and follows the
measured monitor rate. Motion-adaptive clarity suppresses blending on large
pixel changes to reduce double-image trails.

The production build was exercised in both native 4:3 and native-wide 16:9 on a
165 Hz panel. Steady Tomba 2 gameplay produced 29.96-30.16 new display images/s
(the title's normal double-buffered output) while the independent presentation
thread sustained 165.00 presents/s. The 59.94 Hz guest pacing remains unchanged;
the interpolation-disabled path also starts and runs normally.

The final unattended acceptance covered Beach, Whoopee Camp/FMZ, Mines, Mine
Cart, and the repeat boundary for 540 seconds. Across 107 production-safe
five-second records, guest cadence was 59.76 Hz minimum / 59.94 Hz median, audio
reported zero underruns and zero post-start overflows, and the settled cache
remained at 849 DLLs. Current-code screenshots separately confirmed each named
segment. Shared overlay fixes were also required: exact cached helpers reached
inside interpreter-local flow now promote to native without regressing CPS
continuations, small dynamic-text DLL images are mapped ahead of use, and
coverage-manifest serialization no longer blocks the emulation thread.

### Original regression (historical)

Originally, the OpenGL renderer made both the Whoopee Camp logo/FMVs and regular
gameplay visibly sluggish. The measurements below are retained as the pre-fix
baseline.

### Reproduction

1. Build the Release target with debug tools enabled.
2. Set `renderer = "opengl"`, `supersampling = 1`, and
   `texture_filtering = "nearest"` in the active settings.
3. Use either 4:3 or 16:9. The Whoopee Camp logo is slow in both; 16:9 also
   makes the gameplay cost easy to profile in the first unattended attract demo.
4. Launch with the Tomba 2 game config and a debug port:

   ```powershell
   .\Tomba2Recomp.exe --game ..\game.toml --no-launcher --debug-port 4615
   ```

5. Query the always-on measurements:

   ```powershell
   python ..\psxrecomp-v4\tools\raw_tcp.py 4615 frame_perf
   python ..\psxrecomp-v4\tools\raw_tcp.py 4615 latency window=240
   python ..\psxrecomp-v4\tools\raw_tcp.py 4615 overlay_loader_status
   python ..\psxrecomp-v4\tools\raw_tcp.py 4615 dispatch_stats
   python ..\psxrecomp-v4\tools\raw_tcp.py 4615 fmv_state
   ```

The first Beach Town attract demo starts without controller input. FMV skipping
may be enabled to reach it sooner, but note that auto-skip still executes the
guest MDEC decode and teardown path; it suppresses pacing, audio, and most
presents rather than deleting guest work.

### Measurements from 2026-07-10

Configuration: Release build, OpenGL, 1x supersampling, nearest filtering,
16:9 native-wide, warm overlay cache.

Whoopee/FMV sample (256 frames):

- Total: 60.250 ms/frame average.
- Emulation/CPU phase: 58.708 ms average.
- Scene GPU: 54.510 ms average, 3155.060 ms maximum.
- Present GPU: 5.741 ms average.
- Observed rate: roughly 16 FPS.

Steady Beach Town gameplay sample after transition frames aged out (256 wide
frames):

- Total: 28.808 ms/frame average, 56.365 ms maximum.
- Frame-period median: 30.211 ms; 95th percentile: 47.912 ms.
- Scene GPU: 20.714 ms average.
- Canonical scene: 17.019 ms average.
- Native-wide mirror: 3.694 ms average.
- Present GPU: 8.052 ms average.
- CPU upload/flush: 2.998 ms average.
- About 945 primitives and 110 textured batches per frame.
- Observed steady rate: roughly 33-35 FPS.

The software renderer previously measured about 17.9 ms/frame in the same
worktree and did not exhibit the severe logo slowdown, which is why it is now
the title default.

### Evidence excluding other causes

- `dispatch_stats`: `miss_total = 0`, `miss_unique = 0` during the gameplay run.
- `overlay_loader_status`: no unregistered functions; the warm cache loaded the
  expected native overlay fragments.
- The 16:9 mirror averaged only 3.694 ms. The canonical OpenGL scene plus the
  presentation path dominates, so disabling the Beach backdrop fix would not
  solve the slowdown.
- The logo is slow in 4:3, where native-wide is inactive.

### Investigation targets

1. Profile synchronization in the canonical OpenGL path. The reported
   emulation/CPU phase tracks scene-GPU cost closely, suggesting serialized GL
   work, timer-query waits, readbacks, upload flushes, or implicit driver sync.
2. Inspect the MDEC-to-OpenGL upload/display path. FMV frames have very few
   primitives but disproportionately high scene-GPU time and multi-second
   maxima.
3. Reduce textured batch count or state churn in gameplay. Beach Town averages
   roughly 110 batches for 945 primitives.
4. Attribute the approximately 8 ms OpenGL present cost independently of the
   3.7 ms native-wide mirror.
5. Use `gl_ws_ablate`, `gl_wide_fast`, `frame_perf`, and `gl_present_ring` for
   controlled A/B measurements. Do not infer performance from visible speed
   alone.

### Acceptance criteria

- Whoopee Camp and streamed FMVs run at full speed with OpenGL in 4:3 and 16:9.
- Steady Beach Town gameplay sustains the intended frame rate without large
  frame-time spikes.
- OpenGL is no slower than the software renderer by enough to cause frame-rate
  loss on the same machine and settings.
- The 4:3 canonical image and the validated 16:9/21:9 Beach Town backdrop remain
  visually correct.
- Static dispatch remains at zero misses and no unregistered overlay functions.
