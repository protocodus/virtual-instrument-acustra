#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "PerformanceBattery.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;
constexpr int editorWidth = 1120;
constexpr int editorHeight = 980;
constexpr int editorMinimumWidth = 896;
constexpr int editorMinimumHeight = 784;
constexpr int editorMaximumWidth = 1456;
constexpr int editorMaximumHeight = 1274;

int failureCount = 0;

void expect (bool condition, const std::string& message)
{
    if (! condition)
    {
        ++failureCount;
        std::cerr << "FAIL: " << message << '\n';
    }
}

float valueOf (const AcustraAudioProcessor& processor, const char* id)
{
    const auto* value = processor.parameters.getRawParameterValue (id);
    expect (value != nullptr, std::string { "missing parameter " } + id);
    return value != nullptr ? value->load (std::memory_order_relaxed) : 0.0f;
}

void setValue (AcustraAudioProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.parameters.getParameter (id);
    expect (parameter != nullptr,
            std::string { "cannot set missing parameter " } + id);
    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

bool finite (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (! std::isfinite (buffer.getSample (channel, sample)))
                return false;
    return true;
}

float peak (const juce::AudioBuffer<float>& buffer, int begin = 0,
            int end = blockSize)
{
    float result = 0.0f;
    begin = std::clamp (begin, 0, buffer.getNumSamples());
    end = std::clamp (end, begin, buffer.getNumSamples());
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = begin; sample < end; ++sample)
            result = std::max (result,
                               std::abs (buffer.getSample (channel, sample)));
    return result;
}

void addRpn (juce::MidiBuffer& midi, int channel, int parameter,
             int valueMsb, int valueLsb = -1, int sample = 0)
{
    midi.addEvent (juce::MidiMessage::controllerEvent (
        channel, 101, (parameter >> 7) & 0x7f), sample);
    midi.addEvent (juce::MidiMessage::controllerEvent (
        channel, 100, parameter & 0x7f), sample);
    midi.addEvent (juce::MidiMessage::controllerEvent (
        channel, 6, std::clamp (valueMsb, 0, 127)), sample);
    if (valueLsb >= 0)
        midi.addEvent (juce::MidiMessage::controllerEvent (
            channel, 38, std::clamp (valueLsb, 0, 127)), sample);
}

void addLowerZone (juce::MidiBuffer& midi, int memberCount, int sample = 0)
{
    addRpn (midi, 1, juce::MPEMessages::zoneLayoutMessagesRpnNumber,
            memberCount, -1, sample);
}

bool sameEngineParameters (const acustra::EngineParameters& a,
                           const acustra::EngineParameters& b)
{
    return a.shape == b.shape && a.bodyMaterial == b.bodyMaterial
        && a.capture == b.capture && a.tuning == b.tuning
        && a.picking == b.picking && a.stringAge == b.stringAge
        && a.pluckPosition == b.pluckPosition && a.touch == b.touch
        && a.bodyAmount == b.bodyAmount && a.stereoWidth == b.stereoWidth
        && a.outputGain == b.outputGain && a.piezoMix == b.piezoMix
        && a.releaseNoise == b.releaseNoise && a.room == b.room
        && a.piezoPolarityInverted == b.piezoPolarityInverted;
}

// The user area of the display the editor opens on (empty without one),
// chosen as the editor chooses it.
juce::Rectangle<int> editorDisplayArea()
{
    const auto* display = juce::Desktop::getInstance().getDisplays()
        .getDisplayForPoint (juce::Desktop::getMousePosition().toFloat());
    return display != nullptr ? display->userBounds.toNearestInt() : juce::Rectangle<int> {};
}

juce::Label* findLabel (juce::Component& root,
                        const std::function<bool (const juce::Label&)>& matches)
{
    for (auto* child : root.getChildren())
    {
        if (auto* label = dynamic_cast<juce::Label*> (child); label != nullptr && matches (*label))
            return label;
        if (auto* found = findLabel (*child, matches))
            return found;
    }
    return nullptr;
}

std::vector<float> flattened (const juce::AudioBuffer<float>& audio)
{
    std::vector<float> result;
    result.reserve (static_cast<std::size_t> (
        audio.getNumChannels() * audio.getNumSamples()));
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        result.insert (result.end(), audio.getReadPointer (channel),
                       audio.getReadPointer (channel) + audio.getNumSamples());
    return result;
}

void testParameterContract()
{
    auto processorOwner = std::make_unique<AcustraAudioProcessor>();
    auto& processor = *processorOwner;
    namespace ids = acustra::parameters;

    constexpr std::array<const char*, ids::parameterCount> expectedIds {
        ids::shape, ids::bodyMaterial, ids::tuning,
        ids::stringAge, ids::pluckPosition, ids::touch, ids::bodyAmount,
        ids::stereoWidth, ids::output, ids::capture, ids::picking,
        ids::upperMic, ids::piezoLoading, ids::captureMode, ids::guitarModel,
        ids::gatherChords, ids::piezoMix, ids::releaseNoise, ids::room,
        ids::sustain, ids::piezoPolarityInverted
    };
    static_assert (ids::parameterCount == 21,
                   "String Material and Bridge Model are gone; Guitar Model is a "
                   "legacy slot; Piezo Mix, Release Noise, Room and Sustain are "
                   "appended");
    // Piezo Mix defaults to 20%, a bit of the piezo under the microphones;
    // Release Noise and Room to the requested 10%; Sustain off.
    constexpr std::array<float, ids::parameterCount> expectedDefaults {
        2.0f, 0.0f, 0.0f, 15.0f, 28.0f, 58.0f, 82.0f, 62.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 20.0f, 10.0f, 10.0f, 0.0f, 0.0f
    };

    const auto& hostParameters = processor.getParameters();
    expect (hostParameters.size() == ids::parameterCount,
            "the public parameter count changed");
    for (int index = 0;
         index < std::min (hostParameters.size(), ids::parameterCount); ++index)
    {
        const auto* ranged =
            dynamic_cast<const juce::RangedAudioParameter*> (hostParameters[index]);
        expect (ranged != nullptr, "a public parameter is not ranged");
        if (ranged == nullptr)
            continue;
        expect (ranged->paramID == expectedIds[static_cast<std::size_t> (index)],
                "the stable parameter order or ID changed at slot "
                    + std::to_string (index));
        const float factory = ranged->convertFrom0to1 (ranged->getDefaultValue());
        expect (std::abs (factory
                          - expectedDefaults[static_cast<std::size_t> (index)])
                    < 0.011f,
                "the factory default changed for " + ranged->paramID.toStdString());
    }

    const auto* shape = dynamic_cast<const juce::AudioParameterChoice*> (
        processor.parameters.getParameter (ids::shape));
    const auto* wood = dynamic_cast<const juce::AudioParameterChoice*> (
        processor.parameters.getParameter (ids::bodyMaterial));
    const auto* tuning = dynamic_cast<const juce::AudioParameterChoice*> (
        processor.parameters.getParameter (ids::tuning));
    const auto* capture = dynamic_cast<const juce::AudioParameterChoice*> (
        processor.parameters.getParameter (ids::capture));
    const auto* picking = dynamic_cast<const juce::AudioParameterChoice*> (
        processor.parameters.getParameter (ids::picking));
    const auto* upperMic = dynamic_cast<const juce::AudioParameterBool*> (
        processor.parameters.getParameter (ids::upperMic));
    const auto* piezoLoading = dynamic_cast<const juce::AudioParameterBool*> (
        processor.parameters.getParameter (ids::piezoLoading));
    expect (shape != nullptr && shape->choices.size() == 4,
            "Shape does not expose four bodies");
    expect (wood != nullptr && wood->choices
                == juce::StringArray { "Spruce", "Mahogany", "Maple" },
            "Body Material does not expose Spruce, Mahogany and Maple");
    expect (processor.parameters.getParameter ("stringMaterial") == nullptr
                && processor.parameters.getParameter ("bridgeModel") == nullptr,
            "the retired String Material or Bridge Model parameter is still public");
    expect (tuning != nullptr && tuning->choices.size() == 5,
            "Tuning does not expose the five supported tunings");

    expect (capture != nullptr && capture->choices == juce::StringArray {
                "Stereo mics", "Treble mic", "Bass mic", "Saddle piezo",
                "Magnetic (steel)" },
            "the five legacy capture choices changed their automation contract");
    expect (upperMic != nullptr && upperMic->getVersionHint() == 4
                && upperMic->getParameterIndex() == 11,
            "Upper mic is not an appended boolean with AU version hint 4");
    expect (piezoLoading != nullptr && piezoLoading->getVersionHint() == 5
                && piezoLoading->getParameterIndex() == 12,
            "Piezo loading changed the legacy parameter order or AU version hint");
    expect (picking != nullptr && picking->choices
                == juce::StringArray { "Finger", "Pick", "Thumb" },
            "Picking does not expose finger, pick and thumb");
    // The retired Guitar Model keeps its identity, as the other legacy slots
    // do: its ID, AU version hint, index and two-choice range, so a session or
    // an index-addressing host finds every later control where it was.
    const auto* model = dynamic_cast<const juce::AudioParameterChoice*> (
        processor.parameters.getParameter (ids::guitarModel));
    expect (model != nullptr && model->getVersionHint() == 7
                && model->getParameterIndex() == 14 && ! model->isAutomatable()
                && model->choices == juce::StringArray { "Original", "Classical 78" },
            "the retired Guitar Model lost its legacy slot identity");
    if (auto* modelParameter = processor.parameters.getParameter (ids::guitarModel))
    {
        // What a host's normalised snapshot holds: every packaged build
        // stores Original as 0 and Classical 78 as 1.
        const auto* choice = dynamic_cast<const juce::AudioParameterChoice*> (modelParameter);
        modelParameter->setValueNotifyingHost (1.0f);
        const bool secondModel = choice != nullptr && choice->getIndex() == 1;
        modelParameter->setValueNotifyingHost (0.0f);
        expect (secondModel && choice->getIndex() == 0,
                "the legacy Guitar Model's normalised 0 and 1 changed their storage");
    }
    const auto* gather = dynamic_cast<const juce::AudioParameterBool*> (
        processor.parameters.getParameter (ids::gatherChords));
    expect (gather != nullptr && gather->getVersionHint() == 8
                && gather->getParameterIndex() == 15 && ! gather->isAutomatable(),
            "Gather Chords must append a non-automatable switch with AU version hint 8");
    const auto* piezoMix = dynamic_cast<const juce::AudioParameterFloat*> (
        processor.parameters.getParameter (ids::piezoMix));
    expect (piezoMix != nullptr && piezoMix->getVersionHint() == 9
                && piezoMix->getParameterIndex() == 16 && piezoMix->isAutomatable()
                && piezoMix->getName (32) == "Piezo Mix"
                && piezoMix->range.start == 0.0f && piezoMix->range.end == 100.0f,
            "Piezo Mix must append a 0-100% parameter with AU version hint 9");
    expect (std::abs (processor.snapshotEngineParameters().piezoMix - 0.2f) < 1.0e-6f,
            "a new session does not start with a bit of the piezo under the microphones");
    const auto* releaseNoise = dynamic_cast<const juce::AudioParameterFloat*> (
        processor.parameters.getParameter (ids::releaseNoise));
    expect (releaseNoise != nullptr && releaseNoise->getVersionHint() == 10
                && releaseNoise->getParameterIndex() == 17 && releaseNoise->isAutomatable()
                && releaseNoise->getName (32) == "Release Noise"
                && releaseNoise->range.start == 0.0f && releaseNoise->range.end == 100.0f,
            "Release Noise must append a 0-100% parameter with AU version hint 10");
    expect (std::abs (processor.snapshotEngineParameters().releaseNoise - 0.1f) < 0.002f,
            "a new session's key-ups are not at the requested 10%");
    const auto* room = dynamic_cast<const juce::AudioParameterFloat*> (
        processor.parameters.getParameter (ids::room));
    expect (room != nullptr && room->getVersionHint() == 11
                && room->getParameterIndex() == 18 && room->isAutomatable()
                && room->getName (32) == "Room"
                && room->range.start == 0.0f && room->range.end == 100.0f,
            "Room must append a 0-100% parameter with AU version hint 11");
    expect (std::abs (processor.snapshotEngineParameters().room - 0.1f) < 0.002f,
            "a new session's room is not at the requested 10% default");
    const auto* sustain = dynamic_cast<const juce::AudioParameterBool*> (
        processor.parameters.getParameter (ids::sustain));
    expect (sustain != nullptr && sustain->getVersionHint() == 12
                && sustain->getParameterIndex() == 19 && sustain->isAutomatable()
                && sustain->getName (32) == "Sustain" && ! sustain->get(),
            "Sustain must append an automatable switch, off, with AU version hint 12");

    setValue (processor, ids::shape, 3.0f);
    setValue (processor, ids::bodyMaterial, 2.0f);
    setValue (processor, ids::tuning, 2.0f);
    setValue (processor, ids::stringAge, 73.0f);
    setValue (processor, ids::pluckPosition, 41.0f);
    setValue (processor, ids::touch, 19.0f);
    setValue (processor, ids::bodyAmount, 66.0f);
    setValue (processor, ids::stereoWidth, 35.0f);
    setValue (processor, ids::output, -3.0f);
    setValue (processor, ids::captureMode, 2.0f);
    setValue (processor, ids::picking, 2.0f);
    const auto* polarity = dynamic_cast<const juce::AudioParameterBool*> (
        processor.parameters.getParameter (ids::piezoPolarityInverted));
    expect (polarity != nullptr && polarity->getVersionHint() == 13
                && polarity->getParameterIndex() == 20 && polarity->isAutomatable()
                && ! polarity->get(), "piezo polarity lost its appended host identity or normal default");
    setValue (processor, ids::piezoPolarityInverted, 1.0f);
    expect (processor.snapshotEngineParameters().piezoPolarityInverted,
            "host polarity parameter did not reach the engine");
    setValue (processor, ids::piezoPolarityInverted, 0.0f);
    setValue (processor, ids::piezoMix, 37.0f);
    setValue (processor, ids::room, 23.0f);
    const auto engine = processor.snapshotEngineParameters();
    expect (engine.shape == acustra::BodyShape::Jumbo
                && engine.bodyMaterial == acustra::BodyMaterial::Maple
                && engine.tuning == acustra::Tuning::Dadgad
                && engine.capture == acustra::CaptureType::Piezo
                && engine.picking == acustra::PickingTechnique::Thumb,
            "choice parameters did not reach the engine snapshot");
    expect (std::abs (engine.stringAge - 0.73f) < 0.002f
                && std::abs (engine.pluckPosition - 0.41f) < 0.002f
                && std::abs (engine.touch - 0.19f) < 0.002f
                && std::abs (engine.bodyAmount - 0.66f) < 0.002f
                && std::abs (engine.stereoWidth - 0.35f) < 0.002f
                && std::abs (engine.piezoMix - 0.37f) < 0.002f
                && std::abs (engine.room - 0.23f) < 0.002f,
            "continuous parameters did not reach the engine snapshot");
    expect (std::abs (engine.outputGain
                      - juce::Decibels::decibelsToGain (-3.0f)) < 0.001f,
            "Output was not converted from dB to linear gain");

    const auto* publicCapture = dynamic_cast<const juce::AudioParameterChoice*> (
        processor.parameters.getParameter (ids::captureMode));
    expect (publicCapture != nullptr && publicCapture->choices == juce::StringArray {
                "Stereo mic", "Mono mic", "Piezo" }
                && publicCapture->getVersionHint() == 6 && publicCapture->getParameterIndex() == 13,
            "the public capture must append exactly three supported observations");
    expect (! capture->isAutomatable() && ! upperMic->isAutomatable()
                && ! piezoLoading->isAutomatable(),
            "retired capture parameters still advertise new automation");
    // Nothing reads the legacy Guitar Model: either stored value plays the
    // same engine parameters.
    {
        setValue (processor, ids::guitarModel, 0.0f);
        const auto original = processor.snapshotEngineParameters();
        setValue (processor, ids::guitarModel, 1.0f);
        const auto retired = processor.snapshotEngineParameters();
        setValue (processor, ids::guitarModel, 0.0f);
        expect (sameEngineParameters (original, retired),
                "the legacy Guitar Model still reaches the engine");
    }
    constexpr std::array supported { acustra::CaptureType::StereoMic,
        acustra::CaptureType::MonoMic, acustra::CaptureType::Piezo };
    for (int mode = 0; mode < 3; ++mode)
    {
        setValue (processor, ids::captureMode, static_cast<float> (mode));
        for (int legacy = 0; legacy < 5; ++legacy)
        {
            setValue (processor, ids::capture, static_cast<float> (legacy));
            setValue (processor, ids::upperMic, 1.0f);
            setValue (processor, ids::piezoLoading, 1.0f);
            expect (processor.snapshotEngineParameters().capture == supported[static_cast<std::size_t> (mode)],
                    "a retired capture parameter overrode the public capture selection");
        }
    }
}

void testProcessorContractAndSampleAccurateMidi()
{
    auto processorOwner = std::make_unique<AcustraAudioProcessor>();
    auto& processor = *processorOwner;
    expect (processor.acceptsMidi() && ! processor.producesMidi()
                && ! processor.isMidiEffect() && processor.hasEditor()
                && processor.supportsMPE(),
            "processor MIDI/editor capabilities are wrong");
    expect (std::abs (processor.getTailLengthSeconds() - 30.0) < 1.0e-9,
            "the host tail declaration changed");
    expect (processor.getTotalNumInputChannels() == 0
                && processor.getTotalNumOutputChannels() == 2,
            "Acustra must remain a no-input stereo instrument");

    auto mono = processor.getBusesLayout();
    mono.outputBuses.set (0, juce::AudioChannelSet::mono());
    expect (! processor.isBusesLayoutSupported (mono),
            "a mono main output was accepted");

    processor.prepareToPlay (sampleRate, blockSize);
    expect (processor.isEngineReady()
                && std::abs (processor.getCurrentSampleRateForDisplay()
                             - sampleRate) < 0.1,
            "prepareToPlay did not publish a ready 48 kHz engine");

    constexpr int onset = 91;
    juce::AudioBuffer<float> audio { 2, blockSize };
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 40, 0.9f), onset);
    processor.processBlock (audio, midi);
    expect (finite (audio), "processBlock produced NaN or infinity");
    expect (peak (audio, 0, onset) == 0.0f,
            "a note sounded before its MIDI sample offset");
    float onsetPeak = peak (audio, onset, blockSize);
    juce::MidiBuffer empty;
    audio.clear();
    processor.processBlock (audio, empty);
    constexpr int remainingFiveMillisecondWindow = 240 - (blockSize - onset);
    onsetPeak = std::max (onsetPeak,
                          peak (audio, 0, remainingFiveMillisecondWindow));
    expect (onsetPeak > 0.001f,
            "a playable E2 note-on did not render audible audio");
    expect (processor.getActiveVoiceCount() == 1,
            "a playable note did not own one physical string");

    juce::MidiBuffer panic;
    panic.addEvent (juce::MidiMessage::controllerEvent (1, 120, 0), 0);
    audio.clear();
    processor.processBlock (audio, panic);
    expect (processor.getActiveVoiceCount() == 0 && peak (audio) == 0.0f,
            "MIDI All Sound Off did not stop and clear the model");

    juce::MidiBuffer tooLow;
    tooLow.addEvent (juce::MidiMessage::noteOn (1, 20, 1.0f), 0);
    audio.clear();
    processor.processBlock (audio, tooLow);
    expect (processor.getActiveVoiceCount() == 0 && peak (audio) == 0.0f,
            "a note outside the physical guitar range was accepted");

    processor.releaseResources();
    expect (! processor.isEngineReady()
                && processor.getCurrentSampleRateForDisplay() == 0.0,
            "releaseResources left the engine advertised as ready");
}

std::vector<float> renderChord (const std::array<int, 6>& notes,
                                int& activeVoiceCount)
{
    auto processorOwner = std::make_unique<AcustraAudioProcessor>();
    auto& processor = *processorOwner;
    processor.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> audio { 2, blockSize };
    juce::MidiBuffer midi;
    for (const int note : notes)
        midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 37);
    processor.processBlock (audio, midi);

    std::vector<float> result;
    result.reserve (static_cast<std::size_t> (2 * blockSize));
    for (int channel = 0; channel < 2; ++channel)
        result.insert (result.end(), audio.getReadPointer (channel),
                       audio.getReadPointer (channel) + blockSize);
    activeVoiceCount = processor.getActiveVoiceCount();
    processor.releaseResources();
    return result;
}

