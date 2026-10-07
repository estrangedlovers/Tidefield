# Tidefield progress

Read `CLAUDE.md` (rules) and `docs/ARCHITECTURE.md` (design) first.

## Status: phase 2 complete, phase 3 next

### Phase 1: skeleton, device settings, safety chain, drone, render harness

**Built**
- CMake project (JUCE 8.0.15 and Catch2 3.9.1 via FetchContent), `CMakePresets.json`
  with `dev`, `release` and `headless` presets. App target defaults on for macOS only.
- `tidefield_dsp` (no JUCE): `Smoother` (linear, one-pole, log-domain), `Denormal`
  (`flushDenormal`, `ScopedFlushDenormals` for x86 and arm64), `Random` (xoshiro128+),
  `Svf`, `OnePole`, `DcBlocker`, `Drift`, lookahead `Limiter`, `DroneGenerator`.
- `tidefield_engine` (no JUCE): `SpscQueue`, `ControlEvent`, `TelemetryFrame` /
  `EngineNotice`, X-macro `ParamDefs` + `ParamRegistry` + `ParamState`, `ChannelStrip`
  (level/pan/width), `MasterChain` (guard, level, fade, DC blocker, limiter, panic),
  `Engine` (32-sample control ticks aligned to absolute time).
- `tidefield_render`: score JSON -> WAV + analysis JSON (peak, RMS, DC, non-finite
  count, samples above ceiling, longest silence, RMS per second). `--strict` fails on
  safety violations. Scores in `scores/`.
- `Tidefield` app: `AudioHost` (device manager, persisted device state, load
  measurer, mic permission and hardened-runtime audio-input entitlement), placeholder
  `MainComponent` (audio settings dialog, fade in/out, panic/resume, 15 parameter
  knobs, peak meters, drone voice dots, CPU/xrun/limiter/guard status).
- 30 unit tests, including: `Engine::process` performs zero allocations (global
  `operator new` counter), output identical across host block sizes, limiter never
  exceeds ceiling, filters decay to exact zero, panic silent within 60 ms, NaN guard.

**Verified (Linux container)**
- `headless` preset builds with zero warnings under `-Wall -Wextra -Wconversion ...`.
- All tests pass.
- Renders: `drone_basic` (40 s), `drone_evolve_long` (180 s), `safety_panic` (12 s,
  random block sizes, +12 dB drive, panic and resume) all pass `--strict`. Peak holds at
  exactly -1.0 dBFS on the driven score; no non-finite samples; DC < 3e-4.
- App compiles on Linux and runs under Xvfb without crashing (no audio device there).

**Untested (needs the M1 Pro)**
- Building the `.app` on macOS / in CLion, code signing, mic permission prompt.
- Real audio devices, device switching while running, sample-rate changes
  (`ParamState` keeps values across re-prepare, but untested live).
- `ScopedFlushDenormals` arm64 path (FPCR FZ bit); only the x86 path ran here.
- How the drone actually *sounds*; renders were checked numerically only. Listen to
  `out/*.wav` after running the scores. Drone sits around -30 dBFS RMS at default
  level, which may want a gain-staging pass once more sources exist.

**Known limitations**
- Inputs are ignored until the live input source (phase 3).
- Tide is fixed at 1.0 (the drone already takes a `timeScale`).
- Control events are applied at the start of each host block (not sample-accurate
  within it). Fine for UI/MIDI; revisit for gesture playback.
- The 8 s default fade length means "Fade In" is slow by design; lower it in the panel.

### Phase 1 review pass (plugin-developer eyes)
Re-read everything after phase 1. Verified the limiter's guarantee by hand (the
delayed sample is always inside the sliding-minimum window, so the box average can
never exceed its required gain). Changes made:
- Gain staging: drone default 0 dB, voice gain raised, channel strip switched to the
  conventional equal-power law (-3 dB centre, 0 dB edges). Default patch now peaks
  around -8 dBFS with RMS near -24 dBFS instead of -31 dBFS.
