# Tidefield progress

Read `CLAUDE.md` (rules) and `docs/ARCHITECTURE.md` (design) first.

## Status: 1.2.0, tempo sync, gestures, projector window, device presets; debugged and optimised

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

### Phase 7: recording to disk, CPU guardrails, polish

**Built**
- `engine/record/RecordTap`: the engine's output into a preallocated SPSC ring (4 s
  at full stem width) with an Idle / Running / Stopping / Stopped state machine. The
  audio thread latches the channel count per block, drops a whole block rather than
  letting channels slip if the writer stalls, and never resets the write index (a new
  take moves the reader instead), so start and stop never race the producer.
- Stems: `ChannelStrip::processAdd` can also write its post-fader signal; the engine
  fills strip and bus-return stems only while a stem take runs. Channel layout:
  master (post-Medium, post-limiter: what the speakers get), the 8 strips, send A
  and send B returns. The stems sum to the mix before the master chain.
- `io/Recorder`: background writer thread, 32-bit float WAV, `master.wav` plus
  `stems/<strip>.wav` in `~/Music/Tidefield/<date> <session>/` (folder choosable).
  Ends a take cleanly on a write failure (full disk) and when the sample rate changes;
  reports dropped frames; a stop with no audio running is forced after 1 s.
- App: record controls in `AppCore` (stems preference and folder in the app
  settings), a MIDI action "Record" (footswitch), web UI record button with timer,
  stems marker and right-click menu (stems, folder, reveal), Shift+R; classic panel
  button with the same menu. Render harness `--stems <dir>` writes a take offline
  through the same tap.
- `engine/guard/DegradationPolicy`: smoothed load (fast rise, slow fall), steps down
  after 0.35 s above 80 % or at once on a block over budget, steps back up after 6 s
  below 50 %, doubling that hold (to 60 s) when it has to step down again soon after
  stepping up. Six levels trim cloud grains (96 to 6), resonator modes (24 to 6),
  drone voices (6 to 2) and Bloom polyphony (8 to 3).
- The engine times each `process()` (steady clock) when guardrails are enabled (the
  app enables them; renders and tests leave them off so output stays deterministic).
  Load and level go out in telemetry; the top bar shows the load and a "lite" badge,
  and a toast says when quality is reduced and restored.
- Polish found on the way: lowering the resonator's mode count (by hand or by the
  guardrails) used to cut ringing modes dead, an audible click; removed modes now
  stop taking excitation and ring out over 80 ms. Bloom gained a voice limit that
  releases surplus voices over half a second.

**Verified**: 98 ctest tests pass. New: tap state machine and whole-block dropping;
the recorded master equals the engine output bit for bit; stems present, silent when
their source is, and summing to within 3 dB of the master; zero allocations while
recording stems with guardrails on; policy thresholds, hold times, back-off, spike
response, NaN rejection; engine levels follow forced load and reset when disabled;
resonator mode cuts ring out. io test: the recorder's master.wav is bit-exact with
the output across a background-thread and synchronous drain, stems exist and match
in length, back-to-back takes. `tidefield_render --stems` on `ecosystem` produces a
master identical to the render's WAV. Web UI typecheck and build; the record button
checked in the browser mock; the classic panel under Xvfb.

**Untested (needs the Mac)**: real disk throughput while recording stems at 96 kHz
(22 channels, about 8 MB/s), the guardrails against real CPU load (thresholds may
want tuning once heard), reveal-in-Finder, the folder chooser in the WebView app.
Reverb quality is not part of the degradation ladder: effects are opaque
`Processor`s; if the FDN shows up in profiles it can grow a quality control.

### Phase 8: the ambient feature pack

