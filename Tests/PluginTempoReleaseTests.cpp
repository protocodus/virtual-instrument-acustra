#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int totalSamples = 20000;
constexpr int onsetSample = 17;
constexpr int noteOffSample = 6161;
constexpr int note = 69; // A stopped note makes physical damping audible.
int failures = 0;

void expect (bool condition, const std::string& message)
{
    if (! condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct HostState
{
    bool attached { true };
    bool hasPosition { true };
    juce::Optional<double> bpm { 120.0 };
};

class MockPlayHead final : public juce::AudioPlayHead
{
public:
    HostState state;
    bool insideCallback { false };
    mutable int queries { 0 };
    mutable int outsideCallbackQueries { 0 };

    juce::Optional<PositionInfo> getPosition() const override
    {
        ++queries;
        if (! insideCallback)
            ++outsideCallbackQueries;
        if (! state.hasPosition)
            return {};
        PositionInfo result;
        result.setBpm (state.bpm);
        // A stopped transport still supplies a valid tempo.
        result.setIsPlaying (false);
        return result;
    }
};

struct RenderSettings
{
    HostState initial;
    double expectedBpm { 120.0 };
    bool sendsNoteOff { true };
    int repeatSample { -1 };
    int changeSample { -1 }; // Exactly a block boundary.
    HostState changed;
    double changedExpectedBpm { 120.0 };
};

struct Rendered
{
    std::vector<float> audio;
    std::vector<float> directReference;
};

void append (std::vector<float>& destination, const float* left,
             const float* right, int count)
{
    for (int sample = 0; sample < count; ++sample)
    {
        destination.push_back (left[sample]);
        destination.push_back (right[sample]);
    }
}

Rendered renderAudio (const RenderSettings& settings, int blockSize)
{
    MockPlayHead playHead;
    playHead.state = settings.initial;
    auto processor = std::make_unique<AcustraAudioProcessor>();
    processor->setPlayHead (settings.initial.attached ? &playHead : nullptr);
    for (const auto* id : { acustra::parameters::room,
                           acustra::parameters::releaseNoise })
    {
        auto* parameter = processor->parameters.getParameter (id);
        expect (parameter != nullptr, "missing dry-render parameter");
        if (parameter != nullptr)
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (0.0f));
    }
    processor->prepareToPlay (sampleRate, blockSize);
    expect (playHead.queries == 0, "prepare queried a host playhead outside processBlock");
    expect (processor->getLatencySamples() == acustra::AcustraEngine::outputLatencySamples(),
            "tempo release joining added reported attack latency");

    auto reference = std::make_unique<acustra::Performer>();
    reference->setParameters (processor->snapshotEngineParameters());
    reference->prepare (sampleRate, blockSize);
    reference->setTempoBpm (120.0);

    Rendered result;
    result.audio.reserve (totalSamples * 2);
    result.directReference.reserve (totalSamples * 2);
    double expectedBpm = settings.expectedBpm;
    int expectedQueries = 0;
    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const int count = std::min (blockSize, totalSamples - start);
        if (start == settings.changeSample)
        {
            playHead.state = settings.changed;
            processor->setPlayHead (playHead.state.attached ? &playHead : nullptr);
            expectedBpm = settings.changedExpectedBpm;
        }
        juce::MidiBuffer midi;
        const auto event = [&](int at, const juce::MidiMessage& message)
        {
            if (at >= start && at < start + count)
                midi.addEvent (message, at - start);
        };
        event (onsetSample, juce::MidiMessage::noteOn (1, note, 0.72f));
        if (settings.sendsNoteOff)
            event (noteOffSample, juce::MidiMessage::noteOff (
                1, note, static_cast<juce::uint8> (64)));
        if (settings.repeatSample >= 0)
            event (settings.repeatSample, juce::MidiMessage::noteOn (1, note, 0.61f));

        juce::AudioBuffer<float> audio (2, count);
        playHead.insideCallback = true;
        processor->processBlock (audio, midi);
        playHead.insideCallback = false;
        if (playHead.state.attached)
            ++expectedQueries;

        std::vector<float> left (static_cast<std::size_t> (count));
        std::vector<float> right (static_cast<std::size_t> (count));
        reference->setTempoBpm (expectedBpm);
        reference->beginBlock (left.data(), right.data(), count);
        for (const auto metadata : midi)
            reference->handleMidi (metadata.samplePosition, metadata.data,
                                   metadata.numBytes);
        reference->endBlock();
        append (result.audio, audio.getReadPointer (0), audio.getReadPointer (1), count);
        append (result.directReference, left.data(), right.data(), count);
    }
    expect (playHead.queries == expectedQueries, "host BPM was not queried exactly once per ready block");
    expect (playHead.outsideCallbackQueries == 0, "host queried outside an audio callback");
    processor->releaseResources();
    expect (playHead.queries == expectedQueries, "releaseResources queried the host playhead");
    expect (std::all_of (result.audio.begin(), result.audio.end(),
                        [] (float sample) { return std::isfinite (sample); }),
            "host-tempo render produced non-finite samples");
    expect (std::any_of (result.audio.begin(), result.audio.end(),
                        [] (float sample) { return std::abs (sample) > 1.0e-6f; }),
            "host-tempo render was silent");
    expect (result.audio == result.directReference,
            "JUCE host tempo/MIDI render differs from directly configured Performer");
    return result;
}

