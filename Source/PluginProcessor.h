#pragma once

#include <JuceHeader.h>

#include "DSP/AcustraPerformer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace acustra::parameters
{
inline constexpr auto shape = "shape";
inline constexpr auto bodyMaterial = "bodyMaterial";
inline constexpr auto tuning = "tuning";
inline constexpr auto stringAge = "stringAge";
inline constexpr auto pluckPosition = "pluckPosition";
inline constexpr auto touch = "touch";
inline constexpr auto bodyAmount = "bodyAmount";
inline constexpr auto stereoWidth = "stereoWidth";
inline constexpr auto output = "output";
inline constexpr auto capture = "capture";
inline constexpr auto picking = "picking";
inline constexpr auto upperMic = "upperMic";
inline constexpr auto piezoLoading = "piezoLoading";

inline constexpr auto captureMode = "captureMode";
// Retired on 2026-10-10 with the second guitar model; kept for host identity.
inline constexpr auto guitarModel = "guitarModel";
inline constexpr auto gatherChords = "gatherChords";
inline constexpr auto piezoMix = "piezoMix";
inline constexpr auto releaseNoise = "releaseNoise";
inline constexpr auto room = "room";
inline constexpr auto sustain = "sustain";

inline constexpr int activeParameterCount = 16;
// Four non-automatable host slots remain solely for old session identity.
inline constexpr int parameterCount = activeParameterCount + 4;
} // namespace acustra::parameters

class AcustraAudioProcessorEditor;

class AcustraAudioProcessor final
    : public juce::AudioProcessor,
      private juce::AudioProcessorValueTreeState::Listener
{
public:
    AcustraAudioProcessor();
    ~AcustraAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool supportsMPE() const override { return true; }
    double getTailLengthSeconds() const override { return maximumTailLengthSeconds; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destinationData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void requestPanic() noexcept;

    [[nodiscard]] acustra::EngineParameters snapshotEngineParameters() const noexcept;
    [[nodiscard]] int getActiveVoiceCount() const noexcept
    {
        return activeVoiceCount.load (std::memory_order_relaxed);
    }
    [[nodiscard]] int getSympatheticStringCount() const noexcept
    {
        return sympatheticStringCount.load (std::memory_order_relaxed);
    }
    // Whether the engine sleeps (see processBlock). For tests, read between
    // audio callbacks on the thread that makes them.
    [[nodiscard]] bool isAsleep() const noexcept
    {
        return performer.engine().isSuspended();
    }
    [[nodiscard]] double getCurrentSampleRateForDisplay() const noexcept
    {
        return displaySampleRate.load (std::memory_order_relaxed);
    }
    [[nodiscard]] bool isEngineReady() const noexcept
    {
        return engineReady.load (std::memory_order_acquire);
    }
    // False after a prepareToPlay at a rate the engine does not model
    // (8 kHz to AcustraEngine::maximumSupportedSampleRate). It would run at
    // another rate and play every note off pitch, so the processor stays
    // silent.
    [[nodiscard]] bool isSampleRateSupported() const noexcept
    {
        return sampleRateSupported.load (std::memory_order_relaxed);
    }
    // The width an editor was last resized to, saved with the state (not a
    // parameter, so hosts never automate it) for the next editor to open
    // at; 0 until one is resized or a state carrying a width is loaded.
    [[nodiscard]] int getEditorWidth() const noexcept
    {
        return editorWidth.load (std::memory_order_relaxed);
    }
    void setEditorWidth (int width) noexcept
    {
        editorWidth.store (width, std::memory_order_relaxed);
    }
    // The editor subscribes only while open. Reads never touch the live
    // engine: each string's assignment and activity travel in one atomic.
    void setStringDisplayEnabled (bool enabled) noexcept;
    [[nodiscard]] std::array<acustra::AcustraEngine::StringActivity,
                            acustra::AcustraEngine::stringCount>
        getStringActivityForDisplay() const noexcept;

    juce::AudioProcessorValueTreeState parameters;
    juce::MidiKeyboardState keyboardState;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    static constexpr double maximumTailLengthSeconds = 30.0;
    // Output bus indices: 0 is Main (Capture-selected, stereo), 1 the
    // optional mono Piezo.
    static constexpr int piezoBus = 1;

    void parameterChanged (const juce::String& parameterID,
                           float newValue) override;
    void updateEngineParameters() noexcept;
    void updateSleep (const juce::AudioBuffer<float>& buffer, const float* piezo,
                      int lastStrike) noexcept;
    void publishStringActivity (std::uint32_t revision) noexcept;

    std::array<std::atomic<float>*, acustra::parameters::activeParameterCount>
        parameterPointers {};
    // Everything between MIDI and the engine: see DSP/AcustraPerformer.h.
    acustra::Performer performer;
    // The performer's Gather Chords window, for the latency the listener
    // reports from the message thread.
    std::atomic<int> gatherWindowSamples { 0 };
    std::atomic<bool> panicRequested { false };
    std::atomic<bool> engineReady { false };
    std::atomic<bool> sampleRateSupported { true };
    std::atomic<int> editorWidth { 0 };
    std::atomic<int> activeVoiceCount { 0 };
    std::atomic<int> sympatheticStringCount { 0 };
    std::atomic<double> displaySampleRate { 0.0 };
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free,
                   "String display publication must never lock the audio thread");
    std::array<std::atomic<std::uint64_t>, acustra::AcustraEngine::stringCount>
        stringDisplayActivity {};
    std::atomic<bool> stringDisplayEnabled { false };
    std::atomic<std::uint32_t> stringDisplayRevision { 1 };
    std::atomic<std::uint32_t> stringDisplayPublishedRevision { 0 };
    // These fields belong exclusively to prepare/process/release.
    std::uint32_t audioStringDisplayRevision { 0 };
    int stringDisplayIntervalSamples { 1600 };
    int stringDisplaySamplesUntilUpdate { 0 };
    // Sleep (processBlock): the controls the engine last took, how long the
    // instrument has been silent and idle, and how long since a key was
    // struck, against the two waits counted at the engine's rate.
    acustra::EngineParameters appliedParameters {};
    int silentSamples { 0 };
    std::int64_t samplesSinceNoteOn { 0 };
    int sleepAfterSamples { 24000 };
    std::int64_t strumRestSamples { 96000 };
    // Opens MIDI inputs in the desktop Standalone; null in every other
    // wrapper (PluginProcessor.cpp).
    class StandaloneMidiInputs;
    std::unique_ptr<StandaloneMidiInputs> standaloneMidiInputs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AcustraAudioProcessor)
};