**Built**
- Three new mixer strips (each with level, pan, width, sends and two inserts):
  - **Loop**: `Disintegrator`, a looper-pedal tape loop (record, close, overdub,
    clear; up to 60 s) whose every pass is rewritten through an erosion chain:
    narrowing band, saturation, level loss, hiss, and random permanent "oxide
    flakes". Records the live input or the mix itself (read one prepared block back
    from the catch ring, so it stays block-size independent).
  - **Weather**: `WeatherBed`, procedural wind (gust process, whistle, rumble),
    rain (hiss plus individual rising drops) and surf (7-13 s waves panned across).
  - **Freeze**: freeze all, the last two seconds of the pre-Medium mix held as a
    granular cloud while the rest of the mix ducks underneath.
- **Hold input**: `SpectralFreeze` (new radix-2 `Fft`) holds the live input as a
  wide spectral pad, audible whether or not the input monitor is on.
- **Modulation layer** in `ParamState`: normalised offsets on top of the smoothed
  value, never on targets. Used by:
  - **Swell** (hold S, or a pedal): sends bloom, filters open, clouds thicken;
    rises over Swell Rise, ebbs over Swell Ebb / Tide.
  - **Seasons**: up to eight minutes-long sine, triangle or drift curves on any
    continuous parameter, scaled together by `seasons.depth`, following Tide.
    `SeasonManager` publishes them; sessions store them.
- **Loops**: incommensurate note loops (17-41 s periods, one note each, snapped to
  the key) played into Bloom and/or struck on the resonator.
- Effects: **Ensemble**, **Spectral Blur** (blur, smear, drift, shimmer, tilt,
  freeze; identity at rest), **Worn Echo** (a full Medium inside the feedback loop),
  **Sympathetic Strings** (Karplus-Strong strings tuned to the key through
  `ModContext::harmony`).
- **Your shimmer and fuzz**: `src/fx_juce/AudioProcessorEffect.h` hosts any
  `juce::AudioProcessor` in an FX slot (parameters set from a message-thread timer,
  so the audio thread takes no locks). Add the sources and one line each in
  `src/fx_juce/UserEffects.cpp`.
- MIDI actions: Loop record, Loop clear, Freeze all, Hold input.
- UI: a gesture bar under the terrain (Swell, Freeze all, Looper with ring and pass
  count, Hold input, Loops with flashing voices, Wind/Rain/Surf), keys S (hold), F,
  L, Shift+L, I, E; Edit pages Gestures, Looper, Loops (lanes per voice), Weather,
  Seasons (editor with live curve readout and suggested targets).
- CI: `.github/workflows/macos.yml` builds, tests, renders and packages the app on
  Apple Silicon (macos-14) for every push and uploads `Tidefield-macOS-arm64.zip`.

**Verified**: 116 ctest tests pass. New: FFT round trip and bin accuracy; spectral
freeze holds a note within 6 dB after it stops, releases, and is decorrelated L/R;
looper pedal states, loop length, erosion wearing a loop down over 40 passes while
erosion 0 repeats it bit for bit; each weather element in a usable level range and
bounded; freeze all holding with the source gone and fully ducked; looper recording
the mix in the engine; input pad sounding unarmed; zero allocations with every new
source running; modulation offsets, clamping and discrete exclusion; swell timing
without moving targets; seasons hitting peaks and troughs on schedule and speeding up
with Tide; loops firing in key into Bloom; seasons through a session; every processor
fuzzed for finiteness, bounds and zero allocations; blur identity at rest (< -60 dB
error); strings tuned to D minor and ringing 8x more for an in-key note than a
quarter-tone off; worn echo timing and decay; a JUCE AudioProcessor hosted and
driven through the adapter. The macOS CI build passed (tests and strict renders on
arm64). UI checked in the browser mock at 1440x900.

**Untested (needs the Mac and ears)**: how all of it sounds; looper levels with a
real instrument; whether swell's targets and depths feel right; spectral freeze on
a real cello; the user's own JUCE plugins inside the adapter.

### After phase 8: auto master, more gestures, optimisation, debugging, review

