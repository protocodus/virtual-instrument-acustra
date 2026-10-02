#include "PluginEditor.h"
#include "AcustraUIAssets.h"

#include <vector>

namespace
{
constexpr int designWidth = 1120;
constexpr int designHeight = 800;
constexpr int minimumWidth = 896;
constexpr int minimumHeight = 640;
constexpr int maximumWidth = 1456;
constexpr int maximumHeight = 1040;
constexpr int keyboardFirstNote = 38; // Drop-D low string
constexpr int keyboardLastNote = 84;  // twentieth fret of the high E string
constexpr int keyboardWhiteKeyCount = 28;

struct ConstructionPreset
{
    const char* name;
    const char* caption;
    acustra::BodyShape shape;
    acustra::BodyMaterial wood;
    acustra::GuitarModel model { acustra::GuitarModel::Original };
};

// Construction directions, not measured replicas of manufacturer models.
// The style presets play the Original body: g21's radiation and its own
// bridge on the radiation's poles, at a steel-string guitar's measured
// mobility, so one body loads the string and radiates it (README, How it
// works). The Bellido preset selects that measured guitar in its own family,
// in Mahogany, the kept wood nearest the cedar it was built of.
constexpr std::array<ConstructionPreset, 4> constructionPresets {{
    { "Dreadnought / Martin style", "Dreadnought", acustra::BodyShape::Dreadnought,
      acustra::BodyMaterial::Spruce },
    { "Auditorium / Taylor style", "Auditorium", acustra::BodyShape::Auditorium,
      acustra::BodyMaterial::Spruce },
    { "Parlor / Fender style", "Parlor", acustra::BodyShape::Parlor,
      acustra::BodyMaterial::Spruce },
    { "Bellido 1978", "Bellido 1978", acustra::BodyShape::Auditorium,
      acustra::BodyMaterial::Mahogany, acustra::GuitarModel::Bellido1978 }
}};

// Spruce soundboard, rosewood sides, ebony fingerboard, bone inlays and
// aged brass. The Rack artwork uses the same material and colour direction.
const juce::Colour ebony { 0xff151311 };
const juce::Colour pianoBlack { 0xff0d0c0a };
const juce::Colour pianoHighlight { 0xff454038 };
const juce::Colour darkWood { 0xff34251c };
const juce::Colour rosewood { 0xff30231c };
const juce::Colour soundboard { 0xffe4c99a };
const juce::Colour ivory { 0xfff3e8cf };
const juce::Colour mutedText { 0xffcaba9d };
const juce::Colour brass { 0xffc5a56b };
const juce::Colour panel { 0xff241c16 };
const juce::Colour panelEdge { 0xff756044 };

juce::Font displayFont (float height, int style = juce::Font::plain)
{
    return juce::Font (juce::FontOptions (
        juce::Font::getDefaultSansSerifFontName(), height, style));
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto area = bounds.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillRoundedRectangle (area.translated (0.0f, 2.0f), 8.0f);
    juce::ColourGradient fill { panel.brighter (0.09f).withAlpha (0.97f),
                                area.getX(), area.getY(),
                                panel.darker (0.14f).withAlpha (0.97f),
                                area.getX(), area.getBottom(),
                                false };
    g.setGradientFill (fill);
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (ivory.withAlpha (0.58f));
    g.drawRoundedRectangle (area.reduced (0.7f), 8.0f, 1.2f);
    g.setColour (ebony);
    g.drawRoundedRectangle (area.reduced (2.4f), 6.0f, 1.0f);
}
} // namespace

AcustraLookAndFeel::AcustraLookAndFeel()
{
    setColour (juce::Slider::rotarySliderFillColourId, brass);
    setColour (juce::Slider::rotarySliderOutlineColourId, panelEdge);
    setColour (juce::Slider::textBoxTextColourId, ivory);
    setColour (juce::Slider::textBoxBackgroundColourId, ebony.withAlpha (0.72f));
    setColour (juce::Slider::textBoxOutlineColourId, panelEdge);
    setColour (juce::Label::textColourId, ivory);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, panel.withAlpha (1.0f));
    setColour (juce::TooltipWindow::textColourId, ivory);
    setColour (juce::TooltipWindow::outlineColourId, panelEdge);
    setColour (juce::TextButton::buttonColourId, ebony);
    setColour (juce::TextButton::buttonOnColourId, ivory);
    setColour (juce::TextButton::textColourOffId, ivory);
    setColour (juce::TextButton::textColourOnId, darkWood);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, ivory);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, rosewood);
    setColour (juce::PopupMenu::highlightedTextColourId, ivory);
}

