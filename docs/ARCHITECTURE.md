# Tidefield architecture

Tidefield is a standalone instrument for performing ambient music live. It runs a
small ecosystem of sound sources that drift on their own; the performer steers the
overall state instead of playing every note. This document is the design of record.
Change it when the design changes.

## 1. Main decisions

**A standalone app first, with the same instrument as an AU/VST3 plugin.** The app is
`juce_add_gui_app` + `AudioDeviceManager` + our own `AudioIODeviceCallback` that calls
`Engine::process()`. APVTS assumes a fixed parameter list and message-thread
listeners; Tidefield has slot-based sources and FX, a terrain layer underneath every
parameter, and soft takeover, so it has no APVTS. The plugin is a thin
`AudioProcessor` (`src/plugin`) around the same `Engine`; both implement `app::Host`,
and everything above it (the app core, the native interface) is shared.

**Targets and dependency direction**

```
tidefield_dsp     (static, no JUCE)  pure DSP. No engine, no UI, no threads.
tidefield_engine  (static, no JUCE)  params, control flow, scenes, terrain, tide, harmony, mixer, master, catch, MIDI map, CPU policy.
tidefield_io      (JUCE, phase 4)    session files, audio file I/O, recorder, worker thread. Never realtime.
tidefield_app_core (JUCE, interface) AppCore (managers, telemetry pump), factory content, the native interface.
Tidefield         (app)              JUCEApplication and the device host (AudioHost).
TidefieldPlugin   (AU, VST3)         TidefieldProcessor: the DAW drives the engine; state = session format.
tidefield_render  (CLI)              offline harness: engine + score -> WAV + JSON analysis.
tidefield_tests   (Catch2 v3)        dsp/engine/io tests. No JUCE GUI.
```

`dsp` and `engine` have no JUCE dependency at all, so they build and test headless
in seconds. The imported plugin DSP (reverse shimmer, fuzz) is JUCE-based, so it goes
in a separate `tidefield_fx_juce` library behind the same `Processor` interface
instead of pulling JUCE into `dsp`.

**Plugin.** `Engine::process` follows the `AudioProcessor` contract: inputs may alias
outputs (inputs are copied to scratch before any write), block sizes may vary and
exceed the prepared size, `getLatencySamples()` reports the limiter lookahead, and all
state lives in the engine, never in a view. `TidefieldProcessor` forwards track MIDI
to an engine MIDI port, reports latency per sample rate and a 30 s tail, and saves
its state with the session serializer (`io::writeSession`/`readSession`, sounds
included). `tools/plugincheck` hosts the built plugin like a DAW and CI runs it on
the VST3 and the AU, plus `auval`.

**One event path.** Every change (UI, MIDI, scores, terrain, later OSC) is a
`ControlEvent { type, source, command, param, value }`. Gesture recording is
recording and replaying that stream; OSC is another producer; render scores are that
stream as JSON.

**Determinism.** Seeded per-object `dsp::Random` (xoshiro128+), no wall clock on the
audio path, control ticks aligned to absolute sample positions. Same score + seed =
bit-identical WAV regardless of host block size (tested).

## 2. Threads and communication

| Thread | Owns | Talks via |
|---|---|---|
| Audio | `Engine::process`, smoothers, terrain eval, all DSP | pops control SPSC queues; pushes telemetry + notice SPSC queues |
| Message | UI, session edits, building snapshots | sole producer of the UI control queue; sole consumer of telemetry |
| MIDI in (phase 5) | forwarding only | sole producer of its own MIDI SPSC queue |
| Worker (phase 4) | disk, Catch copy-out, session load/save, sample decode | job queue from message thread |

- Small values: `SpscQueue<T>` (bounded, wait-free, preallocated).
- Large structures (scene sets, MIDI maps, FX chains, sample buffers): built off the
  audio thread, published as immutable snapshots via atomic pointer exchange; retired
  snapshots go back through a `ReleasePool` queue and are freed on the message thread.
- Telemetry: `TelemetryFrame` pushed ~60 Hz through an SPSC queue; the UI drains it
  and keeps the latest. Discrete events go through a separate `EngineNotice` queue.

