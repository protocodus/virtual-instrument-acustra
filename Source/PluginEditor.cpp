#include "PluginEditor.h"
#include "AcustraUIAssets.h"

#include <cmath>
#include <vector>

namespace
{
constexpr int designWidth = 1120;
constexpr int designHeight = 980;
constexpr int minimumWidth = 896;
constexpr int minimumHeight = 784;
constexpr int maximumWidth = 1456;
constexpr int maximumHeight = 1274;
constexpr int keyboardFirstNote = 38; // Drop-D low string
constexpr int keyboardLastNote = 84;  // twentieth fret of the high E string
constexpr int keyboardWhiteKeyCount = 28;

struct ConstructionPreset
{
    const char* name;
    const char* caption;
    acustra::BodyShape shape;
    acustra::BodyMaterial wood;
};

// Construction directions, not measured replicas of manufacturer models.
// Every preset plays the one measured body: g21's radiation and its own
// bridge on the radiation's poles, at a steel-string guitar's measured
// mobility, so one body loads the string and radiates it (README, How it
// works).
constexpr std::array<ConstructionPreset, 3> constructionPresets {{
    { "Dreadnought / Martin style", "Dreadnought", acustra::BodyShape::Dreadnought,
      acustra::BodyMaterial::Spruce },
    { "Auditorium / Taylor style", "Auditorium", acustra::BodyShape::Auditorium,
      acustra::BodyMaterial::Spruce },
    { "Parlor / Fender style", "Parlor", acustra::BodyShape::Parlor,
      acustra::BodyMaterial::Spruce }
}};

// Satin walnut, honey spruce, bone inlay and aged brass: a luthier's
// material palette, with every interactive legend kept independent of grain.
const juce::Colour ebony { 0xff181512 };
const juce::Colour pianoBlack { 0xff10100e };
const juce::Colour pianoHighlight { 0xff4f4a40 };
const juce::Colour darkWood { 0xff38271d };
const juce::Colour rosewood { 0xff231a16 };
const juce::Colour soundboard { 0xffe1c292 };
const juce::Colour ivory { 0xfff3e6cb };
const juce::Colour mutedText { 0xffd5c2a1 };
const juce::Colour brass { 0xffbda16d };
const juce::Colour panel { 0xff211b16 };
const juce::Colour panelEdge { 0xff766247 };
const juce::Colour engraved { 0xff493525 };

juce::Font displayFont (float height, int style = juce::Font::plain)
{
    return juce::Font (juce::FontOptions (
        juce::Font::getDefaultSansSerifFontName(), height, style));
}

juce::Font brandFont (float height)
{
    return juce::Font (juce::FontOptions (
        juce::Font::getDefaultSerifFontName(), height, juce::Font::plain)
            .withKerningFactor (0.12f));
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds,
                const juce::Image& wood, bool light, float scale)
{
    const auto area = bounds.toFloat();
    const auto radius = 7.0f * scale;
    g.setColour (juce::Colours::black.withAlpha (0.58f));
    g.fillRoundedRectangle (area.translated (0.0f, 3.0f * scale), radius);
    {
        juce::Graphics::ScopedSaveState saved (g);
        juce::Path inset;
        inset.addRoundedRectangle (area, radius);
        g.reduceClipRegion (inset);
        g.setColour (light ? soundboard : panel);
        g.fillRect (area);
        if (wood.isValid())
        {
            g.setOpacity (light ? 0.60f : 0.42f);
            g.drawImage (wood, area, juce::RectanglePlacement::fillDestination);
            g.setOpacity (1.0f);
        }
        g.setColour ((light ? soundboard : panel).withAlpha (light ? 0.37f : 0.65f));
        g.fillRect (area);
        juce::ColourGradient finish {
            ivory.withAlpha (light ? 0.31f : 0.08f),
            area.getX(), area.getY(), juce::Colours::black.withAlpha (light ? 0.07f : 0.24f),
            area.getX(), area.getBottom(), false };
        g.setGradientFill (finish);
        g.fillRect (area);
    }
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawRoundedRectangle (area, radius, 1.7f * scale);
    g.setColour ((light ? ivory : brass).withAlpha (0.64f));
    g.drawRoundedRectangle (area.reduced (1.6f * scale), 5.5f * scale, 0.8f * scale);
    g.setColour ((light ? engraved : juce::Colours::black).withAlpha (0.36f));
    g.drawRoundedRectangle (area.reduced (3.2f * scale), 4.2f * scale, 0.6f * scale);
}

void drawRosette (juce::Graphics& g, juce::Point<float> centre, float scale)
{
    for (const auto radius : { 22.0f, 19.5f, 15.0f })
    {
        g.setColour (brass.withAlpha (radius == 19.5f ? 0.32f : 0.8f));
        g.drawEllipse (centre.x - radius * scale, centre.y - radius * scale,
                       radius * 2.0f * scale, radius * 2.0f * scale, scale);
    }
    g.setColour (ivory.withAlpha (0.75f));
    for (int string = 0; string < 6; ++string)
    {
        const auto x = centre.x + (static_cast<float> (string) - 2.5f) * 3.2f * scale;
        g.drawLine (x, centre.y - 12.0f * scale, x, centre.y + 12.0f * scale,
                    (0.50f + 0.08f * static_cast<float> (string)) * scale);
    }
}
} // namespace

