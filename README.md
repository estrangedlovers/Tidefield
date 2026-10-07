# Tidefield

A desktop instrument for performing ambient music live. There are no tracks and no
timeline. A small ecosystem of sound sources drifts on its own, and you steer the
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
4. Choose your audio interface with the sliders icon (top right).

macOS 12 or later.

## First five minutes

Press **Space** to fade in. The drone, a granular cloud and the Bloom keyboard are
loaded with factory sounds.

- Play the keyboard at the bottom (or a MIDI keyboard). **Bloom** turns each note into
  a slow ambient event; pick a transform on the right.
- Shape a sound you like, then double-click the terrain to save it as a scene. Shape
  another and place it elsewhere. Drag the cursor between them, and raise **Wander**
  to let it roam (try the **Journey** style).
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
| Loops | E | Eno-style note loops on long, never-aligning cycles, in the key |
| Catch | K | last N seconds of the output (or input) into a cloud |
| Capture scene | C | saves what you hear at the cursor |
| Release | R | hands held controls back to the terrain |
| Fade | Space | fade in / out |
| Panic | Esc | fast fade to silence and reset; press again to resume |
| Record | Shift+R | to ~/Music/Tidefield (32-bit float WAV, optional stems) |
| Views | Tab | Perform / Edit |

Every control can be MIDI-learned: right-click it. Buttons and pads can learn actions
on the Edit, MIDI page. The default layout maps CC 21-28 to terrain X/Y, Tide, Wander,
Gravity, reverb return, Cloud 1 density and master level.

## What is inside

- **Sources**: drone, four granular clouds, resonator bank, live input (with spectral
  hold), Bloom one-shot keyboard, disintegrating looper, weather (wind, rain, surf),
  freeze all.
- **Recording type** (on the master, heard live): digital, cassette, vinyl, noisy sampler.
- **Effects** for any of 28 slots: reverb with infinite hold, tape delay, worn echo
  (a Medium inside the feedback), ensemble, spectral blur, sympathetic strings, Medium.
  Your own JUCE effects plug in through `src/fx_juce/UserEffects.cpp`.
- **Over time**: seasons (minutes-long curves on any parameter), harmonic gravity with
  crossfaded key changes, Tide.
- **Safety**: lookahead limiter, DC blocker, NaN guard, panic, CPU guardrails that
  lighten the load before it glitches.
- **Sessions**: one `.tidefield` file with every setting, scene, season, mapping and
  sample inside.

## Build from source

CMake 3.24+, Ninja, a C++20 compiler and Node 20+. JUCE 8 and Catch2 are fetched
automatically.

```
cmake --preset release
cmake --build --preset release
ctest --test-dir build/release
```

On macOS this builds `Tidefield.app` (in `build/release/src/app/Tidefield_artefacts/`).
Open the folder in CLion and pick a preset. To work on the UI in a browser without
audio: `cd ui && npm install && npm run dev`, then open http://localhost:5173/?demo.

- Design: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- Status, what was verified and what was not: [`PROGRESS.md`](PROGRESS.md)
- Rules for contributors (human or AI): [`CLAUDE.md`](CLAUDE.md)