void AcustraLookAndFeel::drawRotarySlider (
    juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
    float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height)
                            .toFloat().reduced (5.0f);
    const auto radius = 0.5f * juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle
                     + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto trackRadius = radius - 3.5f;
    const auto trackWidth = juce::jmax (2.5f, radius * 0.055f);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (panelEdge.withAlpha (0.75f));
    g.strokePath (track, juce::PathStrokeType (
        trackWidth, juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));

    if (angle > rotaryStartAngle + 0.001f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, trackRadius, trackRadius,
                                0.0f, rotaryStartAngle, angle, true);
        g.setColour (slider.findColour (
            juce::Slider::rotarySliderFillColourId));
        g.strokePath (valueArc, juce::PathStrokeType (
            trackWidth, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
    }

    const auto bodyRadius = radius - trackWidth * 1.9f;
    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillEllipse (centre.x - bodyRadius + 1.8f,
                   centre.y - bodyRadius + 3.0f,
                   bodyRadius * 2.0f, bodyRadius * 2.0f);

    juce::ColourGradient body { pianoHighlight,
                                centre.x - bodyRadius * 0.64f,
                                centre.y - bodyRadius * 0.72f,
                                pianoBlack, centre.x + bodyRadius * 0.52f,
                                centre.y + bodyRadius * 0.80f, false };
    body.addColour (0.34, juce::Colour { 0xff2b2721 });
    body.addColour (0.72, ebony);
    g.setGradientFill (body);
    g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius,
                   bodyRadius * 2.0f, bodyRadius * 2.0f);
    g.setColour (ivory.withAlpha (0.36f));
    g.drawEllipse (centre.x - bodyRadius, centre.y - bodyRadius,
                   bodyRadius * 2.0f, bodyRadius * 2.0f, 1.25f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    juce::Path highlight;
    highlight.addCentredArc (centre.x, centre.y,
                             bodyRadius * 0.78f, bodyRadius * 0.78f,
                             0.0f, 3.65f, 5.78f, true);
    g.strokePath (highlight, juce::PathStrokeType (
        1.3f, juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));

    const auto pointerLength = bodyRadius * 0.78f;
    const auto pointerWidth = juce::jmax (1.7f, bodyRadius * 0.065f);
    juce::Path pointer;
    pointer.addRoundedRectangle (-pointerWidth * 0.5f, -pointerLength,
                                 pointerWidth, pointerLength * 0.62f,
                                 pointerWidth * 0.5f);
    pointer.applyTransform (
        juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.setColour (ivory);
    g.fillPath (pointer);

    if (slider.hasKeyboardFocus (true))
    {
        g.setColour (ivory.withAlpha (0.90f));
        g.drawEllipse (bounds.reduced (1.0f), 2.0f);
    }
}

void AcustraLookAndFeel::drawButtonBackground (
    juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
    bool isHighlighted, bool isDown)
{
    auto fill = colour;
    if (isDown)
        fill = fill.brighter (0.28f);
    else if (isHighlighted)
        fill = fill.brighter (0.13f);

    const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (button.getToggleState()
                     ? brass : panelEdge);
    g.drawRoundedRectangle (bounds, 4.0f,
                            button.getToggleState() ? 1.5f : 1.0f);
    if (button.hasKeyboardFocus (true))
    {
        g.setColour (brass.brighter (0.20f));
        g.drawRoundedRectangle (bounds.reduced (2.0f), 3.0f, 2.0f);
    }
}

juce::Font AcustraLookAndFeel::getTextButtonFont (juce::TextButton&,
                                                   int buttonHeight)
{
    return displayFont (juce::jlimit (
        13.0f, 18.0f, static_cast<float> (buttonHeight) * 0.40f),
        juce::Font::bold);
}

class AcustraAudioProcessorEditor::ChoiceButtonGroup final
    : public juce::Component
{
public:
    ChoiceButtonGroup (juce::RangedAudioParameter& parameter,
                       const juce::StringArray& items,
                       const juce::String& groupName,
                       const juce::String& description, int columns = 2)
        : ChoiceButtonGroup (items, groupName, description, columns)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (
            parameter, [this] (float value) { setSelectedIndex (juce::roundToInt (value)); });
        onChoice = [this] (int item)
        {
            attachment->setValueAsCompleteGesture (static_cast<float> (item));
        };
        attachment->sendInitialUpdate();
    }

    ChoiceButtonGroup (const juce::StringArray& items,
                       const juce::String& groupName,
                       const juce::String& description, int columns = 2)
        : columnCount (columns)
    {
        jassert (! items.isEmpty() && items.size() < 8);
        buttons.reserve (static_cast<std::size_t> (items.size()));

        for (int item = 0; item < items.size(); ++item)
        {
            auto button = std::make_unique<juce::TextButton> (items[item]);
            button->setName (groupName + ": " + items[item]);
            button->setTitle (groupName + ": " + items[item]);
            button->setDescription (description);
            button->setTooltip (description);
            button->setWantsKeyboardFocus (true);
            button->setClickingTogglesState (true);
            button->setRadioGroupId (1, juce::dontSendNotification);
            button->onClick = [this, item]
            {
                if (onChoice != nullptr)
                    onChoice (item);
            };
            addAndMakeVisible (*button);
            buttons.push_back (std::move (button));
        }

    }

    std::function<void (int)> onChoice;

    void setSelectedIndex (int selected)
    {
        for (std::size_t index = 0; index < buttons.size(); ++index)
            buttons[index]->setToggleState (static_cast<int> (index) == selected,
                                            juce::dontSendNotification);
    }

    void setChoiceDescription (std::size_t index, const juce::String& description)
    {
        buttons[index]->setDescription (description);
        buttons[index]->setTooltip (description);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const auto scale = static_cast<float> (getParentComponent()->getWidth()) / designWidth;
        const auto gap = juce::roundToInt (4.0f * scale);
        const auto columns = columnCount;
        const auto rows = (static_cast<int> (buttons.size()) + columns - 1)
                        / columns;
        const auto rowHeight = (area.getHeight() - gap * (rows - 1)) / rows;

        for (int row = 0, item = 0; row < rows; ++row)
        {
            auto rowArea = area.removeFromTop (rowHeight);
            if (row + 1 < rows)
                area.removeFromTop (gap);

            const auto remaining = static_cast<int> (buttons.size()) - item;
            const auto count = juce::jmin (columns, remaining);
            const auto buttonWidth = (rowArea.getWidth() - gap * (columns - 1)) / columns;
            for (int column = 0; column < count; ++column)
            {
                buttons[static_cast<std::size_t> (item++)]->setBounds (
                    rowArea.removeFromLeft (buttonWidth));
                rowArea.removeFromLeft (gap);
            }
        }
    }

private:
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int columnCount;
};

