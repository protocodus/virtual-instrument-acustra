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
    slotStringMaterial,
    slotTuning,
    slotStringAge,
    slotPluckPosition,
    slotTouch,
    slotBodyAmount,
    slotStereoWidth,
    slotOutput,
    slotCapture,
    slotPicking,
    slotBridgeModel,
    slotUpperMic,
    slotPiezoLoading,
    slotCaptureMode,
    slotGuitarModel,
    slotGatherChords,
    slotCount
};

static_assert (static_cast<int> (slotCount) == ids::parameterCount);

constexpr std::array<const char*, slotCount> parameterIds {
    ids::shape,
    ids::bodyMaterial,
    ids::stringMaterial,
    ids::tuning,
    ids::stringAge,
    ids::pluckPosition,
    ids::touch,
    ids::bodyAmount,
    ids::stereoWidth,
    ids::output,
    ids::capture,
    ids::picking,
    ids::bridgeModel,
    ids::upperMic,
    ids::piezoLoading,
    ids::captureMode,
    ids::guitarModel,
    ids::gatherChords
};

std::unique_ptr<juce::RangedAudioParameter> makePercentParameter (
    const juce::String& id, const juce::String& name, float defaultValue)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name,
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
        // A session saved before the bridge parameter existed was made on the
        // Original bridge, so it keeps that bridge rather than taking the
        // measured steel bridge a new session starts on.
        const float value = ranged->paramID == ids::bridgeModel
            ? 0.0f
            : ranged->convertFrom0to1 (ranged->getDefaultValue());
        parameterState.setProperty (valueProperty, value, nullptr);
        state.appendChild (parameterState, nullptr);
    }
}

template <typename Enum>
Enum choiceValue (float value, int maximumIndex) noexcept
{
    return static_cast<Enum> (
        std::clamp (static_cast<int> (std::lround (value)), 0, maximumIndex));
}

struct PendingNoteOn
{
    int note { 0 };
    int channel { 1 };
    float velocity { 0.0f };
    int pluckDelay { 0 };
};

struct PendingNoteOff
{
    int note { 0 };
    int channel { 1 };
    float lift { 0.0f };
};

// A hand's chord does not reach the keys at once. The melody note a pianist
// voices louder strikes 20 to 30 ms before the rest, mostly because a faster
// key travels sooner, which an electronic keyboard's key-bottom contact
// shares; asynchronies played on purpose, a bass lead or an enlarged melody
// lead, usually exceed 30 ms (Goebl 2001, "Melody lead in piano performance:
// Expressive device or artifact?", JASA 110, 563-572). Thirty milliseconds
// is where one chord ends and notes meant apart begin.
constexpr double chordGatherSeconds = 0.030;

// In explicit CC68 legato mode, release velocity controls an active finger
// lift. Unsensed/default 64 and below keep the finger touching; 127 requests
// the full lift. The engine ignores this active gesture outside legato, so
// a fast ordinary key-up damps the note without generating another stroke.
// A Note On at velocity zero carries no sensed release velocity.
float fingerLiftFromReleaseVelocity (unsigned velocity) noexcept
{
    return velocity <= 64u ? 0.0f
                           : static_cast<float> (velocity - 64u) / 63.0f;
}
} // namespace

