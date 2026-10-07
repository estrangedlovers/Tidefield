// Your own JUCE effects (the reverse shimmer and the fuzz) go here.
//
// 1. Copy the plugin's processor sources (not its editor) into src/fx_juce/user/
//    and list them in src/fx_juce/CMakeLists.txt (USER_EFFECT_SOURCES).
// 2. Remove or rename `createPluginFilter()` in each: a plugin defines it for the
//    plugin wrapper, and two of them in one app would collide.
// 3. Include the processor header and register it below. Pick the six parameters a
//    performer should reach (by parameter ID); they become the slot's controls, so
//    scenes, MIDI learn and sessions work on them like on any built-in effect.
//
//    #include "user/ReverseShimmerProcessor.h"
//    #include "user/FuzzProcessor.h"
//
//    registerAudioProcessorEffect<ReverseShimmerAudioProcessor>(
//        "user.reverseShimmer", "Reverse Shimmer", true, { "mix", "decay", "shift", "reverse", "tone", "size" });
//    registerAudioProcessorEffect<FuzzAudioProcessor>(
//        "user.fuzz", "Fuzz", false, { "gain", "tone", "bias", "gate", "level", "mix" });
//
// The type IDs are stored in sessions: keep them stable once you have saved one.

#include "AudioProcessorEffect.h"

namespace tf::fxjuce {

void registerUserEffects()
{
}

} // namespace tf::fxjuce