void testSameSampleChordOrderIsCanonical()
{
    int ascendingVoices = 0;
    int descendingVoices = 0;
    const auto ascending = renderChord ({ 45, 50, 55, 60, 64, 69 },
                                        ascendingVoices);
    const auto descending = renderChord ({ 69, 64, 60, 55, 50, 45 },
                                         descendingVoices);
    expect (ascending == descending,
            "same-sample chord sound depended on host insertion order");
    expect (ascendingVoices == 6 && descendingVoices == 6,
            "a playable six-string voicing lost a chord member");
    expect (std::any_of (ascending.begin(), ascending.end(), [] (float value)
            { return std::abs (value) > 0.001f; }),
            "the chord determinism check rendered silence");
}

double upperRegisterRise (const std::vector<float>& mono)
{
    // Five-millisecond heterodyne windows locate the rise around C6,
    // relative to first sound, to one millisecond. Use a clip-relative
    // level so the measured per-string dynamics do not set the threshold.
    std::size_t first = 0;
    while (first < mono.size() && std::abs (mono[first]) < 1.0e-5f)
        ++first;
    const double frequency = 440.0 * std::exp2 ((84.0 - 69.0) / 12.0);
    const auto window = static_cast<std::size_t> (0.005 * sampleRate);
    const auto hop = static_cast<std::size_t> (0.001 * sampleRate);
    const auto extent = static_cast<std::size_t> (0.08 * sampleRate);
    std::vector<double> level;
    for (std::size_t start = first; start + window <= mono.size()
                                    && start - first < extent; start += hop)
    {
        double real = 0.0;
        double imaginary = 0.0;
        for (std::size_t index = 0; index < window; ++index)
        {
            const double angle = 2.0 * juce::MathConstants<double>::pi
                * frequency * static_cast<double> (index) / sampleRate;
            real += mono[start + index] * std::cos (angle);
            imaginary += mono[start + index] * std::sin (angle);
        }
        level.push_back (std::hypot (real, imaginary));
    }
    expect (! level.empty(), "the strum direction probe rendered silence");
    if (level.empty())
        return 1.0;
    const double top = *std::max_element (level.begin(), level.end());
    for (std::size_t index = 0; index < level.size(); ++index)
        if (level[index] > 0.2 * top)
            return static_cast<double> (index) * 0.001;
    return 1.0;
}

void testSameSampleChordsAreStrummedAndAlternate()
{
    // Listen to upper-register energy in the rendered stereo output. C6 on
    // the top string separates down/up attacks; the old F#4 probe mistook
    // strong 349 Hz partials from the bass strings for its 370 Hz fundamental.
    // No engine scheduling state or private wrapper flags are inspected.
    auto processorOwner = std::make_unique<AcustraAudioProcessor>();
    auto& processor = *processorOwner;
    processor.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> audio { 2, blockSize };
    const auto upperRegisterDelay = [&] (double restSeconds)
    {
        juce::MidiBuffer chord;
        for (const int note : { 41, 46, 51, 56, 61, 84 })
            chord.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
        std::vector<float> mono;
        const int blocks = static_cast<int> (0.10 * sampleRate / blockSize);
        for (int block = 0; block < blocks; ++block)
        {
            juce::MidiBuffer empty;
            processor.processBlock (audio, block == 0 ? chord : empty);
            for (int sample = 0; sample < blockSize; ++sample)
                mono.push_back (0.5f * (audio.getSample (0, sample)
                                        + audio.getSample (1, sample)));
        }
        juce::MidiBuffer off;
        for (const int note : { 41, 46, 51, 56, 61, 84 })
            off.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        const int restBlocks = static_cast<int> (restSeconds * sampleRate / blockSize);
        for (int block = 0; block < restBlocks; ++block)
        {
            juce::MidiBuffer empty;
            processor.processBlock (audio, block == 0 ? off : empty);
        }
        return upperRegisterRise (mono);
    };
    const auto median = [] (std::vector<double> values)
    {
        std::sort (values.begin(), values.end());
        return values[values.size() / 2];
    };
    std::vector<double> downs, ups, downAgains;
    for (int stroke = 0; stroke < 8; ++stroke)
    {
        // Strums 0.3 s apart sit within the dotted eighth at the default
        // 120 BPM (375 ms) and alternate. The odd-length group ends on a
        // downstroke: the next stroke must restart down after the long rest,
        // overriding its pending upstroke.
        downs.push_back (upperRegisterDelay (0.2));
        ups.push_back (upperRegisterDelay (0.2));
        downAgains.push_back (upperRegisterDelay (2.5));
    }
    const double down = median (downs);
    const double up = median (ups);
    const double downAgain = median (downAgains);
    const double restarted = median ({ downs.begin() + 1, downs.end() });
    // Medians allow the existing measured stroke-speed and string-level
    // variation. The first downstroke can land exactly six 1 ms analysis
    // hops after first sound; the inclusive bound retains that audible sweep.
    // Off-tree wrapper mutations disabling alternation or the rest reset
    // must fail these audible checks; no golden audio is required.
    expect (downs.front() >= 0.006,
            "the first same-sample chord did not sweep low to high");
    expect (up < 0.5 * down,
            "the return strum did not sweep high to low");
    expect (downAgain > 0.006 && downAgain > 2.0 * up,
            "the third strum did not alternate back to low to high");
    expect (restarted > 0.006 && restarted > 2.0 * up,
            "a long rest did not restart with a low-to-high strum");
    std::cout << "Acustra strum sweep medians: down " << down * 1000.0
              << " ms, up " << up * 1000.0 << " ms, down again "
              << downAgain * 1000.0 << " ms, restart " << restarted * 1000.0
              << " ms\n";
}

void testRepeatedHeldChordsKeepTheirAudibleSweep()
{
    // Subtract a processor with identical preceding strokes but no new MIDI.
    // This removes the still-ringing chord from the onset measurement without
    // releasing any key or inspecting the engine's scheduling state.
    std::vector<double> downs, ups;
    for (int previousStrokes = 1; previousStrokes <= 6; ++previousStrokes)
    {
        auto repeatedOwner = std::make_unique<AcustraAudioProcessor>();
        auto& repeated = *repeatedOwner;
        auto continuationOwner = std::make_unique<AcustraAudioProcessor>();
        auto& continuation = *continuationOwner;
        repeated.prepareToPlay (sampleRate, blockSize);
        continuation.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> audio { 2, blockSize };
        juce::AudioBuffer<float> held { 2, blockSize };
        const auto chord = []
        {
            juce::MidiBuffer midi;
            for (int note : { 41, 46, 51, 56, 61, 84 })
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
            return midi;
        };
        for (int stroke = 0; stroke < previousStrokes; ++stroke)
            for (int block = 0; block < 60; ++block)
            {
                auto midi = block == 0 ? chord() : juce::MidiBuffer {};
                auto matched = midi;
                repeated.processBlock (audio, midi);
                continuation.processBlock (held, matched);
            }
        expect (flattened (audio) == flattened (held),
                "held-chord continuation did not match before the repeated stroke");
        expect (repeated.getActiveVoiceCount() == 6,
                "the repeated-chord probe lost a held string");
        std::vector<float> difference;
        for (int block = 0; block < 18; ++block)
        {
            auto midi = block == 0 ? chord() : juce::MidiBuffer {};
            juce::MidiBuffer empty;
            repeated.processBlock (audio, midi);
            continuation.processBlock (held, empty);
            for (int sample = 0; sample < blockSize; ++sample)
                difference.push_back (0.5f * (
                    audio.getSample (0, sample) - held.getSample (0, sample)
                    + audio.getSample (1, sample) - held.getSample (1, sample)));
        }
        (previousStrokes % 2 == 0 ? downs : ups)
            .push_back (upperRegisterRise (difference));
    }
    std::sort (downs.begin(), downs.end());
    std::sort (ups.begin(), ups.end());
    const double down = downs[downs.size() / 2];
    const double up = ups[ups.size() / 2];
    expect (down > 0.006 && down > 2.0 * up,
            "repeated held chords struck together instead of sweeping in alternate directions");
    std::cout << "Acustra held-chord sweep medians: down " << down * 1000.0
              << " ms, up " << up * 1000.0 << " ms\n";
}

// A bypassed instrument keeps playing its MIDI silently, so a key-up
// delivered while bypassed is not lost; the host's reset() is a Panic.
void testBypassAndHostResetKeepNotesCoherent()
{
    auto owner = std::make_unique<AcustraAudioProcessor>();
    auto& processor = *owner;
    processor.prepareToPlay (sampleRate, blockSize);
    processor.setStringDisplayEnabled (true);
    juce::AudioBuffer<float> audio { 2, blockSize };
    // The display publishes at its own rate, so settle a few blocks before
    // reading it, as the display test does.
    juce::MidiBuffer empty;
    const auto settle = [&]
    {
        for (int block = 0; block < 8; ++block)
            processor.processBlock (audio, empty);
        return processor.getStringActivityForDisplay();
    };
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 71, 0.72f), 0);
    processor.processBlock (audio, on);
    auto strings = settle();
    expect (strings[5].keyDown && strings[5].played, "B4 did not start on the high E");

    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::noteOff (1, 71), 0);
    processor.processBlockBypassed (audio, off);
    expect (audio.getMagnitude (0, blockSize) == 0.0f, "a bypassed block was not silent");
    strings = settle();
    expect (! strings[5].keyDown, "a key-up delivered while bypassed was lost");

    processor.processBlock (audio, on);
    processor.reset();
    strings = settle();
    expect (std::none_of (strings.begin(), strings.end(),
                          [] (const auto& string) { return string.keyDown || string.played; }),
            "the host's reset did not act as a Panic");
}

void testSameSampleNoteOnOffDoesNotStick()
{
    const auto renderOneShot = [] (bool noteOnFirst)
    {
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
        processor.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> audio { 2, blockSize };
        juce::MidiBuffer midi;
        const auto on = juce::MidiMessage::noteOn (1, 52, 0.8f);
        const auto off = juce::MidiMessage::noteOff (1, 52);
        midi.addEvent (noteOnFirst ? on : off, 0);
        midi.addEvent (noteOnFirst ? off : on, 0);
        processor.processBlock (audio, midi);
        for (int block = 0; block < 100; ++block)
        {
            juce::MidiBuffer empty;
            processor.processBlock (audio, empty);
        }
        return processor.getActiveVoiceCount();
    };

    expect (renderOneShot (true) == 0 && renderOneShot (false) == 0,
            "same-sample Note On/Off left a string held in one insertion order");
}

void testBridgeHandControllerReachesTheEngine()
{
    // CC2 is the bridge-hand pressure. It must shorten the note it is applied
    // to and leave the panel's parameters alone.
    const auto tailRms = [] (bool muted)
    {
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
        processor.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> audio { 2, blockSize };
        juce::MidiBuffer start;
        if (muted)
            start.addEvent (juce::MidiMessage::controllerEvent (1, 2, 127), 0);
        processor.processBlock (audio, start);
        for (int block = 0; block < 40; ++block)
        {
            juce::MidiBuffer empty;
            processor.processBlock (audio, empty);
        }
        juce::MidiBuffer note;
        note.addEvent (juce::MidiMessage::noteOn (1, 52, 0.85f), 0);
        processor.processBlock (audio, note);
        const int blocks = static_cast<int> (1.5 * sampleRate / blockSize);
        double energy = 0.0;
        int counted = 0;
        for (int block = 0; block < blocks; ++block)
        {
            juce::MidiBuffer empty;
            processor.processBlock (audio, empty);
            if (block < blocks / 2)
                continue;
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const double mono = 0.5 * (audio.getSample (0, sample)
                                         + audio.getSample (1, sample));
                energy += mono * mono;
                ++counted;
            }
        }
        return std::sqrt (energy / std::max (1, counted));
    };
    const double open = tailRms (false);
    const double muted = tailRms (true);
    expect (open > 1.0e-6, "the unmuted reference note was silent");
    expect (muted < 0.2 * open,
            "CC2 did not reach the engine as bridge-hand pressure");
}

void testTheModulationWheelReachesTheEngineAsVibrato()
{
    // CC1 is MIDI's Modulation Wheel, which this instrument reads as the
    // fretting hand's vibrato (see AcustraEngine's vibratoSemitones for what
    // it is bounded by). Zero is the wheel untouched, down to the sample.
    const auto phrase = [] (int wheel, bool send)
    {
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
        processor.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> audio { 2, blockSize };
        std::vector<float> mono;
        juce::MidiBuffer start;
        if (send)
            start.addEvent (juce::MidiMessage::controllerEvent (1, 1, wheel),
                            0);
        start.addEvent (juce::MidiMessage::noteOn (1, 52, 0.85f), 1);
        const int blocks = static_cast<int> (2.0 * sampleRate / blockSize);
        for (int block = 0; block < blocks; ++block)
        {
            juce::MidiBuffer empty;
            processor.processBlock (audio, block == 0 ? start : empty);
            for (int sample = 0; sample < blockSize; ++sample)
                mono.push_back (audio.getSample (0, sample));
        }
        return mono;
    };
    const auto untouched = phrase (0, false);
    const auto zeroed = phrase (0, true);
    const auto deep = phrase (127, true);
    expect (untouched == zeroed,
            "CC1 at zero changed the sound through the wrapper");
    expect (untouched.size() == deep.size() && untouched != deep,
            "CC1 did not reach the engine as vibrato");
    double largest = 0.0;
    for (std::size_t index = 0; index < untouched.size(); ++index)
        largest = std::max (largest, std::abs (
            static_cast<double> (deep[index] - untouched[index])));
    std::cout << "Acustra vibrato wrapper: largest sample difference "
              << largest << "\n";
}

void testReleaseGesturesAndRetiredCc68()
{
    // Missing/64/zero-On release encodings keep nominal damping. Explicit
    // firm releases shorten the tail without creating a new pluck, and the
    // retired CC68 remains inert at the same release velocity.
    namespace ids = acustra::parameters;
    const auto phrase = [] (const juce::MidiMessage& off, bool footswitch = false,
                            float releaseNoise = 0.0f)
    {
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
        setValue (processor, ids::releaseNoise, releaseNoise);
        processor.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> audio { 2, blockSize };
        std::vector<float> mono;
        const auto sweep = [&] (double seconds, juce::MidiBuffer& first)
        {
            const int blocks = std::max (1,
                static_cast<int> (seconds * sampleRate / blockSize));
            for (int block = 0; block < blocks; ++block)
            {
                juce::MidiBuffer empty;
                processor.processBlock (audio, block == 0 ? first : empty);
                for (int sample = 0; sample < blockSize; ++sample)
                    mono.push_back (0.5f * (audio.getSample (0, sample)
                                            + audio.getSample (1, sample)));
            }
        };
        juce::MidiBuffer start;
        if (footswitch)
            start.addEvent (juce::MidiMessage::controllerEvent (1, 68, 127), 0);
        start.addEvent (juce::MidiMessage::noteOn (1, 43, 1.0f), 0);
        sweep (0.8, start);
        juce::MidiBuffer release;
        release.addEvent (off, 0);
        sweep (0.2, release);
        juce::MidiBuffer next;
        if (footswitch)
            next.addEvent (juce::MidiMessage::controllerEvent (1, 68, 0), 0);
        next.addEvent (juce::MidiMessage::noteOn (1, 45, 0.8f), 0);
        next.addEvent (juce::MidiMessage::noteOn (1, 47, 0.8f), 480);
        sweep (0.8, next);
        return mono;
    };
    const auto plain = phrase (juce::MidiMessage::noteOff (1, 43));
    const auto sixtyFour
        = phrase (juce::MidiMessage::noteOff (1, 43, static_cast<juce::uint8> (64)));
    const auto zeroOn
        = phrase (juce::MidiMessage::noteOn (1, 43, static_cast<juce::uint8> (0)));
    const auto fast
        = phrase (juce::MidiMessage::noteOff (1, 43, static_cast<juce::uint8> (127)));
    const auto footswitch
        = phrase (juce::MidiMessage::noteOff (1, 43, static_cast<juce::uint8> (127)), true);
    expect (plain == sixtyFour && plain == zeroOn,
            "missing release velocity changed nominal damping");
    expect (plain != fast, "explicit firm release did not change hand damping");
    expect (fast == footswitch, "CC68 changed the performance");
    const std::size_t releaseAt = static_cast<std::size_t>(
        static_cast<int> (0.8 * sampleRate / blockSize) * blockSize);
    expect (std::equal (plain.begin (), plain.begin () + releaseAt, fast.begin ()),
            "release velocity changed the performance before key-up");

    // Optional release noise follows the same explicit gesture, beside the
    // passive hand loss, while unspecified/default releases remain exact.
    const float standard = 70.0f;
    const auto touched = phrase (juce::MidiMessage::noteOff (1, 43), false, standard);
    const auto touchedSixtyFour = phrase (
        juce::MidiMessage::noteOff (1, 43, static_cast<juce::uint8> (64)), false, standard);
    const auto touchedZeroOn = phrase (
        juce::MidiMessage::noteOn (1, 43, static_cast<juce::uint8> (0)), false, standard);
    const auto touchedFast = phrase (
        juce::MidiMessage::noteOff (1, 43, static_cast<juce::uint8> (127)), false, standard);
    const auto touchedFootswitch = phrase (
        juce::MidiMessage::noteOff (1, 43, static_cast<juce::uint8> (127)), true, standard);
    expect (touched == touchedSixtyFour && touched == touchedZeroOn,
            "a release without a velocity did not read as the nominal 64");
    expect (touched != plain, "Release Noise at the configured amount added nothing at key-up");
    expect (touchedFast == touchedFootswitch, "CC68 changed the performance");
    expect (touchedFast != touched,
            "explicit fast key-up did not change damping and optional touch");
    expect (std::equal (touched.begin (), touched.begin () + releaseAt, touchedFast.begin ()),
            "explicit fast key-up changed the audio before its release");
    expect (std::all_of (touchedFast.begin (), touchedFast.end (),
                        [] (float x) { return std::isfinite (x); }),
            "explicit release gesture produced a non-finite sample");
}

void testResetAllControllersReleasesSustain()
{
    auto processorOwner = std::make_unique<AcustraAudioProcessor>();
    auto& processor = *processorOwner;
    processor.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> audio { 2, blockSize };

    juce::MidiBuffer held;
    held.addEvent (juce::MidiMessage::noteOn (1, 52, 0.8f), 0);
    held.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 1);
    held.addEvent (juce::MidiMessage::noteOff (1, 52), 2);
    processor.processBlock (audio, held);
    expect (processor.getActiveVoiceCount() == 1,
            "sustain setup did not retain the released string");

    juce::MidiBuffer reset;
    reset.addEvent (juce::MidiMessage::controllerEvent (1, 121, 0), 0);
    processor.processBlock (audio, reset);
    for (int block = 0; block < 100; ++block)
    {
        juce::MidiBuffer empty;
        processor.processBlock (audio, empty);
    }
    expect (processor.getActiveVoiceCount() == 0,
            "Reset All Controllers left sustain latched");
}

