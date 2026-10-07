#include "AudioHost.h"
#include "MainComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace tf::app {

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name, AudioHost& host)
        : DocumentWindow(name, juce::Colour(0xff12151a), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(host), true);
        setResizable(true, true);
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

    void initialise(const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Tidefield";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName = "Tidefield";
        settings.setStorageParameters(options);

        host = std::make_unique<AudioHost>(*settings.getUserSettings());
        window = std::make_unique<MainWindow>(getApplicationName(), *host);
    }

    void shutdown() override
    {
        window.reset();
        host.reset();
        settings.closeFiles();
    }

    void systemRequestedQuit() override { quit(); }

private:
    juce::ApplicationProperties settings;
    std::unique_ptr<AudioHost> host;
    std::unique_ptr<MainWindow> window;
};

} // namespace tf::app

START_JUCE_APPLICATION(tf::app::TidefieldApplication)
