#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "PerformanceBattery.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;
constexpr int editorWidth = 1120;
constexpr int editorHeight = 800;
constexpr int editorMinimumWidth = 896;
constexpr int editorMinimumHeight = 640;
constexpr int editorMaximumWidth = 1456;
constexpr int editorMaximumHeight = 1040;

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
        ids::gatherChords, ids::piezoMix
    };
    static_assert (ids::parameterCount == 17,
                   "String Material and Bridge Model are gone; Piezo Mix is appended");
    // Piezo Mix defaults to 0, so Main is the microphones alone.
    constexpr std::array<float, ids::parameterCount> expectedDefaults {
        2.0f, 0.0f, 0.0f, 15.0f, 28.0f, 58.0f, 82.0f, 62.0f, -7.5f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
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
    const auto* model = dynamic_cast<const juce::AudioParameterChoice*> (
        processor.parameters.getParameter (ids::guitarModel));
    expect (model != nullptr && model->getVersionHint() == 7
                && model->getParameterIndex() == 14
                && model->choices == juce::StringArray { "Original", "Bellido 1978" },
            "Guitar Model must append the two measured-body choices with AU version hint 7");
    if (auto* modelParameter = processor.parameters.getParameter (ids::guitarModel))
    {
        // What a host's automation lane or normalised snapshot holds: every
        // packaged build stores Original as 0 and Bellido as 1.
        const auto* choice = dynamic_cast<const juce::AudioParameterChoice*> (modelParameter);
        modelParameter->setValueNotifyingHost (1.0f);
        const bool bellido = choice != nullptr && choice->getIndex() == 1;
        modelParameter->setValueNotifyingHost (0.0f);
        expect (bellido && choice->getIndex() == 0,
                "Guitar Model's normalised 0 and 1 must stay Original and Bellido 1978");
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
    expect (processor.snapshotEngineParameters().piezoMix == 0.0f,
            "a new session mixes the piezo into Main");

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
    setValue (processor, ids::guitarModel, 1.0f);
    setValue (processor, ids::piezoMix, 37.0f);
    const auto engine = processor.snapshotEngineParameters();
    expect (engine.shape == acustra::BodyShape::Jumbo
                && engine.bodyMaterial == acustra::BodyMaterial::Maple
                && engine.tuning == acustra::Tuning::Dadgad
                && engine.capture == acustra::CaptureType::Piezo
                && engine.picking == acustra::PickingTechnique::Thumb
                && engine.guitarModel == acustra::GuitarModel::Bellido1978,
            "choice parameters did not reach the engine snapshot");
    expect (std::abs (engine.stringAge - 0.73f) < 0.002f
                && std::abs (engine.pluckPosition - 0.41f) < 0.002f
                && std::abs (engine.touch - 0.19f) < 0.002f
                && std::abs (engine.bodyAmount - 0.66f) < 0.002f
                && std::abs (engine.stereoWidth - 0.35f) < 0.002f
                && std::abs (engine.piezoMix - 0.37f) < 0.002f,
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
        // The odd-length group ends on a downstroke: the next stroke must
        // restart down after the long rest, overriding its pending upstroke.
        downs.push_back (upperRegisterDelay (0.5));
        ups.push_back (upperRegisterDelay (0.5));
        downAgains.push_back (upperRegisterDelay (2.5));
    }
    const double down = median (downs);
    const double up = median (ups);
    const double downAgain = median (downAgains);
    const double restarted = median ({ downs.begin() + 1, downs.end() });
    // Medians allow the existing measured stroke-speed and string-level
    // variation. At the shipping defaults they are 13/0/12/13/0 ms; the
    // direction margins are several analysis hops, not threshold rounding.
    // Off-tree wrapper mutations disabling alternation or the rest reset
    // must fail these audible checks; no golden audio is required.
    expect (downs.front() > 0.006,
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

void testReleaseVelocityAndCc68ChangeNothing()
{
    // A key-up damps its note however fast it is lifted, and CC68, MIDI's
    // Legato Footswitch, is not read: legato was removed everywhere at the
    // user's request (Docs/decisions.md, 2026-09-28). Every release
    // encoding, with or without the footswitch down, renders the same wave.
    const auto phrase = [] (const juce::MidiMessage& off, bool footswitch = false)
    {
        auto processorOwner = std::make_unique<AcustraAudioProcessor>();
        auto& processor = *processorOwner;
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
    expect (plain == sixtyFour && plain == zeroOn && plain == fast,
            "release velocity re-excited or changed the damped note");
    expect (plain == footswitch, "CC68 changed the performance");
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
                                ids::guitarModel, ids::gatherChords, ids::piezoMix })
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

    constexpr std::array models { acustra::GuitarModel::Original,
        acustra::GuitarModel::Bellido1978 };
    for (std::size_t index = 0; index < models.size(); ++index)
    {
        setValue (source, ids::guitarModel, static_cast<float> (index));
        source.getStateInformation (stored);
        restored.setStateInformation (stored.getData(), static_cast<int> (stored.getSize()));
        expect (restored.snapshotEngineParameters().guitarModel == models[index],
                "a measured guitar model did not survive save/reload");
    }
    // A session saved with a retired model (2-4: the Washburn, Santa Cruz and
    // Martin) reloads as Original, not clamped onto the Bellido.
    for (const float retired : { 2.0f, 3.0f, 4.0f })
    {
        auto old = source.parameters.copyState();
        for (auto child : old)
            if (child.getProperty ("id").toString() == ids::guitarModel)
                child.setProperty ("value", retired, nullptr);
        juce::MemoryBlock bytes;
        if (const auto xml = old.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, bytes);
        // Start from the Bellido saved above, so the reload has to move it.
        restored.setStateInformation (stored.getData(), static_cast<int> (stored.getSize()));
        restored.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
        expect (restored.snapshotEngineParameters().guitarModel == acustra::GuitarModel::Original
                    && valueOf (restored, ids::guitarModel) == 0.0f,
                "a retired guitar model did not reload as Original");
    }
    auto versionSix = source.parameters.copyState();
    for (int child = versionSix.getNumChildren(); --child >= 0;)
        if (versionSix.getChild (child).getProperty ("id").toString() == ids::guitarModel)
            versionSix.removeChild (child, nullptr);
    if (const auto xml = versionSix.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, stored);
    restored.setStateInformation (stored.getData(), static_cast<int> (stored.getSize()));
    expect (restored.snapshotEngineParameters().guitarModel == acustra::GuitarModel::Original
                && valueOf (restored, ids::captureMode) == valueOf (source, ids::captureMode),
            "a pre-model state must default to Original without changing its modern capture");

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
                && std::abs (valueOf (restored, ids::output) + 7.5f) < 0.011f
                && valueOf (restored, ids::capture) == 0.0f
                && valueOf (restored, ids::picking) == 0.0f
                && valueOf (restored, ids::piezoMix) == 0.0f
                && valueOf (restored, ids::upperMic) == 0.0f
                && valueOf (restored, ids::piezoLoading) == 0.0f
                && valueOf (restored, ids::captureMode) == 0.0f
                && valueOf (restored, ids::guitarModel) == 0.0f,
            "parameters absent from an old state did not receive defaults");
    expect (restored.snapshotEngineParameters().piezoMix == 0.0f,
            "a session saved before Piezo Mix existed mixes the piezo into Main");

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

    const char garbage[] = "not an Acustra state";
    restored.setStateInformation (garbage, static_cast<int> (sizeof garbage));
    expect (valueOf (restored, ids::shape) == 1.0f,
            "invalid host state was applied instead of ignored");
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

    expect (editor->getWidth() == editorWidth && editor->getHeight() == editorHeight,
            "the editor did not open at its documented design size");
    expect (editor->isResizable(), "the editor is not host-resizable");
    expect (editor->getNumChildComponents() >= 18,
            "the editor is missing controls or the keyboard");

    std::vector<juce::Component*> pending { editor.get() };
    std::vector<juce::TextButton*> choiceButtons;
    juce::MidiKeyboardComponent* midiKeyboard = nullptr;
    juce::Label* engineStatus = nullptr;
    std::vector<juce::ComboBox*> setupMenus;
    while (! pending.empty())
    {
        auto* parent = pending.back();
        pending.pop_back();
        for (auto* child : parent->getChildren())
        {
            pending.push_back (child);
            if (auto* menu = dynamic_cast<juce::ComboBox*> (child))
                setupMenus.push_back (menu);
            if (auto* label = dynamic_cast<juce::Label*> (child);
                label != nullptr && label->getName() == "Engine status")
                engineStatus = label;
            if (auto* button = dynamic_cast<juce::TextButton*> (child);
                button != nullptr && button->getRadioGroupId() != 0)
                choiceButtons.push_back (button);
            if (auto* candidate = dynamic_cast<juce::MidiKeyboardComponent*> (child))
                midiKeyboard = candidate;
        }
    }

    expect (setupMenus.size() == 4,
            "the guitar, model, picking and capture setup menus are missing");
    const auto refreshDisplayTimer = [&]
    {
        expect (engineStatus != nullptr, "the visible engine status is missing");
        if (engineStatus == nullptr)
            return;
        engineStatus->setText ("timer pending", juce::dontSendNotification);
        // TimerThread may already be waiting for an older queued callback.
        // Keep ComboBox notifications queued until an observed display refresh,
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
    expect (choiceButtons.size() == 12,
            "the three compact choices do not expose all 12 options");
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
    expect (choiceGroups.size() == 3,
            "choice buttons are not split into three radio groups");
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

    for (auto* menu : setupMenus)
    {
        namespace ids = acustra::parameters;
        expect (menu->getWantsKeyboardFocus() && menu->getDescription().isNotEmpty(),
                "a setup menu lacks keyboard focus or an accessible description");
        if (menu->getName() == "GUITAR")
        {
            // Real ComboBox selections notify asynchronously. A due status
            // timer must preserve the pending preset until its callback runs.
            menu->setSelectedId (5, juce::sendNotificationAsync);
            refreshDisplayTimer();
            expect (menu->getSelectedId() == 5,
                    "a display timer discarded the pending guitar preset");
            // Drain the pending notification through a different synchronous
            // selection before testing the resulting construction below.
            menu->setSelectedId (2, juce::sendNotificationSync);
            setValue (processor, ids::tuning, 2.0f);
            setValue (processor, ids::output, -4.0f);
            setValue (processor, ids::captureMode, 2.0f);
            expect (menu->getNumItems() == 5 && menu->getItemText (4) == "Bellido 1978",
                    "the guitar menu does not hold Custom, the three style presets and the Bellido");
            expect (! menu->getTooltip().contains ("Fylde") && ! menu->getTooltip().contains ("nylon"),
                    "the guitar menu still describes a retired bridge or string choice");
            struct Expected { int id; acustra::BodyShape shape; acustra::BodyMaterial wood;
                              acustra::GuitarModel model; };
            constexpr std::array presets {
                Expected { 2, acustra::BodyShape::Dreadnought, acustra::BodyMaterial::Spruce,
                           acustra::GuitarModel::Original },
                Expected { 3, acustra::BodyShape::Auditorium, acustra::BodyMaterial::Spruce,
                           acustra::GuitarModel::Original },
                Expected { 4, acustra::BodyShape::Parlor, acustra::BodyMaterial::Spruce,
                           acustra::GuitarModel::Original },
                Expected { 5, acustra::BodyShape::Auditorium, acustra::BodyMaterial::Mahogany,
                           acustra::GuitarModel::Bellido1978 } };
            for (const auto& preset : presets)
            {
                menu->setSelectedId (preset.id, juce::sendNotificationSync);
                const auto state = processor.snapshotEngineParameters();
                expect (state.shape == preset.shape && state.bodyMaterial == preset.wood
                            && state.guitarModel == preset.model
                            && state.capture == acustra::CaptureType::Piezo,
                        "guitar preset " + std::to_string (preset.id)
                            + " missed its construction or changed capture");
                refreshDisplayTimer();
                expect (menu->getSelectedId() == preset.id,
                        "guitar preset " + std::to_string (preset.id) + " lost its caption");
                expect (engineStatus != nullptr && engineStatus->getText().contains ("kHz"),
                        "a guitar preset did not show normal status");
            }
            expect (valueOf (processor, ids::tuning) == 2.0f
                        && std::abs (valueOf (processor, ids::output) + 4.0f) < 0.011f,
                    "a guitar construction preset changed tuning or output");
            menu->setSelectedId (2, juce::sendNotificationSync);
            expect (processor.snapshotEngineParameters().guitarModel == acustra::GuitarModel::Original,
                    "an original construction preset retained a measured body override");
            setValue (processor, ids::tuning, 0.0f);
            setValue (processor, ids::output, -7.5f);
        }
        else if (menu->getName() == "MODEL")
        {
            expect (menu->getNumItems() == 2 && menu->getItemText (0) == "Original"
                        && menu->getItemText (1) == "Bellido 1978",
                    "the model menu does not expose the two supported bodies");
            expect (! menu->getTooltip().contains ("Martin")
                        && ! menu->getTooltip().contains ("Washburn"),
                    "the model menu still describes a retired body");
            menu->setSelectedItemIndex (1, juce::sendNotificationSync);
            expect (processor.snapshotEngineParameters().guitarModel == acustra::GuitarModel::Bellido1978,
                    "the Bellido model menu selection did not reach the engine");
            setValue (processor, ids::guitarModel, 1.0f);
            expect (menu->getSelectedId() == 2,
                    "host body-model automation did not update the menu");
            setValue (processor, ids::guitarModel, 0.0f);
        }
        else
        {
            const bool captureMenu = menu->getName() == "CAPTURE";
            const auto* id = captureMenu ? ids::captureMode : ids::picking;
            menu->setSelectedItemIndex (2, juce::sendNotificationSync);
            expect (std::abs (valueOf (processor, id) - 2.0f) < 0.011f,
                    "a setup menu did not update its host parameter");
            if (captureMenu)
            {
                expect (menu->getNumItems() == 3 && menu->getItemText (0) == "Stereo mic"
                            && menu->getItemText (1) == "Mono mic" && menu->getItemText (2) == "Piezo",
                        "the Capture menu must expose only stereo mic, mono mic and piezo");
                expect (menu->getTooltip().contains ("ignore Stereo Width"),
                        "the capture menu conceals that mono mic and piezo ignore Stereo Width");
                menu->setSelectedId (2, juce::sendNotificationAsync);
                refreshDisplayTimer();
                expect (menu->getSelectedId() == 2,
                        "a display timer discarded the pending mono-mic selection");
                menu->setSelectedId (3, juce::sendNotificationSync);
                menu->setSelectedId (2, juce::sendNotificationSync);
                expect (processor.snapshotEngineParameters().capture == acustra::CaptureType::MonoMic,
                        "the mono microphone menu selection did not reach the engine");
                setValue (processor, ids::captureMode, 2.0f);
                expect (menu->getSelectedId() == 3,
                        "host capture automation did not update the menu");
            }
            setValue (processor, id, 0.0f);
            expect (menu->getSelectedItemIndex() == 0,
                    "host automation did not update the setup menu");
        }
    }

    const auto renderAt = [&] (int width, int height)
    {
        editor->setSize (width, height);
        for (auto* menu : setupMenus)
            expect (menu->getWidth() >= 175 && menu->getHeight() >= 32
                        && editor->getLocalBounds().contains (menu->getBounds()),
                    "a setup menu is clipped or too small at a supported size");
        expect (midiKeyboard != nullptr && midiKeyboard->getBottom() == height,
                "the MIDI keyboard is not anchored to the editor bottom edge");
        juce::Image image { juce::Image::ARGB, width, height, true };
        juce::Graphics graphics { image };
        editor->paintEntireComponent (graphics, true);

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

    const auto findMenu = [&] (const char* name) -> juce::ComboBox*
    {
        for (auto* menu : setupMenus)
            if (menu->getName() == name)
                return menu;
        return nullptr;
    };
    auto* guitarMenu = findMenu ("GUITAR");
    auto* pickingMenu = findMenu ("PICKING");
    auto* captureMenu = findMenu ("CAPTURE");
    auto* modelMenu = findMenu ("MODEL");
    if (guitarMenu != nullptr && pickingMenu != nullptr && captureMenu != nullptr
        && modelMenu != nullptr)
    {
        // Reload an actual serialized state into an already-open editor. The
        // composite capture menu, preset caption and radio groups must all agree
        // with the restored construction, not retain the intervening controls.
        juce::MemoryBlock initialState, bellidoState;
        processor.getStateInformation (initialState);
        guitarMenu->setSelectedId (5, juce::sendNotificationSync);
        pickingMenu->setSelectedId (3, juce::sendNotificationSync);
        captureMenu->setSelectedId (2, juce::sendNotificationSync);
        processor.getStateInformation (bellidoState);
        guitarMenu->setSelectedId (2, juce::sendNotificationSync);
        pickingMenu->setSelectedId (1, juce::sendNotificationSync);
        captureMenu->setSelectedId (1, juce::sendNotificationSync);
        processor.setStateInformation (bellidoState.getData(),
                                      static_cast<int> (bellidoState.getSize()));
        refreshDisplayTimer();
        const auto mahogany = std::find_if (choiceButtons.begin(), choiceButtons.end(),
            [] (const auto* button) { return button->getName() == "BODY MATERIAL: Mahogany"; });
        expect (guitarMenu->getSelectedId() == 5 && pickingMenu->getSelectedId() == 3
                    && modelMenu->getSelectedId() == 2
                    && captureMenu->getSelectedId() == 2 && captureMenu->isItemEnabled (3)
                    && mahogany != choiceButtons.end() && (*mahogany)->getToggleState()
                    && processor.snapshotEngineParameters().capture == acustra::CaptureType::MonoMic
                    && processor.snapshotEngineParameters().guitarModel == acustra::GuitarModel::Bellido1978,
                "live state reload left the editor showing a different guitar or capture");
        saveImage (renderAt (editorWidth, editorHeight), "-restored-bellido-mono");

        setValue (processor, acustra::parameters::captureMode, 2.0f);
        refreshDisplayTimer();
        saveImage (renderAt (editorMinimumWidth, editorMinimumHeight), "-bellido-piezo");
        captureMenu->setSelectedId (3, juce::sendNotificationSync);
        juce::MemoryBlock loadedState;
        processor.getStateInformation (loadedState);
        captureMenu->setSelectedId (1, juce::sendNotificationSync);
        processor.setStateInformation (loadedState.getData(),
                                      static_cast<int> (loadedState.getSize()));
        refreshDisplayTimer();
        expect (captureMenu->getSelectedId() == 3 && captureMenu->isItemEnabled (3)
                    && processor.snapshotEngineParameters().capture == acustra::CaptureType::LoadedPiezo,
                "live loaded-piezo state reload left an incorrect capture in the editor");
        saveImage (renderAt (editorMinimumWidth, editorMinimumHeight), "-loaded-piezo");
        processor.setStateInformation (initialState.getData(),
                                      static_cast<int> (initialState.getSize()));
        refreshDisplayTimer();
    }

    saveImage (renderAt (editorMinimumWidth, editorMinimumHeight), "-minimum");
    saveImage (renderAt (editorMaximumWidth, editorMaximumHeight), "-maximum");
    saveImage (renderAt (editorWidth, editorHeight), "");

    editor.reset();
    processor.releaseResources();
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
    expect (latency == 1440,
            "Gather Chords did not report its 30 ms window as latency");
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
    expect (processor.getLatencySamples() == 0,
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
    expect (processor.getLatencySamples() == 1440,
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
                        : control.kind == Kind::Model ? ids::guitarModel
                        : control.kind == Kind::Width ? ids::stereoWidth
                        : control.kind == Kind::Age ? ids::stringAge
                        : control.kind == Kind::Pluck ? ids::pluckPosition
                        : control.kind == Kind::Touch ? ids::touch
                        : control.kind == Kind::PiezoMix ? ids::piezoMix : nullptr;
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

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    testParameterContract();
    testProcessorContractAndSampleAccurateMidi();
    testOptionalPiezoOutputBus();
    testSameSampleChordOrderIsCanonical();
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
    testReleaseVelocityAndCc68ChangeNothing();
    testResetAllControllersReleasesSustain();
    testMemberChannelOwnershipAndControllers();
    testMemberPitchBendDoesNotLeakChannels();
    testRpnPitchRangesAndSelectionState();
    testLowerZoneLifecycleAndControllerBoundaries();
    testControllerResetSoundOffAndUiPanic();
    testStateRoundTripAndMigration();
    testEditorRendering();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " Acustra processor contract test(s) failed\n";
        return 1;
    }

    std::cout << "All Acustra processor contract tests passed\n";
    return 0;
}