// The Sustain switch holds released notes on every channel as the pedal
// does, and the two let go independently: the pedal keeps its notes when
// the switch goes off, and the switch keeps them when the pedal comes up.
void testSustainSwitchHoldsReleasedNotes()
{
    namespace ids = acustra::parameters;
    auto processorOwner = std::make_unique<AcustraAudioProcessor>();
    auto& processor = *processorOwner;
    processor.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> audio { 2, blockSize };
    const auto play = [&] (juce::MidiBuffer midi)
    {
        processor.processBlock (audio, midi);
        expect (finite (audio), "Sustain rendered a non-finite sample");
    };
    const auto settle = [&]
    {
        for (int block = 0; block < 100; ++block)
            play ({});
    };
    const auto note = [] (int channel)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (channel, 52, 0.8f), 0);
        midi.addEvent (juce::MidiMessage::noteOff (channel, 52), 2);
        return midi;
    };
    const auto pedal = [] (int channel, bool down)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (channel, 64, down ? 127 : 0), 0);
        return midi;
    };

    play (note (1));
    settle();
    expect (processor.getActiveVoiceCount() == 0,
            "a released note rang on with Sustain off");

    setValue (processor, ids::sustain, 1.0f);
    play (note (3));
    settle();
    expect (processor.getActiveVoiceCount() == 1,
            "Sustain did not hold a released note on channel 3");
    play (pedal (3, true));
    play (pedal (3, false));
    settle();
    expect (processor.getActiveVoiceCount() == 1,
            "a pedal lifted under Sustain let the note go");
    setValue (processor, ids::sustain, 0.0f);
    settle();
    expect (processor.getActiveVoiceCount() == 0,
            "switching Sustain off did not let the held note go");

    play (pedal (1, true));
    setValue (processor, ids::sustain, 1.0f);
    play (note (1));
    setValue (processor, ids::sustain, 0.0f);
    settle();
    expect (processor.getActiveVoiceCount() == 1,
            "switching Sustain off let go a note the pedal still holds");
    play (pedal (1, false));
    settle();
    expect (processor.getActiveVoiceCount() == 0,
            "the pedal lifted after Sustain went off did not let the note go");

    // Panic clears a held note and leaves the switch where it was.
    setValue (processor, ids::sustain, 1.0f);
    play (note (1));
    processor.requestPanic();
    play ({});
    expect (processor.getActiveVoiceCount() == 0, "Panic left a Sustain-held note");
    play (note (1));
    settle();
    expect (processor.getActiveVoiceCount() == 1
                && valueOf (processor, ids::sustain) >= 0.5f,
            "Panic switched Sustain off");
}

void testMemberChannelOwnershipAndControllers()
{
    auto processorOwner = std::make_unique<AcustraAudioProcessor>();
    auto& processor = *processorOwner;
    processor.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> audio { 2, blockSize };
    const auto render = [&] (juce::MidiBuffer& midi)
    {
        audio.clear();
        processor.processBlock (audio, midi);
    };
    const auto renderTail = [&] (int blocks)
    {
        juce::MidiBuffer empty;
        for (int block = 0; block < blocks; ++block)
            render (empty);
    };
    const auto soundOff = [&] (int channel)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (channel, 120, 0), 0);
        render (midi);
    };

    juce::MidiBuffer samePitch;
    samePitch.addEvent (juce::MidiMessage::noteOn (2, 52, 0.8f), 0);
    samePitch.addEvent (juce::MidiMessage::noteOn (3, 52, 0.8f), 0);
    render (samePitch);
    expect (processor.getActiveVoiceCount() == 2,
            "same-pitch member notes did not receive separate string ownership");
    soundOff (2);
    expect (processor.getActiveVoiceCount() == 1,
            "member All Sound Off affected another channel");
    soundOff (3);
    expect (processor.getActiveVoiceCount() == 0,
            "member All Sound Off missed its owned string");
    soundOff (1);

    // A boundary controller acts before its sample's notes, whatever the
    // host's insertion order: the notes that start on its sample sound, on
    // its own channel and on others.
    juce::MidiBuffer boundary;
    boundary.addEvent (juce::MidiMessage::noteOn (2, 52, 0.8f), 0);
    boundary.addEvent (juce::MidiMessage::noteOn (3, 52, 0.8f), 0);
    boundary.addEvent (juce::MidiMessage::controllerEvent (2, 123, 0), 0);
    render (boundary);
    expect (processor.getActiveVoiceCount() == 2,
            "member All Notes Off cancelled a Note On on its own sample (active="
                + std::to_string (processor.getActiveVoiceCount()) + ")");
    soundOff (2);
    soundOff (3);

    juce::MidiBuffer held;
    held.addEvent (juce::MidiMessage::noteOn (2, 52, 0.8f), 0);
    held.addEvent (juce::MidiMessage::controllerEvent (2, 64, 127), 1);
    held.addEvent (juce::MidiMessage::noteOff (2, 52), 2);
    render (held);
    renderTail (400);
    expect (processor.getActiveVoiceCount() == 1,
            "member sustain did not retain its released owner");
    juce::MidiBuffer unrelatedReset;
    unrelatedReset.addEvent (juce::MidiMessage::controllerEvent (3, 121, 0), 0);
    render (unrelatedReset);
    expect (processor.getActiveVoiceCount() == 1,
            "member Reset All Controllers affected another channel (active="
                + std::to_string (processor.getActiveVoiceCount()) + ")");
    juce::MidiBuffer ownerReset;
    ownerReset.addEvent (juce::MidiMessage::controllerEvent (2, 121, 0), 0);
    render (ownerReset);
    renderTail (400);
    expect (processor.getActiveVoiceCount() == 0,
            "member Reset All Controllers did not release its sustain");
    soundOff (1);

    juce::MidiBuffer memberIsolation;
    memberIsolation.addEvent (
        juce::MidiMessage::controllerEvent (2, 64, 127), 0);
    memberIsolation.addEvent (juce::MidiMessage::noteOn (3, 52, 0.8f), 1);
    memberIsolation.addEvent (juce::MidiMessage::noteOff (3, 52), 2);
    render (memberIsolation);
    renderTail (400);
    expect (processor.getActiveVoiceCount() == 0,
            "member sustain leaked to another channel");
    juce::MidiBuffer memberPedalUp;
    memberPedalUp.addEvent (
        juce::MidiMessage::controllerEvent (2, 64, 0), 0);
    render (memberPedalUp);

    // Before an RPN 6 MCM actually creates a zone, channel 1 is ordinary
    // MIDI and must not sustain or control channel 3.
    juce::MidiBuffer conventionalMaster;
    conventionalMaster.addEvent (
        juce::MidiMessage::controllerEvent (1, 64, 127), 0);
    conventionalMaster.addEvent (juce::MidiMessage::noteOn (3, 52, 0.8f), 1);
    conventionalMaster.addEvent (juce::MidiMessage::noteOff (3, 52), 2);
    render (conventionalMaster);
    renderTail (400);
    expect (processor.getActiveVoiceCount() == 0,
            "conventional channel 1 acted as an MPE master before RPN 6");
    juce::MidiBuffer conventionalPedalUp;
    conventionalPedalUp.addEvent (
        juce::MidiMessage::controllerEvent (1, 64, 0), 0);
    render (conventionalPedalUp);

    juce::MidiBuffer zoneSetup;
    addLowerZone (zoneSetup, 2);
    render (zoneSetup);

    juce::MidiBuffer masterHeld;
    masterHeld.addEvent (juce::MidiMessage::noteOn (3, 52, 0.8f), 0);
    masterHeld.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 1);
    masterHeld.addEvent (juce::MidiMessage::noteOff (3, 52), 2);
    render (masterHeld);
    renderTail (400);
    expect (processor.getActiveVoiceCount() == 1,
            "channel 1 sustain did not act as the MPE master (active="
                + std::to_string (processor.getActiveVoiceCount()) + ")");

    juce::MidiBuffer masterReset;
    masterReset.addEvent (juce::MidiMessage::controllerEvent (1, 121, 0), 0);
    render (masterReset);
    renderTail (400);
    expect (processor.getActiveVoiceCount() == 0,
            "channel 1 Reset All Controllers did not release master sustain");
}

void testMemberPitchBendDoesNotLeakChannels()
{
    const auto render = [] (int bendChannel, int wheel, bool mpe)
    {
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
        processor.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> audio { 2, blockSize };
        juce::MidiBuffer midi;
        if (mpe)
            addLowerZone (midi, 2);
        midi.addEvent (juce::MidiMessage::pitchWheel (bendChannel, wheel), 0);
        midi.addEvent (juce::MidiMessage::noteOn (2, 52, 0.8f), 0);
        processor.processBlock (audio, midi);

        std::vector<float> result;
        result.reserve (2 * blockSize);
        for (int channel = 0; channel < 2; ++channel)
            result.insert (result.end(), audio.getReadPointer (channel),
                           audio.getReadPointer (channel) + blockSize);
        return result;
    };

    const auto baseline = render (2, 8192, false);
    const auto unrelated = render (3, 10240, false);
    const auto conventionalMaster = render (1, 10240, false);
    const auto owned = render (2, 10240, false);
    const auto mpeMaster = render (1, 10240, true);
    float unrelatedDifference = 0.0f;
    float conventionalMasterDifference = 0.0f;
    float ownedDifference = 0.0f;
    float mpeMasterDifference = 0.0f;
    for (std::size_t sample = 0; sample < baseline.size(); ++sample)
    {
        unrelatedDifference = std::max (
            unrelatedDifference,
            std::abs (unrelated[sample] - baseline[sample]));
        conventionalMasterDifference = std::max (
            conventionalMasterDifference,
            std::abs (conventionalMaster[sample] - baseline[sample]));
        ownedDifference = std::max (
            ownedDifference,
            std::abs (owned[sample] - baseline[sample]));
        mpeMasterDifference = std::max (
            mpeMasterDifference,
            std::abs (mpeMaster[sample] - baseline[sample]));
    }
    expect (unrelatedDifference == 0.0f
                && conventionalMasterDifference == 0.0f
                && ownedDifference > 1.0e-6f
                && mpeMasterDifference > 1.0e-6f,
            "channel bend isolation/master routing did not follow zone state");
}

void testRpnPitchRangesAndSelectionState()
{
    const auto renderConventional = [] (int variant)
    {
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
        processor.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> audio { 2, blockSize };
        juce::MidiBuffer midi;
        addRpn (midi, 2, 0, variant == 1 ? 2 : variant == 5 ? 96 : 2,
                variant == 1 || variant == 5 ? -1 : 50);
        if (variant == 2)
        {
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 101, 127), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 100, 127), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 38, 99), 0);
        }
        else if (variant == 3)
        {
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 99, 0), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 98, 0), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 6, 12), 0);
        }
        else if (variant == 4)
        {
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 121, 0), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 38, 99), 0);
        }
        midi.addEvent (juce::MidiMessage::pitchWheel (2, 16383), 0);
        midi.addEvent (juce::MidiMessage::noteOn (2, 52, 0.8f), 0);
        processor.processBlock (audio, midi);
        return std::pair { flattened (audio), finite (audio) };
    };

    const auto exact = renderConventional (0);
    const auto whole = renderConventional (1);
    const auto rpnNull = renderConventional (2);
    const auto nrpn = renderConventional (3);
    const auto reset = renderConventional (4);
    const auto maximum = renderConventional (5);
    expect (exact.first != whole.first,
            "RPN 0 Data Entry LSB did not refine pitch sensitivity");
    expect (exact.first == rpnNull.first && exact.first == nrpn.first
                && exact.first == reset.first,
            "RPN Null/NRPN/CC121 did not preserve the configured exact range");
    expect (maximum.second && maximum.first != whole.first,
            "the legal 96-semitone RPN 0 range was ignored or became non-finite");

    const auto renderSharedMember = [] (int rangeChannel, bool configureRange,
                                        bool resetMaster)
    {
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
        processor.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> audio { 2, blockSize };
        juce::MidiBuffer midi;
        addLowerZone (midi, 2);
        if (configureRange)
            addRpn (midi, rangeChannel, 0, 5, 25);
        if (resetMaster)
        {
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 121, 0), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (
                rangeChannel, 38, 99), 0);
        }
        midi.addEvent (juce::MidiMessage::pitchWheel (3, 16383), 0);
        midi.addEvent (juce::MidiMessage::noteOn (3, 52, 0.8f), 0);
        processor.processBlock (audio, midi);
        return flattened (audio);
    };

    const auto memberFromTwo = renderSharedMember (2, true, false);
    const auto memberFromThree = renderSharedMember (3, true, false);
    const auto memberAfterReset = renderSharedMember (2, true, true);
    const auto defaultMember = renderSharedMember (2, false, false);
    expect (memberFromTwo == memberFromThree
                && memberFromTwo == memberAfterReset
                && memberFromTwo != defaultMember,
            "MPE member RPN 0 was not shared or survived CC121 incorrectly");

    const auto renderConventionalCrossChannel = [] (int rangeChannel)
    {
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
        processor.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> audio { 2, blockSize };
        juce::MidiBuffer midi;
        if (rangeChannel != 0)
            addRpn (midi, rangeChannel, 0, 5, 0);
        midi.addEvent (juce::MidiMessage::pitchWheel (3, 16383), 0);
        midi.addEvent (juce::MidiMessage::noteOn (3, 52, 0.8f), 0);
        processor.processBlock (audio, midi);
        return flattened (audio);
    };
    const auto conventionalDefault = renderConventionalCrossChannel (0);
    expect (renderConventionalCrossChannel (2) == conventionalDefault
                && renderConventionalCrossChannel (3) != conventionalDefault,
            "conventional RPN 0 range was not channel-scoped");
}

void testLowerZoneLifecycleAndControllerBoundaries()
{
    auto processorOwner = std::make_unique<AcustraAudioProcessor>();
    auto& processor = *processorOwner;
    processor.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> audio { 2, blockSize };
    const auto render = [&] (juce::MidiBuffer& midi)
    {
        audio.clear();
        processor.processBlock (audio, midi);
    };

    juce::MidiBuffer notes;
    notes.addEvent (juce::MidiMessage::noteOn (2, 52, 0.8f), 0);
    notes.addEvent (juce::MidiMessage::noteOn (8, 55, 0.8f), 0);
    render (notes);
    juce::MidiBuffer enable;
    addLowerZone (enable, 2);
    render (enable);
    expect (processor.getActiveVoiceCount() == 1,
            "lower-zone enable did not stop exactly the newly affected channels");
    juce::MidiBuffer resize;
    addLowerZone (resize, 7);
    render (resize);
    expect (processor.getActiveVoiceCount() == 0,
            "lower-zone resize did not stop the expanded affected union");
    juce::MidiBuffer member;
    member.addEvent (juce::MidiMessage::noteOn (2, 52, 0.8f), 0);
    render (member);
    juce::MidiBuffer disable;
    addLowerZone (disable, 0);
    render (disable);
    expect (processor.getActiveVoiceCount() == 0,
            "lower-zone deactivation left an old member voice sounding");

    auto nrpnProcessorOwner = std::make_unique<AcustraAudioProcessor>();
    auto& nrpnProcessor = *nrpnProcessorOwner;
    nrpnProcessor.prepareToPlay (sampleRate, blockSize);
    juce::MidiBuffer nrpnNotes;
    nrpnNotes.addEvent (juce::MidiMessage::noteOn (2, 52, 0.8f), 0);
    nrpnNotes.addEvent (juce::MidiMessage::noteOn (8, 55, 0.8f), 0);
    nrpnProcessor.processBlock (audio, nrpnNotes);
    juce::MidiBuffer nrpnLayout;
    nrpnLayout.addEvent (juce::MidiMessage::controllerEvent (1, 99, 0), 0);
    nrpnLayout.addEvent (juce::MidiMessage::controllerEvent (1, 98, 6), 0);
    nrpnLayout.addEvent (juce::MidiMessage::controllerEvent (1, 6, 2), 0);
    nrpnProcessor.processBlock (audio, nrpnLayout);
    expect (nrpnProcessor.getActiveVoiceCount() == 2,
            "NRPN 6 was incorrectly accepted as an MPE layout RPN");

    for (const int controller : { 120, 123 })
        for (const bool controllerFirst : { false, true })
        {
            auto boundaryOwner = std::make_unique<AcustraAudioProcessor>();
            auto& boundary = *boundaryOwner;
            boundary.prepareToPlay (sampleRate, blockSize);
            juce::MidiBuffer setup;
            addLowerZone (setup, 2);
            boundary.processBlock (audio, setup);
            juce::MidiBuffer events;
            const auto control = juce::MidiMessage::controllerEvent (
                1, controller, 0);
            const auto affected = juce::MidiMessage::noteOn (2, 52, 0.8f);
            const auto outside = juce::MidiMessage::noteOn (8, 55, 0.8f);
            events.addEvent (controllerFirst ? control : affected, 0);
            events.addEvent (outside, 0);
            events.addEvent (controllerFirst ? affected : control, 0);
            boundary.processBlock (audio, events);
            expect (boundary.getActiveVoiceCount() == 2,
                    "same-sample CC" + std::to_string (controller)
                        + " cancelled a zone Note On in one insertion order");
        }

    for (const int controller : { 120, 123 })
        for (const bool controllerFirst : { false, true })
        {
            auto boundaryOwner = std::make_unique<AcustraAudioProcessor>();
            auto& boundary = *boundaryOwner;
            boundary.prepareToPlay (sampleRate, blockSize);
            juce::MidiBuffer events;
            const auto control = juce::MidiMessage::controllerEvent (
                2, controller, 0);
            const auto affected = juce::MidiMessage::noteOn (2, 52, 0.8f);
            const auto outside = juce::MidiMessage::noteOn (3, 55, 0.8f);
            events.addEvent (controllerFirst ? control : affected, 0);
            events.addEvent (outside, 0);
            events.addEvent (controllerFirst ? affected : control, 0);
            boundary.processBlock (audio, events);
            expect (boundary.getActiveVoiceCount() == 2,
                    "same-sample conventional CC" + std::to_string (controller)
                        + " cancelled a Note On or depended on order");
        }
}

void testControllerResetSoundOffAndUiPanic()
{
    const auto renderTail = [] (AcustraAudioProcessor& processor,
                                juce::AudioBuffer<float>& audio, int blocks)
    {
        for (int block = 0; block < blocks; ++block)
        {
            juce::MidiBuffer empty;
            processor.processBlock (audio, empty);
        }
    };

    auto preservedOwner = std::make_unique<AcustraAudioProcessor>();
    auto& preserved = *preservedOwner;
    preserved.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> audio { 2, blockSize };
    juce::MidiBuffer setup;
    setup.addEvent (juce::MidiMessage::controllerEvent (2, 64, 127), 0);
    setup.addEvent (juce::MidiMessage::pitchWheel (2, 16383), 0);
    setup.addEvent (juce::MidiMessage::noteOn (2, 52, 0.8f), 0);
    preserved.processBlock (audio, setup);
    juce::MidiBuffer soundOff;
    soundOff.addEvent (juce::MidiMessage::controllerEvent (2, 120, 0), 0);
    preserved.processBlock (audio, soundOff);
    expect (preserved.getActiveVoiceCount() == 0,
            "CC120 did not silence its conventional channel");
    juce::MidiBuffer afterSoundOff;
    afterSoundOff.addEvent (juce::MidiMessage::noteOn (2, 52, 0.8f), 0);
    afterSoundOff.addEvent (juce::MidiMessage::noteOff (2, 52), 1);
    preserved.processBlock (audio, afterSoundOff);
    renderTail (preserved, audio, 400);
    expect (preserved.getActiveVoiceCount() == 1,
            "CC120 reset sustain instead of preserving controller state");
    juce::MidiBuffer pedalUp;
    pedalUp.addEvent (juce::MidiMessage::controllerEvent (2, 64, 0), 0);
    preserved.processBlock (audio, pedalUp);
    renderTail (preserved, audio, 400);
    expect (preserved.getActiveVoiceCount() == 0,
            "preserved CC120 sustain did not release on pedal-up");

    auto panicOwner = std::make_unique<AcustraAudioProcessor>();
    auto& panic = *panicOwner;
    panic.prepareToPlay (sampleRate, blockSize);
    juce::MidiBuffer pedalDown;
    pedalDown.addEvent (juce::MidiMessage::controllerEvent (2, 64, 127), 0);
    panic.processBlock (audio, pedalDown);
    panic.requestPanic();
    juce::MidiBuffer empty;
    panic.processBlock (audio, empty);
    juce::MidiBuffer afterPanic;
    afterPanic.addEvent (juce::MidiMessage::noteOn (2, 52, 0.8f), 0);
    afterPanic.addEvent (juce::MidiMessage::noteOff (2, 52), 1);
    panic.processBlock (audio, afterPanic);
    renderTail (panic, audio, 400);
    expect (panic.getActiveVoiceCount() == 0,
            "front-panel Panic failed to reset controller state fully");

    auto masterResetOwner = std::make_unique<AcustraAudioProcessor>();
    auto& masterReset = *masterResetOwner;
    masterReset.prepareToPlay (sampleRate, blockSize);
    juce::MidiBuffer mpe;
    addLowerZone (mpe, 2);
    mpe.addEvent (juce::MidiMessage::noteOn (2, 52, 0.8f), 0);
    mpe.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 1);
    mpe.addEvent (juce::MidiMessage::noteOn (3, 56, 0.8f), 2);
    mpe.addEvent (juce::MidiMessage::noteOff (3, 56), 3);
    masterReset.processBlock (audio, mpe);
    const int beforeResetCount = masterReset.getActiveVoiceCount();
    juce::MidiBuffer reset;
    reset.addEvent (juce::MidiMessage::controllerEvent (1, 121, 0), 0);
    masterReset.processBlock (audio, reset);
    const int afterResetCount = masterReset.getActiveVoiceCount();
    juce::MidiBuffer removeTail;
    removeTail.addEvent (juce::MidiMessage::controllerEvent (3, 120, 0), 0);
    masterReset.processBlock (audio, removeTail);
    expect (beforeResetCount == 2 && afterResetCount == 2
                && masterReset.getActiveVoiceCount() == 1,
            "MPE master CC121 released a held key or reset the wrong zone state "
                "(before=" + std::to_string (beforeResetCount)
                + ", after=" + std::to_string (afterResetCount)
                + ", after member CC120="
                + std::to_string (masterReset.getActiveVoiceCount()) + ")");
}