**Built**
- **Auto master** (`dsp::AutoMaster`, after the Medium, before the safety chain):
  analysis of tonal balance (four bands against a warm ambient target), K-weighted
  short-term loudness and stereo balance; slow corrections through a three-band EQ,
  a 1.6:1 glue compressor (threshold from its own input's loudness, at most 6 dB),
  width with mono lows, and make-up to -23, -16 or -14 LUFS. Never lifts silence;
  bit-transparent and free when off; switching crossfades. This is adaptive DSP, not a
  trained model and not an online service.
- **More gestures**: Journey (a wander style that tours the scenes), the Shape pad
  (Colour and Space macros), Hush (hold H), Slow time (hold T).
- **Optimisation**: granular window per chunk and a shared-index Hermite fast path,
  hand-written FFT butterflies, trig-free spectral phase rotors, muted drone and
  resonator skipped. Stress score 17.6 s to 12.9 s for 60 s; typical patches ~4 % of
  one core (container x86; the M1 Pro should be faster).
- **Debugging**: sessions from before a parameter or slot existed now reset it; the
  auto master's glue could chase its own meter into heavy compression (fixed); held
  gestures release on focus loss; freeze all was ~9 dB under the moment it caught
  (made up); journey dwelt at the next scene instead of the current one (fixed);
  "0 Hz" readouts for slow rates; key and scale pickers overflowing.
- **Review build**: version 1.0.0, icon, copyright, deployment target applied
  correctly; `Tidefield --self-test` (bundled UI, factory sounds, full-engine render)
  run by CI on the shipped bundle together with architecture and minimum-OS checks;
  a version tag publishes a GitHub Release with the app.

**Verified**: 126 ctest tests; the whole engine suite under AddressSanitizer,
UndefinedBehaviorSanitizer and leak detection; a two-minute monkey test (every
parameter, command, note, scene, season and FX swap at random, varying block sizes)
stays finite and under the ceiling; the app's self-test passes on Linux and in macOS
CI; every macOS CI run green on Apple Silicon.

**Untested (needs the Mac and ears)**: everything audible: balance between sources,
whether the auto master's target shape suits your material, how the new gestures
feel, CPU on the M1 Pro under real load, and the app on your interface and controller.

### 1.3.0: a deeper instrument

Built in this round, each with tests:
- **Modulation**: 15 sources (4 LFOs, 2 randoms, input level and brightness, mix
  level, velocity, note pitch, mod wheel, pressure, terrain X/Y), up to 16 routes, a
  Modulation tab, Modulate with on every knob, depth arcs.
- **Plugin hosting** (app only): AU/VST3 effects in any slot, scanned with a crash
  file, state saved per slot, and Choose controls to point each knob at any plugin
  parameter. `tidefield_hostcheck` drives a test plugin, remaps a knob and reloads.
- **Sync and remote**: MIDI clock in and out, OSC in and out, MPE, Link behind
  `TIDEFIELD_WITH_LINK` (off by default because Link is GPL).
- **Undo and redo** for controls, scenes, routes, seasons, effects and timeline edits.
- **Bloom**: up to eight sounds across the keyboard, YIN pitch detection on load, a
  preview voice; Browser search, preview and favourites.
- **Timeline**: record a performance, replay from any point, erase, mute, smooth, trim,
  save as a session, render offline to master, stems or a seamless loop.
- **Installation mode**: launch session, auto fade-in, daily schedule, keep awake,
  device and panic recovery, log.
- **Space**: binaural headphones and 4/6/8-speaker rings, a Direction per source,
  Spread and Rotate; ring channels share the limiter (and feed its detector).
- **Drone**: 15 new controls (waves, chords, sub, FM ratio, tilt, filter type, key
  track, vibrato, tremolo, glide, revoice time, drive, breath tone), defaults identical
  to 1.2, 15 presets, `scores/drone_deep.json`.
- **Effects**: eight new built-in types (see below); 44 effect presets.
- **Content**: 90 factory sounds, 149 factory presets (105 instrument, 44 effect).
  Presets now reset any control they do not name, so they sound the same every time.

Debugging pass: an independent review of the new audio code found no crash, NaN or
audio-thread issues; its six audible or edge findings were fixed (ring speakers could
run past the limiter's detector, drone filter-type clicks, chord changes missed by
voices mid-fade, sample-rate-dependent breath tone, FM aliasing at high ratios,
binaural clicks on snapped direction jumps, lo-fi sample-rate reduction at 96 kHz,
filter resonance peaks). An AddressSanitizer build runs all 288 tests with no memory
errors; under ASan and parallel load the "faster than real time" render check can
miss its timing, and it passes alone.

Verified on Linux: 288/288 ctest, every score `--strict`, the app's `--self-test` and
`--ui-test` under Xvfb, `tidefield_plugincheck`, `tidefield_hostcheck`. Not verified:
the macOS build of this round (CI builds it), hosting real third-party plugins, speaker
rings on real multichannel hardware, Link against Ableton Live, and listening on
speakers.

### Factory sounds expansion (after 1.2.0)

- 41 new original sounds in `make_samples.py` (`expansion()`), 59 in all, in five
  Browser groups: Tonal (vibraphone, glass harmonica, music box, celesta, harp, koto,
  tongue drum, electric piano, bonang, temple bell, crystal bowl), Pad (warm analog,
  string ensemble, airy voices, reed organ, glass pad, cello section, chamber choir,
  warped tape), Drone (new: tanpura, cello drone, bowed metal, organ pedal, sub hum),
  Texture (forest at dawn, stream, distant thunder, radio static, vinyl crackle, fire,
  night insects, wind in wires, rain on a tin roof, underwater) and One-shot (new: wood
  knock, bowl strike, piano harmonic, metal scrape, breath swell, vocal swell, felt
  mallet).
- Modal, additive (formants applied per harmonic), plucked-string and noise-shaping
  synthesis; each sound has its own seed from its name, so adding one never changes
  another, and the 18 older files still regenerate bit for bit.
- Drones and the new textures are seamless loops: frequencies and modulation rates are
  snapped to whole cycles of the loop, noise is shaped in the frequency domain over the
  whole loop and events wrap around the end.
- Five presets for the new material: clouds Endless drone and Field recording; Bloom
  Struck halo, Rising voice and Scattered knocks (34 in all).
- Adds 32.1 MB (30.6 MiB) of 16-bit 48 kHz WAV, mono except Forest at dawn, Stream
  and Rain on a tin roof.
- Verified: every single-voice pitched sound within 2 cents of its root (detuned
  ensembles within 7), no clipping or DC, loop seams no larger than a normal sample
  step;
  self-test (59 sounds decode, 34 presets) and all 143 ctest tests pass on Linux.
  Not yet heard on speakers.

### Second factory sounds expansion and device presets

- 31 more original sounds, 90 in all, appended to `expansion()` so every older file
  still regenerates bit for bit (all 59 checked by SHA-256): Tonal (hang drum,
  glockenspiel, dulcimer, prepared piano, gong, lyre, bowed vibraphone), Pad (drifting
  pad, frost, hollow fifths, vowel morph, midnight pad), Drone (shruti box, bowed glass,
  low brass, overtone choir, granular hum, hurdy-gurdy, analog drone), Texture
  (snowfall, cave drips, harbour, distant bells, rain on glass, pine wind, frozen lake)
  and One-shot (reverse bell, bowed cymbal, rain stick, breath flute, sub bloom).
  Drones and textures are seamless loops built the same way as the first expansion.
- Adds 25.4 MB (24.2 MiB) of 16-bit 48 kHz WAV, mono except Harbour and Rain on glass.
- 60 more factory presets, 94 in all: 9 cloud, 8 resonator, 8 Bloom, 8 weather, 8
  medium, 7 cycles, and the first ones for the tape looper (6) and the live input (6,
  whose device now has a Presets menu). The self-test now also covers the looper and
  input kinds and checks every preset value lies in its parameter's range.
- Verified: each pitched sound's strongest partial sits on its root (or an octave or
  fifth of it for chords), loop seams no larger than a normal sample step, self-test
  passes on Linux (90 sounds decode; with the drone and effect presets merged, 149 presets). Not yet heard on speakers.