AcustraAudioProcessor::AcustraAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput (
          "Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ACUSTRA_STATE", createParameterLayout())
{
    conventionalPitchBendRanges.fill (2.0f);
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
        juce::StringArray { "Spruce", "Cedar", "Mahogany", "Maple" }, 0));
    result.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::stringMaterial, 1 }, "String Material",
        juce::StringArray { "Nylon", "Steel" }, 1));
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

    // Append new controls, including a later AU version hint, so existing host
    // automation retains the original ten parameter indices.
    result.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::capture, 2 }, "Legacy capture",
        juce::StringArray { "Stereo mics", "Treble mic", "Bass mic",
                            "Saddle piezo", "Magnetic (steel)" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    result.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::picking, 2 }, "Picking",
        juce::StringArray { "Finger", "Pick", "Thumb" }, 0));
    // New sessions start on the measured steel-string bridge, which the steel
    // construction presets select; a session saved before this parameter
    // existed keeps the Original bridge it was made with (see
    // addMissingParameterDefaults), not this default.
    result.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::bridgeModel, 3 }, "Bridge Model",
        juce::StringArray { "Original", "Measured Fylde (steel)" }, 1));
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
    result.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ids::guitarModel, 7 }, "Guitar Model",
        juce::StringArray { "Original", "Bellido 1978" }, 0));
    // Off by default: gathering holds every note back (see processBlock), so
    // it changes the plug-in's latency and is not automatable.
    result.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ids::gatherChords, 8 }, "Gather Chords", false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));

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
        value (slotBodyMaterial), 3);
    result.stringMaterial = choiceValue<acustra::StringMaterial> (
        value (slotStringMaterial), 1);
    result.tuning = choiceValue<acustra::Tuning> (value (slotTuning), 4);
    result.stringAge = 0.01f * value (slotStringAge);
    result.pluckPosition = 0.01f * value (slotPluckPosition);
    result.touch = 0.01f * value (slotTouch);
    result.bodyAmount = 0.01f * value (slotBodyAmount);
    result.stereoWidth = 0.01f * value (slotStereoWidth);
    result.outputGain = juce::Decibels::decibelsToGain (value (slotOutput));
    constexpr std::array captures { acustra::CaptureType::StereoMic,
        acustra::CaptureType::MonoMic, acustra::CaptureType::Piezo };
    result.capture = captures[static_cast<std::size_t> (
        std::clamp (static_cast<int> (std::lround (value (slotCaptureMode))), 0, 2))];
    result.picking = choiceValue<acustra::PickingTechnique> (value (slotPicking), 2);
    result.bridgeModel = choiceValue<acustra::BridgeModel> (value (slotBridgeModel), 1);
    result.guitarModel = choiceValue<acustra::GuitarModel> (value (slotGuitarModel), 1);
    return result;
}

void AcustraAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engineReady.store (false, std::memory_order_release);
    keyboardState.reset();
    rawPitchWheels.fill (0.0f);
    for (auto& detector : rpnDetectors)
        detector.reset();
    engine.setParameters (snapshotEngineParameters());
    engine.prepare (sampleRate, samplesPerBlock);
    currentSampleRate = sampleRate;
    processedSamples = 0;
    lastStrumSample = -1;
    strumUpstroke = false;
    heldMidiCount = 0;
    gatherWindowSamples.store (static_cast<int> (std::lround (
        chordGatherSeconds * sampleRate)), std::memory_order_relaxed);
    parameterChanged (ids::gatherChords, parameterPointers[slotGatherChords]->load (
        std::memory_order_relaxed));
    engine.setLowerZoneMemberCount (lowerZoneMemberCount);
    displaySampleRate.store (sampleRate, std::memory_order_relaxed);
    activeVoiceCount.store (0, std::memory_order_relaxed);
    sympatheticStringCount.store (0, std::memory_order_relaxed);
    engineReady.store (true, std::memory_order_release);
}

void AcustraAudioProcessor::releaseResources()
{
    engineReady.store (false, std::memory_order_release);
    engine.reset();
    keyboardState.reset();
    heldMidiCount = 0;
    activeVoiceCount.store (0, std::memory_order_relaxed);
    sympatheticStringCount.store (0, std::memory_order_relaxed);
    displaySampleRate.store (0.0, std::memory_order_relaxed);
}

