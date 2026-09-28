#pragma once

#include <JuceHeader.h>

#include "DSP/AcustraPerformer.h"

#include <array>
#include <atomic>

namespace acustra::parameters
{
inline constexpr auto shape = "shape";
inline constexpr auto bodyMaterial = "bodyMaterial";
inline constexpr auto stringMaterial = "stringMaterial";
inline constexpr auto tuning = "tuning";
inline constexpr auto stringAge = "stringAge";
inline constexpr auto pluckPosition = "pluckPosition";
inline constexpr auto touch = "touch";
inline constexpr auto bodyAmount = "bodyAmount";
inline constexpr auto stereoWidth = "stereoWidth";
inline constexpr auto output = "output";
inline constexpr auto capture = "capture";
inline constexpr auto picking = "picking";
inline constexpr auto bridgeModel = "bridgeModel";
inline constexpr auto upperMic = "upperMic";
inline constexpr auto piezoLoading = "piezoLoading";

inline constexpr auto captureMode = "captureMode";
inline constexpr auto guitarModel = "guitarModel";
inline constexpr auto gatherChords = "gatherChords";

inline constexpr int parameterCount = 18;
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
    [[nodiscard]] double getCurrentSampleRateForDisplay() const noexcept
    {
        return displaySampleRate.load (std::memory_order_relaxed);
    }
    [[nodiscard]] bool isEngineReady() const noexcept
    {
        return engineReady.load (std::memory_order_acquire);
    }

    juce::AudioProcessorValueTreeState parameters;
    juce::MidiKeyboardState keyboardState;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    static constexpr double maximumTailLengthSeconds = 30.0;

    void parameterChanged (const juce::String& parameterID,
                           float newValue) override;
    void updateEngineParameters() noexcept;

    std::array<std::atomic<float>*, acustra::parameters::parameterCount>
        parameterPointers {};
    // Everything between MIDI and the engine: see DSP/AcustraPerformer.h.
    acustra::Performer performer;
    // The performer's Gather Chords window, for the latency the listener
    // reports from the message thread.
    std::atomic<int> gatherWindowSamples { 0 };
    std::atomic<bool> panicRequested { false };
    std::atomic<bool> engineReady { false };
    std::atomic<int> activeVoiceCount { 0 };
    std::atomic<int> sympatheticStringCount { 0 };
    std::atomic<double> displaySampleRate { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AcustraAudioProcessor)
};
