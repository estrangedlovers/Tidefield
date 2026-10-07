# Tidefield progress

Read `CLAUDE.md` (rules) and `docs/ARCHITECTURE.md` (design) first.

## Status: phase 6 complete, phase 7 next

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

### Phase 3: sources, mixer, buses, Tide, harmonic gravity, Medium

**Built**
- DSP (`src/dsp`, no JUCE): `SampleBuffer`, Hermite `DelayLine`, `Scale` (12
  built-in scales) and `HarmonicGravity` (voice-by-voice key migration),
  `GranularCloud` (96-grain pool, perc/Hann/Tukey windows, spray, scan, reverse,
  harmonize, gravity), `ResonatorBank` (24 unity-gain two-pole modes: harmonic,
  scale-chordal or bell tunings; rain self-excitation; -200 dB floor gate against
  float limit cycles), `LiveInput` (channel, gain, low cut, soft gate), `Biquad`,
  `Processor` interface + `ProcessorFactory`, `FdnReverb` (8-line Householder FDN,
  modulated, lossless "Hold" freeze), `TapeDelay` (gliding time, wow/flutter, age,
  saturated feedback up to 110%), `Medium` (Digital, Cassette, Vinyl, Noisy sampler;
  3 ms common base delay so mix and 300 ms type crossfades are phase-coherent) and
  `MediumProcessor` for FX slots.
- Engine: fixed layout of 8 strips (drone, clouds 1-4, resonator, input, bloom) with
  level/pan/width/2 post-fader sends; 22 FX slots (2 inserts per strip, 2 per bus, 2
  on master) with generic `p1..p6 + mix` parameters whose meaning comes from the loaded
  processor; `FxSlot` crossfades processor swaps (50 ms) and retires old ones to the
  message thread; `FxManager` creates/prepares processors off the audio thread
  (default layout: reverb on bus A, delay on bus B). Clouds receive samples through
  per-cloud snapshot channels with a 30 ms fade-out/swap/fade-in. Tide scales every
  modulation rate. Harmonic gravity drives drone voices, grains and resonator modes.
  Medium sits on the master before the safety chain. Telemetry carries strip meters,
  grains, mode levels, voice notes, input level, tide, key and morph progress.
- `src/io` (JUCE, INTERFACE library): `loadSample` / `writeSample`.
- Render harness: `samples`, `fx`, `defaultFx`, `input` in scores;
  `scores/ecosystem.json` (90 s, every source, key change, tide, all Medium types).
- `resources/samples/*.wav`: four original synthesized one-shots
  (`tools/scripts/make_samples.py`). `tools/scripts/spectrogram.py` plots renders.
- Placeholder app restructured: Perform / Sources / Mixer / FX tabs, shared
  `ParamKnob` (follows telemetry, gold when live, Alt-click releases), sample loading
  per cloud (decoded off the message thread), FX type menus with processor-specific
  control names and value formatting; empty FX slots collapse. The modal "no device"
  prompt was replaced by a status warning and highlighted Audio Settings button.

**Verified**: 63 tests pass, including every factory processor allocation-free and
finite, reverb tail reaching exact zero and Hold sustaining within 6 dB over 40 s
without growing, delay self-oscillation bounded, resonator pitch accuracy and
stability at 60 s decay, Medium bypass exactness and click-free type changes, FX
hot-swap without clicks, cloud sample swaps, input arming, voice-by-voice key
migration, Tide scaling, and a full engine (every slot loaded, all clouds, input,
wander) with zero audio-thread allocations. All five scores pass `--strict`;
ecosystem renders 90 s in 4.4 s on one container core.

**Untested (needs the Mac)**: CPU on the M1 Pro with everything running; real
instrument input; how Medium types, reverb hold and the key morph *sound*.

### Phase 4: Catch, sample import, Bloom, sessions

**Built**
- `SampleHandle`: samples travel to the audio thread inside a handle that holds a
  `shared_ptr`; the message thread keeps its own reference (for saving), so the audio
  thread never touches a reference count. Engine keeps message-side mirrors
  (`getCloudSample`, `getBloomSample`).
