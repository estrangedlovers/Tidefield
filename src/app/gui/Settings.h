#pragma once

#include "Model.h"

namespace tf::app::gui {
class MainView;

enum class SettingsTab { Look, Audio, Midi, Plugins, Files, Record, About, Count };

void openSettings(MainView& view, Model& model, SettingsTab tab = SettingsTab::Look);
void closeSettings();
bool isSettingsOpen();

float interfaceScale(AppCore& core);
void setInterfaceScale(AppCore& core, float scale);
bool hoverHelpEnabled(AppCore& core);
}
