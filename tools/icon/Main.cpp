// tidefield_icon <output dir> <wordmark font.ttf>
// Writes icon_1024.png (the app and plugin icon) and logo.png (mark and wordmark,
// for the README) from the paths in src/app/gui/Logo.h.

#include "app/gui/Logo.h"

#include <iostream>

namespace {

namespace logo = tf::app::gui::logo;

bool writePng(const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat png;
    return out.openedOk() && png.writeImageToStream(image, out);
}

/** The macOS icon grid: an 824 px tile centred on a 1024 px canvas, with the soft
    drop shadow the system icons carry. */
juce::Image icon()
{
    juce::Image image(juce::Image::ARGB, 1024, 1024, true);
    juce::Graphics g(image);
    const auto tile = juce::Rectangle<float>(100.0f, 100.0f, 824.0f, 824.0f);
    juce::DropShadow(juce::Colours::black.withAlpha(0.28f), 28, { 0, 12 }).drawForPath(g, logo::outline(tile));
    logo::drawMark(g, tile);
    return image;
}

/** Mark above the lowercase wordmark, on the paper colour, like the brand sheet. */
juce::Image sheet(const juce::Typeface::Ptr& face)
{
    juce::Image image(juce::Image::ARGB, 1200, 1200, true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xfff8f8f5));
    logo::drawMark(g, { 330.0f, 210.0f, 540.0f, 540.0f });
    g.setColour(logo::ink);
    g.setFont(juce::Font(juce::FontOptions(face).withHeight(196.0f)));
    g.drawText("tidefield", juce::Rectangle<float>(0.0f, 790.0f, 1200.0f, 230.0f), juce::Justification::centred);
    return image;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: tidefield_icon <output dir> <wordmark font.ttf>\n";
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    juce::MemoryBlock fontData;
    if (! juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]).loadFileAsData(fontData))
    {
        std::cerr << "cannot read " << argv[2] << "\n";
        return 1;
    }
    const auto face = juce::Typeface::createSystemTypefaceFor(fontData.getData(), fontData.getSize());
    dir.createDirectory();
    const bool ok = writePng(icon(), dir.getChildFile("icon_1024.png"))
                 && writePng(sheet(face), dir.getChildFile("logo.png"));
    std::cout << (ok ? "wrote " : "failed to write ") << dir.getFullPathName() << "/{icon_1024,logo}.png\n";
    return ok ? 0 : 1;
}