### Themes (after 1.2.0)

- Ten themes under Appearance: dark Slate, Night swim, Control room (after Logic Pro:
  near-black greys, blue selection, green and yellow states, Apple system colours), Ember, Graphite, Heather;
  light Paper, Dune, Sea glass, Daylight. Each light theme borrows a dark palette for
  its displays (Paper/Slate, Dune/Ember, Sea glass/Night swim, Daylight/Graphite), and
  the terrain's gradient now follows the display palette. Saved by id in settings.
- `--ui-test` steps through every theme with the projector open. Screenshots of all ten
  checked under Xvfb; not seen on a Mac.

### Design pass (after 1.2.0)

- Palette taken from the mark: Slate (default) and Paper themes, chosen under the
  session menu's Appearance and saved in settings; the interface rebuilds in place.
  Displays keep the deep-water colours in both (`display::` palette).
- Pads use the mark's corner ratio, show level as a tide line and carry their key in
  a keycap. Renamed for clarity: Loops to Cycles, Gesture to Take, Disintegration
  looper to Tape looper, Noisy sampler to Sampler, auto master Mud to Low mid and
  Quiet -23 to Broadcast -23. Parameter IDs are unchanged.
- Every comment removed from `src/`, `tools/`, `tests/` and the CMake files, at the
  owner's request; CLAUDE.md now says not to add them.