## 3. Parameters and smoothing

`ParamDefs.h` holds one X-macro table: stable string ID, range, taper, default,
smoothing kind and time, unit, flags (`kTerrainBound`, `kMidiLearnable`,
`kTideScaled`, `kPerformance`). It generates the `P` enum (dense indices for the audio
thread) and the `ParamRegistry` (string lookup for the non-realtime side).

Per control tick (32 samples, aligned to absolute sample time):

1. Drain control queues: param targets, commands.
2. (Phase 2) Advance terrain cursor and wander, compute scene weights, write
   terrain-bound targets.
3. Combine: terrain value or live-layer override, plus macro modulation, clamped.
4. Advance smoothers (`ParamState`): linear for gains, one-pole for most, one-pole in
   the log domain for frequencies. Consumers read `current()` and ramp per sample.

**Terrain vs direct edits (decided):** direct edits go into a *live layer* that
overrides the terrain for that parameter until released or committed. "Commit to
scene" writes the live layer into the selected scene explicitly.

**MIDI learn scope (decided):** every `kMidiLearnable` parameter can be learned; the
performance view surfaces the performance macros, the edit view exposes the rest.

## 4. Scenes and terrain (phase 2)

A `Scene` is a sparse map param-index -> value. A scene that does not mention a
parameter has no opinion on it: that parameter blends only among the scenes that
define it, and parameters no scene defines are left alone by the terrain. Captured
scenes store every terrain-bound parameter. New parameters never break old scenes. `Terrain` places scenes at 2D points.
Interpolation is inverse-distance weighting with a smooth falloff kernel and optional
nearest-k limit, computed on the audio thread at control rate (cheap, deterministic).
Wander is a seeded, tide-scaled Ornstein-Uhlenbeck walk blended with the performer's
cursor. Any parameter can be pinned out of terrain control.

## 5. Tide and harmonic gravity (phase 3)

**Tide**: `TideClock` provides a smoothed rate multiplier. Every modulator, drift,
probability gate, envelope and wander advances by `dt * tide`. Only `kTideScaled`
rates follow it; pitch and delay times never do. The clock is an interface so
Ableton Link can implement it later. (Phase 1 sources already take a `timeScale`.)

**Harmonic gravity**: `HarmonicGravity` holds current and target `Scale` (pitch-class
mask + root) and a crossfade position. Pitched sources call
`quantize(freq, voiceSeed, amount)`. During a key change each voice migrates once the
crossfade passes its own random threshold, with a short glide, so the harmony shifts
voice by voice over several seconds. `amount < 1` pulls toward scale tones instead of
snapping.

## 6. Sources

DSP classes in `dsp/sources` have plain setters and `process(L, R, n, timeScale)`. The
engine wraps each in a node that reads `ParamState` and feeds harmony, tide and notes.
All voice/grain pools are allocated in `prepare()` with hard caps.

| Source | Phase | Summary |
|---|---|---|
| Drone | 1 (done) | 6 voices x 3 PolyBLEP saws blended to sine, breath noise, per-voice drifting SVF, density fades, probabilistic re-voicing |
| Granular cloud x4 slots | 3 | fixed grain pool; reads `SampleBuffer` handles; Catch fills a slot |
| Resonator bank | 3 | modal resonators excited by noise, live input or other sources; tuned by harmony |
| Live input | 3 | gain, gate, freeze-to-cloud, muted to master until armed |
| **Bloom keyboard** (one-shot transformer) | 4 | see section 9 |
| **Guest** (a hosted instrument plugin) | 1.4 | see section 16 |

Slots never change the graph shape at runtime; the UI just sees slots become active.

## 7. Mixer, buses, master

```
Source -> ChannelStrip [insert x2] -> level / pan / width(M/S) -> sends A,B (pre/post)
                                          |                          |
                                       master sum <- SendBus A: FxChain (reverb)
                                                  <- SendBus B: FxChain (delay)
Master: inserts -> MEDIUM -> master level -> fade -> DC blocker -> limiter -> panic -> out
                                        (guard on the master sum before everything)
```