void testStateRoundTripAndMigration()
{
    namespace ids = acustra::parameters;
    auto sourceOwner = std::make_unique<AcustraAudioProcessor>();
    auto& source = *sourceOwner;
    setValue (source, ids::shape, 0.0f);
    setValue (source, ids::bodyMaterial, 2.0f);
    setValue (source, ids::tuning, 4.0f);
    setValue (source, ids::stringAge, 87.0f);
    setValue (source, ids::pluckPosition, 64.0f);
    setValue (source, ids::output, -2.4f);
    setValue (source, ids::capture, 4.0f);
    setValue (source, ids::picking, 1.0f);
    setValue (source, ids::upperMic, 1.0f);
    setValue (source, ids::piezoLoading, 1.0f);
    setValue (source, ids::captureMode, 1.0f);
    setValue (source, ids::guitarModel, 1.0f);
    setValue (source, ids::piezoMix, 45.0f);
    setValue (source, ids::piezoPolarityInverted, 1.0f);
    setValue (source, ids::releaseNoise, 33.0f);
    setValue (source, ids::room, 64.0f);

    juce::MemoryBlock stored;
    source.getStateInformation (stored);
    expect (stored.getSize() > 0, "getStateInformation returned no state");
    // A state saved now says which layout it holds, so a reload does not
    // migrate it: its Body Material 2 is Maple, not the old layout's Mahogany.
    {
        const auto xml = juce::AudioProcessor::getXmlFromBinary (
            stored.getData(), static_cast<int> (stored.getSize()));
        expect (xml != nullptr && xml->getIntAttribute ("stateVersion") == 2,
                "a saved state does not carry stateVersion 2");
    }

    auto restoredOwner = std::make_unique<AcustraAudioProcessor>();
    auto& restored = *restoredOwner;
    restored.setStateInformation (stored.getData(),
                                  static_cast<int> (stored.getSize()));
    const auto roundTripped = [&] (const AcustraAudioProcessor& from,
                                   const AcustraAudioProcessor& to)
    {
        bool same = true;
        for (const char* id : { ids::shape, ids::bodyMaterial,
                                ids::tuning, ids::stringAge, ids::pluckPosition,
                                ids::touch, ids::bodyAmount, ids::stereoWidth,
                                ids::output, ids::capture, ids::picking,
                                ids::upperMic, ids::piezoLoading, ids::captureMode,
                                ids::guitarModel, ids::gatherChords, ids::piezoMix,
                                ids::releaseNoise, ids::room, ids::piezoPolarityInverted })
        {
            const bool kept = std::abs (valueOf (to, id) - valueOf (from, id)) < 0.011f;
            expect (kept, std::string { "state round trip lost " } + id);
            same = same && kept;
        }
        return same;
    };
    roundTripped (source, restored);
    expect (restored.snapshotEngineParameters().bodyMaterial == acustra::BodyMaterial::Maple
                && std::abs (restored.snapshotEngineParameters().piezoMix - 0.45f) < 0.002f,
            "a current state's Body Material or Piezo Mix changed on reload");
    {
        // And again: saving the reloaded state and reloading it changes nothing.
        juce::MemoryBlock again;
        restored.getStateInformation (again);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (
            again.getData(), static_cast<int> (again.getSize()));
        expect (xml != nullptr && xml->getIntAttribute ("stateVersion") == 2,
                "a reloaded state was saved without stateVersion 2");
        auto secondOwner = std::make_unique<AcustraAudioProcessor>();
        secondOwner->setStateInformation (again.getData(), static_cast<int> (again.getSize()));
        roundTripped (source, *secondOwner);
    }

    // A session that chose Classical 78 (or, from a five-choice local
    // build, a retired 2-4) reloads every other control unchanged and plays
    // the Original guitar: the engine parameters equal those of the same
    // state saved on Original.
    {
        setValue (source, ids::guitarModel, 0.0f);
        source.getStateInformation (stored);
        restored.setStateInformation (stored.getData(), static_cast<int> (stored.getSize()));
        const auto original = restored.snapshotEngineParameters();
        for (const float chosen : { 1.0f, 2.0f, 3.0f, 4.0f })
        {
            auto old = source.parameters.copyState();
            old.setProperty ("stateVersion", 2, nullptr);
            for (auto child : old)
                if (child.getProperty ("id").toString() == ids::guitarModel)
                    child.setProperty ("value", chosen, nullptr);
            juce::MemoryBlock bytes;
            if (const auto xml = old.createXml())
                juce::AudioProcessor::copyXmlToBinary (*xml, bytes);
            auto reloadedOwner = std::make_unique<AcustraAudioProcessor>();
            reloadedOwner->setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
            const auto reloaded = reloadedOwner->snapshotEngineParameters();
            expect (sameEngineParameters (original, reloaded),
                    "a session that chose a retired guitar model did not play the Original");
        }
    }
    auto versionSix = source.parameters.copyState();
    for (int child = versionSix.getNumChildren(); --child >= 0;)
        if (versionSix.getChild (child).getProperty ("id").toString() == ids::guitarModel)
            versionSix.removeChild (child, nullptr);
    if (const auto xml = versionSix.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, stored);
    restored.setStateInformation (stored.getData(), static_cast<int> (stored.getSize()));
    expect (valueOf (restored, ids::guitarModel) == 0.0f
                && valueOf (restored, ids::captureMode) == valueOf (source, ids::captureMode),
            "a pre-model state must take the legacy slot's default without changing its modern capture");

    // Every retired choice, with either legacy override, migrates to a
    // supported observation. A saved modern value takes precedence.
    for (int legacy = 0; legacy < 5; ++legacy)
        for (int upper = 0; upper < 2; ++upper)
            for (int loaded = 0; loaded < 2; ++loaded)
            {
                setValue (source, ids::capture, static_cast<float> (legacy));
                setValue (source, ids::upperMic, static_cast<float> (upper));
                setValue (source, ids::piezoLoading, static_cast<float> (loaded));
                auto old = source.parameters.copyState();
                for (int child = old.getNumChildren(); --child >= 0;)
                    if (old.getChild (child).getProperty ("id").toString() == ids::captureMode)
                        old.removeChild (child, nullptr);
                juce::MemoryBlock bytes;
                if (const auto xml = old.createXml())
                    juce::AudioProcessor::copyXmlToBinary (*xml, bytes);
                restored.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
                const int expected = upper ? 1 : legacy == 0 ? 0 : legacy < 3 ? 1 : 2;
                expect (std::abs (valueOf (restored, ids::captureMode) - static_cast<float> (expected)) < 0.01f,
                        "a retired capture choice was not migrated deterministically");
                restored.getStateInformation (bytes);
                auto roundTripOwner = std::make_unique<AcustraAudioProcessor>();
                auto& roundTrip = *roundTripOwner;
                roundTrip.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
                expect (roundTrip.snapshotEngineParameters().capture == restored.snapshotEngineParameters().capture,
                        "a migrated capture changed on its next save and reload");
            }

    // A session saved before later controls existed must receive their factory
    // defaults, not whatever values happen to be live in the destination.
    setValue (restored, ids::stringAge, 99.0f);
    setValue (restored, ids::output, 5.0f);
    setValue (restored, ids::capture, 3.0f);
    setValue (restored, ids::picking, 2.0f);
    setValue (restored, ids::piezoMix, 80.0f);
    setValue (restored, ids::piezoPolarityInverted, 1.0f);
    setValue (restored, ids::releaseNoise, 90.0f);
    setValue (restored, ids::room, 77.0f);
    setValue (restored, ids::upperMic, 1.0f);
    setValue (restored, ids::piezoLoading, 1.0f);
    setValue (restored, ids::guitarModel, 4.0f);
    juce::ValueTree oldState { restored.parameters.state.getType() };
    juce::ValueTree shape { "PARAM" };
    shape.setProperty ("id", ids::shape, nullptr);
    shape.setProperty ("value", 1.0f, nullptr);
    oldState.appendChild (shape, nullptr);
    juce::MemoryBlock oldBytes;
    if (const auto xml = oldState.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, oldBytes);
    restored.setStateInformation (oldBytes.getData(),
                                  static_cast<int> (oldBytes.getSize()));
    expect (valueOf (restored, ids::shape) == 1.0f,
            "a retained parameter was not restored from an old state");
    expect (std::abs (valueOf (restored, ids::stringAge) - 15.0f) < 0.011f
                && std::abs (valueOf (restored, ids::output)) < 0.011f
                && valueOf (restored, ids::capture) == 0.0f
                && valueOf (restored, ids::picking) == 0.0f
                && valueOf (restored, ids::piezoMix) == 0.0f
                && ! restored.snapshotEngineParameters().piezoPolarityInverted
                && valueOf (restored, ids::upperMic) == 0.0f
                && valueOf (restored, ids::piezoLoading) == 0.0f
                && valueOf (restored, ids::captureMode) == 0.0f
                && valueOf (restored, ids::guitarModel) == 0.0f,
            "parameters absent from an old state did not receive defaults");
    expect (restored.snapshotEngineParameters().piezoMix == 0.0f,
            "a session saved before Piezo Mix existed mixes the piezo into Main");
    // Release Noise is new for them too, but a session saved before it keeps
    // its silent key-ups rather than taking the new default.
    expect (valueOf (restored, ids::releaseNoise) == 0.0f
                && restored.snapshotEngineParameters().releaseNoise == 0.0f,
            "a session saved before Release Noise existed gained key-up sounds");
    // Room likewise: an older session stays as dry as it was saved.
    expect (valueOf (restored, ids::room) == 0.0f
                && restored.snapshotEngineParameters().room == 0.0f,
            "a session saved before Room existed gained a room");

    // A state saved before the simplification (no stateVersion): its String
    // Material and Bridge Model are dropped, and its Body Material from
    // Spruce, Cedar, Mahogany, Maple moves to Spruce, Mahogany, Maple, Cedar
    // to Mahogany. Every such session plays steel on the Original bridge.
    constexpr std::array<float, 4> migratedWood { 0.0f, 1.0f, 1.0f, 2.0f };
    constexpr std::array woods { acustra::BodyMaterial::Spruce, acustra::BodyMaterial::Mahogany,
        acustra::BodyMaterial::Mahogany, acustra::BodyMaterial::Maple };
    for (int oldWood = 0; oldWood < 4; ++oldWood)
        for (int oldStrings = 0; oldStrings < 2; ++oldStrings)
        {
            juce::ValueTree versionOne { restored.parameters.state.getType() };
            const auto add = [&] (const char* id, float value)
            {
                juce::ValueTree parameter { "PARAM" };
                parameter.setProperty ("id", id, nullptr);
                parameter.setProperty ("value", value, nullptr);
                versionOne.appendChild (parameter, nullptr);
            };
            add (ids::shape, 3.0f);
            add (ids::bodyMaterial, static_cast<float> (oldWood));
            add ("stringMaterial", static_cast<float> (oldStrings));
            add ("bridgeModel", 1.0f);
            add (ids::tuning, 2.0f);
            add (ids::captureMode, 2.0f);
            juce::MemoryBlock bytes;
            if (const auto xml = versionOne.createXml())
                juce::AudioProcessor::copyXmlToBinary (*xml, bytes);
            // Start from Maple, so a missed migration shows.
            setValue (restored, ids::bodyMaterial, 2.0f);
            restored.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
            const std::string label = "a version 1 state with Body Material "
                + std::to_string (oldWood) + " and String Material "
                + std::to_string (oldStrings);
            expect (valueOf (restored, ids::bodyMaterial)
                            == migratedWood[static_cast<std::size_t> (oldWood)]
                        && restored.snapshotEngineParameters().bodyMaterial
                            == woods[static_cast<std::size_t> (oldWood)],
                    label + " did not migrate its Body Material");
            expect (valueOf (restored, ids::shape) == 3.0f
                        && valueOf (restored, ids::tuning) == 2.0f
                        && valueOf (restored, ids::captureMode) == 2.0f,
                    label + " lost a parameter it kept");
            bool retiredDropped = true;
            for (auto child : restored.parameters.state)
            {
                const auto id = child.getProperty ("id").toString();
                retiredDropped = retiredDropped && id != "stringMaterial" && id != "bridgeModel";
            }
            expect (retiredDropped, label + " kept its String Material or Bridge Model");
            // Saved again, it is a current state and reloads unchanged.
            juce::MemoryBlock resaved;
            restored.getStateInformation (resaved);
            const auto xml = juce::AudioProcessor::getXmlFromBinary (
                resaved.getData(), static_cast<int> (resaved.getSize()));
            expect (xml != nullptr && xml->getIntAttribute ("stateVersion") == 2
                        && xml->toString().indexOf ("stringMaterial") < 0
                        && xml->toString().indexOf ("bridgeModel") < 0,
                    label + " was resaved without stateVersion 2 or with a retired parameter");
            auto reloadOwner = std::make_unique<AcustraAudioProcessor>();
            reloadOwner->setStateInformation (resaved.getData(), static_cast<int> (resaved.getSize()));
            expect (valueOf (*reloadOwner, ids::bodyMaterial)
                        == migratedWood[static_cast<std::size_t> (oldWood)],
                    label + " migrated its Body Material twice");
        }

    // The migration cases above leave their own Shape; a state that is not
    // Acustra's must leave whatever is there.
    const float shapeBeforeGarbage = valueOf (restored, ids::shape);
    const char garbage[] = "not an Acustra state";
    restored.setStateInformation (garbage, static_cast<int> (sizeof garbage));
    expect (valueOf (restored, ids::shape) == shapeBeforeGarbage,
            "invalid host state was applied instead of ignored");
}

void testMalformedStateParameters()
{
    namespace ids = acustra::parameters;
    auto owner = std::make_unique<AcustraAudioProcessor>();
    auto& processor = *owner;
    const auto restore = [] (AcustraAudioProcessor& target,
                            const juce::ValueTree& state)
    {
        juce::MemoryBlock bytes;
        if (const auto xml = state.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, bytes);
        target.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
    };

    for (const char* invalid : { "nan", "inf", "-inf" })
    {
        auto state = processor.parameters.copyState();
        state.setProperty ("stateVersion", 2, nullptr);
        for (auto child : state)
            if (child.hasType ("PARAM"))
                child.setProperty ("value", invalid, nullptr);
        // An explicitly present modern Capture still wins the legacy
        // overrides when its malformed value falls back to its own default.
        for (auto child : state)
        {
            const auto id = child.getProperty ("id").toString();
            if (id == ids::capture)
                child.setProperty ("value", 4.0f, nullptr);
            else if (id == ids::upperMic)
                child.setProperty ("value", 1.0f, nullptr);
        }
        restore (processor, state);

        const auto check = [&] (const AcustraAudioProcessor& target)
        {
            for (const auto* hostParameter : target.getParameters())
            {
                const auto* parameter = dynamic_cast<const juce::RangedAudioParameter*> (
                    hostParameter);
                if (parameter == nullptr)
                    continue;
                const float expected = parameter->paramID == ids::capture ? 4.0f
                    : parameter->paramID == ids::upperMic ? 1.0f
                    : parameter->convertFrom0to1 (parameter->getDefaultValue());
                const float actual = valueOf (target, parameter->paramID.toRawUTF8());
                expect (std::isfinite (actual) && std::abs (actual - expected) < 0.011f,
                        std::string { invalid } + " state did not recover the default for "
                            + parameter->paramID.toStdString());
            }
            const auto snapshot = target.snapshotEngineParameters();
            expect (snapshot.capture == acustra::CaptureType::StereoMic
                        && snapshot.outputGain == 1.0f
                        && std::abs (snapshot.room - 0.1f) < 0.002f
                        && std::abs (snapshot.releaseNoise - 0.1f) < 0.002f,
                    "malformed explicit controls used legacy missing-control defaults");
        };
        check (processor);
        juce::MemoryBlock resaved;
        processor.getStateInformation (resaved);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (
            resaved.getData(), static_cast<int> (resaved.getSize()));
        expect (xml != nullptr, "repaired state could not be saved");
        if (xml != nullptr)
            for (const auto& child : juce::ValueTree::fromXml (*xml))
                if (child.hasType ("PARAM"))
                    expect (std::isfinite (static_cast<float> (child.getProperty ("value"))),
                            "repaired state saved a non-finite parameter");
        auto reloaded = std::make_unique<AcustraAudioProcessor>();
        reloaded->setStateInformation (resaved.getData(), static_cast<int> (resaved.getSize()));
        check (*reloaded);
    }

    // Clamp old choice values in floating point before rounding: finite
    // corrupt values can exceed both lround's and int's representable range.
    for (const float oldWood : { std::numeric_limits<float>::max(),
                                -std::numeric_limits<float>::max() })
    {
        juce::ValueTree old { "ACUSTRA_STATE" };
        juce::ValueTree wood { "PARAM" };
        wood.setProperty ("id", ids::bodyMaterial, nullptr);
        wood.setProperty ("value", oldWood, nullptr);
        old.appendChild (wood, nullptr);
        restore (processor, old);
        expect (valueOf (processor, ids::bodyMaterial) == (oldWood > 0.0f ? 2.0f : 0.0f),
                "an extreme finite legacy wood did not clamp before migration");
        expect (valueOf (processor, ids::room) == 0.0f
                    && valueOf (processor, ids::releaseNoise) == 0.0f,
                "repairing an old state added room or release noise");
    }
}