AcustraAudioProcessorEditor::AcustraAudioProcessorEditor (
    AcustraAudioProcessor& processorToUse)
    : AudioProcessorEditor (&processorToUse), audioProcessor (processorToUse)
{
    setLookAndFeel (&lookAndFeel);
    cedarBackground = juce::ImageCache::getFromMemory (
        AcustraUIAssets::cedarbackground_png,
        AcustraUIAssets::cedarbackground_pngSize);
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setTitle ("Acustra acoustic guitar controls");
    setDescription (
        "Physically modelled acoustic guitar controls and an on-screen MIDI keyboard");

    titleLabel.setText ("ACUSTRA", juce::dontSendNotification);
    titleLabel.setFont (displayFont (40.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, ivory);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setInterceptsMouseClicks (false, false);
    titleLabel.setAccessible (false);
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText ("PHYSICALLY MODELLED ACOUSTIC GUITAR",
                           juce::dontSendNotification);
    subtitleLabel.setFont (displayFont (15.5f, juce::Font::bold));
    subtitleLabel.setColour (juce::Label::textColourId, brass);
    subtitleLabel.setJustificationType (juce::Justification::centredLeft);
    subtitleLabel.setInterceptsMouseClicks (false, false);
    subtitleLabel.setAccessible (false);
    addAndMakeVisible (subtitleLabel);

    statusLabel.setName ("Engine status");
    statusLabel.setTitle ("Engine status");
    statusLabel.setDescription (
        "Audio sample rate and sounding strings");
    statusLabel.setFont (displayFont (15.5f));
    statusLabel.setColour (juce::Label::textColourId, mutedText);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);

    panicButton.setName ("Panic");
    panicButton.setTitle ("Panic");
    panicButton.setDescription ("Immediately stop every sounding string");
    panicButton.setTooltip ("Stop all strings and clear the sustain pedal");
    panicButton.setWantsKeyboardFocus (true);
    panicButton.onClick = [this] { audioProcessor.requestPanic(); };
    addAndMakeVisible (panicButton);

    const juce::String gatherDescription {
        "Gather keys pressed within 30 ms of each other into one chord, "
        "fretted and strummed as a guitarist would. Every note then sounds "
        "30 ms later; hosts compensate that on playback." };
    gatherChordsButton.setName ("Gather chords");
    gatherChordsButton.setTitle ("Gather chords");
    gatherChordsButton.setDescription (gatherDescription);
    gatherChordsButton.setTooltip (gatherDescription);
    gatherChordsButton.setWantsKeyboardFocus (true);
    gatherChordsButton.setClickingTogglesState (true);
    addAndMakeVisible (gatherChordsButton);
    gatherChordsAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ButtonAttachment> (
            audioProcessor.parameters, acustra::parameters::gatherChords,
            gatherChordsButton);

    configureSetupChoice (
        0, "GUITAR", nullptr,
        "Set the body model, shape and wood together. "
        "Style presets use the original body; named years select measured bodies. "
        "Adjust any construction control below to make your own guitar.");
    for (std::size_t index = 0; index < constructionPresets.size(); ++index)
        setupControls[0]->setChoiceDescription (
            index, juce::String (constructionPresets[index].name) + ". "
                + "Set the body model, shape and wood together; keep tuning, playing and capture.");
    setupControls[0]->onChoice = [this] (int index)
    {
        const auto& preset = constructionPresets[static_cast<std::size_t> (index)];
        const auto setChoice = [this] (const char* id, auto value)
        {
            if (auto* parameter = audioProcessor.parameters.getParameter (id))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (
                    parameter->convertTo0to1 (static_cast<float> (value)));
                parameter->endChangeGesture();
            }
        };
        setChoice (acustra::parameters::shape, preset.shape);
        setChoice (acustra::parameters::bodyMaterial, preset.wood);
        setChoice (acustra::parameters::guitarModel, preset.model);
        timerCallback();
    };
    configureSetupChoice (
        1, "PICKING", acustra::parameters::picking,
        "Finger: balanced attack. Pick: crisp and bridgeward. "
        "Thumb: rounder and neckward. Touch adjusts the contact within each "
        "technique. MIDI: CC2 bridge-hand damping.", 3);
    configureSetupChoice (
        2, "CAPTURE", acustra::parameters::captureMode,
        "Stereo body microphones, one mono body microphone, or "
        "an under-saddle piezo through its onboard preamp. Mono mic and piezo "
        "send the same signal to both channels and ignore Stereo Width.", 3);
    configureSetupChoice (
        3, "MODEL", acustra::parameters::guitarModel,
        "Choose the measured guitar body. Original is the "
        "steel-string voice; Bellido is a measured 1978 classical strung with "
        "steel, with three microphones. Shape and wood controls remain "
        "adjustable construction changes.");

    configureChoice (
        0, "BODY SHAPE", acustra::parameters::shape,
        "Compact to large body: changes resonance and sustain in both microphone and piezo captures");
    configureChoice (
        1, "BODY MATERIAL", acustra::parameters::bodyMaterial,
        "Bounded high-frequency wood direction, not captured wood identification");
    configureChoice (
        2, "TUNING", acustra::parameters::tuning,
        "Open-string tuning used by the six-string allocator");

    configureSlider (
        0, "STRING AGE", acustra::parameters::stringAge,
        "Fresh strings at zero; increasingly worn and dark strings at 100 percent");
    configureSlider (
        1, "PLUCK POSITION", acustra::parameters::pluckPosition,
        "Pluck near the bridge at zero or toward the neck at 100 percent");
    configureSlider (
        2, "TOUCH", acustra::parameters::touch,
        "Softer, darker contact at zero; harder, brighter contact at 100 percent");
    configureSlider (
        3, "BODY", acustra::parameters::bodyAmount,
        "Strength of the measurement-derived body radiation");
    configureSlider (
        4, "STEREO", acustra::parameters::stereoWidth,
        "Width between body microphones. Mono mic and piezo ignore this control.");
    configureSlider (
        5, "PIEZO", acustra::parameters::piezoMix,
        "Mix the under-saddle piezo into the main output beneath the "
        "microphones. With Capture on Piezo the piezo is already the whole "
        "output, so this has no effect.");
    configureSlider (
        6, "OUTPUT", acustra::parameters::output,
        "Final output level in decibels", true);

    keyboard.setName ("Acustra MIDI keyboard");
    keyboard.setTitle ("MIDI keyboard");
    keyboard.setDescription (
        "Play Acustra from the computer mouse, keyboard, or an attached MIDI controller");
    keyboard.setWantsKeyboardFocus (true);
    keyboard.setAvailableRange (keyboardFirstNote, keyboardLastNote);
    keyboard.setLowestVisibleKey (keyboardFirstNote);
    keyboard.setMidiChannel (1);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, ivory);
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, ebony);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, panelEdge);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                        brass.withAlpha (0.25f));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId,
                        brass.withAlpha (0.55f));
    addAndMakeVisible (keyboard);

    setResizable (true, true);
    setResizeLimits (minimumWidth, minimumHeight, maximumWidth, maximumHeight);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio (
            static_cast<double> (designWidth) / designHeight);
    setSize (designWidth, designHeight);
    startTimerHz (12);
    timerCallback();
}

