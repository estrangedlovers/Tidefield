#pragma once

#include "Model.h"

namespace tf::app::gui {
class MainView;

enum class SettingsTab { Look, Audio, Midi, Controllers, Keys, Plugins, Files, Record, About, Count };

void openSettings(MainView& view, Model& model, SettingsTab tab = SettingsTab::Look);
void openSettingsFrom(juce::Component& component, SettingsTab tab);
void closeSettings();
bool isSettingsOpen();

float interfaceScale(AppCore& core);
void setInterfaceScale(AppCore& core, float scale);
bool hoverHelpEnabled(AppCore& core);
}
