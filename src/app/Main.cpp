#include "AppCore.h"
#include "AudioHost.h"
#include "ClassicUI.h"
#include "web/WebUI.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace tf::app {

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name, juce::Component* content)
        : DocumentWindow(name, juce::Colour(0xff0d1014), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(content, true);
        setResizable(true, true);
        setResizeLimits(1100, 720, 10000, 10000);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class TidefieldApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Tidefield";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName = "Tidefield";
        settings.setStorageParameters(options);

        host = std::make_unique<AudioHost>(*settings.getUserSettings());
        core = std::make_unique<AppCore>(*host);

        // The web UI is the instrument's face; the JUCE panel is the fallback when the
        // built UI is missing, or on request (--classic or TIDEFIELD_CLASSIC_UI=1).
        const bool forceClassic = commandLine.contains("--classic")
                                  || juce::SystemStats::getEnvironmentVariable("TIDEFIELD_CLASSIC_UI", {}) == "1";
        juce::Component* content = nullptr;
        const auto uiRoot = WebUI::findUiRoot();
        if (! forceClassic && (uiRoot.has_value() || WebUI::devServerUrl().isNotEmpty()))
            content = new WebUI(*core, uiRoot.value_or(juce::File()));
        else
            content = new ClassicUI(*core);

        window = std::make_unique<MainWindow>(getApplicationName() + " - " + core->session.getName(), content);
    }

    void shutdown() override
    {
        window.reset();
        core.reset();
        host.reset();
        settings.closeFiles();
    }

    void systemRequestedQuit() override { quit(); }

private:
    juce::ApplicationProperties settings;
    std::unique_ptr<AudioHost> host;
    std::unique_ptr<AppCore> core;
    std::unique_ptr<MainWindow> window;
};

} // namespace tf::app

START_JUCE_APPLICATION(tf::app::TidefieldApplication)
