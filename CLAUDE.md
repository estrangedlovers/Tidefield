# Tidefield: rules for working in this repo

Tidefield is a standalone desktop instrument for performing ambient music live.
Read `docs/ARCHITECTURE.md` for the design and `PROGRESS.md` for where work stands
before changing anything. Update `PROGRESS.md` at the end of every phase or session.

## Hard rules (non-negotiable)

1. **Audio thread**: no allocation, no locks, no I/O, no logging inside
   `Engine::process` or anything it calls. That includes `std::vector::push_back`,
   `std::string`, `std::function` construction, `new`/`delete`, `shared_ptr` release of
   the last reference, mutexes, `std::cout`, `DBG`, file access and system calls.
   Everything is allocated in `prepare()`.
2. **Lock-free FIFOs for all UI <-> engine communication.** The UI never touches engine
   state directly. Changes go in as `ControlEvent`s through `SpscQueue`; state comes back
   as `TelemetryFrame`s and `EngineNotice`s through `SpscQueue`. Each queue has exactly
   one producer thread and one consumer thread.
3. **Every user-facing parameter is smoothed.** DSP code receives parameter values only
   through the engine's smoother bank (`ParamState`), never raw from an event.
4. **Denormal protection on all feedback paths.** The audio callback and the render
   harness both install `ScopedFlushDenormals`, and every recursive state (filters,
   delays, reverbs, resonators, envelopes) also calls `flushDenormal()` on its state.
   High-Q float recursions can also settle into rounding limit cycles above the
   denormal range; gate those explicitly (see `ResonatorBank`).
5. **DSP is independent of the UI.** `src/dsp` and `src/engine` are plain C++20 with no
   JUCE dependency, so they build and unit-test headless. JUCE is used only in
   `src/app`, `src/io` and `tools/`.
6. **Determinism.** No wall clock and no global RNG on the audio path. All randomness
   comes from seeded `dsp::Random` instances so offline renders are bit-reproducible.

## Layering

```
dsp     <- engine <- io/app/tools/tests
(no JUCE)  (no JUCE)   (JUCE allowed)
```

`dsp` must never include from `engine`. `engine` must never include from `app` or `io`.
`tidefield_io` is a CMake INTERFACE library: each executable compiles it against its
own JUCE modules, so JUCE module code is never linked twice.

## Where things go

- New sound source: DSP class in `src/dsp/sources/<name>`, a strip in
  `engine/mix/Layout.h`, its parameters in `ParamDefs.h`, wiring in `Engine.cpp`.
- New effect: implement `dsp::Processor` and register it in `ProcessorFactory`; it is
  then loadable into any of the 22 FX slots with no engine changes. JUCE-based
  effects (the imported shimmer and fuzz) live in a separate library and register
  themselves at startup.
- Anything that allocates or touches files: message thread or worker, handed to the
  audio thread through `SnapshotChannel`, `FxSlot` or `SpscQueue`.

## Build

```
cmake --preset dev            # Debug, tests + render harness (+ app on macOS)
cmake --build --preset dev
ctest --preset dev
./build/dev/tools/render/tidefield_render scores/ecosystem.json -o out/ecosystem.wav --strict
python3 tools/scripts/spectrogram.py out/ecosystem.wav   # needs numpy + matplotlib
```

Presets live in `CMakePresets.json`; CLion picks them up. JUCE 8 and Catch2 are fetched
by `cmake/Dependencies.cmake`. The app target defaults on for macOS only; on Linux
pass `-DTIDEFIELD_BUILD_APP=ON` (needs ALSA/X11 headers) to compile-check it.

## Conventions

- C++20, 4-space indent, `camelCase` functions and variables, `PascalCase` types,
  `kConstant` constants. Namespaces: `tf::dsp`, `tf::engine`, `tf::app`, `tf::io`.
- Header-only is fine for small DSP classes; put larger code in `.cpp`.
- Parameter IDs are stable dotted strings (`drone.cutoff`). Never rename one without
  a session migration.
- New DSP gets a unit test in `tests/` and, when it makes sound, a score in `scores/`.
- Prefer rendering a score and checking the analysis report over guessing what
  something sounds like.
- Commit messages: imperative mood, one subject line, body explaining why.
