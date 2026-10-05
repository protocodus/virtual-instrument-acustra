#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
namespace ids = acustra::parameters;

double hostTempoBpm (juce::AudioPlayHead* playHead)
{
    // PositionInfo and its BPM are independently optional. Never retain a
    // stale tempo when a host stops supplying one.
    if (playHead != nullptr)
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm())
                if (std::isfinite (*bpm) && *bpm > 0.0)
                    return *bpm;
    return 120.0;
}

using StringActivity = acustra::AcustraEngine::StringActivity;

// One lock-free word keeps a string's pitch, ownership and raw envelope
// coherent. The float retains its exact bits; conversion to visual motion
// belongs to the editor. All supported frets fit in five bits.
static_assert (acustra::AcustraEngine::fretCount <= 31);
std::uint64_t packStringActivity (const StringActivity& activity) noexcept
{
    std::uint32_t levelBits {};
    static_assert (sizeof (levelBits) == sizeof (activity.level));
    std::memcpy (&levelBits, &activity.level, sizeof (levelBits));
    const auto field = [] (int value, int maximum, int shift)
    {
        return static_cast<std::uint64_t> (std::clamp (value, 0, maximum)) << shift;
    };
    return levelBits
        | field (activity.openMidi, 127, 32)
        | field (activity.midiNote, 127, 39)
        | field (activity.fret, 31, 46)
        | field (activity.harmonic, 127, 51)
        | (static_cast<std::uint64_t> (activity.keyDown) << 58)
        | (static_cast<std::uint64_t> (activity.played) << 59)
        | (static_cast<std::uint64_t> (activity.pedalHeld) << 60);
}

StringActivity unpackStringActivity (std::uint64_t packed) noexcept
{
    StringActivity activity;
    const auto levelBits = static_cast<std::uint32_t> (packed);
    std::memcpy (&activity.level, &levelBits, sizeof (levelBits));
    activity.openMidi = static_cast<int> ((packed >> 32) & 127u);
    activity.midiNote = static_cast<int> ((packed >> 39) & 127u);
    activity.fret = static_cast<int> ((packed >> 46) & 31u);
    activity.harmonic = static_cast<int> ((packed >> 51) & 127u);
    activity.keyDown = ((packed >> 58) & 1u) != 0;
    activity.played = ((packed >> 59) & 1u) != 0;
    activity.pedalHeld = ((packed >> 60) & 1u) != 0;
    return activity;
}

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
    slotReleaseNoise,
    slotRoom,
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
    ids::piezoMix,
    ids::releaseNoise,
    ids::room
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
    // The engine's own output latency holds before prepareToPlay too.
    setLatencySamples (acustra::Performer::latencySamples (false, 0));
}

AcustraAudioProcessor::~AcustraAudioProcessor()
{
    parameters.removeParameterListener (ids::gatherChords, this);
}

void AcustraAudioProcessor::parameterChanged (const juce::String& parameterID,
                                              float newValue)
{
    // Holding MIDI back is latency, on top of the engine's own (the
    // microphones wait out the piezo's pipeline); the host compensates what
    // it is told.
    if (parameterID == ids::gatherChords)
        setLatencySamples (acustra::Performer::latencySamples (
            newValue >= 0.5f, gatherWindowSamples.load (std::memory_order_relaxed)));
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
        juce::NormalisableRange<float> { -24.0f, 6.0f, 0.1f }, 0.0f,
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
    // The key-up's own sound: the damping hand landing on the string. 50%
    // is its nominal level, 100% twice that, 0% silent and an exact no-op.
    // On for a new instance at 70%, the level a listener chose on
    // 2026-09-30 (Docs/decisions.md); a session saved before it existed
    // loads it at zero (setStateInformation) and so sounds as it did.
    result.push_back (makePercentParameter (ids::releaseNoise, "Release Noise",
                                            70.0f, 10));
    // The room around the microphones (acustra::EngineParameters::room): 0%
    // the dry, close-miked instrument, 100% a microphone well out in a small
    // studio. On for a new instance at 50%, where the room sits 10-12.5 dB
    // under the guitar, the amount the open recordings measured best against
    // (Docs/decisions.md, 2026-10-01); a session saved before it existed
    // loads it at zero (setStateInformation) and so sounds as it did. The
    // piezo never hears it.
    result.push_back (makePercentParameter (ids::room, "Room", 50.0f, 11));

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
    result.releaseNoise = 0.01f * value (slotReleaseNoise);
    result.room = 0.01f * value (slotRoom);
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
    const auto displayRevision = stringDisplayRevision.fetch_add (
        1, std::memory_order_acq_rel) + 1;
    keyboardState.reset();
    performer.setParameters (snapshotEngineParameters());
    performer.prepare (sampleRate, samplesPerBlock);
    // JUCE permits play-head queries only inside processBlock. The first
    // block supplies host tempo before any MIDI, including its Note Offs.
    performer.setTempoBpm (120.0);
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
    stringDisplayIntervalSamples = std::max (1, static_cast<int> (
        std::lround (performer.engine().sampleRate() / 30.0)));
    stringDisplaySamplesUntilUpdate = 0;
    if (stringDisplayEnabled.load (std::memory_order_acquire))
        publishStringActivity (displayRevision);
    engineReady.store (true, std::memory_order_release);
}