- `FxChain`: fixed-length array of `Processor` slots. Types are registered in a
  `ProcessorFactory` by string id (`tf.reverb`, `tf.delay`, later `tf.reverseShimmer`,
  `tf.fuzz`). Chains are built off-thread and swapped as snapshots with a short
  crossfade. Inserts on strips let the fuzz sit on a single source.
- Panic: ~50 ms ramp to silence, then every feedback state is reset so nothing
  returns on resume. Resume fades in over the master fade length.
- Guard: any non-finite sample on the master sum zeroes the block, resets state,
  notifies the UI and recovers with a 1 s fade.
- Limiter: stereo-linked lookahead (3 ms), sliding-minimum + box-filter gain path that
  provably never exceeds the ceiling, plus a final clamp.

## 8. Medium: the "recording type" stage (phase 3)

A global character stage that makes everything sound as if it was printed to a
medium. One `Medium` processor with a type selector and shared controls:

| Type | What it does |
|---|---|
| **Digital** | clean bypass |
| **Cassette** | hiss (shaped noise that rides slightly with level), wow and flutter (two modulated fractional delays, slow + fast, tide-independent), head-bump low boost, high-frequency roll-off that drops with *age*, soft asymmetric tape saturation, slight L/R crosstalk, occasional dropouts at high age |
| **Vinyl** | crackle and pops (random impulse generator, filtered), surface noise and rumble, slow 0.55 Hz wow (33 rpm), low-end mono summing, inner-groove HF loss, gentle tilt EQ |
| **Sampler** | 12-bit style quantisation, sample-rate reduction with selectable anti-alias filter (clean / gritty), input drive into clipping, noise floor, warm output filter (SP-1200 / S950 territory) |

Shared controls: `medium.type`, `medium.age` (0..1, how worn), `medium.noise`,
`medium.wobble`, `medium.drive`, `medium.mix`. Switching type crossfades the old and
new models over ~300 ms (both pre-allocated). Each model is a `Processor`, so the
same code also works as a strip insert ("only the cloud is on cassette") or inside a
delay's feedback loop (a degrading tape echo).

*Placement (decided):* on the master, before the limiter, so it is heard live and
printed to recordings. Recording to disk captures post-Medium.

## 9. Bloom: one-shot sampler keyboard (phase 4)

A playable source that turns ordinary one-shots (a snare, a pluck, a vocal chop, a
glass tap) into ambient material. Load samples onto keys or a key range; each note
spawns a voice that runs the one-shot through a *transform*:

| Transform | Result |
|---|---|
| **Swell** | reverse of the tail rising into the attack, then a granular stretch of the attack into a pad |
| **Smear** | 10x to 100x granular time-stretch with pitch held, grain jitter from drift |
| **Freeze** | spectral freeze of a chosen moment (position knob), infinite sustain with slow spectral shimmer |
| **Ghost** | attack removed; only the reverb-like resonant tail, fed through the resonator bank tuned to the scale |
| **Constellation** | the one-shot replayed as a scattered chord from the gravity scale, notes arriving with random delays and pitch-shifted copies |
| **Tape** | varispeed playback (pitch and speed linked) through its own Medium instance |

Per-key randomisation (start, pitch cents, pan, transform depth) keeps repeated notes
from sounding identical. Voices are capped and steal the quietest. Notes come from the
on-screen keyboard (performance view), MIDI note input, or the Eno loop generator.
This brings sample import into scope earlier than first planned; it shares the
worker-thread decode path with Catch.

## 10. Ambient feature catalogue (approved in full; built in phase 8)

Each item is either a `Source`, a `Processor` or a control-layer feature, so any of
them can be added without changing the core.

**Sources and generators**
- **Tape looper**: records a phrase; every repeat passes through a Medium so
  it slowly decays (the William Basinski technique).
