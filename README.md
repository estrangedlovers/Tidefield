# Tidefield

<img src="resources/icon/logo.png" alt="tidefield" width="240">

A desktop instrument for performing ambient music live. There are no tracks to
arrange. A small ecosystem of sound sources drifts on its own, and you steer the
whole of it: where it sits on the terrain, how fast time moves, which key it settles
into, and the gestures you make on top.

## Install (macOS, Apple Silicon)

1. Download `Tidefield-macOS-arm64.zip` from the
   [latest release](../../releases/latest) (or from the newest "macOS app" run under
   Actions), unzip it and drag `Tidefield.app` to Applications.
2. The app is signed ad hoc, not notarised. The first time you open it, right-click
   it and choose Open, then Open again. If macOS still refuses, run
   `xattr -cr /Applications/Tidefield.app` in Terminal once.
3. Allow microphone access if you want to play an instrument through the live input.
4. Choose your audio interface in **Settings** (top right, or Cmd+,), under Audio.

macOS 12 or later.

**In a DAW**: the same release has `Tidefield-Plugins-macOS-arm64.zip` with an Audio
Unit and a VST3 instrument. Copy `Tidefield.component` to
`~/Library/Audio/Plug-Ins/Components` and `Tidefield.vst3` to
`~/Library/Audio/Plug-Ins/VST3`, then rescan. Track MIDI plays Bloom and drives your
mappings, and the whole piece (sounds included) is saved in the project.

## Install (Windows 10/11, 64-bit)

1. Download `Tidefield-Windows-x64.zip` from the same release (or from the newest
   "Windows app" run under Actions) and unzip it anywhere, for example into
   `C:\Program Files` or your Documents folder.
2. Run `Tidefield.exe`. The app is not code-signed, so SmartScreen may say "Windows
   protected your PC": choose **More info**, then **Run anyway**. It only asks once.
3. Choose your audio interface in **Settings** (top right, or Ctrl+,), under Audio.
   Windows Audio (WASAPI) works with any device; shortcuts written Cmd here are Ctrl
   on Windows.

**In a DAW**: `Tidefield-Plugins-Windows-x64.zip` holds the VST3 instrument. Copy the
whole `Tidefield.vst3` folder to `C:\Program Files\Common Files\VST3`, then rescan.

The full guide to every control is the [user manual](docs/MANUAL.md).

## First five minutes

Press **Space** to fade in. A starter terrain is already laid out: six scenes
(Still, Dawn, Glass rain, Tidepool, Deep water, Storm) over a choir, a singing bowl
and glass in Bloom.

- Drag anywhere on the terrain and the sound glides there; each scene glows as it
  takes over. Click a scene (or press **1** to **6**) to travel to it.
- Raise **Wander** to let it roam on its own; try the **Journey** style, or press
  **Draw path** (or **P**), draw a loop, and the sound travels it.
- Press **M** and the letter rows play Bloom like a keyboard (Z/X octave), or open the
  **Bloom** tab and play the keys there, or a MIDI keyboard.
- Load other sounds from the **Browser** on the left: 59 factory sounds (tonal,
  pads, drones, textures and one-shots) or your own files.
- Shape a sound you like, then double-click the terrain to save it as a new scene.
- Move **Tide** to speed up or slow down every drift at once, and **Gravity** to pull
  pitches into the key.
- Press **K** to catch the last 20 seconds into a granular cloud.
- Turn on **Auto master** (bottom right) for a finished, balanced level.
- Press **Shift+R** to record what you hear (right-click Rec for stems).

## Gestures

| Gesture | Key | What it does |
|---|---|---|
| Swell | hold S | every send blooms, filters open, clouds thicken; ebbs back on release |
| Hush | hold H | every source sinks while the reverb and delay ring on |
| Slow | hold T | time slows to a quarter, then eases back |
| Shape pad | drag | left/right: dark to bright; down/up: close and dry to far and wet |
| Freeze all | F | holds the last two seconds as a cloud while the rest steps back |
| Loop | L, Shift+L clears | a disintegrating tape loop: record, close, overdub; it wears away each pass |
| Hold input | I | freezes the live input's sound into a spectral pad |
| Cycles | E | Eno-style note loops on long, never-aligning cycles, in the key |
| Catch | K | last N seconds of the output (or input) into a cloud |
| Capture scene | C | saves what you hear at the cursor |
| Release | R | hands held controls back to the terrain |
| Fade | Space | fade in / out |
| Panic | Esc | fast fade to silence and reset; press again to resume |
| Record | Shift+R | to ~/Music/Tidefield (32-bit float WAV, optional stems) |
| Scenes | 1-9, Shift jumps | glide to a scene |
| Nudge | arrows, Shift fine | move the cursor |
| Draw path | P | draw a loop for the sound to travel (Path wander) |
| Take | G, Shift+G new take | record your moves (knobs, terrain, notes), then play them back, looped |
| Projector | Cmd+P | the terrain alone in its own window, full screen on a second display |
| Keys | M | letter rows play Bloom; Z/X octave, C/V velocity |
| Pages | Tab, Shift+Tab | step through the device tabs at the bottom |