AcustraLookAndFeel::AcustraLookAndFeel()
{
    setColour (juce::Slider::rotarySliderFillColourId, brass);
    setColour (juce::Slider::rotarySliderOutlineColourId, panelEdge);
    setColour (juce::Slider::textBoxTextColourId, ivory);
    setColour (juce::Slider::textBoxBackgroundColourId, ebony);
    setColour (juce::Slider::textBoxOutlineColourId, brass.withAlpha (0.75f));
    setColour (juce::Label::textColourId, ivory);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, panel.withAlpha (1.0f));
    setColour (juce::TooltipWindow::textColourId, ivory);
    setColour (juce::TooltipWindow::outlineColourId, panelEdge);
    setColour (juce::TextButton::buttonColourId, ebony);
    setColour (juce::TextButton::buttonOnColourId, juce::Colour { 0xffe9d5aa });
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
    const auto trackRadius = radius - 4.0f;
    const auto trackWidth = juce::jmax (1.8f, radius * 0.033f);
    const auto trackColour = slider.findColour (juce::Slider::rotarySliderOutlineColourId);

    // Quiet engraved graduations sit outside a brass position arc.
    for (int tick = 0; tick <= 10; ++tick)
    {
        const auto tickAngle = rotaryStartAngle
            + static_cast<float> (tick) * (rotaryEndAngle - rotaryStartAngle) / 10.0f;
        const auto inner = radius - (tick % 5 == 0 ? 2.2f : 0.5f);
        const auto outer = radius + 2.0f;
        g.setColour (trackColour.withAlpha (tick % 5 == 0 ? 0.72f : 0.40f));
        g.drawLine (centre.x + std::sin (tickAngle) * inner,
                    centre.y - std::cos (tickAngle) * inner,
                    centre.x + std::sin (tickAngle) * outer,
                    centre.y - std::cos (tickAngle) * outer, 0.8f);
    }
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (trackColour.withAlpha (0.32f));
    g.strokePath (track, juce::PathStrokeType (trackWidth));
    if (angle > rotaryStartAngle + 0.001f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, trackRadius, trackRadius,
                                0.0f, rotaryStartAngle, angle, true);
        g.setColour (engraved.withAlpha (0.86f));
        g.strokePath (valueArc, juce::PathStrokeType (
            trackWidth, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
    }

    const auto bodyRadius = radius - 11.0f;
    const juce::Rectangle<float> bodyBounds {
        centre.x - bodyRadius, centre.y - bodyRadius,
        bodyRadius * 2.0f, bodyRadius * 2.0f };
    for (int shadow = 5; shadow > 0; --shadow)
    {
        g.setColour (juce::Colours::black.withAlpha (0.028f * static_cast<float> (6 - shadow)));
        g.fillEllipse (bodyBounds.expanded (static_cast<float> (shadow))
                          .translated (1.0f, 3.0f));
    }
    juce::ColourGradient collet { ivory, bodyBounds.getX(), bodyBounds.getY(),
                                  juce::Colour { 0xff665037 }, bodyBounds.getRight(),
                                  bodyBounds.getBottom(), false };
    collet.addColour (0.25, brass);
    collet.addColour (0.5, juce::Colour { 0xff8e754f });
    collet.addColour (0.72, juce::Colour { 0xffd7bc85 });
    g.setGradientFill (collet);
    g.fillEllipse (bodyBounds.expanded (2.0f));
    g.setColour (pianoBlack);
    g.drawEllipse (bodyBounds.expanded (2.0f), 0.9f);

    juce::ColourGradient body { pianoHighlight,
                                centre.x - bodyRadius * 0.55f,
                                centre.y - bodyRadius * 0.82f,
                                pianoBlack, centre.x + bodyRadius * 0.40f,
                                centre.y + bodyRadius * 0.88f, false };
    body.addColour (0.34, juce::Colour { 0xff302d27 });
    body.addColour (0.70, ebony);
    g.setGradientFill (body);
    g.fillEllipse (bodyBounds);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawEllipse (bodyBounds.reduced (2.4f), 1.0f);
    g.setColour (ivory.withAlpha (0.14f));
    g.drawEllipse (bodyBounds.reduced (3.4f), 0.75f);
    g.setColour (ivory.withAlpha (0.12f));
    juce::Path highlight;
    highlight.addCentredArc (centre.x, centre.y,
                             bodyRadius * 0.91f, bodyRadius * 0.91f,
                             0.0f, 3.65f, 5.78f, true);
    g.strokePath (highlight, juce::PathStrokeType (1.2f));

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
        fill = fill.darker (0.10f);
    else if (isHighlighted)
        fill = fill.brighter (0.14f);

    const auto bounds = button.getLocalBounds().toFloat().reduced (1.4f);
    const auto selected = button.getToggleState();
    g.setColour (juce::Colours::black.withAlpha (0.48f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 1.2f), 3.8f);
    juce::ColourGradient surface { fill.brighter (selected ? 0.13f : 0.15f),
                                   0.0f, bounds.getY(), fill.darker (0.10f),
                                   0.0f, bounds.getBottom(), false };
    g.setGradientFill (surface);
    g.fillRoundedRectangle (bounds, 3.8f);
    g.setColour (selected ? brass : panelEdge.withAlpha (0.80f));
    g.drawRoundedRectangle (bounds, 3.8f, selected ? 1.25f : 0.8f);
    g.setColour (ivory.withAlpha (selected ? 0.55f : 0.12f));
    g.drawLine (bounds.getX() + 5.0f, bounds.getY() + 1.6f,
                bounds.getRight() - 5.0f, bounds.getY() + 1.6f, 0.7f);
    if (button.hasKeyboardFocus (true))
    {
        g.setColour (selected ? engraved : ivory);
        g.drawRoundedRectangle (bounds.reduced (2.0f), 2.5f, 1.5f);
    }
}

