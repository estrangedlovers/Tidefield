Tidefield plugins (Audio Unit and VST3, macOS 12+, Apple Silicon)

Install
  Tidefield.component  ->  ~/Library/Audio/Plug-Ins/Components/
  Tidefield.vst3       ->  ~/Library/Audio/Plug-Ins/VST3/
Then rescan plugins in your DAW. Tidefield appears as an instrument.

If macOS blocks them (they are signed ad hoc, not notarised), run in Terminal:
  xattr -cr ~/Library/Audio/Plug-Ins/Components/Tidefield.component
  xattr -cr ~/Library/Audio/Plug-Ins/VST3/Tidefield.vst3

In a DAW
  - MIDI from the track plays Bloom; CCs drive your MIDI mappings and MIDI learn.
  - An optional stereo input feeds the live-input strip (route audio to it as a sidechain).
  - The whole piece (scenes, sounds, effects, seasons, path) is saved in your project.
  - Space belongs to the DAW; every other Tidefield key works when its window has focus.