int firstDifference (const Rendered& left, const Rendered& right)
{
    const auto mismatch = std::mismatch (left.audio.begin(), left.audio.end(), right.audio.begin());
    return mismatch.first == left.audio.end() ? -1
        : static_cast<int> (mismatch.first - left.audio.begin()) / 2;
}

int graceSamples (double bpm)
{
    return static_cast<int> (std::ceil (0.125 * 60.0 * sampleRate / bpm));
}

void checkDeadline (const Rendered& released, const Rendered& held,
                    int inclusiveEndSample, const std::string& description)
{
    const int earliestOutputChange = inclusiveEndSample + 1
        + acustra::AcustraEngine::outputLatencySamples();
    const int changed = firstDifference (released, held);
    expect (changed >= earliestOutputChange,
            description + " damped before the inclusive release-joining deadline");
    // The bridge/body can reveal damping only after causal propagation.
    // This bound is independent of host block size and allows that response.
    expect (changed >= 0 && changed <= earliestOutputChange + 512,
            description + " did not begin damping promptly after its deadline");
}

void testHostTempoAndInclusiveDeadline()
{
    for (const int blockSize : { 37, 257 })
    {
        RenderSettings heldSettings;
        heldSettings.sendsNoteOff = false;
        const auto held = renderAudio (heldSettings, blockSize);
        for (const double bpm : { 74.0, 120.0 })
        {
            RenderSettings settings;
            settings.initial.bpm = bpm;
            settings.expectedBpm = bpm;
            const auto released = renderAudio (settings, blockSize);
            checkDeadline (released, held, noteOffSample + graceSamples (bpm),
                           std::to_string (bpm) + "BPM / block" + std::to_string (blockSize));
            settings.sendsNoteOff = false;
            expect (renderAudio (settings, blockSize).audio == held.audio,
                    "host tempo changed a held note or its attack timing");
        }
        const int firstAudible = static_cast<int> (std::find_if (
            held.audio.begin(), held.audio.end(),
            [] (float sample) { return std::abs (sample) > 1.0e-12f; }) - held.audio.begin()) / 2;
        expect (firstAudible >= onsetSample && firstAudible < onsetSample + 64,
                "release joining delayed or anticipated the MIDI attack");
    }
}

void testMissingAndInvalidHostTempo()
{
    constexpr int blockSize = 257;
    const auto valid120 = renderAudio ({}, blockSize);
    std::vector<HostState> missing { { false, false, {} }, { true, false, {} },
                                   { true, true, {} } };
    for (const double bpm : { 0.0, -74.0, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity() })
        missing.push_back ({ true, true, bpm });
    for (const auto& state : missing)
    {
        RenderSettings settings;
        settings.initial = state;
        expect (renderAudio (settings, blockSize).audio == valid120.audio,
                "missing/invalid host tempo did not fall back to120BPM");
        settings.initial = { true, true, 74.0 };
        settings.expectedBpm = 74.0;
        settings.changeSample = 4 * blockSize; // Before NoteOff; valid tempo must not stick.
        settings.changed = state;
        settings.changedExpectedBpm = 120.0;
        expect (renderAudio (settings, blockSize).audio == valid120.audio,
                "missing/invalid host tempo retained the previous valid BPM");
    }
}

void testTempoChangeWhileReleaseIsPending()
{
    constexpr int blockSize = 257;
    RenderSettings heldSettings;
    heldSettings.sendsNoteOff = false;
    const auto held = renderAudio (heldSettings, blockSize);
    for (const auto& tempos : { std::pair { 74.0, 120.0 }, std::pair { 120.0, 74.0 } })
    {
        RenderSettings settings;
        settings.initial.bpm = tempos.first;
        settings.expectedBpm = tempos.first;
        settings.changeSample = (noteOffSample / blockSize + 5) * blockSize;
        settings.changed.bpm = tempos.second;
        settings.changedExpectedBpm = tempos.second;
        const double elapsedBeats = (settings.changeSample - noteOffSample)
            * tempos.first / (60.0 * sampleRate);
        const int remainingSamples = static_cast<int> (std::ceil (
            (0.125 - elapsedBeats) * 60.0 * sampleRate / tempos.second));
        const auto released = renderAudio (settings, blockSize);
        checkDeadline (released, held, settings.changeSample + remainingSamples,
                       "tempo change while release is pending");
    }
}

void testRepeatAtDeadlineKeepsTheStringRinging()
{
    for (const double bpm : { 74.0, 120.0 })
    {
        RenderSettings settings;
        settings.initial.bpm = bpm;
        settings.expectedBpm = bpm;
        settings.repeatSample = noteOffSample + graceSamples (bpm);
        const auto joined = renderAudio (settings, 257);
        settings.sendsNoteOff = false;
        expect (joined.audio == renderAudio (settings, 257).audio,
                "a repeat on the inclusive deadline did not match the still-vibrating string");
    }
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    testHostTempoAndInclusiveDeadline();
    testMissingAndInvalidHostTempo();
    testTempoChangeWhileReleaseIsPending();
    testRepeatAtDeadlineKeepsTheStringRinging();
    if (failures != 0)
    {
        std::cerr << failures << " Acustra host-tempo release test(s) failed\n";
        return 1;
    }
    std::cout << "All Acustra host-tempo release tests passed\n";
    return 0;
}
