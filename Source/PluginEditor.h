#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

#include <array>
#include <memory>

class AcustraLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AcustraLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour&, bool isHighlighted,
                               bool isDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

class AcustraAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit AcustraAudioProcessorEditor (AcustraAudioProcessor&);
    ~AcustraAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class ChoiceButtonGroup;
    class StringActivityDisplay;

    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void updateConstructionControls();
    void configureSetupChoice (std::size_t index, const juce::String& name,
                               const char* parameterId,
                               const juce::String& description, int columns = 2);
    void configureChoice (std::size_t index, const juce::String& name,
                          const char* parameterId,
                          const juce::String& description);
    void configureSlider (std::size_t index, const juce::String& name,
                          const char* parameterId, const juce::String& description,
                          bool decibels = false);

    AcustraAudioProcessor& audioProcessor;
    AcustraLookAndFeel lookAndFeel;
    juce::Image cedarBackground;
    juce::Image walnutBackground;
    juce::TooltipWindow tooltipWindow { this, 600 };

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label statusLabel;
    juce::TextButton panicButton { "PANIC" };
    juce::TextButton gatherChordsButton { "CHORDS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        gatherChordsAttachment;

    std::array<juce::Label, 3> setupLabels;
    std::array<std::unique_ptr<ChoiceButtonGroup>, 3> setupControls;

    std::array<juce::Label, 3> choiceLabels;
    std::array<std::unique_ptr<ChoiceButtonGroup>, 3> choiceControls;
    std::array<juce::Label, 7> sliderLabels;
    std::array<juce::Slider, 7> sliderControls;
    std::unique_ptr<StringActivityDisplay> stringActivityDisplay;

    juce::MidiKeyboardComponent keyboard {
        audioProcessor.keyboardState,
        juce::MidiKeyboardComponent::horizontalKeyboard
    };

    std::array<std::unique_ptr<SliderAttachment>, 7> sliderAttachments;

    juce::Rectangle<int> guitarPanelBounds;
    juce::Rectangle<int> playerPanelBounds;
    juce::Rectangle<int> capturePanelBounds;
    juce::Rectangle<int> keyboardPanelBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AcustraAudioProcessorEditor)
};
