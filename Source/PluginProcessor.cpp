#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace
{
namespace ids = acustra::parameters;

enum ParameterSlot
{
    slotShape = 0,
    slotBodyMaterial,
    slotTuning,
    slotStringAge,
    slotPluckPosition,
    slotTouch,
    slotBodyAmount,
    slotStereoWidth,
    slotOutput,
    slotCapture,
    slotPicking,
    slotUpperMic,
    slotPiezoLoading,
    slotCaptureMode,
    slotGuitarModel,
    slotGatherChords,
    slotPiezoMix,
    slotCount
};

static_assert (static_cast<int> (slotCount) == ids::parameterCount);

constexpr std::array<const char*, slotCount> parameterIds {
    ids::shape,
    ids::bodyMaterial,
    ids::tuning,
    ids::stringAge,
    ids::pluckPosition,
    ids::touch,
    ids::bodyAmount,
    ids::stereoWidth,
    ids::output,
    ids::capture,
    ids::picking,
    ids::upperMic,
    ids::piezoLoading,
    ids::captureMode,
    ids::guitarModel,
    ids::gatherChords,
    ids::piezoMix
};

std::unique_ptr<juce::RangedAudioParameter> makePercentParameter (
    const juce::String& id, const juce::String& name, float defaultValue,
    int versionHint = 1)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, versionHint }, name,
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, defaultValue,
        juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (value, 1);
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                return text.retainCharacters ("0123456789.-").getFloatValue();
            }));
}

bool containsParameterState (const juce::ValueTree& state,
                             const juce::String& parameterId)
{
    static const juce::Identifier parameterType { "PARAM" };
    static const juce::Identifier idProperty { "id" };

    for (const auto& child : state)
        if (child.hasType (parameterType)
            && child.getProperty (idProperty).toString() == parameterId)
            return true;

    return false;
}

void addMissingParameterDefaults (
    juce::ValueTree& state, juce::AudioProcessorValueTreeState& parameters,
    const juce::Array<juce::AudioProcessorParameter*>& hostParameters)
{
    static const juce::Identifier parameterType { "PARAM" };
    static const juce::Identifier idProperty { "id" };
    static const juce::Identifier valueProperty { "value" };

    for (const auto* hostParameter : hostParameters)
    {
        const auto* ranged =
            dynamic_cast<const juce::RangedAudioParameter*> (hostParameter);
        if (ranged == nullptr
            || parameters.getParameter (ranged->paramID) == nullptr
            || containsParameterState (state, ranged->paramID))
            continue;

        juce::ValueTree parameterState { parameterType };
        parameterState.setProperty (idProperty, ranged->paramID, nullptr);
        const float value = ranged->convertFrom0to1 (ranged->getDefaultValue());
        parameterState.setProperty (valueProperty, value, nullptr);
        state.appendChild (parameterState, nullptr);
    }
}

// Saved states carry this in their root; one without it predates the
// 2026-09-29 simplification (four Body Materials, the String Material and
// Bridge Model parameters).
const juce::Identifier stateVersionProperty { "stateVersion" };
constexpr int currentStateVersion = 2;

// A state saved before the simplification: the String Material and Bridge
// Model it may hold are dropped (every session now plays steel strings on
// the Original bridge), and its Body Material, from Spruce, Cedar, Mahogany,
// Maple, moves to Spruce, Mahogany, Maple, Cedar to Mahogany, the nearest of
// the three kept directions.
void migrateVersionOneState (juce::ValueTree& state)
{
    static const juce::Identifier parameterType { "PARAM" };
    static const juce::Identifier idProperty { "id" };
    static const juce::Identifier valueProperty { "value" };
    for (int index = state.getNumChildren(); --index >= 0;)
    {
        auto child = state.getChild (index);
        if (! child.hasType (parameterType))
            continue;
        const auto id = child.getProperty (idProperty).toString();
        if (id == "stringMaterial" || id == "bridgeModel")
            state.removeChild (index, nullptr);
        else if (id == ids::bodyMaterial)
        {
            const auto value = static_cast<float> (child.getProperty (valueProperty));
            const int old = std::isfinite (value)
                ? std::clamp (static_cast<int> (std::lround (value)), 0, 3) : 0;
            constexpr std::array<float, 4> migrated { 0.0f, 1.0f, 1.0f, 2.0f };
            child.setProperty (valueProperty,
                               migrated[static_cast<std::size_t> (old)], nullptr);
        }
    }
}