bool AcustraAudioProcessor::isBusesLayoutSupported (
    const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
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
    {
        rawPitchWheels.fill (0.0f);
        for (auto& detector : rpnDetectors)
            detector.reset();
        engine.reset();
        heldMidiCount = 0;
    }

    // Hosts may store simultaneous chord members in any insertion order.  A
    // physical six-string allocator must see one canonical wrist event, or the
    // same MIDI chord can land on different strings in different hosts.
    std::array<PendingNoteOn, 128> pendingNoteOns {};
    int pendingNoteOnCount = 0;
    std::array<PendingNoteOff, 128> pendingNoteOffs {};
    int pendingNoteOffCount = 0;
    std::array<bool, 16> cancelledNoteOns {};
    int groupedSample = -1;
    const auto flushNoteGroup = [&]
    {
        std::sort (pendingNoteOns.begin(),
                   pendingNoteOns.begin() + pendingNoteOnCount,
                   [] (const PendingNoteOn& left, const PendingNoteOn& right)
                   {
                       return left.note != right.note
                           ? left.note > right.note
                           : left.channel < right.channel;
                   });
        // Three or more notes on one sample are a chord nobody can play at
        // once: a strum reaches its strings one after another, low to high
        // on a downstroke and back on the return, so consecutive strums
        // alternate. A rest long enough to start over starts over with a
        // downstroke; two seconds is that convention, not a measurement.
        // Legato groups are hammer-ons and stay as they are.
        const bool strum = pendingNoteOnCount >= 3 && ! legatoDown
            && std::all_of (pendingNoteOns.begin(),
                            pendingNoteOns.begin() + pendingNoteOnCount,
                            [&] (const PendingNoteOn& note)
                            { return note.channel == pendingNoteOns[0].channel; });
        if (strum)
        {
            const bool restarted = lastStrumSample < 0
                || processedSamples - lastStrumSample
                       > static_cast<std::int64_t> (2.0 * currentSampleRate);
            if (restarted)
                strumUpstroke = false;
            lastStrumSample = processedSamples;
            float meanVelocity = 0.0f;
            for (int index = 0; index < pendingNoteOnCount; ++index)
                meanVelocity += pendingNoteOns[static_cast<std::size_t> (index)].velocity;
            meanVelocity /= static_cast<float> (pendingNoteOnCount);
            // The list is sorted high to low, so a downstroke's rank counts
            // from the end.
            for (int index = 0; index < pendingNoteOnCount; ++index)
            {
                const int rank = strumUpstroke ? index
                                               : pendingNoteOnCount - 1 - index;
                pendingNoteOns[static_cast<std::size_t> (index)].pluckDelay
                    = engine.strumDelaySamples (rank, meanVelocity);
            }
            strumUpstroke = ! strumUpstroke;
            engine.beginStrum();
        }
        // Notes that land together on one channel are one fretting-hand
        // shape: the engine places them as a chord before they sound.
        // Legato groups are hammer-ons onto what is already held.
        const bool oneChannel = std::all_of (
            pendingNoteOns.begin(), pendingNoteOns.begin() + pendingNoteOnCount,
            [&] (const PendingNoteOn& note)
            { return note.channel == pendingNoteOns[0].channel; });
        if (pendingNoteOnCount >= 2 && oneChannel && ! legatoDown
            && pendingNoteOnCount <= acustra::AcustraEngine::stringCount)
        {
            std::array<int, acustra::AcustraEngine::stringCount> chord {};
            for (int index = 0; index < pendingNoteOnCount; ++index)
                chord[static_cast<std::size_t> (index)]
                    = pendingNoteOns[static_cast<std::size_t> (index)].note;
            engine.planChord (chord.data(), pendingNoteOnCount,
                              pendingNoteOns[0].channel);
        }
        for (int index = 0; index < pendingNoteOnCount; ++index)
        {
            const auto& note = pendingNoteOns[static_cast<std::size_t> (index)];
            engine.noteOn (note.note, note.velocity, note.channel,
                           strum ? note.pluckDelay : 0, strum);
        }
        pendingNoteOnCount = 0;

        // Resolve a zero-duration Note On/Off at one host sample in the same
        // direction regardless of MidiBuffer insertion order.  The On must
        // establish ownership before the Off can release it.
        std::sort (pendingNoteOffs.begin(),
                   pendingNoteOffs.begin() + pendingNoteOffCount,
                   [] (const PendingNoteOff& left, const PendingNoteOff& right)
                   {
                       return left.note != right.note
                           ? left.note > right.note
                           : left.channel < right.channel;
                   });
        for (int index = 0; index < pendingNoteOffCount; ++index)
        {
            const auto& note = pendingNoteOffs[static_cast<std::size_t> (index)];
            engine.noteOff (note.note, note.channel, note.lift);
        }
        pendingNoteOffCount = 0;
    };

    int renderedTo = 0;
    // Returns true when a Note On joined the group at its sample.
    const auto handleEvent = [&] (int eventSample, const juce::uint8* data,
                                  int numBytes)
    {
        if (groupedSample >= 0 && eventSample != groupedSample)
        {
            flushNoteGroup();
            cancelledNoteOns.fill (false);
        }
        if (eventSample > renderedTo)
        {
            engine.process (buffer.getWritePointer (0, renderedTo),
                            buffer.getWritePointer (1, renderedTo),
                            eventSample - renderedTo);
            renderedTo = eventSample;
        }
        groupedSample = eventSample;

        const auto status = numBytes > 0
            ? static_cast<unsigned> (data[0]) & 0xf0u : 0u;
        const int midiChannel = numBytes > 0
            ? static_cast<int> (data[0] & 0x0fu) + 1 : 1;
        const bool positiveNoteOn = status == 0x90u && numBytes >= 3
            && (data[2] & 0x7fu) != 0u;
        const bool noteOff = numBytes >= 2
            && (status == 0x80u
                || (status == 0x90u && numBytes >= 3
                    && (data[2] & 0x7fu) == 0u));
        if (positiveNoteOn)
        {
            if (! cancelledNoteOns[static_cast<std::size_t>(midiChannel - 1)]
                && pendingNoteOnCount
                    < static_cast<int> (pendingNoteOns.size()))
            {
                pendingNoteOns[static_cast<std::size_t> (pendingNoteOnCount++)]
                    = { static_cast<int> (data[1] & 0x7fu),
                        midiChannel,
                        static_cast<float> (data[2] & 0x7fu) / 127.0f };
                return true;
            }
        }
        else if (noteOff
                 && pendingNoteOffCount < static_cast<int> (pendingNoteOffs.size()))
        {
            const unsigned releaseVelocity = status == 0x80u && numBytes >= 3
                ? static_cast<unsigned> (data[2] & 0x7fu) : 64u;
            pendingNoteOffs[static_cast<std::size_t> (pendingNoteOffCount++)] = {
                static_cast<int> (data[1] & 0x7fu), midiChannel,
                fingerLiftFromReleaseVelocity (releaseVelocity)
            };
        }
        else
        {
            // All Sound/Notes Off at a chord boundary owns that boundary and
            // must not be undone by Note Ons merely inserted before it.
            if (status == 0xb0u && numBytes >= 3
                && ((data[1] & 0x7fu) == 120u || (data[1] & 0x7fu) == 123u))
            {
                for (int channel = 1; channel <= 16; ++channel)
                    if (channelIsInControllerScope (midiChannel, channel))
                        cancelledNoteOns[static_cast<std::size_t>(channel - 1)]
                            = true;
                const auto removeChannel = [this, midiChannel] (const auto& note)
                {
                    return channelIsInControllerScope (midiChannel,
                                                       note.channel);
                };
                pendingNoteOnCount = static_cast<int> (std::remove_if (
                    pendingNoteOns.begin(),
                    pendingNoteOns.begin() + pendingNoteOnCount,
                    removeChannel) - pendingNoteOns.begin());
                pendingNoteOffCount = static_cast<int> (std::remove_if (
                    pendingNoteOffs.begin(),
                    pendingNoteOffs.begin() + pendingNoteOffCount,
                    removeChannel) - pendingNoteOffs.begin());
            }
            dispatchMidiData (data, numBytes);
        }
        return false;
    };

    // Gather Chords: a hand's chord reaches the keys spread over the
    // gathering window (see chordGatherSeconds), in whatever order the
    // fingers land. The allocator can only fret it as a guitarist would if
    // it sees the whole chord as one wrist event, as it does a sequencer's
    // same-sample chord, so every event is held back by the window; a Note
    // On that comes due takes along the Note Ons its channel received
    // within the window after it. The group then sounds, voiced and strummed
    // (see flushNoteGroup), at the first key's time plus the window, which
    // is the latency the host is told. A key repeated inside the window, or
    // a controller that changes how notes are allocated, ends the chord.
    // Legato groups and string-per-channel controllers already say how
    // each note is played, so they pass through in time without gathering.
    const bool gathering = parameterPointers[slotGatherChords]->load (
                               std::memory_order_relaxed) >= 0.5f;
    if (! gathering && heldMidiCount == 0)
    {
        for (const auto metadata : midiMessages)
            static_cast<void> (handleEvent (
                juce::jlimit (0, numSamples, metadata.samplePosition),
                metadata.data, metadata.numBytes));
    }
    else
    {
        const int window = gathering
            ? gatherWindowSamples.load (std::memory_order_relaxed) : 0;
        // Switched off with events still held: they are due now, in order,
        // ahead of everything that arrives from here on.
        if (! gathering)
            for (int index = 0; index < heldMidiCount; ++index)
                heldMidi[static_cast<std::size_t> (index)].due = std::min (
                    heldMidi[static_cast<std::size_t> (index)].due,
                    processedSamples);
        // Only channel messages of one to three bytes reach dispatchMidiData.
        for (const auto metadata : midiMessages)
        {
            if (metadata.numBytes < 1 || metadata.numBytes > 3
                || heldMidiCount == static_cast<int> (heldMidi.size()))
                continue;
            auto& held = heldMidi[static_cast<std::size_t> (heldMidiCount++)];
            held.due = processedSamples
                + juce::jlimit (0, numSamples, metadata.samplePosition) + window;
            held.size = metadata.numBytes;
            std::copy_n (metadata.data, metadata.numBytes, held.bytes.begin());
            held.gathered = false;
        }

        const auto gatherChord = [&] (int first)
        {
            const auto& lead = heldMidi[static_cast<std::size_t> (first)];
            const int channel = static_cast<int> (lead.bytes[0] & 0x0fu) + 1;
            for (int index = first + 1; index < heldMidiCount; ++index)
            {
                auto& held = heldMidi[static_cast<std::size_t> (index)];
                if (held.due > lead.due + window)
                    return;
                if (held.gathered)
                    continue;
                const auto status = static_cast<unsigned> (held.bytes[0]) & 0xf0u;
                if (status == 0xb0u && held.size >= 3)
                {
                    // Legato, reset, sound/notes off, mono/poly and the RPNs
                    // that lay out an MPE zone all change the allocation.
                    switch (held.bytes[1] & 0x7fu)
                    {
                        case 6: case 38: case 68: case 96: case 97: case 98:
                        case 99: case 100: case 101: case 120: case 121:
                        case 123: case 126: case 127:
                            return;
                        default:
                            continue;
                    }
                }
                if (status != 0x90u || held.size < 3
                    || (held.bytes[2] & 0x7fu) == 0u
                    || static_cast<int> (held.bytes[0] & 0x0fu) + 1 != channel)
                    continue;
                const int note = static_cast<int> (held.bytes[1] & 0x7fu);
                if (pendingNoteOnCount == static_cast<int> (pendingNoteOns.size())
                    || std::any_of (pendingNoteOns.begin(),
                                    pendingNoteOns.begin() + pendingNoteOnCount,
                                    [&] (const PendingNoteOn& pending)
                                    {
                                        return pending.note == note
                                            && pending.channel == channel;
                                    }))
                    return;
                pendingNoteOns[static_cast<std::size_t> (pendingNoteOnCount++)]
                    = { note, channel,
                        static_cast<float> (held.bytes[2] & 0x7fu) / 127.0f };
                held.gathered = true;
            }
        };

        const auto blockEnd = processedSamples + numSamples;
        int next = 0;
        for (; next < heldMidiCount
               && heldMidi[static_cast<std::size_t> (next)].due < blockEnd; ++next)
        {
            const auto& held = heldMidi[static_cast<std::size_t> (next)];
            if (held.gathered)
                continue;
            const bool joined = handleEvent (
                static_cast<int> (std::max<std::int64_t> (
                    0, held.due - processedSamples)),
                held.bytes.data(), held.size);
            if (joined && gathering && ! legatoDown
                && ! engine.isStringPerChannelMode())
                gatherChord (next);
        }
        int kept = 0;
        for (int index = next; index < heldMidiCount; ++index)
            if (! heldMidi[static_cast<std::size_t> (index)].gathered)
                heldMidi[static_cast<std::size_t> (kept++)]
                    = heldMidi[static_cast<std::size_t> (index)];
        heldMidiCount = kept;
    }

    flushNoteGroup();

    if (renderedTo < numSamples)
        engine.process (buffer.getWritePointer (0, renderedTo),
                        buffer.getWritePointer (1, renderedTo),
                        numSamples - renderedTo);

    processedSamples += numSamples;
    activeVoiceCount.store (engine.getActiveVoiceCount(),
                            std::memory_order_relaxed);
    sympatheticStringCount.store (engine.getSympatheticStringCount(),
                                  std::memory_order_relaxed);
}