- Verified: 143 tests, all eight scores strict, self-test, `--ui-test` (now switches
  theme with the projector open) clean under AddressSanitizer, icon regenerates
  byte-identical. Not seen on a Mac yet.

### Brand mark (after 1.2.0)

- The logo is drawn in code: `src/app/gui/Logo.h` defines the layered-tide tile as
  four cubic tide lines over five colours (juce_graphics only). The header draws the
  mark and the lowercase wordmark in Quicksand Medium (OFL, embedded, used for the
  wordmark only; Inter stays the interface face).
- `tools/icon` (built with the app) regenerates `resources/icon/icon_1024.png` (macOS
  icon grid: 824 px tile, 1024 canvas, drop shadow) and `logo.png` from the same
  paths. The app and plugin pick the icon up through `ICON_BIG`.
- Verified: self-test checks the wordmark typeface; header screenshot under Xvfb.
  Not yet seen: the icon in the macOS Dock and Finder at small sizes.

### 1.2.0: tempo sync, gestures, projector, presets; a debugger's and optimiser's pass

**Built**
- **Tempo sync**: Tempo Sync and Tempo parameters, the DAW's tempo and song position
  in the plugin, beat-locked loops (prime numbers of beats), delays snapped to note
  lengths with the division shown on the knob; a transport-style tempo field with a
  beat dot and Tap.
- **Gestures**: record your moves (G), play them back looped or once; saved in
  sessions and plugin state.
- **Projector window** (Cmd+P): the terrain alone, full screen on another display.
- **Device presets**: a Presets menu on every device and effect; 29 factory presets.

**Found and fixed** (three independent code reviews, sanitizers, random-input runs)
- Gestures: loading a session or recording again mid-recording let the abandoned
  take overwrite the new one (generations now); a full queue could lose a take's end
  (re-sent); notes held by a take could hang after Stop (released); a take recorded
  across a sample-rate change was labelled with the wrong rate; Play could race the
  take's publish (Play waits for its version).
- Synced loops lined up at extreme Pace (periods shared factors) and fired stray
  notes when Pace changed while playing.
- Hostile files: a season with period 0 produced NaN; damaged zips could crash;
  "NaN" values reached parameters; an unknown effect type left the old effect
  loaded; huge or bogus audio headers could allocate gigabytes; a failed save could
  lose the previous file; two preset names could share one file.
