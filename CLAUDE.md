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
   `src/app`, `src/plugin`, `src/io` and `tools/`.
6. **Determinism.** No wall clock and no global RNG on the audio path. All randomness
   comes from seeded `dsp::Random` instances so offline renders are bit-reproducible.

## Layering

```
dsp     <- engine <- io/app/plugin/tools/tests
(no JUCE)  (no JUCE)   (JUCE allowed)
```

`dsp` must never include from `engine`. `engine` must never include from `app` or `io`.
`tidefield_io` is a CMake INTERFACE library: each executable compiles it against its
own JUCE modules, so JUCE module code is never linked twice.

## Where things go

- New sound source: DSP class in `src/dsp/sources/<name>`, a strip in
  `engine/mix/Layout.h`, its parameters in `ParamDefs.h`, wiring in `Engine.cpp`.
- New effect: implement `dsp::Processor` and register it in `ProcessorFactory`; it is
  then loadable into any of the 28 FX slots with no engine changes. JUCE-based
  effects (the imported shimmer and fuzz) live in a separate library and register
  themselves at startup.
- Anything that allocates or touches files: message thread or worker, handed to the
  audio thread through `SnapshotChannel`, `FxSlot` or `SpscQueue`. Audio going the
  other way (recording) goes through `engine/record/RecordTap`.
- Factory sounds: generate in `tools/scripts/make_samples.py` (library section),
  list in `src/app/FactoryContent.cpp`. Starter scenes and factory presets live there
  too (the self-test checks every preset value names a parameter).
- Your own JUCE effects (shimmer, fuzz): `src/fx_juce/UserEffects.cpp`, registered the
  same way as the existing ones; they become FX types like the built-in ones.
- A performance gesture that moves many parameters at once: add offsets in
  `Engine::updateModulation` (never set targets for this).
- A new expensive voice or grain pool: give it a limit setter and add a column to
  `kGuardLevels` in `engine/guard/DegradationPolicy.h`, applied in
  `Engine::applyGuardLimits`.
- Renders and tests must stay deterministic: anything that reads a clock (like the
  guardrails) is off unless the app turns it on.

## Build

```
cmake --preset dev            # Debug, tests + render harness (+ app on macOS)
cmake --build --preset dev
ctest --preset dev
./build/dev/tools/render/tidefield_render scores/ecosystem.json -o out/ecosystem.wav --strict
python3 tools/scripts/spectrogram.py out/ecosystem.wav   # needs numpy + matplotlib
./build/dev/tests/tidefield_io_tests                     # JUCE-based session tests
./build/dev/tools/plugincheck/tidefield_plugincheck_artefacts/Debug/tidefield_plugincheck \
    build/dev/src/app/TidefieldPlugin_artefacts/Debug/VST3/Tidefield.vst3
```

Presets live in `CMakePresets.json`; CLion picks them up. JUCE 8 and Catch2 are fetched
by `cmake/Dependencies.cmake`. The app target defaults on for macOS only; on Linux
pass `-DTIDEFIELD_BUILD_APP=ON` (needs ALSA/X11 headers) to compile-check it.

## Interface (src/app/gui)

Native JUCE drawing in C++, one typeface (Inter, embedded), palette and metrics in
`gui/Style.h`. The logo lives in `gui/Logo.h` (Quicksand for the wordmark only);
after changing it, regenerate the icons with `tidefield_icon resources/icon
resources/fonts/Quicksand-Medium.ttf`.
- Sixteen themes, ten dark and six light (`kThemeInfo` in `gui/Style.cpp`, switched from
  Settings > Look and Feel, View > Theme or the session menu's Appearance). A new theme is one `Palette` and one row there; a light
  one names the dark palette its displays use. Panel colours come from `colour::`; anything drawn inside a dark display (the
  terrain, meters, waveforms, faders, readouts) uses `display::`, which stays the same in
  both themes. Hover and press states use `colour::lift`, never `brighter()` directly.
- Playable tiles (pads) use the mark's corner ratio (`tileCorner`); panels stay square. Shared by the app and the plugin (`tidefield_app_core`).
- Controls bind to a parameter through `Model` (`gui/Model.h`): `Knob`, `Fader`,
  `Toggle`, `Choice`, `Pad`. They get MIDI learn, release, reset, hover help and the
  live/learn/pickup colours for free. A device page is a list of parameters in
  `DeviceView::build` (`gui/Pages.cpp`); `Device::add(P)` picks the right control.
- Anything animated implements `Animated::tick()` (called once per display frame);
  repaint only when what you draw changes visibly (compare against a threshold, not
  exact floats: smoothed values creep for seconds). Never allocate per frame in a
  way that grows. Menus and async callbacks go through `showMenu`/`later`
  (`gui/Style.h`), which check that their component still exists.
- `Tidefield --null-audio` runs the engine without a sound device, so the interface
  animates under Xvfb (screenshots, CPU measurement). Build with
  `-DJUCE_ENABLE_REPAINT_DEBUGGING=1` to see what repaints.
- Preferences live in `gui/Settings.cpp`: one tab per area, each a `FormPage` of labelled
  rows. The macOS menu bar is `AppMenu` in `Main.cpp`; its Play items call
  `MainView::performAction` so menu and keyboard share one path.
- Keys live in `MainView::keyPressed`; standalone every key is consumed (no macOS
  beep), in a plugin leave Space and unused keys to the DAW. A rebindable key is a row
  in `keyActions()` (`app/KeyBindings.cpp`) plus a case in `MainView::performAction`;
  never compare key codes directly, and show keys with `core.keys.hint()`/`label()`.
- Controller templates: factory ones in `app/ControllerTemplates.cpp` (the self-test
  checks every one names a MIDI-learnable parameter); user ones are JSON files in
  "Controller templates" next to the settings file.
- Check the look under Xvfb on Linux: build with `-DTIDEFIELD_BUILD_APP=ON`, run the
  app under `xvfb-run` and capture the screen (`import -window root`). Without an
  audio device there is no telemetry; values come from the seeded targets.
- `Tidefield --self-test` checks the typeface, every factory sound, the starter
  session and a full-engine render; CI runs it on the shipped bundle.

## Plugin (src/plugin)

`TidefieldProcessor` implements `app::Host` like `AudioHost` does. Nothing in
`AppCore` or the interface may assume an audio device: use `host.isRunning()`,
`host.getDeviceManager()` (null in a DAW) and `host.isPlugin()`. After changing the
plugin, run `tidefield_plugincheck <path to Tidefield.vst3>` (CI also runs auval).

## Conventions

- C++20, 4-space indent, `camelCase` functions and variables, `PascalCase` types,
  `kConstant` constants. Namespaces: `tf::dsp`, `tf::engine`, `tf::app`, `tf::io`.
- Header-only is fine for small DSP classes; put larger code in `.cpp`.
- Parameter IDs are stable dotted strings (`drone.cutoff`). Never rename one without
  a session migration.
- New DSP gets a unit test in `tests/` and, when it makes sound, a score in `scores/`.
- Prefer rendering a score and checking the analysis report over guessing what
  something sounds like.
- No comments in source files (C++, CMake, Python). Names carry the meaning; the why
  goes in commit messages and `docs/ARCHITECTURE.md`.
- Commit messages: imperative mood, one subject line, body explaining why.