void AcustraAudioProcessor::dispatchMidiData (const juce::uint8* data,
                                               int numBytes) noexcept
{
    if (data == nullptr || numBytes < 1)
        return;

    const auto kind = static_cast<unsigned> (data[0]) & 0xf0u;
    const int midiChannel = static_cast<int> (data[0] & 0x0fu) + 1;
    if (kind == 0x90u && numBytes >= 3)
    {
        const auto note = static_cast<int> (data[1] & 0x7fu);
        if ((data[2] & 0x7fu) != 0u)
            engine.noteOn (note,
                           static_cast<float> (data[2] & 0x7fu) / 127.0f,
                           midiChannel);
        else
            engine.noteOff (note, midiChannel);
    }
    else if (kind == 0x80u && numBytes >= 2)
    {
        engine.noteOff (static_cast<int> (data[1] & 0x7fu), midiChannel,
                        fingerLiftFromReleaseVelocity (
                            numBytes >= 3 ? static_cast<unsigned> (data[2] & 0x7fu)
                                          : 64u));
    }
    else if (kind == 0xe0u && numBytes >= 3)
    {
        const auto raw = static_cast<int> (data[1] & 0x7fu)
                       | (static_cast<int> (data[2] & 0x7fu) << 7);
        const float normalised = raw < 8192
            ? static_cast<float> (raw - 8192) / 8192.0f
            : static_cast<float> (raw - 8192) / 8191.0f;
        rawPitchWheels[static_cast<std::size_t> (midiChannel - 1)] = normalised;
        refreshPitchBend (midiChannel);
    }
    else if (kind == 0xd0u && numBytes >= 2)
    {
        // MPE channel pressure: the fretting hand's grip on this note's own
        // member channel. Forwarded unconditionally; the engine applies it
        // only on a channel the lower zone actually made a member (see
        // AcustraEngine::mpePressureFor), so it is inert without an MPE zone.
        engine.setMpePressure (static_cast<float> (data[1] & 0x7fu) / 127.0f,
                               midiChannel);
    }
    else if (kind == 0xb0u && numBytes >= 3)
    {
        const auto controller = data[1] & 0x7fu;
        const auto value = data[2] & 0x7fu;
        static_cast<void> (processRpnController (
            midiChannel, static_cast<int> (controller),
            static_cast<int> (value)));

        if (controller == 1u)
        {
            // The modulation wheel is the fretting hand's vibrato. Like the
            // bridge hand it is one gesture across the instrument rather than
            // a per-channel setting, and zero is an exact no-op.
            engine.setVibrato (static_cast<float> (value) / 127.0f);
        }
        else if (controller == 2u)
        {
            // Bridge-hand pressure. It is a playing gesture rather than a
            // construction setting, so it stays a controller and the panel
            // keeps its ten controls. It is global to the instrument: one hand
            // rests across the strings, not per channel.
            engine.setPalmMutePressure (static_cast<float> (value) / 127.0f);
        }
        else if (controller == 64u)
        {
            engine.setSustainPedal (value >= 64u, midiChannel);
        }
        else if (controller == 74u)
        {
            // MPE Timbre: where this one note's own member channel met the
            // string. Forwarded unconditionally; the engine reads it only on
            // a lower-zone member channel, at that note's own pluck (see
            // AcustraEngine::initialisePluck), so it is inert without an MPE
            // zone.
            engine.setMpeTimbre (static_cast<float> (value) / 127.0f,
                                 midiChannel);
        }
        else if (controller == 126u && midiChannel == 1)
        {
            // MIDI 1.0's own Mono Mode On channel-mode message on the basic
            // channel: value is how many consecutive channels become
            // monophonic voices. M=6 is the standard spelling of "six
            // channels, one voice each", which is why it is the toggle here
            // -- not a message either Roland's GK or Fishman's TriplePlay is
            // documented to transmit (their own manuals describe only the
            // resulting one-string-per-channel layout, not a message that
            // requests it), so today nothing sends this on those rigs; a
            // future control surface or the host's own MIDI editor can. Any
            // other value, including 0, turns the mode back off.
            engine.setStringPerChannelMode (
                value == static_cast<unsigned> (acustra::AcustraEngine::stringCount));
        }
        else if (controller == 127u && midiChannel == 1)
        {
            engine.setStringPerChannelMode (false);
        }
        else if (controller == 68u)
        {
            // MIDI's Legato Footswitch. While it is down a note a sounding
            // string can reach is hammered on rather than replucked, and
            // releasing it pulls off to what that string is still holding.
            // Like the bridge hand it is one gesture across the instrument,
            // not a per-channel setting.
            legatoDown = value >= 64u;
            engine.setLegato (legatoDown);
        }
        else if (controller == 120u)
        {
            engine.allSoundOff (midiChannel);
        }
        else if (controller == 121u)
        {
            resetControllerScope (midiChannel);
        }
        else if (controller == 123u)
        {
            engine.allNotesOff (midiChannel);
        }
    }
}