template <typename Enum>
Enum choiceValue (float value, int maximumIndex) noexcept
{
    return static_cast<Enum> (
        std::clamp (static_cast<int> (std::lround (value)), 0, maximumIndex));
}
} // namespace

AcustraAudioProcessor::AcustraAudioProcessor()
    // Main follows the Capture selector, as it always has. The optional
    // Piezo bus is the under-saddle piezo itself, whatever Capture selects:
    // a host that enables it gets it in the same pass (see
    // acustra::AcustraEngine::OutputBuses). Disabled by default, so a
    // session and a host that know only the stereo output are unchanged.
    : AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
          .withOutput ("Piezo", juce::AudioChannelSet::mono(), false)),
      parameters (*this, nullptr, "ACUSTRA_STATE", createParameterLayout())
{
    for (std::size_t slot = 0; slot < parameterPointers.size(); ++slot)
    {
        parameterPointers[slot] = parameters.getRawParameterValue (parameterIds[slot]);
        jassert (parameterPointers[slot] != nullptr);
    }
    parameters.addParameterListener (ids::gatherChords, this);
}

AcustraAudioProcessor::~AcustraAudioProcessor()
{
    parameters.removeParameterListener (ids::gatherChords, this);
}

void AcustraAudioProcessor::parameterChanged (const juce::String& parameterID,
                                              float newValue)
{
    // Holding MIDI back is latency; the host compensates what it is told.
    if (parameterID == ids::gatherChords)
        setLatencySamples (newValue >= 0.5f
            ? gatherWindowSamples.load (std::memory_order_relaxed) : 0);
}

juce::AudioProcessorValueTreeState::ParameterLayout
AcustraAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> result;
    result.reserve (ids::parameterCount);

    result.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::shape, 1 }, "Shape",
        juce::StringArray { "Parlor", "Auditorium", "Dreadnought", "Jumbo" },
        2));
    result.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::bodyMaterial, 1 }, "Body Material",
        juce::StringArray { "Spruce", "Mahogany", "Maple" }, 0));
    result.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::tuning, 1 }, "Tuning",
        juce::StringArray {
            "Standard", "Drop D", "DADGAD", "Open G", "Half-step down"
        }, 0));

    result.push_back (makePercentParameter (ids::stringAge, "String Age", 15.0f));
    result.push_back (makePercentParameter (
        ids::pluckPosition, "Pluck Position", 28.0f));
    result.push_back (makePercentParameter (ids::touch, "Touch", 58.0f));
    result.push_back (makePercentParameter (ids::bodyAmount, "Body Amount", 82.0f));
    result.push_back (makePercentParameter (
        ids::stereoWidth, "Stereo Width", 62.0f));

    result.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ids::output, 1 }, "Output",
        juce::NormalisableRange<float> { -24.0f, 6.0f, 0.1f }, -7.5f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (value, 1);
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                return text.retainCharacters ("0123456789.-").getFloatValue();
            })));

    // Later controls are appended, with a later AU version hint.
    result.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::capture, 2 }, "Legacy capture",
        juce::StringArray { "Stereo mics", "Treble mic", "Bass mic",
                            "Saddle piezo", "Magnetic (steel)" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    result.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::picking, 2 }, "Picking",
        juce::StringArray { "Finger", "Pick", "Thumb" }, 0));
    // Retain old parameter IDs, indices and ranges for saved-state migration.
    // Only the appended three-choice Capture parameter drives new sessions.
    result.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::upperMic, 4 }, "Legacy upper mic", false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));
    result.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::piezoLoading, 5 }, "Legacy piezo loading", false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));
    result.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::captureMode, 6 }, "Capture",
        juce::StringArray { "Stereo mic", "Mono mic", "Piezo" }, 0));
    // Its choices 2-4, the Washburn 1897, Santa Cruz OM 2022 and Martin D18V
    // 2007, were fitted from measurements with no redistribution license and
    // are retired; setStateInformation moves a state that chose one to Original.
    // The ID and version stayed when the list went from five choices to two,
    // so host automation and normalised snapshots from a five-choice build
    // would read differently; no pushed or packaged build had five (Docs/
    // decisions.md, 2026-09-25). Every packaged build stores Bellido as
    // normalised 1.0, so the list must stay exactly these two, in this order.
    result.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::guitarModel, 7 }, "Guitar Model",
        juce::StringArray { "Original", "Bellido 1978" }, 0));
    // Off by default: gathering holds every note back (see acustra::Performer), so
    // it changes the plug-in's latency and is not automatable.
    result.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::gatherChords, 8 }, "Gather Chords", false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));
    // Off by default: a session saved before it existed gets the default
    // (addMissingParameterDefaults) and so sounds as it did.
    result.push_back (makePercentParameter (ids::piezoMix, "Piezo Mix", 0.0f, 9));

    return { result.begin(), result.end() };
}