AcustraAudioProcessorEditor::~AcustraAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AcustraAudioProcessorEditor::configureSetupChoice (
    std::size_t index, const juce::String& name, const char* parameterId,
    const juce::String& description, int columns)
{
    auto& label = setupLabels[index];
    label.setText (name, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, mutedText);
    label.setInterceptsMouseClicks (false, false);
    label.setAccessible (false);
    addAndMakeVisible (label);

    auto& control = setupControls[index];
    if (parameterId == nullptr)
    {
        juce::StringArray captions;
        for (const auto& preset : constructionPresets)
            captions.add (preset.caption);
        control = std::make_unique<ChoiceButtonGroup> (captions, name, description, columns);
    }
    else if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (
                 audioProcessor.parameters.getParameter (parameterId)))
    {
        control = std::make_unique<ChoiceButtonGroup> (
            *parameter, parameter->choices, name, description, columns);
    }
    jassert (control != nullptr);
    addAndMakeVisible (*control);
}

void AcustraAudioProcessorEditor::updateConstructionControls()
{
    const auto state = audioProcessor.snapshotEngineParameters();
    int selected = -1;
    for (std::size_t index = 0; index < constructionPresets.size(); ++index)
    {
        const auto& preset = constructionPresets[index];
        if (state.guitarModel == preset.model
            && state.shape == preset.shape && state.bodyMaterial == preset.wood)
        {
            selected = static_cast<int> (index);
            break;
        }
    }
    setupControls[0]->setSelectedIndex (selected);
    setupLabels[0].setText (selected < 0 ? "CONSTRUCTION / CUSTOM" : "CONSTRUCTION",
                            juce::dontSendNotification);
}