bool AcustraAudioProcessor::processRpnController (int midiChannel,
                                                   int controller,
                                                   int value) noexcept
{
    if (midiChannel < 1 || midiChannel > 16)
        return false;
    auto parsed = rpnDetectors[static_cast<std::size_t> (midiChannel - 1)]
                      .tryParse (midiChannel, controller, value);
    if (! parsed.has_value() || parsed->isNRPN)
        return false;

    const int wholeValue = parsed->is14BitValue
        ? parsed->value / 128 : parsed->value;
    if (parsed->parameterNumber
            == juce::MPEMessages::zoneLayoutMessagesRpnNumber
        && midiChannel == 1 && wholeValue >= 0 && wholeValue <= 15)
    {
        setLowerZoneMemberCount (wholeValue);
        return true;
    }
    if (parsed->parameterNumber != 0)
        return false;

    const int cents = parsed->is14BitValue
        ? std::min (parsed->value % 128, 99) : 0;
    const float range = juce::jlimit (
        0.0f, 96.0f,
        static_cast<float> (wholeValue) + 0.01f * static_cast<float> (cents));
    if (lowerZoneMemberCount > 0 && midiChannel == 1)
    {
        lowerMasterPitchBendRange = range;
        for (int channel = 1; channel <= lowerZoneMemberCount + 1; ++channel)
            refreshPitchBend (channel);
    }
    else if (lowerZoneMemberCount > 0 && midiChannel >= 2
             && midiChannel <= lowerZoneMemberCount + 1)
    {
        lowerMemberPitchBendRange = range;
        for (int channel = 2; channel <= lowerZoneMemberCount + 1; ++channel)
            refreshPitchBend (channel);
    }
    else
    {
        conventionalPitchBendRanges[static_cast<std::size_t> (
            midiChannel - 1)] = range;
        refreshPitchBend (midiChannel);
    }
    return true;
}