- Catch: 40 s stereo master ring and mono input ring, written on the audio thread.
  `Command::Catch` answers with a `CatchReady` notice (start, length, source, target);
  `copyCatch` reads the region on the message thread (ordered by the notice queue's
  release/acquire) and detects a lapped region instead of returning torn audio.
  `CatchManager` fades the edges, normalises to -3 dBFS, refuses silence, and loads
  the chosen cloud or (Auto) the first empty, else the longest-unused one.
- `BloomSampler`: 8 voices, each with 6 playback taps and 8 grains; transforms
  Swell, Smear, Freeze (granular hold; a true spectral freeze arrives with phase 8's
  input freeze), Ghost, Constellation, Tape; per-note randomisation, gravity on note
  pitch, voice stealing, fast release before sample swaps. Notes reach the engine as
  `ControlEvent::Type::Note`. Bloom can excite the resonator.
- Sessions (`src/io/Session`): `captureSession` / `applySession` / `defaultSession`,
  `.tidefield` = zip of `session.json` (format tag, version, params by stable ID,
  scenes, pins, FX types, MIDI placeholder, sample index) plus 24-bit FLAC audio.
  Atomic save (temp file then move), migration hook per version, newer-version files
  refused, unknown IDs reported as warnings. `SnapParam` events recall without
  sweeping.
- App: Session menu (New, Open, Save, Save As; Cmd+N/O/S) with background load/save
  and a 1.5 s fade-out, swap, fade-in when playing; Catch button (K) with status
  feedback; on-screen keyboard and Bloom/Catch controls on Perform; Bloom section
  with one-shot loading on Sources; built-in samples compiled in (Bloom = glass,
  Cloud 1 = chord) so a first launch makes sound; knob panels pack sections side by
  side.
- Harness: `note`/`noteOff` events, `catch` command, `bloomSample`, `session`, and
  `--save-session`. Reports now default to `<output>.report.json` (the old default
  could overwrite a score sitting next to its output: found while debugging).
- New `tidefield_io_tests` executable (JUCE) for session round trips.

**Fixes found in this phase**
- Catch length/source/target posted with the Catch command applied one tick late
  (read from smoothed values; now from targets, and the target travels in the notice).
- Bloom forward taps died on their first sample when wobble dipped below unity speed.
- Scenes: a scene that does not mention a parameter used to pull it to its default;
  now it has no opinion (blends only among scenes that define it).
- Resonator was ~-75 dBFS at defaults (unity-gain-at-resonance normalisation barely
  rings long modes from short bursts). Now impulse-normalised mallet strikes, with an
  output-driven ducker that bounds sustained in-tune excitation.

**Verified**: 71 core tests + 4 io tests pass (Catch normalisation, fades, targets,
silence refusal, lapped-region detection, input catch; every Bloom transform audible,
bounded and finished; Tape pitch accuracy; voice stealing without allocation; notes
through the engine; session round trip including audio sample-accuracy within 24-bit,
recall into a fresh engine, warnings, refusal of newer formats and garbage files,
default session reset). All seven scores pass `--strict`, including Bloom+Catch with
a saved session that a second score recalls.

**Untested (needs the Mac)**: file dialogs, background save/load timing, keyboard
playing feel, Bloom transforms by ear.

### Phase 5: MIDI

**Built**
- Engine: one SPSC queue per MIDI port (up to 4 devices; each device's delivery
  thread is the sole producer of its queue), a monitor queue back to the UI, and a
  `MidiMap` snapshot with per-(source, channel, number) binding lists. Bindings map a
  CC to any parameter through a range and curve, or a CC/pad note to an action
  (Catch, fade toggle, panic, release live layer, capture scene). Buttons fire on a
  rising edge. Soft takeover per binding: waits until the controller reaches or
  crosses the value, re-arms when anything else moves the parameter; telemetry shows
  which way to turn. Notes play Bloom (channel filter), optionally set the drone root
  (folded into range), CC 64 sustain holds Bloom voices.