- **Cycles** (incommensurate loops): several sparse note loops of different prime lengths
  (Eno's *Music for Airports* system), pitched by gravity, played through Bloom or the
  resonators.
- **Weather bed**: procedural wind, rain and surf from shaped noise with drifting
  gusts.
- **Sympathetic strings**: resonator bank tuned to the scale, excited quietly by
  everything else.
- **Input freeze**: hold a cello note forever as a spectral pad.

**Performance gestures**
- **Swell**: a held control that blooms all sends and opens filters; release eases
  back over the Tide time.
- **Freeze all**: captures the master into a granular hold for transitions.
- **Wander styles** for the terrain cursor: drift, orbit, and "tide pool" (pulled
  toward the nearest scene).
- **Seasons**: very slow (minutes-long) macro curves on chosen parameters.

**Effects**
- Medium (section 8), reverse shimmer and fuzz (your plugins), cloud reverb with
  infinite hold, tape delay with Medium in the feedback, spectral blur, ensemble chorus.
- Utility and colour effects, all built in: a multimode filter with a slow sweep, pitch
  shimmer (dual-tap delay-line shifter with a 150/190 ms echo in its feedback, so the
  repeats climb in steps rather than smearing), phaser, tremolo/auto-pan, saturator
  (the ADAA tanh with bias, a pre/post low shelf so Warmth drives the lows without
  changing the balance, and level compensation computed from the curve's swing at
  -12 dBFS), grain delay (a fixed 24-grain pool reading a 4 s line, normalised by the
  live envelope sum so feedback stays below unity whether grains are correlated or
  not), glue compressor (stereo-linked, high-passed sidechain, soft knee, log-domain
  attack/release) and lo-fi (smoothed bit depth and hold rate so sweeps never zipper).
  Choices that change the signal path (filter mode, phaser stages) crossfade their
  outputs instead of switching.

**Visuals (phase 6)**
- A particle per grain, an orb per drone voice, waves for Tide, and Medium-specific
  texture over the whole UI (tape wobble, vinyl dust, sampler stepping).

## 11. Catch, sessions, recording

- **Catch**: preallocated 40 s stereo master ring (and a mono live-input ring);
  Catch length up to 30 s, leaving >= 10 s before the write head can overrun the
  copy. The engine answers the command with a `CatchReady` notice; the message thread
  copies the region (the notice queue orders the writes before the read), detects a
  lapped region, fades, normalises and loads the target cloud.
- **Samples** travel inside `SampleHandle`s (a `shared_ptr` the audio thread only
  reads through a raw pointer); the message thread keeps references for saving.
- **Session**: one `.tide` zip (`.tidefield` before 1.4, still read) (`session.json` + `audio/*.flac`), schema version
  plus a migration per version step. Loads on the worker; swap is crossfaded via the
  master fade.
- **Recording** (phase 7): the engine pushes the master (post-Medium, post-limiter)
  and, for a stem take, each strip post-fader plus both bus returns into
  `RecordTap`, a preallocated SPSC ring with an Idle/Running/Stopping/Stopped
  handshake. `io::Recorder` drains it on its own thread into 32-bit float WAVs
  (`master.wav`, `stems/<strip>.wav`). JUCE's `ThreadedWriter` was not used: it
  needs a lock to swap writers safely, and one ring keeps every file sample-aligned.

## 12. MIDI (phase 5)

Each MIDI device -> its own SPSC queue (one per port, so each has a single
producer) -> audio thread. `MidiMap` is a snapshot of CC
bindings (range, curve) to any learnable parameter. Soft takeover is per binding on
the audio thread (pickup with tolerance; "waiting to catch" goes out in telemetry).
Learn: the audio thread forwards the next CC as a notice; the message thread builds a
new map. Notes go to pitched sources and Bloom. The default 8-knob layout is
`resources/default_midi_map.json`: terrain X, terrain Y, Tide, wander, gravity
amount, reverb send trim, cloud density, master level. Controller templates
(`app/ControllerTemplates`) are named MIDI maps in the same JSON as sessions; applying
one goes through `applyMidiJson` inside an undoable `AppCore::editMidi` snapshot.

## 13. CPU guardrails (phase 7, built)

The engine times each block itself (so a plugin build gets the same behaviour) when
guardrails are enabled; the app enables them, renders do not. `DegradationPolicy`
smooths the load (fast rise, slow fall), steps down after 0.35 s above 80 % or at once
on a block over budget, and steps up after 6 s below 50 %, doubling that hold when it
pumps. Six levels (`kGuardLevels`) cap cloud grains, then resonator modes, drone
voices and Bloom polyphony together. Every cut is gentle: grains finish, modes ring
out over 80 ms, voices release. Reverb quality is not on the ladder (effects are
opaque processors). Hard caps (pool sizes) apply regardless.

## 14. UI

`AppCore` (message thread) owns the managers and a 30 Hz pump that drains telemetry,
notices and the MIDI monitor. Until telemetry arrives (no device yet, or a DAW that
has not started processing) its parameter snapshot is seeded from the defaults and
from each applied session, so saving never writes stale values.

The interface is native JUCE drawing in C++ (`src/app/gui`, version 1.1; it replaced
a React WebView, which never received key presses on macOS). A `Model` gives every
control the value to show (the local value while the hand is on it, the engine's
target otherwise) and ticks every visual once per display frame from a
`VBlankAttachment`. Layout, in a mid-grey studio style with one orange accent and a
colour per scene: top bar (session, fade, panic, record, auto master, keys, CPU,
meter), browser (scenes with live weights, factory and disk sounds), the terrain,
the performance panel (Tide, Wander, Gravity, Glide, key, scale, wander style,
recording type), performance pads, a tabbed device panel (Drone, Clouds, Resonator,
Bloom, Input, Looper, Weather, Guest, Gestures, Loops, Seasons, Mixer, Effects, Master,
MIDI) and a status bar that explains whatever is under the mouse. `MainView` owns the
keyboard: standalone, every key is consumed so macOS never beeps; holds (S, H, T)
release on key-up, focus loss or the app going to the background; M turns the letter
rows into a Bloom keyboard. In a plugin, Space and unused keys go to the DAW. Which
key does what comes from `KeyBindings` (`app/KeyBindings`): a table of actions with
default chords, user overrides stored in the settings file, matched exact first, then
without Shift, then without Option/Ctrl, so the 1.3 keys behave as before. The menu bar
and the keyboard both call `MainView::performAction`.

The older web protocol (`src/io/UiProtocol`) remains for tools: `tidefield_render
--dump-schema` writes the parameter schema for external controllers.

**Tempo.** `sync.on` and `sync.bpm`; in a plugin the host's tempo and song position
(`Engine::setHostTransport`, called before each block) replace the Tempo parameter.
While synced, each incommensurate loop repeats every prime number of whole beats
(consecutive primes from about 23, halved or doubled by Pace), fires on the beat and
follows the song position; a period change or a jump in the song re-anchors without
firing. `ModContext::beatSeconds` lets delays snap their time to note lengths
(`dsp/core/TempoSync.h`).

**Gestures.** While recording, `Engine::applyEvent` stamps every performer event
(UI and MIDI sources; never terrain, scores or playback) with its sample time and
the recording's generation, and pushes it to `gestureOut` (SPSC, audio thread to
message thread). The end marker carries the length and the sample rate and is
re-sent until the queue takes it; keys still held get note-offs at the end.
`GestureManager` collects a take, drops anything from an abandoned generation, and
publishes the take through a `SnapshotChannel`; Play carries the version it expects
and starts once that version is acquired. Playback replays events at control rate
(source `Score`), rescaled across sample rates, and releases its own held notes when
it stops, wraps or is replaced. Takes are saved in sessions in seconds.