void testEditorRendering()
{
    auto processorOwner = std::make_unique<AcustraAudioProcessor>();
    auto& processor = *processorOwner;
    processor.prepareToPlay (sampleRate, blockSize);
    std::unique_ptr<juce::AudioProcessorEditor> editor { processor.createEditor() };
    expect (editor != nullptr, "createEditor returned null");
    if (editor == nullptr)
        return;

    {
        // The design size, or the largest the display has room for.
        const auto opening = AcustraAudioProcessorEditor::sizingFor (0, editorDisplayArea());
        expect (opening.width <= editorWidth && editor->getWidth() == opening.width
                    && editor->getHeight() == juce::roundToInt (
                        opening.width * static_cast<double> (editorHeight) / editorWidth),
                "the editor did not open at its design size or the display's room");
    }
    expect (editor->isResizable(), "the editor is not host-resizable");
    expect (editor->getNumChildComponents() >= 18,
            "the editor is missing controls or the keyboard");

    std::vector<juce::Component*> pending { editor.get() };
    std::vector<juce::TextButton*> choiceButtons;
    std::vector<juce::Slider*> sliders;
    juce::MidiKeyboardComponent* midiKeyboard = nullptr;
    juce::Label* engineStatus = nullptr;
    juce::Component* stringActivity = nullptr;
    int comboBoxCount = 0;
    while (! pending.empty())
    {
        auto* parent = pending.back();
        pending.pop_back();
        for (auto* child : parent->getChildren())
        {
            pending.push_back (child);
            if (child->getName() == "String activity")
                stringActivity = child;
            if (dynamic_cast<juce::ComboBox*> (child) != nullptr)
                ++comboBoxCount;
            if (auto* label = dynamic_cast<juce::Label*> (child);
                label != nullptr && label->getName() == "Engine status")
                engineStatus = label;
            if (auto* button = dynamic_cast<juce::TextButton*> (child);
                button != nullptr && button->getRadioGroupId() != 0)
                choiceButtons.push_back (button);
            if (auto* slider = dynamic_cast<juce::Slider*> (child))
                sliders.push_back (slider);
            if (auto* candidate = dynamic_cast<juce::MidiKeyboardComponent*> (child))
                midiKeyboard = candidate;
        }
    }

    expect (comboBoxCount == 0, "the editor still hides choices in a combo box");
    expect (sliders.size() == 9,
            "the editor does not expose every continuous sound parameter");
    for (const auto& control : std::array {
        std::pair { acustra::parameters::releaseNoise, "RELEASE NOISE" },
        std::pair { acustra::parameters::room, "ROOM" } })
    {
        const auto found = std::find_if (sliders.begin(), sliders.end(),
            [&] (const auto* slider) { return slider->getName() == control.second; });
        expect (found != sliders.end(), std::string { "missing control: " } + control.second);
        if (found == sliders.end())
            continue;
        auto& slider = **found;
        const float initial = valueOf (processor, control.first);
        expect (std::abs (slider.getValue() - initial) < 0.011
                    && slider.getWantsKeyboardFocus()
                    && slider.getDescription().isNotEmpty(),
                std::string { control.second } + " lost its current value or accessibility");
        setValue (processor, control.first, 0.0f);
        expect (slider.getValue() == 0.0,
                std::string { control.second } + " did not follow host automation");
        slider.setValue (63.2, juce::sendNotificationSync);
        expect (std::abs (valueOf (processor, control.first) - 63.2f) < 0.011f,
                std::string { control.second } + " did not update its host parameter");
        setValue (processor, control.first, initial);
    }
    expect (stringActivity != nullptr,
            "the six-string activity display is missing");
    const auto refreshDisplayTimer = [&]
    {
        expect (engineStatus != nullptr, "the visible engine status is missing");
        if (engineStatus == nullptr)
            return;
        engineStatus->setText ("timer pending", juce::dontSendNotification);
        // Observe a real display refresh, including the construction selection,
        // rather than assuming one sleep made the editor's timer due.
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            juce::Thread::sleep (20);
            juce::Timer::callPendingTimersSynchronously();
            if (engineStatus->getText() != "timer pending")
                return;
        }
        expect (false, "the editor display timer did not run");
    };
    expect (choiceButtons.size() == 21,
            "the six radio groups do not expose all 21 existing options");
    expect (std::all_of (choiceButtons.begin(), choiceButtons.end(),
                        [] (const auto* button)
                        {
                            return button->getWantsKeyboardFocus()
                                && button->isToggleable();
                        }),
            "a choice button is not keyboard-focusable and toggle-accessible");

    std::set<juce::Component*> choiceGroups;
    for (auto* button : choiceButtons)
        choiceGroups.insert (button->getParentComponent());
    expect (choiceGroups.size() == 6,
            "choice buttons are not split into six independent radio groups");
    expect (std::none_of (choiceButtons.begin(), choiceButtons.end(),
                          [] (const auto* button)
                          {
                              return button->getName().startsWith ("STRINGS")
                                  || button->getName().contains ("Cedar");
                          }),
            "the editor still offers a string material or Cedar");
    for (auto* group : choiceGroups)
    {
        const auto selected = std::count_if (
            choiceButtons.begin(), choiceButtons.end(), [group] (const auto* button)
            {
                return button->getParentComponent() == group
                    && button->getToggleState();
            });
        expect (selected == 1,
                "a choice radio group does not have exactly one selection");
    }

    const auto dreadnought = std::find_if (
        choiceButtons.begin(), choiceButtons.end(), [] (const auto* button)
        {
            return button->getName() == "BODY SHAPE: Dreadnought";
        });
    expect (dreadnought != choiceButtons.end(),
            "the Dreadnought body-shape switch is missing");
    if (dreadnought != choiceButtons.end())
    {
        setValue (processor, acustra::parameters::shape, 1.0f);
        (*dreadnought)->setToggleState (true, juce::sendNotification);
        expect (std::abs (valueOf (processor, acustra::parameters::shape) - 2.0f)
                    < 0.011f,
                "a choice button did not update its host parameter");
        setValue (processor, acustra::parameters::shape, 0.0f);
        const auto parlor = std::find_if (
            choiceButtons.begin(), choiceButtons.end(), [] (const auto* button)
            {
                return button->getName() == "BODY SHAPE: Parlor";
            });
        expect (parlor != choiceButtons.end() && (*parlor)->getToggleState(),
                "host automation did not update the visible choice selection");
    }

    const auto findChoice = [&] (const juce::String& name) -> juce::TextButton*
    {
        for (auto* button : choiceButtons)
            if (button->getName() == name)
                return button;
        expect (false, "missing choice: " + name.toStdString());
        return nullptr;
    };
    const auto choose = [&] (const juce::String& name)
    {
        if (auto* button = findChoice (name))
        {
            expect (button->getDescription().isNotEmpty(), "a setup choice lacks an accessible description");
            // Exercise the actual click handler even when reapplying a selected
            // construction. Host automation can make a preset custom between clicks.
            button->onClick();
        }
    };
    const auto selected = [&] (const juce::String& name)
    {
        auto* button = findChoice (name);
        return button != nullptr && button->getToggleState();
    };
    namespace ids = acustra::parameters;
    setValue (processor, ids::tuning, 2.0f);
    setValue (processor, ids::output, -4.0f);
    setValue (processor, ids::captureMode, 2.0f);
    struct Expected { const char* caption; acustra::BodyShape shape; acustra::BodyMaterial wood; };
    constexpr std::array presets {
        Expected { "Dreadnought", acustra::BodyShape::Dreadnought, acustra::BodyMaterial::Spruce },
        Expected { "Auditorium", acustra::BodyShape::Auditorium, acustra::BodyMaterial::Spruce },
        Expected { "Parlor", acustra::BodyShape::Parlor, acustra::BodyMaterial::Spruce } };
    // The GUITAR switch offers exactly the current construction presets, no
    // more and no fewer, and the retired Model switch stays gone.
    {
        std::vector<std::string> guitarButtons;
        for (const auto* button : choiceButtons)
            if (button->getName().startsWith ("GUITAR:"))
                guitarButtons.push_back (button->getName().toStdString());
        std::vector<std::string> expectedGuitarButtons;
        for (const auto& preset : presets)
            expectedGuitarButtons.push_back (
                (juce::String ("GUITAR: ") + preset.caption).toStdString());
        std::sort (guitarButtons.begin(), guitarButtons.end());
        std::sort (expectedGuitarButtons.begin(), expectedGuitarButtons.end());
        expect (guitarButtons == expectedGuitarButtons,
                "the GUITAR switch is not exactly the current construction presets");
    }
    expect (std::none_of (choiceButtons.begin(), choiceButtons.end(), [] (const auto* button)
            { return button->getName().startsWith ("MODEL:"); }),
            "the retired Model switch is still on the editor");
    for (const auto& preset : presets)
    {
        const auto name = juce::String ("GUITAR: ") + preset.caption;
        choose (name);
        const auto state = processor.snapshotEngineParameters();
        expect (state.shape == preset.shape && state.bodyMaterial == preset.wood
                    && state.capture == acustra::CaptureType::Piezo,
                "a construction switch missed its construction or changed capture");
        refreshDisplayTimer();
        expect (selected (name), "a construction switch lost its selection");
        expect (valueOf (processor, ids::tuning) == 2.0f
                    && std::abs (valueOf (processor, ids::output) + 4.0f) < 0.011f,
                "a guitar construction switch changed tuning or output");
    }
    setValue (processor, ids::shape, 3.0f);
    refreshDisplayTimer();
    expect (std::none_of (choiceButtons.begin(), choiceButtons.end(), [] (const auto* button)
            { return button->getName().startsWith ("GUITAR:") && button->getToggleState(); }),
            "a custom construction misleadingly shows a selected preset");
    choose ("GUITAR: Dreadnought");
    setValue (processor, ids::tuning, 0.0f);
    setValue (processor, ids::output, 0.0f);

    for (const auto& setup : std::array {
        std::pair { ids::picking, juce::StringArray { "PICKING: Finger", "PICKING: Pick", "PICKING: Thumb" } },
        std::pair { ids::captureMode, juce::StringArray { "CAPTURE: Stereo mic", "CAPTURE: Mono mic", "CAPTURE: Piezo" } } })
    {
        for (int index = 0; index < setup.second.size(); ++index)
        {
            choose (setup.second[index]);
            expect (std::abs (valueOf (processor, setup.first) - index) < 0.011f,
                    "a setup radio switch did not update its host parameter");
            setValue (processor, setup.first, static_cast<float> ((index + 1) % setup.second.size()));
            expect (selected (setup.second[(index + 1) % setup.second.size()]),
                    "host automation did not update a setup radio switch");
        }
        setValue (processor, setup.first, 0.0f);
    }
    if (auto* capture = findChoice ("CAPTURE: Mono mic"))
        expect (capture->getTooltip().contains ("ignore Stereo Width"),
                "capture switches conceal that mono mic and piezo ignore Stereo Width");

    const auto renderAt = [&] (int width, int height)
    {
        editor->setSize (width, height);
        for (auto* slider : sliders)
        {
            const auto bounds = slider->getBoundsInParent();
            expect (slider->isVisible() && slider->getWidth() >= 96
                        && slider->getHeight() >= 140
                        && editor->getLocalBounds().contains (bounds),
                    "a sound control is hidden, clipped or too small at a supported size");
            for (auto* other : sliders)
                if (other != slider)
                    expect (! bounds.intersects (other->getBoundsInParent()),
                            "sound controls overlap");
        }
        for (auto* button : choiceButtons)
        {
            const auto bounds = button->getBoundsInParent();
            expect (button->getWidth() >= 96 && button->getHeight() >= 20
                        && button->getParentComponent()->getLocalBounds().contains (bounds),
                    "a radio switch is clipped or too small at a supported size");
            for (auto* other : choiceButtons)
                if (other != button && other->getParentComponent() == button->getParentComponent())
                    expect (! bounds.intersects (other->getBoundsInParent()), "radio switches overlap");
        }
        juce::Point<int> leftCell;
        for (auto* button : choiceButtons)
            if (button->getParentComponent()->getX() < width / 3)
            {
                const juce::Point<int> size { button->getWidth(), button->getHeight() };
                if (leftCell.isOrigin())
                    leftCell = size;
                expect (size == leftCell, "left-column radio buttons do not share the same cell size");
            }
        expect (midiKeyboard != nullptr && midiKeyboard->getBottom() == height,
                "the MIDI keyboard is not anchored to the editor bottom edge");
        if (stringActivity != nullptr && midiKeyboard != nullptr)
            expect (stringActivity->getHeight() >= 130
                        && editor->getLocalBounds().contains (stringActivity->getBounds())
                        && ! stringActivity->getBounds().intersects (midiKeyboard->getBounds()),
                    "the string display is clipped or overlaps the keyboard");
        juce::Image image { juce::Image::ARGB, width, height, true };
        {
            // A Direct2D image (Windows' native type) commits its drawing
            // only when the Graphics drawing into it ends; reading pixels
            // before that reads the cleared image.
            juce::Graphics graphics { image };
            editor->paintEntireComponent (graphics, true);
        }

        std::set<juce::uint32> colours;
        bool opaque = true;
        for (int y = 4; y < height; y += 11)
            for (int x = 4; x < width; x += 11)
            {
                const auto pixel = image.getPixelAt (x, y);
                colours.insert (pixel.getARGB());
                opaque = opaque && pixel.getAlpha() >= 250;
            }
        expect (opaque, "the editor rendered transparent holes");
        expect (colours.size() > 96u,
                "the editor lost its visual panels or control detail");
        return image;
    };

    const auto path = juce::SystemStats::getEnvironmentVariable (
        "ACUSTRA_EDITOR_SNAPSHOT", {});
    const auto saveImage = [&] (const juce::Image& image, const juce::String& suffix)
    {
        if (path.isEmpty())
            return;
        const juce::File requested { path };
        const auto destination = requested.getSiblingFile (
            requested.getFileNameWithoutExtension() + suffix + requested.getFileExtension());
        destination.getParentDirectory().createDirectory();
        juce::FileOutputStream output { destination };
        juce::PNGImageFormat png;
        const bool ready = output.openedOk() && output.setPosition (0)
            && output.truncate();
        const bool written = ready && png.writeImageToStream (image, output);
        output.flush();
        expect (written, "the requested editor screenshot could not be written");
    };

    // Reload a serialized state into the open editor. Every switch, including
    // the construction shorthand, must describe the engine's restored state.
    juce::MemoryBlock initialState, parlorState;
    processor.getStateInformation (initialState);
    choose ("GUITAR: Parlor");
    choose ("PICKING: Thumb");
    choose ("CAPTURE: Mono mic");
    processor.getStateInformation (parlorState);
    choose ("GUITAR: Dreadnought");
    choose ("PICKING: Finger");
    choose ("CAPTURE: Stereo mic");
    processor.setStateInformation (parlorState.getData(), static_cast<int> (parlorState.getSize()));
    refreshDisplayTimer();
    expect (selected ("GUITAR: Parlor") && selected ("PICKING: Thumb")
                && selected ("CAPTURE: Mono mic") && selected ("BODY SHAPE: Parlor")
                && selected ("BODY MATERIAL: Spruce")
                && processor.snapshotEngineParameters().capture == acustra::CaptureType::MonoMic
                && processor.snapshotEngineParameters().shape == acustra::BodyShape::Parlor,
            "live state reload left the editor showing a different guitar or capture");
    saveImage (renderAt (editorWidth, editorHeight), "-restored-parlor-mono");
    choose ("CAPTURE: Piezo");
    saveImage (renderAt (editorMinimumWidth, editorMinimumHeight), "-parlor-piezo");
    juce::MemoryBlock loadedState;
    processor.getStateInformation (loadedState);
    choose ("CAPTURE: Stereo mic");
    processor.setStateInformation (loadedState.getData(), static_cast<int> (loadedState.getSize()));
    refreshDisplayTimer();
    expect (selected ("CAPTURE: Piezo")
                && processor.snapshotEngineParameters().capture == acustra::CaptureType::LoadedPiezo,
            "live loaded-piezo state reload left an incorrect capture switch");
    saveImage (renderAt (editorMinimumWidth, editorMinimumHeight), "-loaded-piezo");
    processor.setStateInformation (initialState.getData(), static_cast<int> (initialState.getSize()));
    refreshDisplayTimer();

    saveImage (renderAt (editorMinimumWidth, editorMinimumHeight), "-minimum");
    saveImage (renderAt (editorMaximumWidth, editorMaximumHeight), "-maximum");
    saveImage (renderAt (editorWidth, editorHeight), "");

    // A played B4 also drives the open B sympathetically. Capture both states
    // through the actual processor and display timers for visual verification.
    juce::AudioBuffer<float> sounding { 2, blockSize };
    for (int block = 0; block < 48; ++block)
    {
        juce::MidiBuffer midi;
        if (block == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 71, 0.8f), 0);
        processor.processBlock (sounding, midi);
    }
    refreshDisplayTimer();
    expect (stringActivity != nullptr
                && stringActivity->getDescription().contains ("fret 7, B4, held"),
            "the accessible string display did not follow the actual held fret");
    saveImage (renderAt (editorWidth, editorHeight), "-strings-active");
    saveImage (renderAt (editorMinimumWidth, editorMinimumHeight), "-strings-active-minimum");
    if (stringActivity != nullptr
        && juce::SystemStats::getEnvironmentVariable (
               "ACUSTRA_STRING_DISPLAY_BENCHMARK", {}) == "1")
    {
        editor->setSize (editorWidth, editorHeight);
        juce::Image image { juce::Image::ARGB, stringActivity->getWidth(),
                           stringActivity->getHeight(), true };
        std::vector<double> costs;
        for (int frame = 0; frame < 120; ++frame)
        {
            const auto begin = juce::Time::getHighResolutionTicks();
            {
                juce::Graphics graphics { image };
                stringActivity->paintEntireComponent (graphics, true);
            }
            const double micros = 1.0e6 * juce::Time::highResolutionTicksToSeconds (
                juce::Time::getHighResolutionTicks() - begin);
            if (frame >= 20)
                costs.push_back (micros);
        }
        std::sort (costs.begin(), costs.end());
        std::cout << "Acustra active string panel paint: median " << costs[50]
                  << " us, p95 " << costs[95]
                  << " us (offscreen software renderer, default size)\n";
    }

    editor.reset();
    const auto closedDisplay = processor.getStringActivityForDisplay();
    expect (processor.getActiveVoiceCount() > 0
                && std::all_of (closedDisplay.begin(), closedDisplay.end(),
                    [] (const auto& string) { return ! string.keyDown && string.level == 0.0f; }),
            "closing the editor stopped audio or retained display publication");
    processor.releaseResources();
}

void testEditorSizeFitsTheDisplayAndIsRemembered()
{
    using Editor = AcustraAudioProcessorEditor;
    namespace ids = acustra::parameters;
    const auto heightOf = [] (int width)
    {
        return juce::roundToInt (width * static_cast<double> (editorHeight) / editorWidth);
    };

    // Without a display, or on one with room, a new editor opens at its
    // design size with the usual minimum; a saved width is kept in limits.
    const juce::Rectangle<int> roomy { 0, 0, 2560, 1400 };
    for (const auto area : { juce::Rectangle<int> {}, roomy })
    {
        const auto sizing = Editor::sizingFor (0, area);
        expect (sizing.width == editorWidth && sizing.minimumWidth == editorMinimumWidth,
                "a new editor did not open at its design size where it fits");
    }
    expect (Editor::sizingFor (1300, roomy).width == 1300
                && Editor::sizingFor (99999, roomy).width == editorMaximumWidth
                && Editor::sizingFor (100, roomy).width == editorMinimumWidth,
            "a saved editor width was not kept within the editor's limits");
    // 1366 x 768 under a 40-point taskbar: the editor and its minimum shrink,
    // keeping the aspect ratio, to the room under a 48-point title bar.
    const auto laptop = Editor::sizingFor (1300, { 0, 0, 1366, 728 });
    expect (laptop.width == 777 && laptop.minimumWidth == 777
                && heightOf (laptop.width) + 48 <= 728,
            "the editor did not fit a 1366 x 768 display");
    // A 1440 x 900 MacBook Air under its menu bar has room above the
    // minimum, so only the opening size shrinks.
    const auto air = Editor::sizingFor (0, { 0, 25, 1440, 875 });
    expect (air.width == 945 && air.minimumWidth == editorMinimumWidth,
            "the editor did not fit a 1440 x 900 display");
    // Never below 0.6 of the design size, however small the display.
    const auto tiny = Editor::sizingFor (0, { 0, 0, 640, 480 });
    expect (tiny.width == 672 && tiny.minimumWidth == 672,
            "the editor shrank below 0.6 of its design size");

    // The last width travels with the state, beside the parameters: never
    // a host parameter, and absent until an editor has been resized.
    auto sourceOwner = std::make_unique<AcustraAudioProcessor>();
    auto& source = *sourceOwner;
    const auto savedXml = [] (AcustraAudioProcessor& processor)
    {
        juce::MemoryBlock bytes;
        processor.getStateInformation (bytes);
        return juce::AudioProcessor::getXmlFromBinary (
            bytes.getData(), static_cast<int> (bytes.getSize()));
    };
    {
        const auto xml = savedXml (source);
        expect (xml != nullptr && ! xml->hasAttribute ("editorWidth"),
                "a state saved before any editor opened carries a width");
    }
    const auto area = editorDisplayArea();
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor { source.createEditor() };
        const auto opening = Editor::sizingFor (0, area);
        expect (editor->getWidth() == opening.width
                    && editor->getConstrainer() != nullptr
                    && editor->getConstrainer()->getMinimumWidth() == opening.minimumWidth
                    && editor->getConstrainer()->getMinimumHeight() == heightOf (opening.minimumWidth),
                "a new editor did not open at, or limit itself to, the sizes for its display");
        // The width the editor picked for the display is nobody's choice.
        const auto xml = savedXml (source);
        expect (source.getEditorWidth() == 0 && xml != nullptr && ! xml->hasAttribute ("editorWidth"),
                "opening the editor remembered the width it picked for the display");
        editor->setSize (1280, heightOf (1280));
    }
    expect (source.getEditorWidth() == 1280, "the processor lost the editor's last width");
    juce::MemoryBlock stored;
    source.getStateInformation (stored);
    {
        const auto xml = savedXml (source);
        expect (xml != nullptr && xml->getIntAttribute ("editorWidth") == 1280
                    && xml->getChildByAttribute ("id", "editorWidth") == nullptr,
                "the editor width was not saved beside the parameters");
        expect (source.getParameters().size() == ids::parameterCount,
                "remembering the editor size added a host parameter");
    }
    auto restoredOwner = std::make_unique<AcustraAudioProcessor>();
    auto& restored = *restoredOwner;
    restored.setStateInformation (stored.getData(), static_cast<int> (stored.getSize()));
    expect (restored.getEditorWidth() == 1280
                && ! restored.parameters.state.hasProperty ("editorWidth"),
            "a reloaded state lost its editor width or kept it among the parameters");
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor { restored.createEditor() };
        const auto expected = Editor::sizingFor (1280, area).width;
        expect (editor->getWidth() == expected && editor->getHeight() == heightOf (expected),
                "a reloaded session did not reopen its editor at the size it was left");
        // Fitting a smaller display does not replace the session's width.
        expect (restored.getEditorWidth() == 1280,
                "reopening the editor replaced the session's width");
    }

    // A state without a width (saved before it was kept), or with a
    // malformed one, leaves the last width.
    const auto lastWidth = restored.getEditorWidth();
    for (const juce::var malformed : { juce::var {}, juce::var ("wide"), juce::var (-3) })
    {
        auto state = restored.parameters.copyState();
        state.setProperty ("stateVersion", 2, nullptr);
        if (! malformed.isVoid())
            state.setProperty ("editorWidth", malformed, nullptr);
        juce::MemoryBlock bytes;
        if (const auto xml = state.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, bytes);
        restored.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
        expect (restored.getEditorWidth() == lastWidth,
                "a state without a valid editor width changed the last one");
    }
}

