#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace tf::app {
void checkGuestPath(const std::function<void(bool, const juce::String&)>& check);
}