void AcustraAudioProcessorEditor::configureChoice (
    std::size_t index, const juce::String& name, const char* parameterId,
    const juce::String& description)
{
    auto& label = choiceLabels[index];
    label.setText (name, juce::dontSendNotification);
    label.setFont (displayFont (17.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, mutedText);
    label.setJustificationType (juce::Justification::centredLeft);
    label.setInterceptsMouseClicks (false, false);
    label.setAccessible (false);
    addAndMakeVisible (label);

    auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (
        audioProcessor.parameters.getParameter (parameterId));
    jassert (parameter != nullptr);
    if (parameter == nullptr)
        return;

    auto& control = choiceControls[index];
    control = std::make_unique<ChoiceButtonGroup> (
        *parameter, parameter->choices, name, description);
    addAndMakeVisible (*control);
}

void AcustraAudioProcessorEditor::configureSlider (
    std::size_t index, const juce::String& name, const char* parameterId,
    const juce::String& description, bool decibels)
{
    auto& label = sliderLabels[index];
    label.setText (name, juce::dontSendNotification);
    label.setFont (displayFont (17.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, mutedText);
    label.setJustificationType (juce::Justification::centred);
    label.setInterceptsMouseClicks (false, false);
    label.setAccessible (false);
    addAndMakeVisible (label);

    auto& control = sliderControls[index];
    control.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    control.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 26);
    control.setTextValueSuffix (decibels ? " dB" : " %");
    control.setName (name);
    control.setTitle (name);
    control.setDescription (description);
    control.setTooltip (description);
    control.setWantsKeyboardFocus (true);
    addAndMakeVisible (control);

    sliderAttachments[index] = std::make_unique<SliderAttachment> (
        audioProcessor.parameters, parameterId, control);
}

void AcustraAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    const auto scale = static_cast<float> (getWidth()) / designWidth;
    g.fillAll (soundboard);
    if (cedarBackground.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (cedarBackground, full,
                     juce::RectanglePlacement::fillDestination);
        // A pale satin finish keeps the grain quiet behind the control plates.
        g.setColour (soundboard.withAlpha (0.72f));
        g.fillRect (full);
    }

    juce::ColourGradient headerShade {
        rosewood, 0.0f, 0.0f, darkWood.brighter (0.08f),
        full.getWidth(), 86.0f * scale, false };
    g.setGradientFill (headerShade);
    g.fillRect (0.0f, 0.0f, full.getWidth(), 86.0f * scale);
    g.setColour (ivory.withAlpha (0.6f));
    g.drawLine (22.0f * scale, 82.0f * scale,
                full.getWidth() - 22.0f * scale, 82.0f * scale, scale);

    drawPanel (g, guitarPanelBounds);
    drawPanel (g, playerPanelBounds);
    drawPanel (g, capturePanelBounds);

    const auto heading = [&] (juce::Rectangle<int> bounds, const char* title)
    {
        auto area = bounds.reduced (juce::roundToInt (18.0f * scale),
                                    juce::roundToInt (12.0f * scale));
        const auto headingFont = displayFont (19.0f * scale, juce::Font::bold);
        const auto titleWidth = juce::GlyphArrangement::getStringWidth (headingFont, title);
        g.setFont (headingFont);
        g.setColour (ivory);
        g.drawText (title, area.removeFromTop (juce::roundToInt (24.0f * scale)),
                    juce::Justification::centredLeft, false);
        g.setColour (brass.withAlpha (0.65f));
        g.drawLine (static_cast<float> (area.getX()), static_cast<float> (area.getY()),
                    area.getX() + titleWidth, static_cast<float> (area.getY()), scale);
    };
    heading (guitarPanelBounds, "GUITAR");
    heading (playerPanelBounds, "PLAYER");
    heading (capturePanelBounds, "CAPTURE & OUTPUT");
}