void AcustraAudioProcessor::setLowerZoneMemberCount (int memberCount) noexcept
{
    const int next = juce::jlimit (0, 15, memberCount);
    if (next == lowerZoneMemberCount)
    {
        if (next > 0)
        {
            lowerMasterPitchBendRange = 2.0f;
            lowerMemberPitchBendRange = 48.0f;
            for (int channel = 1; channel <= next + 1; ++channel)
                refreshPitchBend (channel);
        }
        return;
    }

    const int lastAffected = std::max (next, lowerZoneMemberCount) + 1;
    for (int channel = 1; channel <= lastAffected; ++channel)
    {
        rawPitchWheels[static_cast<std::size_t> (channel - 1)] = 0.0f;
        rpnDetectors[static_cast<std::size_t> (channel - 1)].reset();
    }
    lowerMasterPitchBendRange = 2.0f;
    lowerMemberPitchBendRange = 48.0f;
    lowerZoneMemberCount = next;
    engine.setLowerZoneMemberCount (next);
    for (int channel = 1; channel <= lastAffected; ++channel)
        refreshPitchBend (channel);
}

void AcustraAudioProcessor::refreshPitchBend (int midiChannel) noexcept
{
    if (midiChannel < 1 || midiChannel > 16)
        return;
    float range = conventionalPitchBendRanges[static_cast<std::size_t> (
        midiChannel - 1)];
    if (lowerZoneMemberCount > 0 && midiChannel == 1)
        range = lowerMasterPitchBendRange;
    else if (lowerZoneMemberCount > 0 && midiChannel >= 2
             && midiChannel <= lowerZoneMemberCount + 1)
        range = lowerMemberPitchBendRange;
    engine.setPitchBend (
        rawPitchWheels[static_cast<std::size_t> (midiChannel - 1)] * range,
        midiChannel);
}