juce::Font AcustraLookAndFeel::getTextButtonFont (juce::TextButton&,
                                                   int buttonHeight)
{
    return displayFont (juce::jlimit (
        13.0f, 18.0f, static_cast<float> (buttonHeight) * 0.40f),
        juce::Font::bold);
}

class AcustraAudioProcessorEditor::StringActivityDisplay final
    : public juce::Component,
      public juce::SettableTooltipClient,
      private juce::Timer
{
public:
    explicit StringActivityDisplay (AcustraAudioProcessor& processor, const juce::Image& wood)
        : audioProcessor (processor), woodTexture (wood)
    {
        setName ("String activity");
        setTitle ("String activity");
        setOpaque (true);
        setTooltip (
            "Six strings, high E at the top. Filled markers show held frets; "
            "outlined markers show released notes. Blue-green open markers "
            "show sympathetic vibration from another string. H marks a natural "
            "harmonic. Line movement and brightness show schematic vibration "
            "activity, not the audio waveform or the string's actual motion.");
        audioProcessor.setStringDisplayEnabled (true);
        refresh();
        startTimerHz (24);
    }

    ~StringActivityDisplay() override
    {
        stopTimer();
        audioProcessor.setStringDisplayEnabled (false);
    }

    void paint (juce::Graphics& graphics) override
    {
        // The text and fretboard are cached at the display's pixel density.
        // Only six short waves and their markers are drawn on each tick.
        updateBackdrop (graphics.getInternalContext().getPhysicalPixelScaleFactor());
        graphics.drawImage (backdrop, getLocalBounds().toFloat());
        juce::Graphics::ScopedSaveState saved (graphics);
        graphics.addTransform (juce::AffineTransform::scale (
            static_cast<float> (getWidth()) / panelWidth,
            static_cast<float> (getHeight()) / panelHeight));
        auto& g = graphics;
        for (std::size_t string = 0; string < activity.size(); ++string)
        {
            const auto& state = activity[string];
            const auto row = 5 - static_cast<int> (string);
            const auto y = firstY + rowSpacing * static_cast<float> (row);
            const float amount = vibration[string];
            const bool vibrating = amount > 0.0f;
            const bool sympathetic = ! state.played && vibrating;
            const bool assigned = state.played &&
                (state.keyDown || state.pedalHeld || vibrating);
            const bool markerVisible = assigned || sympathetic;
            const int fret = assigned ? juce::jlimit (0, 20, state.fret) : 0;
            const auto colour = sympathetic ? sympatheticColour
                              : state.keyDown ? brass : ivory;
            const float weight = 0.65f + 0.11f * static_cast<float> (5 - string);

            if (vibrating)
            {
                const auto start = fretX (fret);
                const auto end = fretX (20) + 9.0f;
                const float amplitude = 2.5f * amount;
                float previousX = start;
                float previousY = y;
                g.setColour (colour.withAlpha (0.2f + 0.8f * amount));
                // A small fixed drawing, independent of audio frequency.
                // Its envelope comes from the existing string observer.
                for (int segment = 1; segment <= 24; ++segment)
                {
                    const float position = static_cast<float> (segment) / 24.0f;
                    const float x = start + position * (end - start);
                    const float motion = amplitude
                        * std::sin (juce::MathConstants<float>::pi * position)
                        * std::sin (juce::MathConstants<float>::twoPi
                            * (3.0f * position - phase
                               + 0.13f * static_cast<float> (string)));
                    g.drawLine (previousX, previousY, x, y + motion,
                                weight + 0.3f);
                    previousX = x;
                    previousY = y + motion;
                }
            }

            if (markerVisible)
            {
                const juce::Rectangle<float> marker {
                    fretX (fret) - 7.3f, y - 7.3f, 14.6f, 14.6f };
                g.setColour (panel);
                g.fillEllipse (marker);
                g.setColour (colour.withAlpha (
                    state.keyDown ? 1.0f : 0.45f + 0.55f * amount));
                if (state.keyDown)
                    g.fillEllipse (marker);
                else
                    g.drawEllipse (marker, 1.4f);
                g.setColour (state.keyDown ? ebony : colour);
                g.setFont (displayFont (9.5f, juce::Font::bold));
                g.drawText (assigned && state.harmonic > 1 ? "H"
                                                           : juce::String (fret),
                            marker, juce::Justification::centred, false);
            }
        }
    }

    void resized() override { backdropNeedsUpdate = true; }
    void lookAndFeelChanged() override { backdropNeedsUpdate = true; }

private:
    using Activity = acustra::AcustraEngine::StringActivity;
    static constexpr float panelWidth = 1076.0f;
    static constexpr float panelHeight = 168.0f;
    static constexpr float firstY = 57.0f;
    static constexpr float rowSpacing = 18.0f;
    const juce::Colour sympatheticColour { 0xff8dbbb1 };

    void updateBackdrop (float pixelScale)
    {
        const auto width = juce::jmax (1, juce::roundToInt (static_cast<float> (getWidth()) * pixelScale));
        const auto height = juce::jmax (1, juce::roundToInt (static_cast<float> (getHeight()) * pixelScale));
        if (! backdropNeedsUpdate && backdrop.getWidth() == width
            && backdrop.getHeight() == height)
            return;
        if (backdrop.getWidth() != width || backdrop.getHeight() != height)
            backdrop = juce::Image (juce::Image::RGB, width, height, false);
        juce::Graphics g (backdrop);
        g.fillAll (panel);
        if (woodTexture.isValid())
        {
            g.setOpacity (0.17f);
            g.drawImage (woodTexture, backdrop.getBounds().toFloat(),
                         juce::RectanglePlacement::fillDestination);
            g.setOpacity (1.0f);
        }
        g.addTransform (juce::AffineTransform::scale (
            static_cast<float> (width) / panelWidth,
            static_cast<float> (height) / panelHeight));
        drawBackdrop (g);
        backdropNeedsUpdate = false;
    }

    void drawBackdrop (juce::Graphics& g)
    {
        const auto frame = juce::Rectangle<float> (0.8f, 0.8f,
                                                  panelWidth - 1.6f,
                                                  panelHeight - 1.6f);
        g.setColour (brass.withAlpha (0.72f));
        g.drawRoundedRectangle (frame, 7.0f, 1.0f);
        g.setColour (ebony);
        g.drawRoundedRectangle (frame.reduced (2.0f), 5.0f, 1.0f);

        g.setColour (ivory);
        g.setFont (displayFont (16.0f, juce::Font::bold));
        g.drawText ("STRINGS", 18, 9, 116, 22,
                    juce::Justification::centredLeft, false);
        g.setFont (displayFont (11.5f));
        g.setColour (brass);
        g.fillEllipse (628.0f, 15.0f, 7.0f, 7.0f);
        g.drawText ("HELD", 641, 8, 61, 22,
                    juce::Justification::centredLeft, false);
        g.setColour (ivory.withAlpha (0.8f));
        g.drawEllipse (711.0f, 15.0f, 7.0f, 7.0f, 1.1f);
        g.drawText ("RELEASED", 724, 8, 85, 22,
                    juce::Justification::centredLeft, false);
        g.setColour (sympatheticColour);
        g.drawEllipse (825.0f, 15.0f, 7.0f, 7.0f, 1.1f);
        g.drawText ("SYMPATHETIC", 838, 8, 126, 22,
                    juce::Justification::centredLeft, false);

        constexpr float lastY = firstY + 5.0f * rowSpacing;
        g.setFont (displayFont (10.5f));
        for (int fret = 0; fret <= acustra::AcustraEngine::fretCount; ++fret)
        {
            const float x = fretX (fret);
            g.setColour (mutedText.withAlpha (fret == 0 ? 0.9f : 0.68f));
            g.drawText (fret == 0 ? "0" : juce::String (fret),
                        juce::Rectangle<float> (x - 12.0f, 32.0f, 24.0f, 15.0f),
                        juce::Justification::centred, false);
            g.setColour (fret == 0 ? ivory.withAlpha (0.32f)
                                  : panelEdge.withAlpha (0.36f));
            g.drawLine (x, firstY - 7.0f, x, lastY + 7.0f,
                        fret == 0 ? 2.0f : 0.65f);
        }
        for (int fret : { 3, 5, 7, 9, 12, 15, 17, 19 })
        {
            g.setColour (brass.withAlpha (0.25f));
            g.fillEllipse (fretX (fret) - 1.6f, 157.0f, 3.2f, 3.2f);
            if (fret == 12)
                g.fillEllipse (fretX (fret) - 1.6f, 161.5f, 3.2f, 3.2f);
        }

        for (std::size_t string = 0; string < activity.size(); ++string)
        {
            const auto& state = activity[string];
            const auto row = 5 - static_cast<int> (string);
            const auto y = firstY + rowSpacing * static_cast<float> (row);
            const bool vibrating = vibration[string] > 0.0f;
            const bool sympathetic = ! state.played && vibrating;
            const bool assigned = state.played &&
                (state.keyDown || state.pedalHeld || vibrating);
            const bool markerVisible = assigned || sympathetic;
            const auto colour = sympathetic ? sympatheticColour
                              : state.keyDown ? brass : ivory;
            const float weight = 0.65f + 0.11f * static_cast<float> (5 - string);

            g.setColour (mutedText.withAlpha (0.7f));
            g.setFont (displayFont (10.0f));
            g.drawText (juce::String (6 - static_cast<int> (string)),
                        16, juce::roundToInt (y - 8.0f), 16, 16,
                        juce::Justification::centredLeft, false);
            g.setColour (markerVisible ? ivory : mutedText);
            g.setFont (displayFont (12.5f, juce::Font::bold));
            g.drawText (openNames[string],
                        35, juce::roundToInt (y - 8.0f), 48, 16,
                        juce::Justification::centredLeft, false);
            g.setColour (panelEdge.withAlpha (0.75f));
            g.drawLine (fretX (0), y, fretX (20), y, weight);

            g.setFont (displayFont (11.0f));
            g.setColour (markerVisible ? colour.withAlpha (0.9f)
                                      : mutedText.withAlpha (0.5f));
            g.drawText (rowText[string],
                        juce::Rectangle<float> (898.0f, y - 8.0f, 162.0f, 16.0f),
                        juce::Justification::centredRight, false);
        }
    }

    static float fretX (int fret) noexcept
    {
        return 105.0f + static_cast<float> (fret) * 38.5f;
    }

    static float visibleLevel (float level) noexcept
    {
        // The same activity floor as the engine's sympathetic observer.
        // Display a wide dynamic range without depending on Output gain.
        return level > 2.0e-7f && std::isfinite (level)
            ? juce::jlimit (0.0f, 1.0f,
                           std::log10 (level / 2.0e-7f) / 5.0f) : 0.0f;
    }

    static bool sameState (const Activity& a, const Activity& b) noexcept
    {
        return a.openMidi == b.openMidi && a.midiNote == b.midiNote
            && a.fret == b.fret && a.harmonic == b.harmonic
            && a.keyDown == b.keyDown && a.played == b.played
            && a.pedalHeld == b.pedalHeld;
    }

    void timerCallback() override { refresh(); }

    void refresh()
    {
        auto next = audioProcessor.getStringActivityForDisplay();
        const auto standard = acustra::AcustraEngine::openNotes (
            acustra::Tuning::Standard);
        bool stateChanged = ! initialised;
        bool animating = false;
        for (std::size_t string = 0; string < next.size(); ++string)
        {
            if (next[string].openMidi <= 0)
                next[string].openMidi = standard[string];
            const auto amount = visibleLevel (next[string].level);
            stateChanged = stateChanged || ! sameState (next[string], activity[string])
                || ((amount > 0.0f) != (vibration[string] > 0.0f));
            vibration[string] = amount;
            animating = animating || amount > 0.0f;
        }
        activity = next;
        initialised = true;
        if (stateChanged)
            updateDescriptions();
        if (animating)
        {
            phase += 1.25f / 24.0f;
            if (phase >= 1.0f)
                phase -= 1.0f;
        }
        if (stateChanged || animating)
            repaint();
    }

    void updateDescriptions()
    {
        backdropNeedsUpdate = true;
        juce::String summary { "Six strings, high to low. " };
        for (int index = 5; index >= 0; --index)
        {
            const auto string = static_cast<std::size_t> (index);
            const auto& state = activity[string];
            const bool vibrating = vibration[string] > 0.0f;
            const bool assigned = state.played &&
                (state.keyDown || state.pedalHeld || vibrating);
            openNames[string] = juce::MidiMessage::getMidiNoteName (
                state.openMidi, true, true, 4);
            const auto noteName = juce::MidiMessage::getMidiNoteName (
                assigned ? state.midiNote : state.openMidi, true, true, 4);
            juce::String stateName;
            if (state.keyDown)
                stateName = "held";
            else if (state.pedalHeld)
                stateName = "pedal held";
            else if (assigned)
                stateName = "released, ringing";
            else if (vibrating)
                stateName = "sympathetic vibration";
            else
                stateName = "at rest";

            const auto harmonic = assigned && state.harmonic > 1
                ? "H" + juce::String (state.harmonic) + "  " : juce::String {};
            rowText[string] = ! assigned && ! vibrating ? "REST"
                : ! assigned ? "OPEN / SYMPATHY"
                : noteName + " / " + harmonic
                    + (state.keyDown ? "HELD" : state.pedalHeld ? "PEDAL" : "RINGING");
            summary += "String " + juce::String (6 - index) + ", "
                + openNames[string] + " open tuning, "
                + (assigned && state.harmonic > 1
                    ? "natural harmonic " + juce::String (state.harmonic)
                    : "fret " + juce::String (assigned ? state.fret : 0))
                + ", " + noteName + ", " + stateName + ". ";
        }
        setDescription (summary);
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent (
                juce::AccessibilityEvent::valueChanged);
    }

    AcustraAudioProcessor& audioProcessor;
    std::array<Activity, acustra::AcustraEngine::stringCount> activity {};
    std::array<float, acustra::AcustraEngine::stringCount> vibration {};
    std::array<juce::String, acustra::AcustraEngine::stringCount> openNames;
    std::array<juce::String, acustra::AcustraEngine::stringCount> rowText;
    juce::Image backdrop;
    juce::Image woodTexture;
    float phase { 0.0f };
    bool initialised { false };
    bool backdropNeedsUpdate { true };
};

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
    walnutBackground = juce::ImageCache::getFromMemory (
        AcustraUIAssets::walnutsatin_png, AcustraUIAssets::walnutsatin_pngSize);
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setTitle ("Acustra acoustic guitar controls");
    setDescription (
        "Physically modelled acoustic guitar controls, live string activity "
        "and an on-screen MIDI keyboard");

    titleLabel.setText ("ACUSTRA", juce::dontSendNotification);
    titleLabel.setFont (brandFont (40.0f));
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
        "Set the body shape and wood together. "
        "Adjust any construction control below to make your own guitar.");
    for (std::size_t index = 0; index < constructionPresets.size(); ++index)
        setupControls[0]->setChoiceDescription (
            index, juce::String (constructionPresets[index].name) + ". "
                + "Set the body shape and wood together; keep tuning, playing and capture.");
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

    stringActivityDisplay = std::make_unique<StringActivityDisplay> (audioProcessor, walnutBackground);
    addAndMakeVisible (*stringActivityDisplay);

    keyboard.setName ("Acustra MIDI keyboard");
    keyboard.setTitle ("MIDI keyboard");
    keyboard.setDescription (
        "Play Acustra from the computer mouse, keyboard, or an attached MIDI controller");
    keyboard.setWantsKeyboardFocus (true);
    keyboard.setAvailableRange (keyboardFirstNote, keyboardLastNote);
    keyboard.setLowestVisibleKey (keyboardFirstNote);
    keyboard.setMidiChannel (1);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, ivory);
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, pianoBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha (0.50f));
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
    stringActivityDisplay.reset();
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
        if (state.shape == preset.shape && state.bodyMaterial == preset.wood)
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
    label.setColour (juce::Label::textColourId, engraved);
    label.setJustificationType (juce::Justification::centred);
    label.setInterceptsMouseClicks (false, false);
    label.setAccessible (false);
    addAndMakeVisible (label);

    auto& control = sliderControls[index];
    // Slider text boxes are created before the control joins this editor.
    // Set their palette directly so they never retain the global JUCE theme.
    control.setColour (juce::Slider::textBoxTextColourId, ivory);
    control.setColour (juce::Slider::textBoxBackgroundColourId, ebony);
    control.setColour (juce::Slider::textBoxOutlineColourId, brass);
    control.setColour (juce::Slider::rotarySliderOutlineColourId, engraved);
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
    g.fillAll (rosewood);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (walnutBackground.isValid())
        g.drawImage (walnutBackground, full, juce::RectanglePlacement::fillDestination);
    g.setColour (rosewood.withAlpha (0.24f));
    g.fillRect (full);
    juce::ColourGradient lacquer {
        ivory.withAlpha (0.09f), 0.0f, 0.0f,
        juce::Colours::black.withAlpha (0.36f), full.getWidth(), full.getHeight(), false };
    g.setGradientFill (lacquer);
    g.fillRect (full);

    // Double purfling follows the perimeter like the binding of a guitar.
    g.setColour (brass.withAlpha (0.66f));
    g.drawRect (full.reduced (6.0f * scale), 0.8f * scale);
    g.setColour (ivory.withAlpha (0.22f));
    g.drawRect (full.reduced (9.0f * scale), 0.65f * scale);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRect (0.0f, 0.0f, full.getWidth(), 86.0f * scale);
    g.setColour (brass.withAlpha (0.62f));
    g.drawLine (22.0f * scale, 82.0f * scale,
                full.getWidth() - 22.0f * scale, 82.0f * scale, 0.8f * scale);
    drawRosette (g, { 48.0f * scale, 42.0f * scale }, scale);

    drawPanel (g, guitarPanelBounds, walnutBackground, false, scale);
    drawPanel (g, playerPanelBounds, cedarBackground, true, scale);
    drawPanel (g, capturePanelBounds, cedarBackground, true, scale);

    const auto heading = [&] (juce::Rectangle<int> bounds, const char* title, bool light)
    {
        auto area = bounds.reduced (juce::roundToInt (18.0f * scale),
                                    juce::roundToInt (12.0f * scale));
        const auto font = displayFont (17.5f * scale, juce::Font::bold);
        const auto titleWidth = juce::GlyphArrangement::getStringWidth (font, title);
        g.setFont (font);
        g.setColour (light ? engraved : ivory);
        g.drawText (title, area.removeFromTop (juce::roundToInt (24.0f * scale)),
                    juce::Justification::centredLeft, false);
        g.setColour ((light ? engraved : brass).withAlpha (0.4f));
        g.drawLine (static_cast<float> (area.getX()), static_cast<float> (area.getY()),
                    static_cast<float> (area.getX()) + titleWidth, static_cast<float> (area.getY()), 0.6f * scale);
    };
    heading (guitarPanelBounds, "GUITAR", false);
    heading (playerPanelBounds, "PLAYER", true);
    heading (capturePanelBounds, "CAPTURE & OUTPUT", true);
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
            juce::roundToInt (static_cast<float> (x) * scale),
            juce::roundToInt (static_cast<float> (y) * scale),
            juce::roundToInt (static_cast<float> (w) * scale),
            juce::roundToInt (static_cast<float> (h) * scale) };
    };

    titleLabel.setFont (brandFont (37.0f * scale));
    subtitleLabel.setFont (displayFont (10.5f * scale, juce::Font::bold));
    statusLabel.setFont (displayFont (14.0f * scale));
    titleLabel.setBounds (box (80, 10, 440, 44));
    subtitleLabel.setBounds (box (84, 52, 460, 20));
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
    // at the host's minimum or maximum scale, as tall as the technique and
    // capture switches. Odd rows retain the same grid.
    const auto leftSwitches = [&] (int y, int rows)
    {
        auto bounds = box (40, y, 312, 1);
        bounds.setHeight (rows * juce::roundToInt (38.0f * scale)
                          + (rows - 1) * juce::roundToInt (4.0f * scale));
        return bounds;
    };
    setupControls[0]->setBounds (leftSwitches (166, 2));

    const std::array<int, 3> choiceY { 262, 386, 510 };
    const std::array<int, 3> choiceRows { 2, 2, 3 };
    for (std::size_t index = 0; index < choiceControls.size(); ++index)
    {
        choiceLabels[index].setBounds (box (40, choiceY[index], 312, 22));
        if (choiceControls[index] != nullptr)
            choiceControls[index]->setBounds (leftSwitches (choiceY[index] + 28, choiceRows[index]));
    }

    // The technique belongs with the contact controls; the capture belongs
    // with the signal controls. They remain the same parameter attachments.
    setupLabels[1].setColour (juce::Label::textColourId, engraved);
    setupLabels[2].setColour (juce::Label::textColourId, engraved);
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

    stringActivityDisplay->setBounds (box (22, 688, 1076, 168));
    keyboardPanelBounds = box (22, 868, 1076, 112);
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