- `MidiManager` (message thread): bindings, learn for parameters and actions (one
  control drives one target when learned; sustain pedal never learnable), default
  layout CC 21-28 (terrain X/Y, Tide, wander, gravity, reverb return, cloud 1
  density, master level capped at 0 dB), readable descriptions.
- Sessions store the mapping; a session without one leaves the rig's mapping alone.
  The app keeps the rig mapping in its settings across launches.
- App: `MidiInputs` opens devices with per-device callback objects (no shared lookup
  on the MIDI thread), follows hot-plugging, remembers disabled devices. Knob
  right-click menu: MIDI learn / forget, release to terrain, reset. Learning knobs
  turn red; waiting-for-pickup knobs show an arrow. New MIDI tab: devices, note
  channel, notes-to-drone, learn buttons for actions, binding list, activity readout.

**Verified**: 80 core + 5 io tests pass, including pickup catching by approach and by
crossing, re-pickup after another source moves the value, ranges and curves, mixed
any-channel and channel-specific bindings, rising-edge actions from CCs and pads,
note channel filtering, sustain pedal holding voices, notes to drone root, learn
replacing an old target, the default layout's master cap, MIDI session round trip,
and zero allocations while handling a stream of CCs and notes.

**Untested (needs the Mac and a controller)**: real devices, hot-plugging, CoreMIDI
threading, how pickup feels in hand.

### Phase 6: React WebView UI

**Built**
- `AppCore` (managers + message-thread pump) shared by two front ends: `WebUI`
  (the instrument's face) and `ClassicUI` (the former placeholder, now a fallback:
  `--classic` / `TIDEFIELD_CLASSIC_UI=1`, or automatic when the built UI is missing).
- `WebUI`: WebBrowserComponent with one native function and event pushes, resource
  provider for the built UI (bundle `Resources/ui`, next to the binary, or the source
  tree in dev builds), `TIDEFIELD_UI_DEV` for Vite hot reload, file choosers for
  samples, audio settings dialog, sample peaks for waveforms.
- `io/UiProtocol`: schema (params, strips, slots, processors with declarative display
  maps, scales, names, limits) and a delta telemetry encoder (tested).
- `dsp::DisplayMap` replaces per-processor format functions (C++ and TS format the
  same way; tested on both sides).
- `ui/`: Vite + React 19 + TypeScript, Inter variable font, tokens. Bridge (JUCE
  protocol client) and a browser mock engine using the real schema. Store with
  per-parameter subscriptions. Components: Knob, Fader, Choice (pills/tiles), Button,
  Meter, Keyboard, Section, ContextMenu, Toasts. Visuals: TerrainView (living field),
  Waveform with grains, pitch lanes. Views: Perform (with responsive rules for short
  windows) and Edit (global, drone, clouds, resonator, Bloom, input, mixer, effects,
  scenes, MIDI).
- CMake builds the UI with npm (`tidefield_ui`) and copies it into the app; a ctest
  guards the mock schema against drift.

**Verified**: TypeScript strict typecheck, vitest (format/mapping parity with the
engine), production build; screenshots of every page in the browser mock at 1440x900
and 1100x720; the real app on Linux (WebKitGTK under Xvfb) loads the UI, completes the
`hello` handshake and shows the engine's real state. 89 ctest tests pass.

**Untested (needs the Mac)**: WKWebView specifics (first-mouse, keyboard focus,
scrolling feel), live telemetry and visuals with real audio running (here the mock
drove the visuals; the container has no sound device), text input focus inside the
WebView for scene renaming, DPI on a Retina display.

## Next: phase 7
Recording the master (and optional stems) to disk, CPU guardrails (load measurement
in the engine, degradation policy with hysteresis, voice/grain caps), and polish.

## How to run
```
cmake --preset headless && cmake --build --preset headless
./build/headless/tests/tidefield_tests
./build/headless/tools/render/tidefield_render scores/drone_basic.json -o out/drone_basic.wav --strict
# macOS app:
cmake --preset dev && cmake --build --preset dev   # app in build/dev/src/app/Tidefield_artefacts/
```