void AcustraAudioProcessorEditor::resized()
{
    // Lay out one instrument at the documented aspect ratio. Scaling the
    // complete grid keeps captions, gestures and keyboard aligned at every
    // supported host size, rather than reflowing individual controls.
    const auto scale = static_cast<float> (getWidth()) / designWidth;
    const auto box = [scale] (int x, int y, int w, int h)
    {
        return juce::Rectangle<int> {
            juce::roundToInt (x * scale), juce::roundToInt (y * scale),
            juce::roundToInt (w * scale), juce::roundToInt (h * scale) };
    };

    titleLabel.setFont (displayFont (36.0f * scale, juce::Font::bold));
    subtitleLabel.setFont (displayFont (12.5f * scale, juce::Font::bold));
    statusLabel.setFont (displayFont (14.0f * scale));
    titleLabel.setBounds (box (26, 12, 380, 42));
    subtitleLabel.setBounds (box (28, 54, 460, 20));
    statusLabel.setBounds (box (636, 27, 252, 32));
    gatherChordsButton.setBounds (box (908, 25, 94, 36));
    panicButton.setBounds (box (1014, 25, 80, 36));

    guitarPanelBounds = box (22, 96, 348, 580);
    playerPanelBounds = box (382, 96, 716, 282);
    capturePanelBounds = box (382, 390, 716, 286);

    for (auto& label : setupLabels)
        label.setFont (displayFont (13.5f * scale, juce::Font::bold));
    for (auto& label : choiceLabels)
        label.setFont (displayFont (13.5f * scale, juce::Font::bold));
    for (auto& label : sliderLabels)
        label.setFont (displayFont (14.0f * scale, juce::Font::bold));

    setupLabels[0].setBounds (box (40, 143, 312, 20));
    // All left-hand switches share one exact cell size, even after rounding
    // at the host's minimum or maximum scale. Odd rows retain the same grid.
    const auto leftSwitches = [&] (int y, int rows)
    {
        auto bounds = box (40, y, 312, 1);
        bounds.setHeight (rows * juce::roundToInt (32.0f * scale)
                          + (rows - 1) * juce::roundToInt (4.0f * scale));
        return bounds;
    };
    setupControls[0]->setBounds (leftSwitches (166, 2));
    setupLabels[3].setBounds (box (40, 246, 312, 20));
    setupControls[3]->setBounds (leftSwitches (272, 1));

    const std::array<int, 3> choiceY { 316, 424, 532 };
    const std::array<int, 3> choiceRows { 2, 2, 3 };
    for (std::size_t index = 0; index < choiceControls.size(); ++index)
    {
        choiceLabels[index].setBounds (box (40, choiceY[index], 312, 22));
        if (choiceControls[index] != nullptr)
            choiceControls[index]->setBounds (leftSwitches (choiceY[index] + 28, choiceRows[index]));
    }

    // The technique belongs with the contact controls; the capture belongs
    // with the signal controls. They remain the same parameter attachments.
    setupLabels[1].setBounds (box (512, 111, 82, 22));
    setupControls[1]->setBounds (box (594, 104, 486, 38));
    setupLabels[2].setBounds (box (622, 405, 82, 22));
    setupControls[2]->setBounds (box (704, 398, 376, 38));

    for (std::size_t index = 0; index < 3; ++index)
    {
        const auto x = 424 + static_cast<int> (index) * 218;
        sliderLabels[index].setBounds (box (x, 158, 196, 24));
        sliderControls[index].setBounds (box (x, 187, 196, 182));
    }
    for (std::size_t index = 3; index < sliderControls.size(); ++index)
    {
        const auto x = 400 + static_cast<int> (index - 3) * 170;
        sliderLabels[index].setBounds (box (x, 446, 170, 24));
        sliderControls[index].setBounds (box (x + 9, 475, 152, 182));
    }

    keyboardPanelBounds = box (22, 688, 1076, 112);
    keyboardPanelBounds.setBottom (getHeight());
    keyboard.setKeyWidth (static_cast<float> (keyboardPanelBounds.getWidth())
                          / static_cast<float> (keyboardWhiteKeyCount));
    keyboard.setBounds (keyboardPanelBounds);
}

void AcustraAudioProcessorEditor::timerCallback()
{
    updateConstructionControls();
    juce::String next;
    if (! audioProcessor.isEngineReady())
    {
        next = "WAITING FOR AUDIO";
    }
    else
    {
        const auto rate = audioProcessor.getCurrentSampleRateForDisplay();
        const auto voices = audioProcessor.getActiveVoiceCount();
        next = juce::String (rate / 1000.0, 1) + " kHz  |  "
             + juce::String (voices) + (voices == 1 ? " STRING" : " STRINGS");
    }

    if (statusLabel.getText() != next)
    {
        statusLabel.setText (next, juce::dontSendNotification);
        if (auto* handler = statusLabel.getAccessibilityHandler())
            handler->notifyAccessibilityEvent (
                juce::AccessibilityEvent::titleChanged);
    }
}
