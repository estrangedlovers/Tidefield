# Tidefield user manual

<img src="../resources/icon/logo.png" alt="tidefield" width="200">

Tidefield is an instrument for playing ambient music live. It has no tracks and no
timeline. A handful of sound sources run all the time, drifting on their own, and you
steer the whole of them at once: where the sound sits on a map called the terrain, how
fast everything moves, which key it settles into, and the gestures you play over the
top. A performance can last five minutes or three hours.

This manual covers version 1.2 on macOS, as a standalone app and as an Audio Unit or
VST3 plugin.

## Contents

1. [Installing](#1-installing)
2. [A first session](#2-a-first-session)
3. [The window](#3-the-window)
4. [The terrain and scenes](#4-the-terrain-and-scenes)
5. [Time, key and tempo](#5-time-key-and-tempo)
6. [The pads](#6-the-pads)
7. [Sound sources](#7-sound-sources)
8. [Recorded on: the Medium](#8-recorded-on-the-medium)
9. [Seasons](#9-seasons)
10. [Mixer and effects](#10-mixer-and-effects)
11. [Master, auto master and safety](#11-master-auto-master-and-safety)
12. [Takes](#12-takes)
13. [Recording to disk](#13-recording-to-disk)
14. [Sessions, sounds and presets](#14-sessions-sounds-and-presets)
15. [MIDI](#15-midi)
16. [The projector](#16-the-projector)
17. [Appearance](#17-appearance)
18. [Tidefield in a DAW](#18-tidefield-in-a-daw)
19. [Keyboard reference](#19-keyboard-reference)
20. [Troubleshooting](#20-troubleshooting)

---

## 1. Installing

You need a Mac with Apple Silicon and macOS 12 or later.

**The app.** Download `Tidefield-macOS-arm64.zip` from the latest release, unzip it
and drag `Tidefield.app` into Applications. The app is not notarised by Apple, so the
first time you open it, right-click it, choose Open, then Open again. If macOS still
refuses, run this once in Terminal:

```
xattr -cr /Applications/Tidefield.app
```

Allow microphone access when asked if you want to play an instrument or voice through
the live input. Then click **Audio** in the top right (or press Cmd+,) and choose your
audio interface, sample rate and buffer size. A buffer of 256 samples is a good start.

**The plugins.** `Tidefield-Plugins-macOS-arm64.zip` holds an Audio Unit and a VST3.
Copy `Tidefield.component` to `~/Library/Audio/Plug-Ins/Components` and
`Tidefield.vst3` to `~/Library/Audio/Plug-Ins/VST3`, then rescan plugins in your DAW.
Tidefield appears as an instrument. See [Tidefield in a DAW](#18-tidefield-in-a-daw).

---

## 2. A first session

When Tidefield opens, a starter session is already laid out: six scenes named Still,
Dawn, Glass rain, Tidepool, Deep water and Storm, with a choir, a singing bowl and
glass loaded into the sampler sources. Everything is silent until you fade in.

1. Press **Space**. The whole instrument fades in over eight seconds.
2. Drag across the dark terrain in the middle of the window. The sound glides after
   your cursor, and each scene's marker fills as it takes over.
3. Press **1** to **6** to travel to a scene, or click its marker.
4. Raise **Wander** on the right. The sound starts to roam by itself near your cursor.
5. Hold **S** for a few seconds and let go. That is Swell: everything blooms, then ebbs
   back.
6. Press **M** and play the letter keys like a piano. Press **M** again to leave.
7. Press **K**. The last twenty seconds you heard become a new granular cloud that keeps
   playing.
8. When you have found something you like, double-click an empty part of the terrain
   to keep it there as a new scene.
9. Press **Space** to fade out, and **Cmd+S** to save the session.

Hover over anything and the status bar at the bottom says what it does.

---

## 3. The window

| Area | What is there |
|---|---|
| Top bar | the session menu, Fade, Panic, Rec, Auto master, Keys, tempo, CPU load, output meter and Audio |
| Browser (left) | scenes, Capture, the factory sounds and your own sounds |
| Terrain (centre) | the map you play on |
| Performance (right) | Tide, Wander, Gravity, Glide, key, scale, wander style and the Medium |
| Pads (under the terrain) | the gestures, the Shape pad and the take recorder |
| Device tabs (bottom) | every source, the mixer, effects, master and MIDI |
| Status bar | help for whatever is under the mouse, and messages |

**Controls.** Drag knobs and faders up and down; hold Shift for fine moves.
Double-click a control to reset it. Right-click any control for its menu:

- **MIDI learn**: the next knob you move on a controller takes over this control.
- **Forget MIDI mapping**: removes that mapping.
- **Release to the terrain**: hands a held control back (see
  [the live layer](#the-live-layer)).
- **Reset to default**.

**Colours carry meaning.** The theme's highlight colour marks values and selections.
Yellow means a control is held in the live layer. Pink means MIDI learn, and a small
pink arrow beside a knob means your controller has not caught up with it yet: turn the
controller in the direction of the arrow until it picks up.

**The top bar.**

- **Session name**: New, Open, Save, Save as, the projector window and Appearance.
- **Fade in / Fade out**: the same as Space.
- **Panic**: silence now. See [Safety](#safety).
- **Rec**: record to disk. Right-click for stems and the recordings folder.
- **Auto master**: see [Auto master](#auto-master).
- **Keys**: play Bloom from the computer keyboard (the same as M).
- **Sync, tempo and Tap**: see [Tempo sync](#tempo-sync).
- **CPU**: the audio load. "lite 1" to "lite 5" means Tidefield is lightening its own
  load to stay glitch-free (see [Troubleshooting](#20-troubleshooting)).
- **Audio**: the sound card, sample rate and buffer size.

---

## 4. The terrain and scenes

### Scenes

A **scene** is a snapshot of how the instrument sounds, pinned to a place on the
terrain. Where your cursor sits decides the sound: close to one scene you hear that
scene, between several you hear a blend of them. Scenes store only the controls that
shape sound, never the master level or the fade.

| To | Do this |
|---|---|
| Travel to a scene | click its marker, click it in the Browser, or press its number (1 to 9) |
| Arrive at once instead of gliding | Shift-click it, or Shift+number |
| Move a scene | drag its marker |
| Keep what you hear as a new scene | double-click an empty spot, or press C to place it at the cursor |
| Rename, update or delete | right-click the marker or its row in the Browser |

A terrain holds up to 32 scenes. The scene menu has:

- **Glide here**.
- **Rename**.
- **Update with what you hear now**: overwrites the scene with the current sound.
- **Fold held controls into this scene**: writes only the controls you are holding in
  the live layer into the scene, and leaves the rest of it alone.
- **Delete**.

### Moving around

| Control | Range | What it does |
|---|---|---|
| Glide | 50 ms to 30 s | how long the sound takes to arrive where you point |
| Focus (Gestures tab) | 1 to 6 | high: each scene is a distinct place; low: everything blends |
| Wander | 0 to 100% | how far the sound strays from your cursor on its own |
| Wander Rate (Gestures tab) | | how fast it strays |

The arrow keys nudge the cursor (Shift for small steps).

### Wander styles

Under Wander on the right:

- **Drift**: a slow random walk around your cursor.
- **Orbit**: circles around the cursor.
- **Tide pool**: drifts but is pulled towards the nearest scene, so it settles into
  places.
- **Journey**: travels from scene to scene on its own.
- **Path**: follows a loop you draw. Press **Draw path** above the terrain (or **P**),
  then draw a loop with the mouse. The sound travels it; Wander sets how closely and
  Wander Rate how fast. Press P again or Escape to stop drawing.

### The live layer

When you touch a control, it stops following the terrain and stays where you put it.
It turns yellow to show it is held. Moving across the terrain then changes everything
else, but not that control.

Press **R** to hand every held control back to the terrain, or right-click one control
and choose **Release to the terrain**. To make a held setting part of a scene, use
**Fold held controls into this scene** from the scene's menu.

---

## 5. Time, key and tempo

### Tide

**Tide** (0.05x to 8x) is the speed of everything that moves by itself: drift, wander,
rain, the grain scan, the cycles and the seasons. At 0.25x the whole piece breathes
four times slower. Pitch and delay times never change with Tide.

### Key, scale and Gravity

Every pitched source plays in the key and scale chosen on the right.

- **Scales**: Major, Minor, Dorian, Lydian, Mixolydian, Major pentatonic, Minor
  pentatonic, Whole tone, Hirajoshi, In sen, Fifths and Chromatic.
- **Gravity** (0 to 100%) is how strongly every source is pulled into the key. At 100%
  everything snaps to scale notes; lower, notes lean towards them and keep some of their
  own pitch. Several sources also have their own Gravity control, which scales this one
  for that source alone.
- **Changing key.** A key change is never a jump. Each voice moves to the new key at
  its own moment over the **Key Morph** time (0.5 to 60 s, Gestures tab), so the harmony
  turns over gradually.

### Tempo sync

Tidefield is free-time by default. Turn on **Sync** in the top bar and:

- the Cycles lock to whole beats;
- the Tape Delay and Worn Echo snap their times to note lengths.

Drag the tempo number to change it (40 to 200 BPM), or click **Tap** in time. The dot
beside Sync blinks on the beat. In a DAW, Sync follows the project's tempo and position
and the number turns the tide colour.

---

## 6. The pads

The row under the terrain holds the gestures. Each pad shows its key in the corner, its
state underneath and its level as a line of water rising from the bottom.

| Pad | Key | What it does |
|---|---|---|
| Swell | hold S | every send blooms, filters open and the clouds thicken; on release it ebbs back |
| Hush | hold H | every source sinks while the reverb and delay keep ringing |
| Slow | hold T | time slows to a quarter, then eases back when you let go |
| Shape | drag | left darkens, right brightens; up pushes the sound far into the reverb, down pulls it close and dry. Double-click to recentre |
| Freeze all | F | holds the last two seconds as a cloud while the rest of the mix steps back |
| Hold input | I | freezes the live input's sound into an endless spectral pad |
| Loop | L | the tape looper: record, close the loop, overdub (Shift+L clears) |
| Cycles | E | long note loops that never line up |
| Take | G | records your moves and plays them back (see [Takes](#12-takes)) |
| Catch | K | turns the last few seconds into a cloud |

Swell's depth, rise and ebb times, Hush's depth and Freeze's settings live on the
**Gestures** tab.

---

## 7. Sound sources

Each tab along the bottom opens a source. Most tabs end with that source's **strip**:
level, pan, width, reverb and delay sends, and **Effects** for its two insert slots.
A level or send turned all the way down reads Off and is silent.

### Drone

A stack of up to six slowly drifting voices built on one root note.

| Control | Range | What it does |
|---|---|---|
| Root Note | C1 to C5 | the lowest note; the voices stack on it in the key |
| Density | 1 to 6 | how many voices sound at once |
| Shape | 0 to 100% | from a bright saw (left) to a pure sine (right) |
| Detune | 0 to 50 cents | how far the voices spread from true pitch |
| Brightness | 60 Hz to 12 kHz | the filter |
| Resonance | 0 to 95% | the filter's peak |
| Breath | 0 to 100% | air and breath under the tone |
| Evolve | 0 to 100% | how often voices move to new notes of the chord |
| Drift Depth, Drift Rate | | how far and how fast each voice wanders in pitch and tone |
| Spread | 0 to 100% | stereo width of the voices |
| Gravity | 0 to 100% | this source's pull into the key |

### Clouds

Four granular clouds, each playing a sound in tiny overlapping grains. Load a sound by
clicking the waveform, by dropping one from the Browser, or with Catch. Right-click the
waveform to clear it. The dots on the waveform are the grains playing right now, and
the line is where the cloud is reading.

| Control | Range | What it does |
|---|---|---|
| Density | 0.5 to 200 grains a second | how thick the cloud is |
| Grain Size | 10 ms to 2 s | short grains sparkle, long grains smear |
| Position | 0 to 100% | where in the sound the grains come from |
| Spray | 0 to 100% | how far grains scatter around Position |
| Scan | -100 to +100% | moves Position by itself, backwards or forwards, at Tide speed |
| Pitch | -24 to +24 semitones | |
| Detune | 0 to 100% | random pitch spread between grains |
| Harmonize | 0 to 100% | some grains play at other notes of the chord |
| Reverse | 0 to 100% | the share of grains played backwards |
| Envelope | 0 to 100% | the shape of each grain: percussive at 0, soft at 50%, flat and full at 100% |
| Stereo | 0 to 100% | how widely grains spread across the field |
| Gravity | 0 to 100% | pulls grain pitches into the key |

### Resonator

A bank of tuned resonators, like strings or bells, that ring when something strikes
them.

| Control | What it does |
|---|---|
| Root Note, Modes | the lowest note and how many resonances (1 to 24) |
| Structure | from strings on the left to bars and bells on the right |
| Decay | how long each mode rings, 0.1 to 60 s |
| Brightness, Spread, Gravity | tone, stereo width and pull into the key (bells at the far right of Structure keep their own tuning) |
| Rain, Rain Colour | random strikes like drops on a resonant surface, and their tone |
| Excite from: Input, Drone, Clouds, Bloom | lets other sources strike the resonators |

### Bloom

A keyboard sampler that turns ordinary one-shots (a pluck, a glass tap, a word) into
ambient material. Load a sound with the waveform, then play it from the keyboard on the
tab, the computer keyboard (M), a MIDI keyboard or the Cycles.

Each note goes through a **Transform**:

| Transform | Result |
|---|---|
| Swell | the tail reversed rising into the attack, then stretched into a pad |
| Smear | a long granular stretch with the pitch held |
| Freeze | one moment (set by Position) held forever with a slow shimmer |
| Ghost | the attack removed; only a resonant tail, tuned to the key |
| Constellation | the note scattered as a chord from the scale, arriving at random |
| Tape | varispeed playback through its own worn medium |

Other controls: **Amount** (how strongly the transform acts), **Length**, **Attack**
(Swell rises out of the sample by itself, and Ghost always fades in over at least
1.5 s), **Release**, **Pitch**, **Tone** (follows notes that are already sounding),
**Spread**, **Random** (variation between repeated notes so they never sound
identical), **Position** (the moment Freeze and Ghost take), **Gravity** and **Sample
Root** (the note the sound was recorded at, so it plays in tune).

### Input

The live input from your audio interface: a microphone, guitar or synth.

- **Monitor**: lets you hear the input through its strip. It is off until you turn it
  on, so there is no surprise feedback.
- **Channel**: Input 1, Input 2 or both.
- **Input Gain**, **Low Cut** and **Gate**.
- **Hold** (or I, or the Hold input pad): freezes whatever the input is playing into an
  endless pad. **Freeze Level** sets its volume and **Freeze Drift** how much it moves.
- **Catch** settings live here:
  - **Catch Source**: the output or the live input.
  - **Catch Into**: Auto picks a cloud for you, or name one.
  - **Catch Length**: 5 to 30 s.
  - **Catch now**: does the same as K.

### Looper

A tape loop that wears out, in the manner of William Basinski's Disintegration Loops.

1. Press **Record** (or L) to start.
2. Press it again to close the loop. It then plays on its own.
3. Press again to overdub, and again to stop overdubbing.
4. Shift+L fades the loop out and clears it.

| Control | What it does |
|---|---|
| Source | records the live input or the whole mix |
| Erosion | how much each pass wears the tape |
| Flakes | dropouts, like oxide falling off old tape (they come with Erosion; at 0 Erosion the tape stays whole) |
| Overdub | how much of the old loop survives each overdub |

### Weather

Procedural wind, rain and surf. **Wind**, **Rain** and **Surf** set each layer.
**Gusts** sets how much they swell and lull, **Tone** sets the brightness, and
**Distance** runs from right here to far across a valley.

### Cycles

Several sparse note loops of different lengths that never line up, after Brian Eno's
*Music for Airports*. The notes come from the key, so they always agree. Turn them on
with **E** or the Cycles pad.

| Control | What it does |
|---|---|
| Play Into | Bloom, the Resonator or both |
| Voices | how many loops, 1 to 8 |
| Pattern | which set of cycle lengths and notes; try a few |
| Pace | the speed of every loop together, 0.25x to 4x |
| Density | how many of each loop's notes actually play |
| Register, Spread | the centre note and how many octaves the notes cover |
| Velocity | how hard the notes are played |

The **Tempo** device on the same tab holds Tempo Sync and the tempo itself.

---

## 8. Recorded on: the Medium

**Recorded on**, at the bottom of the Performance panel, makes everything sound as if it
had been printed to a medium. It sits on the master, so you hear it live and it is in
your recordings.

| Type | Sound |
|---|---|
| Digital | clean |
| Cassette | hiss, wow and flutter, a soft low boost, highs that fade with age, gentle saturation, the odd dropout |
| Vinyl | crackle and pops, surface noise and rumble, slow wow, mono lows |
| Sampler | 12-bit grit, a lower sample rate, input drive and a warm filter |

On the Gestures tab: **Age** (how worn), **Noise**, **Wobble**, **Drive** and
**Medium Mix**. Switching type crossfades smoothly, so you can change it mid-piece.

---

## 9. Seasons

A season moves one control slowly back and forth for as long as you play: a filter that
opens over ten minutes, a reverb send that rises and falls once an hour.

1. Open the **Seasons** tab and click **Add a season**.
2. Choose the control it should move.
3. Click the season in the list to set its depth, its length (20 seconds to an hour)
   and its shape (Sine, Triangle or Drift).

The dot in each row shows where the cycle is now. Up to eight seasons can run at once.
**Seasons** under All seasons scales every season together, so you can fade the whole
of that slow movement in or out. Seasons follow Tide.

---

## 10. Mixer and effects

### Mixer

The **Mixer** tab shows every source's strip side by side, plus the reverb and delay
**Returns**. Each source has:

- a level fader with a meter;
- pan;
- width (0 is mono, 100% as recorded, 200% wider);
- a send to the reverb bus and a send to the delay bus.

### Effects

There are 28 effect slots:

- two on every source;
- two on the reverb bus;
- two on the delay bus;
- two on the master.

Open the **Effects** tab, pick a chain on the left, then choose an effect type in each
slot. Every slot has six controls and a **Mix**.

| Effect | Controls |
|---|---|
| Cloud Reverb | Size, Decay, Damping, Pre-delay, Modulation, Hold (holds the tail forever) |
| Tape Delay | Time, Feedback, Tone, Spread, Wobble, Age |
| Worn Echo | Time, Feedback, Medium, Age, Wobble, Spread (a delay that degrades with every repeat) |
| Ensemble | Rate, Depth, Vibrato, Voices, Spread, Tone |
| Spectral Blur | Blur, Smear, Drift, Shimmer, Tone, Freeze |
| Sympathetic Strings | Excite, Decay, Brightness, Register, Strings, Level (strings tuned to the key that ring along with the sound) |
| Medium | Type, Age, Noise, Wobble, Drive |

The reverb bus starts with Cloud Reverb and the delay bus with Tape Delay. Effects can
be changed while playing; the change crossfades.

---

## 11. Master, auto master and safety

### Master

The **Master** tab has:

- **Master** level.
- **Fade Length** (0.5 to 120 s): how long Space and the Fade button take.
- **Limiter Ceiling** (-12 to 0 dB): the highest peak that can ever leave Tidefield.
- Buttons for the master, reverb and delay effect chains.

### Auto master

Turn on **Auto master** in the top bar for a finished, balanced sound without a
mastering engineer. It listens to the mix and adjusts loudness, tonal balance, glue and
width as the piece changes.

Choose a **Loudness** target:

- **Broadcast**: -23 LUFS.
- **Streaming**: -16 LUFS.
- **Loud**: -14 LUFS.

**Correction** sets how far the tonal changes go. The display on the Master tab shows
what it is doing at each moment.

### Safety

- **Panic** (Esc) silences everything within a twentieth of a second and clears every
  tail, so nothing returns. Press it again to fade back in.
- The **limiter** guarantees no output peak above the ceiling.
- If anything inside goes numerically wrong, Tidefield silences that moment, resets and
  fades back in by itself, and says so in the status bar.

---

## 12. Takes

A take records your moves (knobs, the terrain, notes) and plays them back on a loop,
so a gesture can keep repeating while you play something else.

1. Press **G** (or the Take pad). It says "recording".
2. Play: move the terrain, turn knobs, play notes.
3. Press **G** again to stop. The take is ready.
4. Press **G** to play it back looped, and **G** once more to stop.

Shift+G always records a new take. Right-click the Take pad to play, stop, turn looping
on or off, or clear the take. A take is saved with the session.

---

## 13. Recording to disk

Press **Shift+R** or **Rec** to record what you hear, and again to stop. Each recording
gets its own folder in `~/Music/Tidefield` containing `master.wav` as 32-bit float.

Right-click Rec for:

- **Record stems too**: adds a `stems` folder with every source and both returns as
  separate files, all sample-aligned.
- **Recordings folder**: choose a different folder.
- **Show recordings**: opens the folder in Finder.

---

## 14. Sessions, sounds and presets

### Sessions

A session is one `.tidefield` file holding everything:

- every setting;
- the scenes and seasons;
- the drawn path;
- the take;
- your MIDI mappings;
- the sounds themselves.

Copy the file to another Mac and it opens complete. Use the session menu or
Cmd+N, Cmd+O, Cmd+S and Shift+Cmd+S. Opening a session crossfades to it.

### Sounds

The **Browser** lists 18 factory sounds in three groups:

- **Tonal**: Glass, Singing bowl, Kalimba, Felt piano, Marimba, Bell, Pluck.
- **Pads**: Chord, Choir, Bowed strings, Harmonium, Sub organ, Shimmer.
- **Textures**: Wind chimes, Breath, Ocean, Rain on leaves, Tape dust.

Click a sound and choose a cloud or Bloom to load it into. Under **Your sounds**, load a
WAV, AIFF, FLAC, Ogg or MP3 file from disk the same way.

### Presets

Every device has a **Presets** menu in its title bar, and so does every effect. There
are 29 factory presets to start from. **Save as preset** keeps the device's current
settings under a name. Your presets live in `~/Music/Tidefield/Presets`, one folder per
kind of device, and can be copied between machines.

---

## 15. MIDI

Tidefield works with any MIDI controller or keyboard. Enable devices on the **MIDI**
tab.

- **Knobs and faders.** Right-click any control, choose **MIDI learn** and move a knob
  on your controller. Mappings use soft takeover: after a scene change, a knob does
  nothing until you turn it past the current value, so nothing jumps.
- **Buttons and pads.** Under **Learn a pad for** on the MIDI tab, click an action, then
  press a button or pad on your controller. Actions:
  - Fade
  - Panic
  - Catch
  - Capture scene
  - Release
  - Record
  - Loop
  - Loop clear
  - Freeze all
  - Hold input
- **Notes** play Bloom. Choose which channel to listen to. Turn on **Notes move the
  drone** to have played notes set the drone's root as well.
- **Default mapping** sets up a generic eight-knob controller on CC 21 to 28: terrain X
  and Y, Tide, Wander, Gravity, reverb return, Cloud 1 density and master level.

The mappings list shows everything mapped. Click the x on a row to remove it. Mappings
belong to your setup, so they are kept between sessions, and are also saved in each
session file.

---

## 16. The projector

Cmd+P (or the session menu) opens the terrain alone in its own window, for an audience.
Drag it to a second display or a projector and make it full screen. It shows the moving
sound and the scenes as places, without buttons or crosshairs. You keep playing in the
main window.

---

## 17. Appearance

Choose a theme from the session menu under **Appearance**. The terrain and meters stay
dark in every theme so the performance surface reads the same.

| Theme | Character |
|---|---|
| Slate | coastal grey-blue with sand highlights; the default |
| Night swim | near black, for dark stages |
| Control room | neutral studio darks with blue selection |
| Ember | warm charcoal with little blue light, for late sets |
| Graphite | plain studio grey |
| Heather | dusky violet |
| Paper | soft light grey-green |
| Dune | warm sand |
| Sea glass | pale aqua |
| Daylight | high contrast, for playing outdoors |

---

## 18. Tidefield in a DAW

Insert Tidefield on an instrument track. It behaves like the app, with a few
differences.

- The DAW owns the audio device, so there is no Audio button. Space and the keys
  Tidefield does not use go to the DAW.
- MIDI on the track plays Bloom and drives your mappings.
- With Sync on, the Cycles and delays follow the project's tempo and position.
- The whole session, sounds included, is saved inside the DAW project.
- Each instance is independent; you can run several.

---

## 19. Keyboard reference

| Key | Action |
|---|---|
| Space | fade in or out |
| Esc | panic, press again to resume (stops path drawing first if drawing) |
| 1 to 9 | glide to a scene; Shift to jump |
| Arrow keys | nudge the cursor; Shift for small steps |
| C | capture a scene at the cursor |
| R | release every held control to the terrain |
| Shift+R | start or stop recording |
| hold S | Swell |
| hold H | Hush |
| hold T | Slow |
| F | Freeze all |
| I | Hold input |
| L | loop record, close, overdub; Shift+L clears |
| E | Cycles on or off |
| K | Catch |
| G | take: record, stop, play; Shift+G records a new take |
| P | draw a path |
| M | Keys on or off |
| Tab, Shift+Tab | next or previous device tab |
| Cmd+N, Cmd+O | new session, open |
| Cmd+S, Shift+Cmd+S | save, save as |
| Cmd+P | projector window |
| Cmd+, | audio settings |

**With Keys on (M)**:

- the row A W S E D F T G Y H U J K plays Bloom chromatically from middle C;
- Z and X move the octave;
- C and V change velocity.

The other letter shortcuts are off until you press M again.

---

## 20. Troubleshooting

**No sound.**

- Press Space; the instrument starts faded out.
- Check that Panic is not on (Esc toggles it).
- Check Audio for the right output.
- Make sure the Master level is up.

**The live input is silent.** Turn on **Monitor** on the Input tab. Check that macOS
allowed microphone access in System Settings, Privacy and Security, Microphone.

**"lite" appears beside the CPU figure.** Tidefield is running close to the limit of
your computer and is gently thinning grains, resonator modes and voices so the audio
never breaks up. It steps back up by itself when there is headroom. To avoid it:

- raise the buffer size under Audio;
- lower cloud densities;
- use fewer resonator modes.

**A knob does nothing when I turn my controller.** It is waiting to pick up: the pink
arrow shows which way to turn until it meets the current value.

**A control ignores the terrain.** It is held in the live layer (yellow). Press R, or
right-click it and choose Release to the terrain.

**macOS says the app is damaged or cannot be opened.** Run
`xattr -cr /Applications/Tidefield.app` in Terminal once, then open it again.

**The plugin does not appear in my DAW.** Check that the files are in
`~/Library/Audio/Plug-Ins/Components` (Audio Unit) or `~/Library/Audio/Plug-Ins/VST3`,
then rescan. Logic Pro only shows Audio Units that pass Apple's validation; restarting
Logic triggers a new scan.
