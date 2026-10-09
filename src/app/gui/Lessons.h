#pragma once

#include "Controls.h"

#include <optional>
#include <span>

namespace tf::app::gui {
class MainView;

enum class LessonWait { None, FadeIn, Cursor, Page, Param, Capture, Route, Season, Take, Record };

struct LessonPage
{
    const char* title = "";
    const char* text = "";
    const char* page = nullptr;
    const char* highlight = nullptr;
    LessonWait wait = LessonWait::None;
    const char* waitFor = nullptr;
    int glideTo = -1;

    constexpr LessonPage on(const char* devicePage) const
    {
        auto p = *this;
        p.page = devicePage;
        return p;
    }
    constexpr LessonPage show(const char* target) const
    {
        auto p = *this;
        p.highlight = target;
        return p;
    }
    constexpr LessonPage until(LessonWait w, const char* what = nullptr) const
    {
        auto p = *this;
        p.wait = w;
        p.waitFor = what;
        return p;
    }
    constexpr LessonPage glide(int scene) const
    {
        auto p = *this;
        p.glideTo = scene;
        return p;
    }
};

struct Lesson
{
    const char* title;
    const char* summary;
    std::span<const LessonPage> pages;
};

std::span<const Lesson> lessons();
const Lesson& whatsNew();
inline constexpr const char* kWhatsNewVersion = "1.4";
int lessonPageIndex(const juce::String& name);
juce::String lessonText(const char* text, const KeyBindings& keys);
juce::StringArray lessonProblems(const engine::ParamRegistry& registry);
bool isNewerVersion(const juce::String& version, const juce::String& than);
bool shouldOfferWhatsNew(const juce::String& lastSeen, const juce::String& running);

struct LessonPosition
{
    int lesson = 0, page = 0;
};
LessonPosition savedLessonPosition(juce::PropertiesFile& settings);
void saveLessonPosition(juce::PropertiesFile& settings, LessonPosition position);

class LessonPanel final : public juce::Component, public Animated
{
public:
    LessonPanel(Model& m, MainView& v, bool runActions);
    ~LessonPanel() override;

    void show(int lesson, int page);
    void showWhatsNew(int page);
    void restoreWhatsNew(int page);
    bool isShowingWhatsNew() const noexcept { return news; }
    LessonPosition getPosition() const noexcept { return { lesson, page }; }
    juce::String missingTarget() const;

    void tick() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    const Lesson& currentLesson() const;
    const LessonPage& current() const;
    int pageCount() const;
    void enter();
    void step(int direction);
    void showIndex(bool on);
    void refresh();
    bool waitDone() const;
    juce::Rectangle<int> indexRow(int i) const;

    Model& model;
    MainView& view;
    int lesson = 0, page = 0;
    bool indexShown = false, done = false, news = false;
    int shownKeys = -1;
    double doneAt = 0.0;
    float baseValue = 0.0f;
    float baseX = 0.0f, baseY = 0.0f;
    std::size_t baseCount = 0;
    int hoverRow = -1;
    std::optional<engine::ParamIndex> waitParam;
    juce::String highlightTarget;
    FlatButton backButton { "Back" }, nextButton { "Next", colour::accent() }, indexButton { "All lessons" }, closeButton { juce::String::fromUTF8("\xc3\x97") };
    juce::TextLayout body;
    juce::Rectangle<int> textArea, tryArea;
};
}
