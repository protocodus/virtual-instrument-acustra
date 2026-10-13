// Renders the plug-in editor, as it opens for a new instance on a display
// with room for it, to a PNG: the README's screenshot
// (Docs/screenshots/acustra-standalone.png) and a way to look at a layout
// change without a host. The editor is painted off screen at its design
// size, so no window is opened; on Linux the fonts still need a display,
// which xvfb-run provides.
//
//   AcustraEditorSnapshot OUTPUT.png [WIDTH]
//
// WIDTH scales the editor at its fixed aspect ratio; the default is the
// design width, 1120, whatever room the (virtual) display has.
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cstdio>
#include <memory>

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 4)
    {
        std::fprintf(stderr, "usage: AcustraEditorSnapshot OUTPUT.png [WIDTH] [rear|inverted]\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    auto processor = std::make_unique<AcustraAudioProcessor>();
    processor->prepareToPlay(48000.0, 256);
    std::unique_ptr<juce::AudioProcessorEditor> editor { processor->createEditor() };
    if (editor == nullptr)
    {
        std::fprintf(stderr, "the processor has no editor\n");
        return 1;
    }
    const int width = argc >= 3 ? juce::String(argv[2]).getIntValue() : 1120;
    if (width > 0)
        editor->setSize(width, juce::roundToInt(
            static_cast<float>(width) * static_cast<float>(editor->getHeight())
            / static_cast<float>(editor->getWidth())));
    if (argc == 4)
    {
        if (juce::String(argv[3]) == "inverted")
            processor->parameters.getParameter(acustra::parameters::piezoPolarityInverted)
                ->setValueNotifyingHost(1.0f);
        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*>(child);
                button != nullptr && button->getName() == "Back settings")
            {
                button->setToggleState(true, juce::dontSendNotification);
                button->onClick();
            }
    }
    // The status line and construction switches follow the editor's timer.
    juce::Timer::callPendingTimersSynchronously();
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), false, 1.0f);
    const juce::File output = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    output.deleteFile();
    juce::FileOutputStream stream(output);
    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream(image, stream))
    {
        std::fprintf(stderr, "could not write %s\n", output.getFullPathName().toRawUTF8());
        return 1;
    }
    std::printf("wrote %s (%d x %d)\n", output.getFullPathName().toRawUTF8(),
                image.getWidth(), image.getHeight());
    return 0;
}