- Plugin: menus, dialogs and timers could call into a closed editor; a knob destroyed
  mid-drag froze; a pad held while the window closed latched; the plugin stole
  keyboard focus from the DAW; state requests from other threads blocked on the
  message thread (now a snapshot); a restore queued from another thread could
  outlive the plugin; background loads ran on detached threads (now a pool the core
  waits for).
- The window opened larger than a 13-inch laptop screen; the performance panel lost
  a control at the minimum size.
- Correction: the 1.1.0 entry's "every score strict at 48 kHz" was run locally with a
  stale render binary. CI's four strict scores were real; all eight now pass with
  the current build.

**Optimised**
- The idle interface: from 9 % of a core to 2 % (meters re-armed their peak hold
  every frame in silence); with the engine running silent, from 74 % to 7 % (ripples
  for inaudible strikes, knobs repainting for invisible changes).
- Playing: from 74 % to 47 % of a core in Linux software rendering (glows drawn once
  into a quarter-resolution layer from a cached falloff image, a rectangular clip);
  CoreGraphics on the Mac should be well below that, measure there.
- Granular clouds: bounds and channel checks hoisted out of the per-sample loop,
  12 % fewer instructions, bit-identical output. Stress score 60 s in 12 s.

**Verified**: 143 ctest tests (also under ASan and UBSan); all eight scores strict;
the app's self-test and `--ui-test` (also under ASan with leak detection and the
engine running); 1000 random clicks, drags and keys under ASan with the engine
running; `tidefield_plugincheck` on the VST3.

### 1.1.0: native interface, factory library, path wander, plugin

**Built**
- **Native interface** (`src/app/gui`), replacing the React WebView and the classic
  panel. The WebView never received key presses on macOS (the JUCE view held focus,
  so S beeped). Lighter mid-grey studio look, one orange accent, a colour per scene,
  Inter embedded. Top bar, browser, terrain (deep-water surface, scene glows that
  grow with their weight, grain particles, orbiting drone voices, resonator and Bloom
  ripples), performance panel, nine pads (Swell, Hush, Slow, Shape, Freeze all, Hold
  input, Loop, Loops, Catch), a tabbed device panel with every engine page, and a
  status bar that explains whatever is under the mouse.
- **Keys**: every key consumed (no beeps); S/H/T hold and release on key-up, focus
  loss or the app going to the background; 1-9 glide to scenes (Shift jumps); arrows
  nudge; P draws a path; M turns the letter rows into a Bloom keyboard (Z/X octave,
  C/V velocity); Tab steps through pages.
- **Factory library**: 14 new original sounds (singing bowl, kalimba, felt piano,
  marimba, bell, wind chimes, choir, bowed strings, harmonium, sub organ, shimmer,
  ocean, rain on leaves, tape dust), generated by `make_samples.py`; a browser to load
  them (or files) into any cloud or Bloom, tuning Bloom to each sound's note.
- **Starter terrain**: New and a first launch open six designed scenes over a choir,
  a singing bowl and glass, so the surface plays from the first touch.
- **Path wander**: draw a loop; the sound travels it at a steady pace, from the
  point nearest where it is. Saved in sessions.
- **AU and VST3 plugin**: the same engine, core and interface in a DAW. Track MIDI
  plays Bloom and drives mappings; optional stereo input for the live strip; latency
  per sample rate; state is the session format with sounds. `tools/plugincheck`
  hosts it like a DAW; CI runs it on both formats plus auval and ships the plugins.

**Fixed along the way**
- Saving read parameter values from telemetry, so before audio ran (a DAW project
  saved before pressing play, or the app without a device) every parameter saved as
  zero. Targets are now seeded from defaults and applied sessions.
- `Wander::setSeed`/`reset` set the offset to `Point2`'s default (0.5, 0.5) instead
  of zero, starting the wandering sound off-centre after every reset.