void testUnsupportedSampleRatesAreSilent()
{
    // The engine models 8 kHz to its maximum rate and clamps any other,
    // which would play every note off pitch: there the processor stays
    // silent and the editor says so. A supported rate plays again.
    auto owner = std::make_unique<AcustraAudioProcessor>();
    auto& processor = *owner;
    const auto play = [&] (double rate)
    {
        processor.prepareToPlay (rate, blockSize);
        juce::AudioBuffer<float> audio { 2, blockSize };
        float loudest = 0.0f;
        for (int block = 0; block < 24; ++block)
        {
            juce::MidiBuffer midi;
            if (block == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 0);
            processor.processBlock (audio, midi);
            expect (finite (audio), "non-finite output at " + std::to_string (rate) + " Hz");
            loudest = std::max (loudest, peak (audio));
        }
        return loudest;
    };
    const auto maximum = acustra::AcustraEngine::maximumSupportedSampleRate;
    for (const double rate : { 8000.0, sampleRate, maximum })
        expect (play (rate) > 0.001f && processor.isSampleRateSupported(),
                "Acustra did not play at its supported " + std::to_string (rate) + " Hz");
    for (const double rate : { 7999.0, maximum + 1.0, 2.0 * maximum, 0.0 })
        expect (! (play (rate) > 0.0f) && ! processor.isSampleRateSupported(),
                "Acustra played at the unsupported " + std::to_string (rate) + " Hz");

    std::unique_ptr<juce::AudioProcessorEditor> editor { processor.createEditor() };
    auto* status = findLabel (*editor, [] (const juce::Label& label)
                                    { return label.getName() == "Engine status"; });
    expect (status != nullptr && status->getText().contains ("UNSUPPORTED RATE")
                && status->getTooltip().contains ("384 kHz"),
            "the editor did not say the host's rate is unsupported");
    play (sampleRate);
    editor.reset();
    editor.reset (processor.createEditor());
    status = findLabel (*editor, [] (const juce::Label& label)
                        { return label.getName() == "Engine status"; });
    expect (status != nullptr && status->getText().startsWith ("48.0 kHz")
                && ! status->getText().contains ("UNSUPPORTED"),
            "the editor still called a supported rate unsupported");
    editor.reset();
    processor.releaseResources();
}

void testEditorShowsTheBuildIdentity()
{
    // The version and build number come from the identity record CMake
    // freezes into every format; reading it keeps it in each binary.
    auto owner = std::make_unique<AcustraAudioProcessor>();
    std::unique_ptr<juce::AudioProcessorEditor> editor { owner->createEditor() };
    const auto* subtitle = findLabel (*editor, [] (const juce::Label& label)
                                      { return label.getText().startsWith ("PHYSICALLY MODELLED"); });
    expect (subtitle != nullptr
                && subtitle->getText().contains (
                    juce::String (JucePlugin_VersionString) + " (BUILD "),
            "the editor does not show the version and build from the identity record");
}
} // namespace

void testMpeTimbreReachesTheEngineOnMemberChannelOnly()
{
    // CC74 is MPE Timbre. On a lower-zone member channel it places this one
    // note's own pluck point (see AcustraEngine::initialisePluck); off a
    // member channel, or with no lower zone at all, it must not move a
    // single sample.
    const auto phrase = [] (int timbre, int channel, bool memberZone)
    {
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
        processor.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> audio { 2, blockSize };
        juce::MidiBuffer start;
        if (memberZone)
            addLowerZone (start, 2);
        start.addEvent (juce::MidiMessage::controllerEvent (channel, 74, timbre), 0);
        start.addEvent (juce::MidiMessage::noteOn (channel, 52, 0.8f), 1);
        std::vector<float> mono;
        const int blocks = static_cast<int> (0.5 * sampleRate / blockSize);
        for (int block = 0; block < blocks; ++block)
        {
            juce::MidiBuffer empty;
            processor.processBlock (audio, block == 0 ? start : empty);
            for (int sample = 0; sample < blockSize; ++sample)
                mono.push_back (audio.getSample (0, sample));
        }
        return mono;
    };
    const auto conventionalLow = phrase (10, 1, false);
    const auto conventionalHigh = phrase (110, 1, false);
    expect (conventionalLow == conventionalHigh,
            "CC74 moved the sound on a conventional, non-member channel");

    const auto memberLow = phrase (10, 2, true);
    const auto memberHigh = phrase (110, 2, true);
    expect (memberLow.size() == memberHigh.size() && memberLow != memberHigh,
            "CC74 did not reach the engine as a member channel's pluck point");
}

void testMpePressureReachesTheEngineOnMemberChannelOnly()
{
    // MPE channel pressure, status 0xD0, biases this note's own vibrato
    // depth (see AcustraEngine::mpePressureFor) and must be inert off a
    // member channel or with no lower zone. The phrase below therefore holds
    // CC1 up and frets the note, so the difference it asserts on comes
    // through the vibrato path alone.
    const auto phrase = [] (int pressure, int channel, bool memberZone)
    {
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
        processor.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> audio { 2, blockSize };
        juce::MidiBuffer start;
        if (memberZone)
            addLowerZone (start, 2);
        start.addEvent (juce::MidiMessage::channelPressureChange (channel, pressure), 0);
        start.addEvent (juce::MidiMessage::controllerEvent (channel, 1, 127), 1);
        start.addEvent (juce::MidiMessage::noteOn (channel, 52, 0.8f), 2);
        std::vector<float> mono;
        const int blocks = static_cast<int> (1.0 * sampleRate / blockSize);
        for (int block = 0; block < blocks; ++block)
        {
            juce::MidiBuffer empty;
            processor.processBlock (audio, block == 0 ? start : empty);
            for (int sample = 0; sample < blockSize; ++sample)
                mono.push_back (audio.getSample (0, sample));
        }
        return mono;
    };
    const auto conventionalLight = phrase (0, 1, false);
    const auto conventionalFirm = phrase (127, 1, false);
    expect (conventionalLight == conventionalFirm,
            "channel pressure moved the sound on a conventional, non-member "
            "channel");

    const auto memberLight = phrase (0, 2, true);
    const auto memberFirm = phrase (127, 2, true);
    expect (memberLight.size() == memberFirm.size() && memberLight != memberFirm,
            "channel pressure did not reach the engine on a member channel");
}

struct TimedMidi
{
    int sample { 0 };
    juce::MidiMessage message;
};

// Renders a MIDI timeline through a fresh processor and returns its stereo
// output, left channel then right.
std::vector<float> renderTimeline (const std::vector<TimedMidi>& events,
                                   int length, bool gather,
                                   int* latency = nullptr)
{
    auto processorOwner = std::make_unique<AcustraAudioProcessor>();
    auto& processor = *processorOwner;
    setValue (processor, acustra::parameters::gatherChords, gather ? 1.0f : 0.0f);
    processor.prepareToPlay (sampleRate, blockSize);
    if (latency != nullptr)
        *latency = processor.getLatencySamples();
    std::vector<float> left, right;
    juce::AudioBuffer<float> audio { 2, blockSize };
    for (int start = 0; start < length; start += blockSize)
    {
        juce::MidiBuffer midi;
        for (const auto& event : events)
            if (event.sample >= start && event.sample < start + blockSize)
                midi.addEvent (event.message, event.sample - start);
        processor.processBlock (audio, midi);
        left.insert (left.end(), audio.getReadPointer (0),
                     audio.getReadPointer (0) + blockSize);
        right.insert (right.end(), audio.getReadPointer (1),
                      audio.getReadPointer (1) + blockSize);
    }
    left.insert (left.end(), right.begin(), right.end());
    return left;
}

void testGatheredChordsVoiceLikeSequencedChords()
{
    // A keyboard hand's C major triad arriving low to high over 20 ms. Taken
    // one key at a time, C4 claims the B string's first fret before G4
    // arrives, and the engine's hand has to refret the chord under way
    // (replucking C4 on the G string); gathered, the triad reaches the
    // allocator as one wrist event, is fretted as one shape (planChord) and
    // sounds exactly as the same chord placed on one sample does.
    constexpr int onset = 1000;
    constexpr int length = 24 * blockSize;
    const std::vector<TimedMidi> rolled {
        { onset, juce::MidiMessage::noteOn (1, 60, 0.8f) },
        { onset + 480, juce::MidiMessage::noteOn (1, 64, 0.8f) },
        { onset + 960, juce::MidiMessage::noteOn (1, 67, 0.8f) }
    };
    const std::vector<TimedMidi> together {
        { onset, juce::MidiMessage::noteOn (1, 60, 0.8f) },
        { onset, juce::MidiMessage::noteOn (1, 64, 0.8f) },
        { onset, juce::MidiMessage::noteOn (1, 67, 0.8f) }
    };
    int latency = 0;
    const auto gatheredRoll = renderTimeline (rolled, length, true, &latency);
    const auto gatheredChord = renderTimeline (together, length, true);
    // The engine's own seven samples (the microphones wait out the piezo's
    // pipeline) plus the 30 ms window.
    expect (latency == 1440 + acustra::AcustraEngine::outputLatencySamples(),
            "Gather Chords did not report its 30 ms window as latency");
    {
        // Below the modelled range the engine is prepared at 8 kHz (the
        // plug-in stays silent there), and the window the player would hold
        // MIDI back for is counted at that rate: the latency reported is
        // that window, not 30 ms of the host's rate.
        auto lowRateOwner = std::make_unique<AcustraAudioProcessor>();
        setValue (*lowRateOwner, acustra::parameters::gatherChords, 1.0f);
        lowRateOwner->prepareToPlay (4000.0, blockSize);
        expect (lowRateOwner->getLatencySamples()
                    == acustra::Performer::gatherWindowSamples (8000.0)
                           + acustra::AcustraEngine::outputLatencySamples(),
                "Gather Chords reported its latency at the host's rate, not the engine's");
    }
    expect (gatheredRoll == gatheredChord,
            "a chord rolled inside the window did not sound as the same chord "
            "on one sample");
    expect (std::any_of (gatheredRoll.begin(), gatheredRoll.end(),
                         [] (float value) { return std::abs (value) > 0.001f; }),
            "the gathered chord rendered silence");
    // Without gathering the same roll sounds differently, which is what
    // the switch is for; if this ever matched, the check above proves nothing.
    expect (renderTimeline (rolled, length, false)
                != renderTimeline (together, length, false),
            "the rolled chord no longer needs gathering to voice like a chord");

    // A six-string E major rolled over 25 ms, after a D major the hand still
    // remembers: the gathered roll and the one-sample chord are the same
    // shape from the same hand.
    std::vector<TimedMidi> earlier {
        { 200, juce::MidiMessage::noteOn (1, 50, 0.7f) },
        { 200, juce::MidiMessage::noteOn (1, 57, 0.7f) },
        { 200, juce::MidiMessage::noteOn (1, 62, 0.7f) },
        { 200, juce::MidiMessage::noteOn (1, 66, 0.7f) },
        { 9000, juce::MidiMessage::noteOff (1, 50) },
        { 9000, juce::MidiMessage::noteOff (1, 57) },
        { 9000, juce::MidiMessage::noteOff (1, 62) },
        { 9000, juce::MidiMessage::noteOff (1, 66) }
    };
    constexpr std::array<int, 6> eMajor { 40, 47, 52, 56, 59, 64 };
    constexpr int later = 14000;
    auto rolledE = earlier;
    auto togetherE = earlier;
    for (std::size_t index = 0; index < eMajor.size(); ++index)
    {
        rolledE.push_back ({ later + static_cast<int> (index) * 240,
                             juce::MidiMessage::noteOn (1, eMajor[index], 0.8f) });
        togetherE.push_back ({ later,
                               juce::MidiMessage::noteOn (1, eMajor[index], 0.8f) });
    }
    constexpr int lengthE = 360 * blockSize;
    expect (renderTimeline (rolledE, lengthE, true)
                == renderTimeline (togetherE, lengthE, true),
            "a gathered six-string roll did not sound as the same chord on one "
            "sample");
}

void testGatheringOnlyDelaysNotesMeantApart()
{
    // Notes more than the window apart, a key repeated inside it, a
    // string-per-channel controller's notes, notes either side of an
    // All Notes Off and notes on two channels are not chords to gather: each
    // sounds exactly as the same timeline played ungathered with every event
    // 30 ms later.
    constexpr int length = 40 * blockSize;
    constexpr int window = 1440;
    const std::vector<std::vector<TimedMidi>> timelines {
        {
            { 300, juce::MidiMessage::noteOn (1, 48, 0.7f) },
            { 300 + 1500, juce::MidiMessage::controllerEvent (1, 1, 40) },
            { 300 + 1600, juce::MidiMessage::noteOn (1, 52, 0.6f) },
            { 300 + 2400, juce::MidiMessage::pitchWheel (1, 9000) },
            { 300 + 3300, juce::MidiMessage::noteOn (1, 55, 0.8f) },
            { 300 + 5000, juce::MidiMessage::noteOff (1, 52) }
        },
        {
            { 300, juce::MidiMessage::noteOn (1, 60, 0.8f) },
            { 300 + 240, juce::MidiMessage::noteOff (1, 60) },
            { 300 + 720, juce::MidiMessage::noteOn (1, 60, 0.8f) }
        },
        {
            // Channel 1 is the low E string, channel 2 the A string.
            { 100, juce::MidiMessage::controllerEvent (1, 126, 6) },
            { 300, juce::MidiMessage::noteOn (1, 45, 0.8f) },
            { 300 + 240, juce::MidiMessage::noteOn (2, 50, 0.8f) },
            { 300 + 480, juce::MidiMessage::noteOn (1, 47, 0.8f) },
            { 300 + 720, juce::MidiMessage::noteOn (1, 48, 0.8f) }
        },
        {
            { 300, juce::MidiMessage::noteOn (1, 60, 0.8f) },
            { 300 + 240, juce::MidiMessage::controllerEvent (1, 123, 0) },
            { 300 + 480, juce::MidiMessage::noteOn (1, 64, 0.8f) }
        },
        {
            { 300, juce::MidiMessage::noteOn (1, 60, 0.8f) },
            { 300 + 480, juce::MidiMessage::noteOn (2, 64, 0.8f) }
        }
    };
    for (std::size_t index = 0; index < timelines.size(); ++index)
    {
        auto later = timelines[index];
        for (auto& event : later)
            event.sample += window;
        const auto held = renderTimeline (timelines[index], length, true);
        expect (held == renderTimeline (later, length, false),
                "timeline " + std::to_string (index)
                    + " was regrouped rather than only delayed");
        expect (std::any_of (held.begin(), held.end(),
                             [] (float value) { return std::abs (value) > 0.001f; }),
                "timeline " + std::to_string (index) + " rendered silence");
    }
}

void testGatherSwitchAndPanicReleaseHeldNotes()
{
    auto processorOwner = std::make_unique<AcustraAudioProcessor>();
    auto& processor = *processorOwner;
    setValue (processor, acustra::parameters::gatherChords, 1.0f);
    processor.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> audio { 2, blockSize };
    juce::MidiBuffer empty;
    const auto sound = [&] (int blocks)
    {
        float loudest = 0.0f;
        for (int block = 0; block < blocks; ++block)
        {
            processor.processBlock (audio, empty);
            loudest = std::max (loudest, peak (audio));
        }
        return loudest;
    };

    // A panic before a held note comes due discards it.
    juce::MidiBuffer note;
    note.addEvent (juce::MidiMessage::noteOn (1, 52, 0.8f), 10);
    processor.processBlock (audio, note);
    expect (peak (audio) == 0.0f, "a gathered note sounded inside its window");
    processor.requestPanic();
    expect (sound (12) == 0.0f, "a panic left a held note to sound later");

    // Switching off releases what is held at once, and the latency with it.
    processor.processBlock (audio, note);
    setValue (processor, acustra::parameters::gatherChords, 0.0f);
    expect (processor.getLatencySamples()
                == acustra::AcustraEngine::outputLatencySamples(),
            "switching Gather Chords off kept its latency");
    expect (sound (2) > 0.001f,
            "switching Gather Chords off lost the note it was holding");
    juce::MidiBuffer release;
    release.addEvent (juce::MidiMessage::noteOff (1, 52), 0);
    processor.processBlock (audio, release);
    static_cast<void> (sound (100));
    expect (processor.getActiveVoiceCount() == 0,
            "a note released after Gather Chords went off stayed held");
    setValue (processor, acustra::parameters::gatherChords, 1.0f);
    expect (processor.getLatencySamples()
                == 1440 + acustra::AcustraEngine::outputLatencySamples(),
            "switching Gather Chords back on did not restore its latency");
}

void testStringPerChannelModeViaMonoModeOn()
{
    // MIDI 1.0's own Mono Mode On (CC126, value = channel count) is the
    // toggle: M=6 is the standard spelling of the one-string-per-channel
    // layout Roland's GK and Fishman's TriplePlay each produce, though
    // neither is documented to transmit this specific message itself.
    // Poly Mode On (CC127) restores the fret-distance allocator. Never
    // sending either leaves the allocator exactly as it was.
    const auto activeVoices = [] (bool sendMonoOn, bool sendPolyOnAfter,
                                  int channel, int midiNote)
    {
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
        processor.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> audio { 2, blockSize };
        juce::MidiBuffer start;
        if (sendMonoOn)
            start.addEvent (juce::MidiMessage::controllerEvent (1, 126, 6), 0);
        if (sendPolyOnAfter)
            start.addEvent (juce::MidiMessage::controllerEvent (1, 127, 0), 1);
        start.addEvent (juce::MidiMessage::noteOn (channel, midiNote, 0.8f), 2);
        processor.processBlock (audio, start);
        return processor.getActiveVoiceCount();
    };
    // Channel 6 is string index 5, the highest string; MIDI note 40 is far
    // below what any fret on it can reach, so a controller enforcing the
    // channel-to-string mapping cannot play it at all.
    expect (activeVoices (false, false, 6, 40) == 1,
            "the allocator, left alone, could not reach a low note on a high "
            "channel it is free to reassign");
    expect (activeVoices (true, false, 6, 40) == 0,
            "Mono Mode On did not force the channel's own string");
    expect (activeVoices (true, true, 6, 40) == 1,
            "Poly Mode On did not restore the fret-distance allocator");
}