**Presets.** `io::PresetLibrary`: JSON files per kind in `~/Music/Tidefield/Presets`,
keys are parameter IDs without the device prefix (a cloud preset fits any cloud;
effect presets have kind `fx:<type>`), plus factory presets registered by the app.

**Path wander.** A loop drawn on the terrain (`PathManager`, message thread) is
smoothed and resampled to 128 evenly spaced points (`TerrainPath`), published through
a `SnapshotChannel`, and travelled by `Wander::Style::Path` at one lap per 1 / rate
seconds of Tide time, starting from the point nearest the sound. Sessions store the
stroke as drawn.

## 15. Version 1.3 systems

**Drone.** `DroneGenerator` keeps six voices of three detuned oscillators. Each
oscillator sample comes from `waveSample(wave, phase, increment, modPhase)`: Classic
(polyBLEP saw to sine), Pulse (two polyBLEP edges, DC removed), Fold (a sine fed back
through a sine), Organ (up to six harmonics with a Shape-controlled slope, skipping any
above 0.45 of the sample rate) and FM (a modulator phase per oscillator at FM Ratio).
Wave changes crossfade both waves for 80 ms. Chords are tables of six starting
intervals and a pool for Evolve; a chord change revoices each upper voice through the
same fade-out, swap, fade-in path Evolve uses, timed by Revoice Time. Vibrato and
tremolo run at control rate on wall time (not Tide), with small per-voice phase
offsets so the voices move together instead of cancelling. Drive is a `TanhAdaa` on the
summed output, blended in by amount and level-matched against a 0.12 reference so the
knob changes colour, not loudness. Every new control's default reproduces 1.2 exactly.