void AcustraAudioProcessor::releaseResources()
{
    engineReady.store (false, std::memory_order_release);
    stringDisplayRevision.fetch_add (1, std::memory_order_acq_rel);
    performer.reset();
    keyboardState.reset();
    activeVoiceCount.store (0, std::memory_order_relaxed);
    sympatheticStringCount.store (0, std::memory_order_relaxed);
    displaySampleRate.store (0.0, std::memory_order_relaxed);
    stringDisplaySamplesUntilUpdate = 0;
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

    // Capture the request before consuming Panic. A request arriving during
    // this callback invalidates its display publication until the next one.
    const auto displayRevision = stringDisplayRevision.load (std::memory_order_acquire);
    const auto numSamples = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer (midiMessages, 0, numSamples, true);
    updateEngineParameters();

    const bool panicked = panicRequested.exchange (false, std::memory_order_acq_rel);
    if (panicked)
        performer.reset();

    performer.setTempoBpm (hostTempoBpm (getPlayHead()));

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
    if (stringDisplayEnabled.load (std::memory_order_acquire))
    {
        if (panicked || displayRevision != audioStringDisplayRevision)
            stringDisplaySamplesUntilUpdate = 0;
        stringDisplaySamplesUntilUpdate -= numSamples;
        if (stringDisplaySamplesUntilUpdate <= 0)
        {
            publishStringActivity (displayRevision);
            stringDisplaySamplesUntilUpdate = stringDisplayIntervalSamples
                + stringDisplaySamplesUntilUpdate % stringDisplayIntervalSamples;
        }
    }
    else
        stringDisplaySamplesUntilUpdate = 0;
}

void AcustraAudioProcessor::setStringDisplayEnabled (bool enabled) noexcept
{
    if (stringDisplayEnabled.exchange (enabled, std::memory_order_acq_rel) != enabled)
        stringDisplayRevision.fetch_add (1, std::memory_order_acq_rel);
}

std::array<acustra::AcustraEngine::StringActivity, acustra::AcustraEngine::stringCount>
AcustraAudioProcessor::getStringActivityForDisplay() const noexcept
{
    std::array<StringActivity, acustra::AcustraEngine::stringCount> silent {};
    const auto tuning = choiceValue<acustra::Tuning> (
        parameterPointers[slotTuning]->load (std::memory_order_relaxed), 4);
    const auto open = acustra::AcustraEngine::openNotes (tuning);
    for (std::size_t string = 0; string < silent.size(); ++string)
        silent[string].openMidi = silent[string].midiNote = open[string];

    const auto revision = stringDisplayRevision.load (std::memory_order_acquire);
    const auto unavailable = [&]
    {
        return ! stringDisplayEnabled.load (std::memory_order_acquire)
            || ! engineReady.load (std::memory_order_acquire)
            || panicRequested.load (std::memory_order_acquire)
            || stringDisplayRevision.load (std::memory_order_acquire) != revision;
    };
    if (unavailable()
        || stringDisplayPublishedRevision.load (std::memory_order_acquire) != revision)
        return silent;
    std::array<StringActivity, acustra::AcustraEngine::stringCount> activity {};
    for (std::size_t string = 0; string < activity.size(); ++string)
        activity[string] = unpackStringActivity (
            stringDisplayActivity[string].load (std::memory_order_relaxed));
    // A lifecycle or Panic request can race the six loads. Return silence
    // rather than retrying, waiting, or exposing the previous editor's notes.
    return unavailable() ? silent : activity;
}

void AcustraAudioProcessor::publishStringActivity (std::uint32_t revision) noexcept
{
    const auto activity = performer.engine().getStringActivity();
    for (std::size_t string = 0; string < activity.size(); ++string)
        stringDisplayActivity[string].store (
            packStringActivity (activity[string]), std::memory_order_relaxed);
    audioStringDisplayRevision = revision;
    stringDisplayPublishedRevision.store (revision, std::memory_order_release);
}

void AcustraAudioProcessor::updateEngineParameters() noexcept
{
    performer.setParameters (snapshotEngineParameters());
}

void AcustraAudioProcessor::requestPanic() noexcept
{
    keyboardState.reset();
    panicRequested.store (true, std::memory_order_release);
    stringDisplayRevision.fetch_add (1, std::memory_order_acq_rel);
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
        // A session saved before Release Noise existed keeps its silent
        // key-ups; addMissingParameterDefaults would give it the new default.
        if (! containsParameterState (restoredState, ids::releaseNoise))
        {
            juce::ValueTree releaseState { "PARAM" };
            releaseState.setProperty ("id", ids::releaseNoise, nullptr);
            releaseState.setProperty ("value", 0.0f, nullptr);
            restoredState.appendChild (releaseState, nullptr);
        }
        // Likewise a session saved before Room existed stays dry.
        if (! containsParameterState (restoredState, ids::room))
        {
            juce::ValueTree roomState { "PARAM" };
            roomState.setProperty ("id", ids::room, nullptr);
            roomState.setProperty ("value", 0.0f, nullptr);
            restoredState.appendChild (roomState, nullptr);
        }
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