// The processor is only an adapter: everything it plays, the player
// (DSP/AcustraPerformer) plays alone from the same parameters, to the bit.
void testTheAdapterPlaysExactlyThePerformer()
{
    namespace ids = acustra::parameters;
    using namespace acustra::battery;
    constexpr int block = 127; // odd, so events land everywhere in a block
    for (const auto& scenario : makeBattery())
        for (const bool gather : { false, true })
        {
            auto processorOwner = std::make_unique<AcustraAudioProcessor>();
            auto& processor = *processorOwner;
            setValue (processor, ids::gatherChords, gather ? 1.0f : 0.0f);
            processor.prepareToPlay (sampleRate, block);
            auto performer = std::make_unique<acustra::Performer>();
            performer->setParameters (processor.snapshotEngineParameters());
            performer->prepare (sampleRate, block);

            const int length = sampleAt (scenario.seconds, sampleRate);
            std::vector<bool> applied (scenario.controls.size(), false);
            juce::AudioBuffer<float> audio { 2, block };
            std::vector<float> left (block), right (block);
            bool same = true;
            for (int start = 0; start < length && same; start += block)
            {
                bool panic = false;
                for (std::size_t index = 0; index < scenario.controls.size(); ++index)
                {
                    const auto& control = scenario.controls[index];
                    if (applied[index] || sampleAt (control.seconds, sampleRate) >= start + block)
                        continue;
                    applied[index] = true;
                    using Kind = Control::Kind;
                    const char* id = control.kind == Kind::GatherChords ? ids::gatherChords
                        : control.kind == Kind::CaptureMode ? ids::captureMode
                        : control.kind == Kind::Picking ? ids::picking
                        : control.kind == Kind::Tuning ? ids::tuning
                        : control.kind == Kind::BodyAmount ? ids::bodyAmount
                        : control.kind == Kind::Output ? ids::output
                        : control.kind == Kind::Shape ? ids::shape
                        : control.kind == Kind::Wood ? ids::bodyMaterial
                        : control.kind == Kind::Width ? ids::stereoWidth
                        : control.kind == Kind::Age ? ids::stringAge
                        : control.kind == Kind::Pluck ? ids::pluckPosition
                        : control.kind == Kind::Touch ? ids::touch
                        : control.kind == Kind::PiezoMix ? ids::piezoMix
                        : control.kind == Kind::Sustain ? ids::sustain : nullptr;
                    if (id != nullptr)
                        setValue (processor, id, control.value);
                    else
                    {
                        processor.requestPanic();
                        panic = true;
                    }
                }
                juce::MidiBuffer midi;
                for (const auto& event : scenario.events)
                {
                    const int at = sampleAt (event.seconds, sampleRate);
                    if (at >= start && at < start + block)
                        midi.addEvent (event.bytes.data(), event.size,
                                       at - start + event.skew);
                }
                processor.processBlock (audio, midi);

                performer->setParameters (processor.snapshotEngineParameters());
                if (panic)
                    performer->reset();
                performer->setGatherChords (valueOf (processor, ids::gatherChords) >= 0.5f);
                performer->setSustainHold (valueOf (processor, ids::sustain) >= 0.5f);
                performer->beginBlock (left.data(), right.data(), block);
                for (const auto metadata : midi)
                    performer->handleMidi (metadata.samplePosition, metadata.data,
                                           metadata.numBytes);
                performer->endBlock();

                same = std::equal (left.begin(), left.end(), audio.getReadPointer (0))
                    && std::equal (right.begin(), right.end(), audio.getReadPointer (1))
                    && processor.getLatencySamples() == performer->latencySamples();
            }
            expect (same, std::string { "the processor and the performer diverged on " }
                              + scenario.name + (gather ? " (gathering)" : ""));
        }
}

// Main plus the optional Piezo (mono) output bus: off by default, so hosts
// and sessions that know only the stereo output see what they always did;
// when a host enables it, it carries the piezo itself, and Main is the same
// either way. A session saved with it enabled loads like any other.
void testOptionalPiezoOutputBus()
{
    namespace ids = acustra::parameters;
    {
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
        expect (processor.getBusCount (false) == 2 && processor.getBusCount (true) == 0,
                "the plug-in must offer Main plus one optional output bus");
        expect (processor.getBus (false, 0)->getName() == "Output"
                    && processor.getBus (false, 1)->getName() == "Piezo",
                "the output buses are misnamed");
        expect (processor.getBus (false, 0)->isEnabled()
                    && ! processor.getBus (false, 1)->isEnabled()
                    && processor.getTotalNumOutputChannels() == 2,
                "the Piezo bus must be off by default");
        expect (processor.getBus (false, 1)->getDefaultLayout()
                    == juce::AudioChannelSet::mono(),
                "Piezo must default to mono");

        const auto layout = [&] (juce::AudioChannelSet main, juce::AudioChannelSet piezo)
        {
            auto result = processor.getBusesLayout();
            result.outputBuses.getReference (0) = main;
            result.outputBuses.getReference (1) = piezo;
            return result;
        };
        const auto stereo = juce::AudioChannelSet::stereo();
        const auto mono = juce::AudioChannelSet::mono();
        const auto off = juce::AudioChannelSet::disabled();
        expect (processor.isBusesLayoutSupported (layout (stereo, off))
                    && processor.isBusesLayoutSupported (layout (stereo, mono)),
                "Main alone or Main with the Piezo output was refused");
        expect (! processor.isBusesLayoutSupported (layout (mono, off))
                    && ! processor.isBusesLayoutSupported (layout (off, mono))
                    && ! processor.isBusesLayoutSupported (layout (stereo, stereo)),
                "a layout with the wrong format on a bus was accepted");
        // The retired three-bus layout (Main, Mic, Piezo) is not offered.
        auto three = layout (stereo, off);
        three.outputBuses.add (mono);
        expect (! processor.isBusesLayoutSupported (three),
                "a layout with a Mic bus was accepted");
    }

    struct Render
    {
        std::vector<std::vector<float>> channels;
        juce::MemoryBlock state;
    };
    // A strummed chord and a single note over 0.5 s at odd block offsets.
    const auto play = [] (int captureChoice, bool piezo,
                          const juce::MemoryBlock* restore = nullptr)
    {
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
        if (restore != nullptr)
            processor.setStateInformation (restore->getData(),
                                           static_cast<int> (restore->getSize()));
        else
            setValue (processor, ids::captureMode, static_cast<float> (captureChoice));
        auto layout = processor.getBusesLayout();
        layout.outputBuses.getReference (1) = piezo ? juce::AudioChannelSet::mono()
                                                    : juce::AudioChannelSet::disabled();
        expect (processor.setBusesLayout (layout), "a supported layout was refused");
        processor.prepareToPlay (sampleRate, blockSize);
        const int channels = processor.getTotalNumOutputChannels();
        Render result;
        result.channels.assign (static_cast<std::size_t> (channels), {});
        juce::AudioBuffer<float> audio { channels, blockSize };
        for (int block = 0; block < 94; ++block)
        {
            juce::MidiBuffer midi;
            if (block == 0)
                for (const int note : { 40, 47, 52, 56, 59, 64 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 37);
            if (block == 40)
                midi.addEvent (juce::MidiMessage::noteOn (1, 69, 0.9f), 201);
            processor.processBlock (audio, midi);
            for (int channel = 0; channel < channels; ++channel)
                result.channels[static_cast<std::size_t> (channel)].insert (
                    result.channels[static_cast<std::size_t> (channel)].end(),
                    audio.getReadPointer (channel),
                    audio.getReadPointer (channel) + blockSize);
        }
        processor.releaseResources();
        processor.getStateInformation (result.state);
        return result;
    };
    const auto audible = [] (const std::vector<float>& channel)
    {
        return std::any_of (channel.begin(), channel.end(),
                            [] (float value) { return std::abs (value) > 1.0e-4f; });
    };

    const auto reference = play (0, true);
    for (const int capture : { 0, 1, 2 })
    {
        const auto plain = play (capture, false);
        const auto withPiezo = play (capture, true);
        expect (plain.channels.size() == 2 && withPiezo.channels.size() == 3,
                "the enabled bus did not reach processBlock's buffer");
        if (plain.channels.size() != 2 || withPiezo.channels.size() != 3
            || reference.channels.size() != 3)
            continue;
        expect (withPiezo.channels[0] == plain.channels[0]
                    && withPiezo.channels[1] == plain.channels[1],
                "enabling the Piezo output changed Main");
        expect (audible (withPiezo.channels[2]),
                "the Piezo output is silent while the guitar plays");
        if (capture == 2)
            expect (withPiezo.channels[2] == withPiezo.channels[0]
                        && withPiezo.channels[2] == withPiezo.channels[1],
                    "the Piezo bus is not Main with Capture on Piezo");
        expect (reference.channels[2] == withPiezo.channels[2],
                "the Piezo output depends on what Capture selects");

        // A session saved with the Piezo output enabled loads into a
        // processor with it off, and the other way round, and plays the same.
        const auto reloadedOff = play (0, false, &withPiezo.state);
        const auto reloadedOn = play (0, true, &plain.state);
        expect (reloadedOff.channels.size() == 2 && reloadedOn.channels.size() == 3
                    && reloadedOff.channels[0] == plain.channels[0]
                    && reloadedOff.channels[1] == plain.channels[1]
                    && reloadedOn.channels[0] == plain.channels[0]
                    && reloadedOn.channels[2] == withPiezo.channels[2],
                "a session did not reload the same across the Piezo bus's state");
    }
}

static void testStringActivityDisplay()
{
    // Observe real allocation and sympathetic motion, not a second MIDI
    // keyboard. Publishing the display must leave every audio sample intact.
    auto visible = std::make_unique<AcustraAudioProcessor>();
    auto hidden = std::make_unique<AcustraAudioProcessor>();
    for (auto* processor : { visible.get(), hidden.get() })
    {
        setValue (*processor, acustra::parameters::shape, 1.0f);
        setValue (*processor, acustra::parameters::bodyMaterial, 1.0f);
        setValue (*processor, acustra::parameters::room, 0.0f);
        processor->prepareToPlay (sampleRate, blockSize);
    }
    visible->setStringDisplayEnabled (true);
    juce::AudioBuffer<float> displayed { 2, blockSize }, undisplayed { 2, blockSize };
    const auto render = [&] (int blocks, const juce::MidiBuffer& events)
    {
        bool identical = true;
        for (int block = 0; block < blocks; ++block)
        {
            juce::MidiBuffer leftMidi, rightMidi;
            if (block == 0)
            {
                leftMidi = events;
                rightMidi = events;
            }
            visible->processBlock (displayed, leftMidi);
            hidden->processBlock (undisplayed, rightMidi);
            for (int channel = 0; channel < 2; ++channel)
                identical = identical && std::memcmp (
                    displayed.getReadPointer (channel),
                    undisplayed.getReadPointer (channel),
                    static_cast<std::size_t> (blockSize) * sizeof (float)) == 0;
        }
        expect (identical, "string display telemetry changed the audio");
    };
    juce::MidiBuffer start;
    start.addEvent (juce::MidiMessage::noteOn (1, 71, 0.72f), 0);
    render (132, start);
    auto strings = visible->getStringActivityForDisplay();
    expect (strings[5].midiNote == 71 && strings[5].fret == 7
                && strings[5].keyDown && strings[5].played && strings[5].level > 0.0f,
            "the display did not report B4 on high E fret 7");
    expect (strings[4].openMidi == 59 && ! strings[4].keyDown
                && ! strings[4].played && strings[4].level > 2.0e-7f,
            "the display omitted the sympathetically vibrating open B string");

    juce::MidiBuffer release;
    release.addEvent (juce::MidiMessage::noteOff (1, 71), 0);
    render (8, release);
    strings = visible->getStringActivityForDisplay();
    expect (! strings[5].keyDown && strings[5].level > 0.0f
                && strings[4].level > 2.0e-7f,
            "key-up cleared actual string vibration or retained the held marker");

    visible->requestPanic();
    hidden->requestPanic();
    render (1, {});
    strings = visible->getStringActivityForDisplay();
    expect (std::all_of (strings.begin(), strings.end(), [] (const auto& string)
            { return ! string.keyDown && ! string.played && string.level == 0.0f; }),
            "panic left a stale active fret or vibration in the display");

    for (auto* processor : { visible.get(), hidden.get() })
        setValue (*processor, acustra::parameters::tuning, 1.0f);
    render (12, {});
    expect (visible->getStringActivityForDisplay()[0].openMidi == 38,
            "the string display did not follow Drop D tuning");

    // A pitch above the fretted range is ignored (the range rule of
    // 2026-10-12): no string takes it and the display shows nothing for it.
    juce::MidiBuffer outside;
    outside.addEvent (juce::MidiMessage::noteOn (1, 88, 0.72f), 0);
    render (12, outside);
    strings = visible->getStringActivityForDisplay();
    expect (std::none_of (strings.begin(), strings.end(), [] (const auto& string)
            { return string.keyDown && string.midiNote == 88; }),
            "the display showed a pitch outside the tuning's range");

    visible->releaseResources();
    strings = visible->getStringActivityForDisplay();
    expect (std::all_of (strings.begin(), strings.end(), [] (const auto& string)
            { return ! string.keyDown && ! string.played && string.level == 0.0f; }),
            "stopping audio left stale string activity");
    visible->prepareToPlay (sampleRate, blockSize);
    strings = visible->getStringActivityForDisplay();
    expect (strings[0].openMidi == 38
                && std::all_of (strings.begin(), strings.end(), [] (const auto& string)
                    { return ! string.keyDown && ! string.played && string.level == 0.0f; }),
            "restarting audio restored stale notes or lost the current tuning");
    visible->setStringDisplayEnabled (false);
    visible->releaseResources();
    hidden->releaseResources();
}

// --- Sleep -----------------------------------------------------------------
// An idle instance stops rendering (AcustraAudioProcessor::updateSleep) and
// its engine is suspended (AcustraEngine::suspend). Each session drives a
// processor with Main and the Piezo bus beside a Performer that never
// sleeps, fed the same MIDI and controls block by block, as
// testTheAdapterPlaysExactlyThePerformer does.
struct SleepSession
{
    struct Event
    {
        std::int64_t sample;
        juce::MidiMessage message;
    };
    struct Control
    {
        std::int64_t sample; // applied at the start of the block holding it
        const char* id;
        float value;
    };

    std::vector<Event> events;
    std::vector<Control> controls;
    int block = 64;
    bool bypassed = false;

    // Main left, Main right and Piezo, per sample, from the processor and
    // from the never-sleeping reference; whether the processor slept after
    // each block, and the end of the block after which it first slept.
    std::array<std::vector<float>, 3> actual, reference;
    std::vector<bool> asleep;
    std::int64_t firstSleep = -1;

    void run (std::int64_t length)
    {
        namespace ids = acustra::parameters;
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
        auto layout = processor.getBusesLayout();
        layout.outputBuses.getReference (1) = juce::AudioChannelSet::mono();
        expect (processor.setBusesLayout (layout), "the Piezo bus could not be enabled");
        processor.prepareToPlay (sampleRate, block);
        auto performer = std::make_unique<acustra::Performer>();
        performer->setParameters (processor.snapshotEngineParameters());
        performer->prepare (sampleRate, block);
        performer->setTempoBpm (120.0);

        juce::AudioBuffer<float> audio { 3, block };
        std::array<std::vector<float>, 3> scratch;
        for (auto& channel : scratch)
            channel.assign (static_cast<std::size_t> (block), 0.0f);
        for (auto& channel : actual)
            channel.clear();
        for (auto& channel : reference)
            channel.clear();
        asleep.clear();
        firstSleep = -1;
        for (std::int64_t start = 0; start < length; start += block)
        {
            const int count = static_cast<int> (std::min<std::int64_t> (block, length - start));
            for (const auto& control : controls)
                if (control.sample >= start && control.sample < start + count)
                    setValue (processor, control.id, control.value);
            juce::MidiBuffer midi;
            for (const auto& event : events)
                if (event.sample >= start && event.sample < start + count)
                    midi.addEvent (event.message, static_cast<int> (event.sample - start));
            juce::AudioBuffer<float> view { audio.getArrayOfWritePointers(), 3, count };
            if (bypassed)
                processor.processBlockBypassed (view, midi);
            else
                processor.processBlock (view, midi);

            performer->setParameters (processor.snapshotEngineParameters());
            performer->setGatherChords (valueOf (processor, ids::gatherChords) >= 0.5f);
            performer->setSustainHold (valueOf (processor, ids::sustain) >= 0.5f);
            performer->setTempoBpm (120.0);
            performer->beginBlock (scratch[0].data(), scratch[1].data(),
                                   acustra::AcustraEngine::OutputBuses { scratch[2].data() },
                                   count);
            for (const auto metadata : midi)
                performer->handleMidi (metadata.samplePosition, metadata.data,
                                       metadata.numBytes);
            performer->endBlock();

            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const auto* rendered = view.getReadPointer (static_cast<int> (channel));
                actual[channel].insert (actual[channel].end(), rendered, rendered + count);
                reference[channel].insert (reference[channel].end(), scratch[channel].begin(),
                                           scratch[channel].begin() + count);
            }
            asleep.push_back (processor.isAsleep());
            if (firstSleep < 0 && processor.isAsleep())
                firstSleep = start + count;
        }
    }

    bool asleepAt (std::int64_t sample) const
    {
        const auto index = static_cast<std::size_t> (sample / block);
        return index < asleep.size() && asleep[index];
    }

    // The largest difference from the reference over [begin, end), on all
    // three outputs.
    float difference (std::int64_t begin, std::int64_t end) const
    {
        float result = 0.0f;
        for (std::size_t channel = 0; channel < 3; ++channel)
            for (auto sample = std::max<std::int64_t> (begin, 0);
                 sample < std::min<std::int64_t> (end, static_cast<std::int64_t> (actual[channel].size()));
                 ++sample)
                result = std::max (result, std::abs (actual[channel][static_cast<std::size_t> (sample)]
                                                     - reference[channel][static_cast<std::size_t> (sample)]));
        return result;
    }

    // Whether every block the processor began asleep was exact zeros on
    // all three outputs.
    bool sleptSilently() const
    {
        for (std::size_t index = 1; index < asleep.size(); ++index)
        {
            if (! asleep[index - 1] || ! asleep[index])
                continue;
            for (const auto& channel : actual)
                for (std::size_t sample = index * static_cast<std::size_t> (block);
                     sample < std::min (channel.size(), (index + 1) * static_cast<std::size_t> (block));
                     ++sample)
                    if (channel[sample] != 0.0f)
                        return false;
        }
        return true;
    }

    // The last sample whose magnitude reaches level on any output, or -1.
    static std::int64_t lastReaching (const std::array<std::vector<float>, 3>& audio, float level)
    {
        std::int64_t result = -1;
        for (const auto& channel : audio)
            for (std::size_t sample = 0; sample < channel.size(); ++sample)
                if (std::abs (channel[sample]) >= level)
                    result = std::max (result, static_cast<std::int64_t> (sample));
        return result;
    }
};

std::int64_t samplesAt (double seconds)
{
    return static_cast<std::int64_t> (std::llround (seconds * sampleRate));
}

const std::array<int, 6> sleepChord { 41, 46, 51, 56, 61, 84 };

void addChord (SleepSession& session, std::int64_t on, std::int64_t off, float velocity = 0.8f)
{
    for (const int note : sleepChord)
    {
        session.events.push_back ({ on, juce::MidiMessage::noteOn (1, note, velocity) });
        if (off >= 0)
            session.events.push_back ({ off, juce::MidiMessage::noteOff (1, note) });
    }
}

std::vector<float> monoOf (const SleepSession& session, std::int64_t begin, std::int64_t end)
{
    std::vector<float> result;
    for (auto sample = begin; sample < end; ++sample)
        result.push_back (0.5f * (session.actual[0][static_cast<std::size_t> (sample)]
                                  + session.actual[1][static_cast<std::size_t> (sample)]));
    return result;
}

void testIdleInstancesSleep()
{
    // A fresh instance with nothing played sleeps after the silent wait,
    // to the block.
    SleepSession idle;
    idle.run (samplesAt (1.0));
    expect (idle.firstSleep == samplesAt (0.5),
            "an idle instance did not sleep after half a second of silence");
    expect (idle.asleep.back() && idle.sleptSilently(),
            "a sleeping instance woke or rendered something");

    // Bypassed, it sleeps by the same rule. (A played instance:
    // testNotesAfterSleepMatchANeverSleepingInstance.)
    SleepSession bypassed;
    bypassed.bypassed = true;
    bypassed.run (samplesAt (0.6));
    expect (bypassed.firstSleep == samplesAt (0.5), "a bypassed idle instance did not sleep");
}