**Modulation.** `ModMatrix` sources (LFOs, randoms, followers, MIDI expression, the
terrain) are computed once per control tick; routes arrive as a `SnapshotChannel` of
`ModRouteSet` and add to `ParamState` through `addModulation`, never touching targets,
so removing a route returns the control exactly.

**Plugin hosting.** `FxManager` takes effects from an `ExternalEffects` provider, so
the engine stays free of JUCE. `PluginHost` (app only) scans with a crash file, creates
`HostedPluginEffect` instances off the audio thread and hands them over through
`FxSlot`. Each of the six knobs points at an index into the plugin's automatable
parameters, held in an atomic; the audio thread skips one block after a change so a
remap never sends a stale value. Slot state is `map=a,b,c,d,e,f;` followed by the
plugin's own state in base64.

**Timeline.** The engine pushes every non-score control event into `performanceOut`
while recording, timestamped from the start of the take. The app keeps the events in
an `io::Performance` with the session it started from. Playback applies that session,
catches controls up to the chosen point and publishes a `GestureTake` with `startAt`
through `performanceChannel`. `renderPerformance` replays the same events into a fresh
engine offline, through `RecordTap`, for the master, stems or a crossfaded loop.

**Space.** After the strips, `spatialiseChunk` either renders each strip's left and
right as two virtual sources through `BinauralSource` (interaural delay by Woodworth's
formula, a far-ear shading filter and rear darkening) into the stereo master, or pans
them pairwise across a ring of 4, 6 or 8 speakers (`ringGains`, equal power). Returns
spread across the ring evenly. `MasterChain::process` carries the ring channels through
the same fade and level, delays them by the limiter's lookahead and applies the stereo
limiter's per-sample gain, so every speaker shares one protection. Mode changes dip the
master level for 40 ms and switch at the silent point. A ring mode with too few device
outputs plays stereo.

**Installation mode.** A one-second message-thread timer in the app: schedule
transitions, keep-awake, reopening a lost device every ten seconds, resuming ten
seconds after a panic, and a plain-text log. None of it touches the audio thread.

## 16. Version 1.4 systems

**Guest: a hosted instrument plugin.** The engine holds an `engine::Instrument`
(`engine/guest/Instrument.h`): `prepare`, `reset`, `setControls` (six smoothed knob
values) and `process(events, numEvents, left, right, numSamples)`, where each
`GuestEvent` is a raw three-byte MIDI message with a sample offset. It knows nothing of
JUCE; the app's `PluginHost` implements `ExternalInstruments` and wraps an
`AudioPluginInstance` in a `HostedInstrument` (stereo or mono out, inputs disabled, MIDI
built into a `MidiBuffer` reserved in `prepare`). `HostedInstrument` and
`HostedPluginEffect` share `HostedPlugin`, so the knob remap (`Choose controls`), the
editor window and the `map=a,b,c,d,e,f;<base64>` state work the same; the Guest uses host
slot `PluginHost::kGuestSlot`, one past the effect slots.