bool AcustraAudioProcessor::channelIsInControllerScope (
    int controllerChannel, int targetChannel) const noexcept
{
    return lowerZoneMemberCount > 0 && controllerChannel == 1
        ? targetChannel >= 1 && targetChannel <= lowerZoneMemberCount + 1
        : targetChannel == controllerChannel;
}

void AcustraAudioProcessor::resetControllerScope (int midiChannel) noexcept
{
    for (int channel = 1; channel <= 16; ++channel)
    {
        if (! channelIsInControllerScope (midiChannel, channel))
            continue;
        rawPitchWheels[static_cast<std::size_t> (channel - 1)] = 0.0f;
        rpnDetectors[static_cast<std::size_t> (channel - 1)].reset();
        engine.setPitchBend (0.0f, channel);
        engine.setSustainPedal (false, channel);
    }
}

void AcustraAudioProcessor::updateEngineParameters() noexcept
{
    engine.setParameters (snapshotEngineParameters());
}

void AcustraAudioProcessor::requestPanic() noexcept
{
    keyboardState.reset();
    panicRequested.store (true, std::memory_order_release);
}

void AcustraAudioProcessor::getStateInformation (
    juce::MemoryBlock& destinationData)
{
    if (const auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destinationData);
}

void AcustraAudioProcessor::setStateInformation (const void* data,
                                                 int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml != nullptr && xml->hasTagName (parameters.state.getType()))
    {
        auto restoredState = juce::ValueTree::fromXml (*xml);
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
        // A retired Guitar Model (2-4) would clamp to Bellido, a nylon
        // classical; the steel guitars those slots held play Original.
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