- Breath noise is now per voice (was one shared noise source feeding all six
  filters, which collapsed the stereo image of the noise component).
- Oscillator sine term uses `fastSin01` (18 `std::sin` per sample removed; accuracy
  test added, error < 0.2%).
- Placeholder UI: grouped sections, fade/panic buttons reflect engine state, Space
  toggles fade, Esc toggles panic, Audio Settings opens automatically when no output
  device could be opened.
- `Engine::process` contract documented for the future plugin adapter (in-place
  safe, variable block size).
Nothing in the audio path allocates, locks or logs; the allocation test still passes.

## Decisions confirmed by the user
- Terrain vs direct edits: live layer override + explicit "commit to scene".
- Every `kMidiLearnable` parameter is learnable.
- Plugin (AU/VST3) build is planned; Engine keeps the `AudioProcessor` contract.
- Imported shimmer/fuzz are JUCE-based: separate `tidefield_fx_juce` library;
  `dsp` stays JUCE-free.
- Medium (recording type) sits on the master, heard live and printed.

- The whole ambient feature catalogue (ARCHITECTURE.md section 10) is approved for
  phase 8. The user asked for phases to continue back to back, stopping only when
  input is needed.

### Phase 2: scenes and terrain

**Built**
- `SnapshotChannel<T>`: two SPSC queues (publish, retire) moving immutable snapshots
  to the audio thread; retired ones are deleted by `collectGarbage()` on the message
  thread. In-flight count is bounded so neither queue can overflow.
- `SceneSet` (dense matrix of terrain-bound, unpinned parameters; log-taper columns
  stored as logs, discrete columns pick the strongest scene), `TerrainMath`
  (inverse-distance weights with `terrain.focus` as the power), `Wander` (Drift = OU
  walk, Orbit, Tide pool attracted to the nearest scene).
- Engine: cursor glide (`terrain.glide`), wander, terrain evaluated every 4 control
  ticks, live layer (any SetParam on a terrain-bound parameter while scenes exist holds
  it; `ReleaseParam` / `ReleaseLiveLayer` hand it back). Telemetry now carries cursor,
  effective position, per-scene weights, every parameter's target and the live mask.
- `SceneManager` (message thread): add, capture current sound, move, rename, delete,
  set value, pin, commit live layer, release. Rebuilds and publishes on every edit;
  `tick()` retries publishes the snapshot queue refused, so rapid edits are never lost
  (a bug found while writing the tests).
- Render harness: `scenes`, `pins` and the `releaseLive` command in scores;
  `scores/terrain_sweep.json`.
- Placeholder app: terrain pad (drag cursor, double-click to capture, drag scenes,
  right-click for commit/replace/rename/delete), Capture and Release buttons (C / R),
  wander style menu, terrain knobs. Knobs follow the terrain and turn gold while held
  in the live layer.

**Verified**: 42 tests pass (IDW properties, log and discrete blending, morphing,
glide, live layer, commit, capture, pinning, wander bounds, tide pool statistics,
snapshot hand-off and a 32-scene wandering terrain with zero audio-thread
allocations). `terrain_sweep` passes `--strict`. App compiles and the layout renders.

**Untested**: interaction feel of the pad on a real trackpad; how morphs sound.

## Next: phase 3
Granular cloud (4 slots), resonator bank, live input; mixer with sends A/B, FX
chains with a `Processor` interface and factory, reverb and delay; Tide clock;
harmonic gravity; the Medium stage on the master.

## How to run
```
cmake --preset headless && cmake --build --preset headless
./build/headless/tests/tidefield_tests
./build/headless/tools/render/tidefield_render scores/drone_basic.json -o out/drone_basic.wav --strict
# macOS app:
cmake --preset dev && cmake --build --preset dev   # app in build/dev/src/app/Tidefield_artefacts/
```