`GuestManager` (message thread) creates the instrument through the provider and hands it
to the audio thread through `InstrumentSlot`, an `FxSlot`-style pair of SPSC queues:
the new one fades in over 50 ms while the old one gets All Notes Off on every channel,
fades out and goes back to the message thread to be deleted. A type the provider cannot
create (not installed, not scanned, or the Tidefield plugin build, which has no
provider) leaves the Guest silent but keeps the type, name and state, so saving keeps
them. Sessions store them under `guest` (`type`, `name`, `state`); a session without the
field has no Guest.

Notes reach the Guest without allocation. Keyboard and gesture notes (`applyEvent`),
MIDI notes, bends, pressure and unmapped controllers (`handleMidi`, channel kept) and the
Cycles push into a fixed array of pending events stamped with the absolute sample time;
`guest.playFrom` decides which sources pass. A table of sounding notes per channel maps
each held key to the transposed note it started, so a note-off always finds its note
when Transpose moves, and lets Play From changes, panics and instrument swaps release
everything. The instrument renders in fixed 128-sample windows aligned to absolute
sample time: when the engine reaches a window boundary it renders the window just
finished with every event inside it at its exact offset, and plays that audio during the
next window. The Guest is therefore 128 samples late (2.7 ms at 48 kHz, not reported as
latency since nothing else waits for it), but its timing is exact and identical for any
host block size, and the plugin always sees the same block size. Non-finite output from
a plugin zeroes that window instead of tripping the master guard. The strip itself is an
ordinary `StripId::Guest` with two inserts (`guest.fx1`, `guest.fx2`), so there are now
30 effect slots.

**Cycles note length and MIDI out.** The Cycles keep one hold per loop: the Guest note,
the MIDI note and channel, and the time left (`loops.gate`, wall time, not Tide). A loop
that fires while its note still sounds releases it first. `loops.midiOut` sends note-on
and note-off as `MidiOutEvent { sampleTime, status, data1, data2 }` through an SPSC
queue (audio thread to `CycleMidiOut`); turning MIDI out off, stopping the Cycles,
changing `loops.midiChannel` (0 is a channel per cycle, otherwise one channel) and panic
release every hold, and panic also silences the Cycles' MIDI and Guest notes until it is
lifted. `CycleMidiOut` (app, `Remote.cpp`) is the queue's only consumer, on a 1 ms
`HighResolutionTimer` like `MidiClockOut`. The audio callback stamps a `BlockClock`
(a seqlock of block start sample, wall time, sample rate and block length) before
`Engine::process`; each event is sent at that wall time plus its sample offset plus one
block, so notes keep their spacing to within the timer's millisecond instead of
bunching at block boundaries, and leave roughly when their audio does. The sender keeps
its own table of sounding notes and releases them (note-offs and All Notes Off) when the
device changes, a session is applied and the app quits; without a device it drains and
drops the queue.

## 17. Extension points for later features

| Later feature | Where it plugs in |
|---|---|
| Ableton Link | a `TideClock` implementation |
| Multichannel output | master bus channel count + a panner interface on strips |
| OSC control | another `ControlEvent` producer with its own SPSC queue |

## 18. Phase plan

1. Skeleton, device settings, safety chain, drone, render harness. **(done)**
2. Scene system and terrain interpolation (placeholder UI). **(done)**
3. Granular, resonator, live input; mixer and send buses; Tide; harmonic gravity;
   **Medium stage**. **(done)**
4. Catch, **sample import + Bloom keyboard**, session save/recall. **(done)**
5. MIDI learn, soft takeover, note input. **(done)**
6. UI: performance view, then edit view. **(done; native C++ since 1.1)**
7. Recording to disk, CPU guardrails, polish. **(done)**
8. Ambient feature pack: the approved items from section 10. **(done)**
9. Native interface, factory library, path wander, AU/VST3 plugin (1.1). **(done)**
10. Tempo sync, gestures, projector window, device presets (1.2). **(done)**