acustra::EngineParameters
AcustraAudioProcessor::snapshotEngineParameters() const noexcept
{
    const auto value = [this] (ParameterSlot slot)
    {
        const auto* pointer = parameterPointers[static_cast<std::size_t> (slot)];
        return pointer != nullptr ? pointer->load (std::memory_order_relaxed) : 0.0f;
    };

    acustra::EngineParameters result;
    result.shape = choiceValue<acustra::BodyShape> (value (slotShape), 3);
    result.bodyMaterial = choiceValue<acustra::BodyMaterial> (
        value (slotBodyMaterial), 2);
    result.tuning = choiceValue<acustra::Tuning> (value (slotTuning), 4);
    result.stringAge = 0.01f * value (slotStringAge);
    result.pluckPosition = 0.01f * value (slotPluckPosition);
    result.touch = 0.01f * value (slotTouch);
    result.bodyAmount = 0.01f * value (slotBodyAmount);
    result.stereoWidth = 0.01f * value (slotStereoWidth);
    result.outputGain = juce::Decibels::decibelsToGain (value (slotOutput));
    result.piezoMix = 0.01f * value (slotPiezoMix);
    constexpr std::array captures { acustra::CaptureType::StereoMic,
        acustra::CaptureType::MonoMic, acustra::CaptureType::Piezo };
    result.capture = captures[static_cast<std::size_t> (
        std::clamp (static_cast<int> (std::lround (value (slotCaptureMode))), 0, 2))];
    result.picking = choiceValue<acustra::PickingTechnique> (value (slotPicking), 2);
    result.guitarModel = choiceValue<acustra::GuitarModel> (value (slotGuitarModel), 1);
    return result;
}

void AcustraAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engineReady.store (false, std::memory_order_release);
    keyboardState.reset();
    performer.setParameters (snapshotEngineParameters());
    performer.prepare (sampleRate, samplesPerBlock);
    // The player gathers at the engine's own rate, which it clamps to the
    // modelled range, so the latency it reports must be counted there too.
    gatherWindowSamples.store (acustra::Performer::gatherWindowSamples (
                                   performer.engine().sampleRate()),
                               std::memory_order_relaxed);
    parameterChanged (ids::gatherChords, parameterPointers[slotGatherChords]->load (
        std::memory_order_relaxed));
    displaySampleRate.store (sampleRate, std::memory_order_relaxed);
    activeVoiceCount.store (0, std::memory_order_relaxed);
    sympatheticStringCount.store (0, std::memory_order_relaxed);
    engineReady.store (true, std::memory_order_release);
}

void AcustraAudioProcessor::releaseResources()
{
    engineReady.store (false, std::memory_order_release);
    performer.reset();
    keyboardState.reset();
    activeVoiceCount.store (0, std::memory_order_relaxed);
    sympatheticStringCount.store (0, std::memory_order_relaxed);
    displaySampleRate.store (0.0, std::memory_order_relaxed);
}

bool AcustraAudioProcessor::isBusesLayoutSupported (
    const BusesLayout& layouts) const
{
    if (! layouts.getMainInputChannelSet().isDisabled()
        || layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()
        || layouts.outputBuses.size() != 2)
        return false;
    // The separate Piezo output is either off or mono.
    const auto& piezo = layouts.getChannelSet (false, piezoBus);
    return piezo.isDisabled() || piezo == juce::AudioChannelSet::mono();
}

void AcustraAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (! engineReady.load (std::memory_order_acquire))
        return;

    const auto numSamples = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer (midiMessages, 0, numSamples, true);
    updateEngineParameters();

    if (panicRequested.exchange (false, std::memory_order_acq_rel))
        performer.reset();

    // The performer splits the block at its events and plays them; see
    // DSP/AcustraPerformer.h.
    performer.setGatherChords (parameterPointers[slotGatherChords]->load (
                                   std::memory_order_relaxed) >= 0.5f);
    // The Piezo output is rendered only when the host enabled it; a null
    // pointer tells the engine not to.
    const auto channel = [&] (int bus, int index) -> float*
    {
        if (index >= getChannelCountOfBus (false, bus))
            return nullptr;
        const int at = getChannelIndexInProcessBlockBuffer (false, bus, index);
        return at < buffer.getNumChannels() ? buffer.getWritePointer (at) : nullptr;
    };
    const acustra::AcustraEngine::OutputBuses buses { channel (piezoBus, 0) };
    performer.beginBlock (buffer.getWritePointer (0), buffer.getWritePointer (1),
                          buses, numSamples);
    for (const auto metadata : midiMessages)
        performer.handleMidi (metadata.samplePosition, metadata.data,
                              metadata.numBytes);
    performer.endBlock();

    const auto& engine = performer.engine();
    activeVoiceCount.store (engine.getActiveVoiceCount(),
                            std::memory_order_relaxed);
    sympatheticStringCount.store (engine.getSympatheticStringCount(),
                                  std::memory_order_relaxed);
}

void AcustraAudioProcessor::updateEngineParameters() noexcept
{
    performer.setParameters (snapshotEngineParameters());
}

void AcustraAudioProcessor::requestPanic() noexcept
{
    keyboardState.reset();
    panicRequested.store (true, std::memory_order_release);
}

void AcustraAudioProcessor::getStateInformation (
    juce::MemoryBlock& destinationData)
{
    auto state = parameters.copyState();
    state.setProperty (stateVersionProperty, currentStateVersion, nullptr);
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destinationData);
}

void AcustraAudioProcessor::setStateInformation (const void* data,
                                                 int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml != nullptr && xml->hasTagName (parameters.state.getType()))
    {
        auto restoredState = juce::ValueTree::fromXml (*xml);
        if (static_cast<int> (restoredState.getProperty (stateVersionProperty, 1))
            < currentStateVersion)
            migrateVersionOneState (restoredState);
        restoredState.setProperty (stateVersionProperty, currentStateVersion, nullptr);
        // The old five-choice capture and two overrides had different ranges.
        // Migrate once, before defaults are added; an explicit modern value wins.
        if (! containsParameterState (restoredState, ids::captureMode))
        {
            const auto legacyValue = [&] (const char* id)
            {
                for (const auto& child : restoredState)
                    if (child.hasType ("PARAM")
                        && child.getProperty ("id").toString() == id)
                    {
                        const auto value = static_cast<float> (child.getProperty ("value"));
                        return std::isfinite (value) ? value : 0.0f;
                    }
                return 0.0f;
            };
            const int legacy = static_cast<int> (std::lround (
                std::clamp (legacyValue (ids::capture), 0.0f, 4.0f)));
            const float migrated = legacyValue (ids::upperMic) >= 0.5f
                ? 1.0f : legacy == 0 ? 0.0f : legacy < 3 ? 1.0f : 2.0f;
            juce::ValueTree captureState { "PARAM" };
            captureState.setProperty ("id", ids::captureMode, nullptr);
            captureState.setProperty ("value", migrated, nullptr);
            restoredState.appendChild (captureState, nullptr);
        }
        // A retired Guitar Model (2-4) would clamp to Bellido, a classical;
        // the steel guitars those slots held play Original.
        for (auto child : restoredState)
            if (child.hasType ("PARAM")
                && child.getProperty ("id").toString() == ids::guitarModel
                && static_cast<float> (child.getProperty ("value")) >= 1.5f)
                child.setProperty ("value", 0.0f, nullptr);
        addMissingParameterDefaults (restoredState, parameters, getParameters());
        parameters.replaceState (restoredState);
        requestPanic();
    }
}

juce::AudioProcessorEditor* AcustraAudioProcessor::createEditor()
{
    return new AcustraAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AcustraAudioProcessor();
}