Every control can be MIDI-learned: right-click it. Buttons and pads can learn actions
on the MIDI tab. Hovering anything explains it in the status bar. The default layout maps CC 21-28 to terrain X/Y, Tide, Wander,
Gravity, reverb return, Cloud 1 density and master level.

## What is inside

- **Sources**: a deep drone (five waves, eight chord voicings, sub, filter types, drive,
  vibrato and tremolo), four granular clouds, resonator bank, live input (with spectral
  hold), Bloom (several sounds across the keyboard, tuned automatically, MPE),
  disintegrating looper, weather (wind, rain, surf), freeze all.
- **Recording type** (on the master, heard live): digital, cassette, vinyl, sampler.
- **Effects** for any of 28 slots: reverb with infinite hold, tape delay, worn echo,
  ensemble, spectral blur, sympathetic strings, Medium, filter, pitch shimmer, phaser,
  tremolo, saturator, grain delay, glue compressor and lo-fi. In the app, any slot can
  also host an Audio Unit or VST3 effect, with its knobs pointed at any of the
  plugin's parameters.
- **Modulation**: four LFOs, two random sources, input and mix followers, velocity,
  pitch, mod wheel, pressure and the terrain, routed to any control (up to 16 routes).
- **Over time**: seasons (minutes-long curves on any parameter), harmonic gravity with
  crossfaded key changes, Tide.
- **The timeline**: record a whole performance, replay it from any point, erase, mute,
  smooth and trim lanes, and render it offline to a master, stems or a seamless loop.
- **Space**: stereo, a binaural headphone mode, or a ring of 4, 6 or 8 speakers, with a
  direction for every source, spread and rotation.
- **Sync and remote**: host tempo, MIDI clock in and out, OSC in and out, MPE, and
  Ableton Link as a build option.
- **Installation mode**: opens a session at launch, fades in, follows a daily schedule,
  keeps the computer awake, recovers a lost audio device and logs it all.
- **Undo and redo** for controls, scenes, routes, seasons, effects and timeline edits.
- **Safety**: lookahead true-peak limiter, DC blocker, NaN guard, panic, CPU guardrails
  that lighten the load before it glitches.
- **Sessions**: one `.tide` project file with every setting, scene, season, route, path,
  mapping, plugin state and sample inside.
- **Sounds**: original factory sounds in five groups (tonal, pads, drones, textures and
  one-shots), all synthesised by `tools/scripts/make_samples.py`, with search, preview
  and favourites, and anything you load from disk.
- **Presets** for every device and effect (the menu in each device's title bar), yours
  saved in `~/Music/Tidefield/Presets`.
- **Formats**: standalone app, Audio Unit and VST3.

## Build from source

CMake 3.24+, Ninja and a C++20 compiler. JUCE 8 and Catch2 are fetched automatically.

```
cmake --preset release
cmake --build --preset release
ctest --test-dir build/release
```

On Windows, configure from a "x64 Native Tools Command Prompt for VS 2022" (Visual
Studio 2022 with the C++ workload; it brings CMake and Ninja) and add
`-DTIDEFIELD_BUILD_APP=ON` to the first command; the app is
`build\release\src\app\Tidefield_artefacts\Release\Tidefield.exe` and the plugin is in
`build\release\src\app\TidefieldPlugin_artefacts\Release\VST3`.

On macOS this builds `Tidefield.app` (in `build/release/src/app/Tidefield_artefacts/`)
and the plugins (in `build/release/src/app/TidefieldPlugin_artefacts/`).
`tidefield_plugincheck <plugin>` hosts a built plugin the way a DAW does and checks it.
Open the folder in CLion and pick a preset.

- Design: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- Status, what was verified and what was not: [`PROGRESS.md`](PROGRESS.md)
- Rules for contributors (human or AI): [`CLAUDE.md`](CLAUDE.md)