void testNotesAfterSleepMatchANeverSleepingInstance()
{
    // A chord and its release, then sleep, then another six-string chord
    // about a second later on a frame inside a block, at two block sizes and
    // with Gather Chords. The output falls under -120 dBFS within a few
    // seconds, but the engine suspends only once it has cleared its own
    // residue (AcustraEngine::suspend): the instance sleeps at the end of the
    // block in which the never-sleeping reference falls to exact zero. Its
    // clocks ran on, so the chord meets the hand, repeat memory and strum
    // clock where the never-sleeping instance does, and the whole run is
    // that instance's, bit for bit. (Clearing a residue under -120 dBFS
    // sooner changed such a chord by up to 6e-4.)
    for (const int block : { 61, 512 })
        for (const bool gather : { false, true })
        {
            if (gather && block != 61)
                continue;
            SleepSession session;
            session.block = block;
            if (gather)
                session.controls.push_back ({ 0, acustra::parameters::gatherChords, 1.0f });
            addChord (session, samplesAt (0.1) + 7, samplesAt (0.4));
            const auto chord = samplesAt (14.0) + 29;
            addChord (session, chord, chord + samplesAt (0.3));
            const auto end = chord + samplesAt (1.5);
            session.run (end);
            const auto label = " (block " + std::to_string (block)
                + (gather ? ", gathering" : "") + ")";
            std::array<std::vector<float>, 3> beforeChord;
            for (std::size_t channel = 0; channel < 3; ++channel)
                beforeChord[channel].assign (session.reference[channel].begin(),
                                             session.reference[channel].begin() + chord);
            const auto lastAudible = SleepSession::lastReaching (beforeChord, 1.0e-6f);
            const auto residueEnd = SleepSession::lastReaching (
                beforeChord, std::numeric_limits<float>::denorm_min()) + 1;
            expect (lastAudible > 0 && session.firstSleep >= residueEnd
                        && session.firstSleep < residueEnd + block,
                    "the instance did not sleep when its engine had cleared its residue" + label);
            expect (session.asleepAt (chord - block) && ! session.asleepAt (chord),
                    "the instance did not sleep before the chord and wake for it" + label);
            expect (session.sleptSilently(), "a sleeping instance rendered something" + label);
            // The waking block renders exact zeros up to the chord (from its
            // flushed state) and the chord at its sample.
            const auto wakeBlock = chord / block * block;
            float wakeLevel = 0.0f;
            for (const auto& channel : session.actual)
                for (auto sample = wakeBlock; sample < chord; ++sample)
                    wakeLevel = std::max (wakeLevel, std::abs (channel[static_cast<std::size_t> (sample)]));
            expect (wakeLevel == 0.0f, "the waking block sounded before its chord" + label);
            float chordLevel = 0.0f;
            for (auto sample = chord; sample < end; ++sample)
                chordLevel = std::max (chordLevel, std::abs (session.reference[0][static_cast<std::size_t> (sample)]));
            expect (chordLevel > 0.1f, "the reference chord did not sound" + label);
            const float difference = session.difference (0, end);
            expect (difference == 0.0f,
                    "a chord after sleep differed from a never-sleeping instance" + label
                    + ": " + std::to_string (difference));
            std::cout << "Acustra chord after sleep" << label << ": after a chord at 0.1 s, "
                      << "last sample >= 1e-6 at " << lastAudible / sampleRate
                      << " s, reference residue until " << residueEnd / sampleRate
                      << " s, asleep at " << session.firstSleep / sampleRate << " s; chord at "
                      << chord / sampleRate << " s, largest difference " << difference
                      << " (chord peak " << chordLevel << ")\n";
        }
}

void testEventsWhileAsleepTakeEffectAsAwake()
{
    // From silence the instance sleeps at 0.5 s. At 1 s Output and Shape
    // are automated, which wakes it for the silent wait. Asleep again, at
    // 1.6 s it is sent the sustain pedal, a bend (with its RPN 0 range) and
    // the upstroke keyswitch; none wakes it, so only the suspended engine
    // takes them. A strum at 2.2 s, released at 2.4 s, must then be an
    // upstroke, bent and held by the pedal, exactly as in the never-sleeping
    // reference.
    namespace ids = acustra::parameters;
    const auto events = [] (bool bend)
    {
        SleepSession session;
        session.block = 61;
        const auto at = samplesAt (1.6) + 3;
        session.events.push_back ({ at, juce::MidiMessage::controllerEvent (1, 64, 127) });
        session.events.push_back ({ at, juce::MidiMessage::controllerEvent (1, 101, 0) });
        session.events.push_back ({ at, juce::MidiMessage::controllerEvent (1, 100, 0) });
        session.events.push_back ({ at, juce::MidiMessage::controllerEvent (1, 6, 7) });
        if (bend)
            session.events.push_back ({ at + 11, juce::MidiMessage::pitchWheel (1, 8192 + 2048) });
        session.events.push_back ({ at + 20, juce::MidiMessage::noteOn (1, acustra::Performer::keyswitchUpstroke, 0.7f) });
        session.events.push_back ({ at + 40, juce::MidiMessage::noteOff (1, acustra::Performer::keyswitchUpstroke) });
        session.controls.push_back ({ samplesAt (1.0), ids::output, -9.0f });
        session.controls.push_back ({ samplesAt (1.0), ids::shape, 0.0f });
        addChord (session, samplesAt (2.2) + 13, samplesAt (2.4));
        session.run (samplesAt (3.5));
        return session;
    };
    const auto session = events (true);
    const auto at = samplesAt (1.6) + 3;
    expect (session.asleepAt (samplesAt (0.75)) && ! session.asleepAt (samplesAt (1.0))
                && session.asleepAt (at - session.block),
            "an automated parameter did not wake the instance for the silent wait");
    bool sleptThrough = true;
    for (auto sample = at; sample < samplesAt (2.2) - session.block; sample += session.block)
        sleptThrough = sleptThrough && session.asleepAt (sample);
    expect (sleptThrough, "a controller, bend, RPN or keyswitch woke the sleeping instance");
    const float difference = session.difference (0, samplesAt (3.5));
    expect (difference == 0.0f,
            "events sent while asleep did not take effect as on a never-sleeping instance: "
                + std::to_string (difference));
    const double rise = upperRegisterRise (monoOf (session, samplesAt (2.2), samplesAt (2.4)));
    expect (rise < 0.004, "the upstroke keyswitch sent while asleep was lost");
    float held = 0.0f;
    for (auto sample = samplesAt (3.3); sample < samplesAt (3.5); ++sample)
        held = std::max (held, std::abs (session.actual[0][static_cast<std::size_t> (sample)]));
    expect (held > 1.0e-3f && ! session.asleep.back(),
            "the pedal pressed while asleep did not hold the released chord");
    const auto unbent = events (false);
    float bendChange = 0.0f;
    for (auto sample = samplesAt (2.2); sample < samplesAt (3.5); ++sample)
        bendChange = std::max (bendChange, std::abs (session.actual[0][static_cast<std::size_t> (sample)]
                                                     - unbent.actual[0][static_cast<std::size_t> (sample)]));
    expect (bendChange > 1.0e-3f, "the bend sent while asleep changed nothing");
    std::cout << "Acustra events while asleep: largest difference " << difference
              << ", upstroke rise " << rise * 1000.0 << " ms\n";
}

void testTheFirstStrumAfterSleepIsADownstroke()
{
    // A downstroke and its return, then sleep, then a strum: a downstroke,
    // as after any long rest, with the strum clock run on through sleep.
    SleepSession session;
    session.block = 61;
    addChord (session, samplesAt (0.1) + 5, samplesAt (0.25));
    addChord (session, samplesAt (0.4) + 5, samplesAt (0.55));
    const auto strum = samplesAt (16.0) + 17;
    addChord (session, strum, strum + samplesAt (0.2));
    session.run (strum + samplesAt (0.5));
    expect (session.firstSleep > 0 && session.asleepAt (strum - session.block),
            "the instance did not sleep before the strum");
    const double down = upperRegisterRise (monoOf (session, samplesAt (0.1), samplesAt (0.25)));
    const double up = upperRegisterRise (monoOf (session, samplesAt (0.4), samplesAt (0.55)));
    const double after = upperRegisterRise (monoOf (session, strum, strum + samplesAt (0.2)));
    expect (down >= 0.006 && up < 0.5 * down, "the strums before sleep did not alternate");
    expect (after >= 0.006 && after > 2.0 * up, "the first strum after sleep was not a downstroke");
    const float difference = session.difference (0, strum + samplesAt (0.5));
    expect (difference == 0.0f, "the strum after sleep differed from a never-sleeping instance: "
                                    + std::to_string (difference));
    std::cout << "Acustra strum after sleep: down " << down * 1000.0 << " ms, up "
              << up * 1000.0 << " ms, after sleep " << after * 1000.0
              << " ms; largest difference " << difference << '\n';
}

void testSleepWaitsForEverythingThatCanSound()
{
    namespace ids = acustra::parameters;
    // A held key, even one the bridge hand has silenced.
    {
        SleepSession session;
        session.events.push_back ({ samplesAt (0.05), juce::MidiMessage::controllerEvent (1, 2, 127) });
        session.events.push_back ({ samplesAt (0.1), juce::MidiMessage::noteOn (1, 64, 0.7f) });
        session.run (samplesAt (8.0));
        expect (session.firstSleep < 0, "the instance slept while a key was held");
    }
    // A released note held by the pedal or the Sustain switch: no sleep while
    // it rings, and sleep after it is let go. Held to 16 s, past the 14.5 s
    // at which the same note left alone puts the instance to sleep.
    for (const bool pedal : { true, false })
    {
        SleepSession session;
        if (pedal)
        {
            session.events.push_back ({ samplesAt (0.05), juce::MidiMessage::controllerEvent (1, 64, 127) });
            session.events.push_back ({ samplesAt (16.0), juce::MidiMessage::controllerEvent (1, 64, 0) });
        }
        else
        {
            session.controls.push_back ({ 0, ids::sustain, 1.0f });
            session.controls.push_back ({ samplesAt (16.0), ids::sustain, 0.0f });
        }
        session.events.push_back ({ samplesAt (0.1), juce::MidiMessage::noteOn (1, 64, 0.7f) });
        session.events.push_back ({ samplesAt (0.3), juce::MidiMessage::noteOff (1, 64) });
        session.run (samplesAt (19.0));
        const std::string label = pedal ? "the sustain pedal" : "the Sustain switch";
        expect (session.firstSleep > samplesAt (16.0),
                "the instance slept while " + label + " held a ringing note");
        expect (session.asleep.back(), "the instance did not sleep after " + label + " let go");
    }
    // Gather Chords: a chord arriving at the end of a block is still held
    // when the block ends, and the instance is awake for it.
    {
        SleepSession session;
        session.controls.push_back ({ 0, ids::gatherChords, 1.0f });
        const auto chord = samplesAt (1.0) / session.block * session.block + session.block - 3;
        addChord (session, chord, chord + samplesAt (0.3));
        session.run (samplesAt (1.5));
        expect (session.asleepAt (chord - session.block) && ! session.asleepAt (chord),
                "the instance slept while Gather Chords held notes");
        expect (session.difference (0, samplesAt (1.5)) == 0.0f,
                "a gathered chord after sleep differed from a never-sleeping instance");
    }
}

void testSleepThroughResetsPanicAndStateLoads()
{
    // Whatever happens to a sleeping instance, the next note sounds; the
    // displays show the idle strings while it sleeps.
    juce::MemoryBlock state;
    {
        AcustraAudioProcessor source;
        source.getStateInformation (state);
    }
    for (int action = 0; action < 6; ++action)
    {
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
        processor.prepareToPlay (sampleRate, blockSize);
        processor.setStringDisplayEnabled (true);
        juce::AudioBuffer<float> audio { 2, blockSize };
        juce::MidiBuffer empty;
        for (int block = 0; block * blockSize < samplesAt (0.6); ++block)
            processor.processBlock (audio, empty);
        expect (processor.isAsleep(), "an idle instance did not sleep");
        const auto idle = processor.getStringActivityForDisplay();
        expect (processor.getActiveVoiceCount() == 0 && processor.getSympatheticStringCount() == 0
                    && std::none_of (idle.begin(), idle.end(), [] (const auto& string)
                           { return string.played || string.keyDown || string.level != 0.0f; }),
                "a sleeping instance showed string activity");

        juce::MidiBuffer events;
        const char* name = "";
        switch (action)
        {
            case 0: name = "host reset"; processor.reset(); break;
            case 1: name = "Panic"; processor.requestPanic(); break;
            case 2: name = "prepareToPlay"; processor.prepareToPlay (sampleRate, blockSize); break;
            case 3: name = "state load";
                processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
                break;
            case 4: name = "All Sound Off";
                events.addEvent (juce::MidiMessage::allSoundOff (1), 0); break;
            default: name = "All Notes Off";
                events.addEvent (juce::MidiMessage::allNotesOff (1), 0); break;
        }
        processor.processBlock (audio, events);
        juce::MidiBuffer note;
        note.addEvent (juce::MidiMessage::noteOn (1, 71, 0.8f), 100);
        processor.processBlock (audio, note);
        float level = audio.getMagnitude (0, 100, blockSize - 100);
        // Past the display's 30 Hz publication after the action's reset.
        for (int block = 0; block < 8; ++block)
        {
            processor.processBlock (audio, empty);
            level = std::max (level, audio.getMagnitude (0, 0, blockSize));
        }
        const auto strings = processor.getStringActivityForDisplay();
        expect (! processor.isAsleep() && level > 1.0e-3f && processor.getActiveVoiceCount() == 1
                    && strings[5].keyDown && strings[5].fret == 7,
                std::string { "a note after " } + name + " while asleep did not sound or show: level "
                    + std::to_string (level) + ", strings " + std::to_string (processor.getActiveVoiceCount()));
    }

    // The editor opened on a sleeping instance shows it ready and idle.
    auto processorOwner = std::make_unique<AcustraAudioProcessor>();
    auto& processor = *processorOwner;
    processor.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> audio { 2, blockSize };
    juce::MidiBuffer empty;
    for (int block = 0; block * blockSize < samplesAt (0.6); ++block)
        processor.processBlock (audio, empty);
    std::unique_ptr<juce::AudioProcessorEditor> editor { processor.createEditor() };
    auto* status = editor != nullptr
        ? findLabel (*editor, [] (const juce::Label& label) { return label.getName() == "Engine status"; })
        : nullptr;
    expect (processor.isAsleep() && status != nullptr, "the sleeping instance's editor has no status");
    if (status != nullptr)
    {
        status->setText ("timer pending", juce::dontSendNotification);
        for (int attempt = 0; attempt < 50 && status->getText() == "timer pending"; ++attempt)
        {
            juce::Thread::sleep (20);
            juce::Timer::callPendingTimersSynchronously();
        }
        expect (status->getText().endsWith ("0 STRINGS"),
                "the editor of a sleeping instance did not show it ready and idle: "
                    + status->getText().toStdString());
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
        expect (image.isValid(), "the editor of a sleeping instance did not paint");
    }
}

void testRearPolarityControls()
{
    auto processor = std::make_unique<AcustraAudioProcessor>();
    processor->prepareToPlay (48000.0, 64);
    std::unique_ptr<juce::AudioProcessorEditor> editor { processor->createEditor() };
    juce::TextButton* back = nullptr;
    juce::TextButton* polarity = nullptr;
    juce::Label* indicator = nullptr;
    for (auto* child : editor->getChildren())
    {
        if (child->getName() == "Back settings") back = dynamic_cast<juce::TextButton*> (child);
        if (child->getName() == "Invert piezo blend") polarity = dynamic_cast<juce::TextButton*> (child);
        if (child->getName() == "Piezo polarity indicator") indicator = dynamic_cast<juce::Label*> (child);
    }
    expect (back && polarity && indicator, "rear polarity controls are missing");
    if (! back || ! polarity || ! indicator) return;
    expect (! polarity->isVisible() && indicator->getText() == "NORMAL",
            "polarity is not rear-only with a normal default indicator");
    back->setToggleState (true, juce::dontSendNotification);
    back->onClick();
    expect (polarity->isVisible() && indicator->isVisible() && back->getButtonText() == "FRONT",
            "Back did not expose the rear controls and return action");
    expect (polarity->getWantsKeyboardFocus() && polarity->getDescription().contains ("microphones"),
            "polarity control lost keyboard access or blend description");
    polarity->setToggleState (true, juce::sendNotificationSync);
    expect (processor->snapshotEngineParameters().piezoPolarityInverted
                && indicator->getText() == "INVERTED", "rear switch/indicator missed the host parameter");
    setValue (*processor, acustra::parameters::piezoPolarityInverted, 0.0f);
    expect (! polarity->getToggleState() && indicator->getText() == "NORMAL",
            "rear polarity did not follow host automation");
    for (const int width : {672, 896, 1120, 1456})
    {
        editor->setSize (width, juce::roundToInt (width * 980.0 / 1120.0));
        std::vector<juce::Component*> visible;
        for (auto* child : editor->getChildren())
        {
            if (! child->isVisible() || child->getName().contains ("resizer")) continue;
            if (dynamic_cast<juce::TextButton*> (child) || dynamic_cast<juce::Slider*> (child)
                || child == indicator)
            {
                expect (editor->getLocalBounds().contains (child->getBounds()),
                        "a rear control extends beyond the panel");
                visible.push_back (child);
            }
        }
        for (std::size_t a = 0; a < visible.size(); ++a)
            for (std::size_t b = a + 1; b < visible.size(); ++b)
                expect (! visible[a]->getBounds().intersects (visible[b]->getBounds()),
                        "rear controls or polarity indicator overlap");
        expect (editor->createComponentSnapshot (editor->getLocalBounds()).isValid(),
                "rear settings did not render at a supported size");
    }
    back->setToggleState (false, juce::dontSendNotification);
    back->onClick();
    expect (! polarity->isVisible() && ! indicator->isVisible(), "Front left rear controls visible");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    testParameterContract();
    testProcessorContractAndSampleAccurateMidi();
    testOptionalPiezoOutputBus();
    testSameSampleChordOrderIsCanonical();
    testBypassAndHostResetKeepNotesCoherent();
    testSameSampleNoteOnOffDoesNotStick();
    testSameSampleChordsAreStrummedAndAlternate();
    testRepeatedHeldChordsKeepTheirAudibleSweep();
    testGatheredChordsVoiceLikeSequencedChords();
    testGatheringOnlyDelaysNotesMeantApart();
    testGatherSwitchAndPanicReleaseHeldNotes();
    testBridgeHandControllerReachesTheEngine();
    testTheModulationWheelReachesTheEngineAsVibrato();
    testMpeTimbreReachesTheEngineOnMemberChannelOnly();
    testMpePressureReachesTheEngineOnMemberChannelOnly();
    testStringPerChannelModeViaMonoModeOn();
    testTheAdapterPlaysExactlyThePerformer();
    testReleaseGesturesAndRetiredCc68();
    testResetAllControllersReleasesSustain();
    testSustainSwitchHoldsReleasedNotes();
    testMemberChannelOwnershipAndControllers();
    testMemberPitchBendDoesNotLeakChannels();
    testRpnPitchRangesAndSelectionState();
    testLowerZoneLifecycleAndControllerBoundaries();
    testControllerResetSoundOffAndUiPanic();
    testStateRoundTripAndMigration();
    testMalformedStateParameters();
    testStringActivityDisplay();
    testEditorRendering();
    testRearPolarityControls();
    testEditorSizeFitsTheDisplayAndIsRemembered();
    testUnsupportedSampleRatesAreSilent();
    testEditorShowsTheBuildIdentity();
    testIdleInstancesSleep();
    testNotesAfterSleepMatchANeverSleepingInstance();
    testEventsWhileAsleepTakeEffectAsAwake();
    testTheFirstStrumAfterSleepIsADownstroke();
    testSleepWaitsForEverythingThatCanSound();
    testSleepThroughResetsPanicAndStateLoads();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " Acustra processor contract test(s) failed\n";
        return 1;
    }

    std::cout << "All Acustra processor contract tests passed\n";
    return 0;
}
