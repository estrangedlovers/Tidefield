# Tidefield architecture

Tidefield is a standalone instrument for performing ambient music live. It runs a
small ecosystem of sound sources that drift on their own; the performer steers the
overall state instead of playing every note. This document is the design of record.
Change it when the design changes.

## 1. Main decisions

**Standalone JUCE GUI app with its own audio callback, not an `AudioProcessor`.**
`juce_add_gui_app` + `AudioDeviceManager` + our own `AudioIODeviceCallback` that calls
`Engine::process()`. APVTS assumes a fixed parameter list and message-thread
listeners; Tidefield has slot-based sources and FX, a terrain layer underneath every
parameter, and soft takeover. The `Engine` is host-agnostic, so a thin
`AudioProcessor` wrapper (AU/VST3) stays cheap to add later.

**Targets and dependency direction**

```
tidefield_dsp     (static, no JUCE)  pure DSP. No engine, no UI, no threads.
tidefield_engine  (static, no JUCE)  params, control flow, scenes, terrain, tide, harmony, mixer, master, catch, MIDI map, CPU policy.
tidefield_io      (JUCE, phase 4)    session files, audio file I/O, recorder, worker thread. Never realtime.
Tidefield         (app)              JUCEApplication, device host, WebView bridge, telemetry pump.
tidefield_render  (CLI)              offline harness: engine + score -> WAV + JSON analysis.
tidefield_tests   (Catch2 v3)        dsp/engine/io tests. No JUCE GUI.
```

`dsp` and `engine` have no JUCE dependency at all, so they build and test headless
in seconds. The imported plugin DSP (reverse shimmer, fuzz) is JUCE-based, so it goes
in a separate `tidefield_fx_juce` library behind the same `Processor` interface
instead of pulling JUCE into `dsp`.

**Plugin build is planned.** `Engine::process` follows the `AudioProcessor` contract
already: inputs may alias outputs (inputs are copied to scratch before any write),
block sizes may vary and exceed the prepared size, `getLatencySamples()` reports the
limiter lookahead, and all state lives in the engine, never in a view. The AU/VST3
target will be a thin `AudioProcessor` adapter plus a `getStateInformation` that
reuses the session serializer. Tail length is not applicable to an instrument.

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
| **Noisy sampler** | 12-bit style quantisation, sample-rate reduction with selectable anti-alias filter (clean / gritty), input drive into clipping, noise floor, warm output filter (SP-1200 / S950 territory) |

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

## 10. Ambient feature catalogue (approved in full)

Each item is either a `Source`, a `Processor` or a control-layer feature, so any of
them can be added without changing the core.

**Sources and generators**
- **Disintegration looper**: records a phrase; every repeat passes through a Medium so
  it slowly decays (the William Basinski technique).
- **Incommensurate loops**: several sparse note loops of different prime lengths
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
- **Session**: one `.tidefield` zip (`session.json` + `audio/*.flac`), schema version
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
amount, reverb send trim, cloud density, master level.

## 13. CPU guardrails (phase 7, built)

The engine times each block itself (so a plugin build gets the same behaviour) when
guardrails are enabled; the app enables them, renders do not. `DegradationPolicy`
smooths the load (fast rise, slow fall), steps down after 0.35 s above 80 % or at once
on a block over budget, and steps up after 6 s below 50 %, doubling that hold when it
pumps. Six levels (`kGuardLevels`) cap cloud grains, then resonator modes, drone
voices and Bloom polyphony together. Every cut is gentle: grains finish, modes ring
out over 80 ms, voices release. Reverb quality is not on the ladder (effects are
opaque processors). Hard caps (pool sizes) apply regardless.

## 14. UI (phase 6)

`AppCore` (message thread) owns the managers and a 30 Hz pump that drains telemetry,
notices and the MIDI monitor; front ends observe it through callbacks. The face of
the instrument is `WebUI`: a `juce::WebBrowserComponent` (WKWebView on macOS) serving
`ui/dist` through a resource provider, or a Vite dev server via `TIDEFIELD_UI_DEV`.
The JUCE `ClassicUI` panel remains as a fallback.

Protocol (`src/io/UiProtocol`, tested): one native function `tidefield(method,
...args)` for everything the UI asks; events back to the page: `telemetry` (~30 Hz,
parameter targets/live/pickup as deltas, the rest compact), `scenes`, `fx`,
`samples`, `midi`, `session` (pushed when their JSON changes), `status`,
`midiActivity`. The page asks once for the schema (`hello`), so the front end has no
hard-coded parameter tables. FX control display is declarative (`dsp::DisplayMap`)
so C++ and TypeScript format identically.

Front end (`ui/`): React 19 + TypeScript + Vite. A single external store keeps
parameters in typed arrays with per-parameter subscriptions, so 30 Hz telemetry only
re-renders the controls whose values moved; canvases (terrain field, waveform with
grains, pitch lanes, meters, keyboard glow) read telemetry in requestAnimationFrame
loops. Performance view: three tall faders (Tide, Wander, Gravity), key and scale,
the terrain (scenes, cursor, wander trail, particles per grain, drone voices as
orbiting lights, resonator strikes as ripples, Bloom blooms, a Medium texture), the
scene strip, recording-type tiles, Catch, Bloom transforms, fade and panic, and a
four-octave Bloom keyboard. Edit view: global, every source, mixer, effects, scenes,
MIDI. Knobs: drag, Shift fine, wheel, double-click default, Alt-click release,
right-click learn/forget/release/reset; sand = live layer, coral = learning, arrows
= soft takeover. Shortcuts: Space fade, Esc panic, K catch, C capture, R release,
Shift+R record, Tab switch view, Cmd+N/O/S sessions.

## 15. Extension points for later features

| Later feature | Where it plugs in |
|---|---|
| Gesture recording | record/replay the `ControlEvent` stream with sample times |
| Sample import | worker decode path shared with Catch and Bloom |
| OSC | another `ControlEvent` producer with its own SPSC queue |
| Ableton Link | a `TideClock` implementation |
| Projector visuals window | a second WebView consuming the same telemetry |
| Multichannel output | master bus channel count + a panner interface on strips |
| Plugin build | `AudioProcessor` adapter around `Engine` |

## 16. Phase plan

1. Skeleton, device settings, safety chain, drone, render harness. **(done)**
2. Scene system and terrain interpolation (placeholder UI). **(done)**
3. Granular, resonator, live input; mixer and send buses; Tide; harmonic gravity;
   **Medium stage**. **(done)**
4. Catch, **sample import + Bloom keyboard**, session save/recall. **(done)**
5. MIDI learn, soft takeover, note input. **(done)**
6. React WebView UI: performance view, then edit view. **(done)**
7. Recording to disk, CPU guardrails, polish. **(done)**
8. Ambient feature pack: the approved items from section 10.