**Verified**: 130 ctest tests; every score strict at 48 kHz; the app's self-test and
`--ui-test` (every page, clean quit) on the shipped macOS bundle; Apple's `auval`
passes on the AU; `tidefield_plugincheck` passes on the VST3 and the AU in macOS CI
(44.1/48/96 kHz, block sizes 1-1024, state round-trip with sounds, scenes and every
parameter) and on the Linux VST3 with its editor; the app under AddressSanitizer and
UBSan through 800 random clicks, drags and keys and a clean quit with leak
detection; screenshots of every page under Xvfb.

**Untested (needs the Mac and ears)**: the interface on a Retina display and with a
trackpad, key handling inside Logic/Live/Bitwig, how the starter scenes and new
sounds actually sound together, CPU of the plugin in a busy project.

### Control audit

`tests/engine/ControlAuditTests.cpp` (helpers in `ControlAudit.h`, tag `[audit]`)
renders every parameter in the registry at its minimum, middle and maximum (every
value for discrete ones) in a context where it should matter, checks the output is
finite and under the ceiling, that it changes measurably in the expected direction
(level, centroid, side, balance or a telemetry measure), and that neither half of the
range is dead. Context-only controls get timing checks (fade length, key morph, glide,
swell rise/ebb, catch length/source/target). Every effect type's six controls and Mix
are swept, and every one of the 28 slots' seven parameters is checked for routing. A
coverage test fails if a new parameter is not audited. `TF_AUDIT_REPORT=1` prints one
`AUDIT|...` line per check. 75 test cases, about 45 s on 4 cores with `ctest -j8`.

**Fixed by the audit**: `Drift` applied a new rate only after the current segment
(drone Drift Rate, tape wow, gusts and Drift seasons ignored their rate for seconds
to minutes); drone Shape's saw ran in antiphase to the sine, so the middle of the
knob lost 8 dB (voice gain rebalanced to keep the default patch's level); cloud
Position wrapped, so 100% played the start and 0% could jump to the end; resonator
Gravity did nothing below Structure 50%; Gusts did not move the rain; Bloom Tone only
applied at note-on (so Colour and Swell missed held notes); Bloom Swell's Amount
saturated on samples shorter than 4 s; master and bus levels shown as Off still
passed -60 dB; long fades ended early (float accumulation: 120 s took 117 s); Tape
Delay's Tone and Age skipped the first echo; both delays clipped the right channel's
spread time above 2.2 s. Docs/help: drone Shape direction, cloud Envelope, resonator
Gravity, Bloom Attack/Position, Flakes; strip Width is now on every strip.

### Built-in effects expansion
Eight new built-in effects, loadable in any of the 28 slots: Filter (`tf.filter`),
Pitch Shimmer (`tf.pitchShimmer`), Phaser (`tf.phaser`), Tremolo (`tf.tremolo`),
Saturator (`tf.saturator`), Grain Delay (`tf.grainDelay`), Glue Compressor
(`tf.compressor`) and Lo-fi (`tf.lofi`). Each allocates in `prepare`, smooths its gains
per sample, bounds its feedback and resets to a bit-identical state.
`tests/dsp/BuiltinFxTests.cpp` checks them at 44.1/48/96 kHz at every extreme (finite,
bounded, no allocation), that `reset` reproduces the same output, that tails fall
silent, and one behaviour per effect. The control audit now has a group per effect type
and fails if a registered type has no group or a control is not swept. Factory presets
for the new types (`fx:<type>`), and `scores/fx_palette.json` (passes `--strict`).

## How to run
```
cmake --preset headless && cmake --build --preset headless
./build/headless/tests/tidefield_tests
./build/headless/tools/render/tidefield_render scores/drone_basic.json -o out/drone_basic.wav --strict
./build/headless/tools/render/tidefield_render scores/ecosystem.json -o out/eco.wav --stems out/eco_take
# macOS app:
cmake --preset dev && cmake --build --preset dev   # app in build/dev/src/app/Tidefield_artefacts/
```
