#include "AcustraPerformer.h"

#include <algorithm>
#include <cmath>

namespace acustra
{
namespace
{
// MIDI's MPE Configuration Message is RPN 6 (MPE specification 1.0, 2.1).
constexpr int mpeZoneLayoutRpn = 6;

int dataByte(int value) noexcept { return std::clamp(value, 0, 127); }

std::uint8_t statusByte(unsigned kind, int channel) noexcept
{
    return static_cast<std::uint8_t>(
        kind | static_cast<unsigned>(std::clamp(channel, 1, 16) - 1));
}
} // namespace

int Performer::gatherWindowSamples(double sampleRate) noexcept
{
    return static_cast<int>(std::lround(chordGatherSeconds * sampleRate));
}

Performer::Performer() noexcept
{
    conventionalPitchBendRanges_.fill(2.0f);
}

void Performer::prepare(double sampleRate, int maximumBlockSize)
{
    rawPitchWheels_.fill(0.0f);
    rpnStates_.fill(RpnState {});
    vibratoChannel_ = 0;
    engine_.prepare(sampleRate, maximumBlockSize);
    // The engine's own rate, which it sanitised: the strum rest and the
    // gathering window then keep time with it at any rate a host passes.
    sampleRate_ = engine_.sampleRate();
    processedSamples_ = 0;
    lastStrumSample_ = -1;
    strumUpstroke_ = false;
    heldCount_ = 0;
    droppedEvents_ = 0;
    gatherWindow_ = gatherWindowSamples(sampleRate_);
    engine_.setLowerZoneMemberCount(lowerZoneMemberCount_);
    // The engine starts every bend at zero; the master tune rides on them.
    for (int channel = 1; channel <= 16; ++channel)
        refreshPitchBend(channel);
}

void Performer::setParameters(const EngineParameters& parameters) noexcept
{
    engine_.setParameters(parameters);
}

void Performer::setMasterTuneCents(float cents) noexcept
{
    const float semitones = 0.01f * cents;
    if (semitones == masterTuneSemitones_)
        return;
    masterTuneSemitones_ = semitones;
    for (int channel = 1; channel <= 16; ++channel)
        refreshPitchBend(channel);
}

void Performer::reset() noexcept
{
    rawPitchWheels_.fill(0.0f);
    rpnStates_.fill(RpnState {});
    vibratoChannel_ = 0;
    engine_.reset();
    lastStrumSample_ = -1;
    strumUpstroke_ = false;
    for (int channel = 1; channel <= 16; ++channel)
        refreshPitchBend(channel);
    heldCount_ = 0;
    // A reset inside a block also drops that sample's pending notes.
    pendingNoteOnCount_ = 0;
    pendingNoteOffCount_ = 0;
}

void Performer::beginBlock(float* left, float* right,
                           const AcustraEngine::OutputBuses& buses,
                           int numSamples) noexcept
{
    left_ = left;
    right_ = right;
    buses_ = buses;
    blockSamples_ = std::max(0, numSamples);
    renderedTo_ = 0;
    groupedSample_ = -1;
    pendingNoteOnCount_ = 0;
    pendingNoteOffCount_ = 0;

    // Without gathering, and nothing left held from when it was on, events
    // play as they arrive; otherwise they queue (see endBlock).
    direct_ = ! gatherChords_ && heldCount_ == 0;
    // Switched off with events still held: they are due now, in order,
    // ahead of everything that arrives from here on.
    if (! direct_ && ! gatherChords_)
        for (int index = 0; index < heldCount_; ++index)
            held_[static_cast<std::size_t>(index)].due = std::min(
                held_[static_cast<std::size_t>(index)].due, processedSamples_);
}

void Performer::handleMidi(int sampleOffset, const std::uint8_t* data,
                           int size) noexcept
{
    if (data == nullptr)
        size = 0;
    const int sample = std::clamp(sampleOffset, 0, blockSamples_);
    if (direct_)
    {
        static_cast<void>(handleEvent(sample, data, size));
        return;
    }

    // Only channel messages of one to three bytes reach dispatchMidiData.
    if (size < 1 || size > 3)
        return;
    if (heldCount_ == heldEventCapacity)
    {
        ++droppedEvents_;
        return;
    }
    auto& held = held_[static_cast<std::size_t>(heldCount_++)];
    held.due = processedSamples_ + sample
        + (gatherChords_ ? gatherWindow_ : 0);
    held.size = size;
    std::copy_n(data, size, held.bytes.begin());
    held.gathered = false;
}

void Performer::endBlock() noexcept
{
    if (! direct_)
    {
        // Gather Chords: a hand's chord reaches the keys spread over the
        // gathering window (see chordGatherSeconds), in whatever order the
        // fingers land. The allocator can only fret it as a guitarist would
        // if it sees the whole chord as one wrist event, as it does a
        // sequencer's same-sample chord, so every event is held back by the
        // window; a Note On that comes due takes along the Note Ons its
        // channel received within the window after it. The group then
        // sounds, voiced and strummed (see flushNoteGroup), at the first
        // key's time plus the window, which is the latency the host is told.
        // A key repeated inside the window, or a controller that changes how
        // notes are allocated, ends the chord. String-per-channel
        // controllers already say how each note is played, so they pass
        // through in time without gathering.
        const auto blockEnd = processedSamples_ + blockSamples_;
        int next = 0;
        for (; next < heldCount_
               && held_[static_cast<std::size_t>(next)].due < blockEnd; ++next)
        {
            const auto& held = held_[static_cast<std::size_t>(next)];
            if (held.gathered)
                continue;
            const bool joined = handleEvent(
                static_cast<int>(std::max<std::int64_t>(
                    0, held.due - processedSamples_)),
                held.bytes.data(), held.size);
            if (joined && gatherChords_ && ! engine_.isStringPerChannelMode())
                gatherChord(next);
        }
        int kept = 0;
        for (int index = next; index < heldCount_; ++index)
            if (! held_[static_cast<std::size_t>(index)].gathered)
                held_[static_cast<std::size_t>(kept++)]
                    = held_[static_cast<std::size_t>(index)];
        heldCount_ = kept;
    }

    flushNoteGroup();
    renderTo(blockSamples_);
    processedSamples_ += blockSamples_;
    left_ = right_ = nullptr;
    buses_ = {};
    blockSamples_ = 0;
}

void Performer::renderTo(int sample) noexcept
{
    if (sample <= renderedTo_)
        return;
    const auto at = [this] (float* bus)
    {
        return bus != nullptr ? bus + renderedTo_ : nullptr;
    };
    engine_.process(left_ + renderedTo_, right_ + renderedTo_,
                    AcustraEngine::OutputBuses { at(buses_.piezo) },
                    sample - renderedTo_);
    renderedTo_ = sample;
}

// Hosts may store simultaneous chord members in any insertion order. A
// physical six-string allocator must see one canonical wrist event, or the
// same MIDI chord can land on different strings in different hosts.
void Performer::flushNoteGroup() noexcept
{
    std::sort(pendingNoteOns_.begin(),
              pendingNoteOns_.begin() + pendingNoteOnCount_,
              [](const PendingNoteOn& left, const PendingNoteOn& right)
              {
                  return left.note != right.note ? left.note > right.note
                                                 : left.channel < right.channel;
              });
    // Three or more notes on one sample are a chord nobody can play at once:
    // a strum reaches its strings one after another, low to high on a
    // downstroke and back on the return, so consecutive strums alternate. A
    // rest long enough to start over starts over with a downstroke; two
    // seconds is that convention, not a measurement.
    const bool oneChannel = std::all_of(
        pendingNoteOns_.begin(), pendingNoteOns_.begin() + pendingNoteOnCount_,
        [&](const PendingNoteOn& note)
        { return note.channel == pendingNoteOns_[0].channel; });
    // Form the shape before timing the stroke: pitch order can cross string
    // order (C4 on G5, G4 on B8, E4 on open E), and a skipped string still
    // takes the pick one spacing to cross.
    if (pendingNoteOnCount_ >= 2 && oneChannel
        && pendingNoteOnCount_ <= AcustraEngine::stringCount)
    {
        std::array<int, AcustraEngine::stringCount> chord {};
        for (int index = 0; index < pendingNoteOnCount_; ++index)
            chord[static_cast<std::size_t>(index)]
                = pendingNoteOns_[static_cast<std::size_t>(index)].note;
        engine_.planChord(chord.data(), pendingNoteOnCount_,
                          pendingNoteOns_[0].channel);
    }
    // Only notes the tuning can sound make a stroke: a note no string
    // reaches neither times it (below) nor turns one or two notes into one.
    int soundingNotes = 0;
    if (pendingNoteOnCount_ >= 3 && oneChannel)
        for (int index = 0; index < pendingNoteOnCount_; ++index)
        {
            const auto& note = pendingNoteOns_[static_cast<std::size_t>(index)];
            // Duplicate MIDI owners share one physical string; two pitches
            // do not become a three-string stroke because one key repeats.
            if (index > 0 && note.note
                == pendingNoteOns_[static_cast<std::size_t>(index - 1)].note)
                continue;
            soundingNotes += engine_.canSound(note.note, note.channel) ? 1 : 0;
        }
    const bool strum = soundingNotes >= 3;
    if (strum)
    {
        // The rest is measured between the strums' own samples, so no block
        // size can move it. groupedSample_ is still this group's sample
        // wherever it is flushed (handleEvent, endBlock).
        const auto strumSample = processedSamples_
            + static_cast<std::int64_t>(std::max(0, groupedSample_));
        const auto interval = lastStrumSample_ < 0 ? std::int64_t { 0 }
                                                   : strumSample - lastStrumSample_;
        const bool restarted = lastStrumSample_ < 0
            || interval > static_cast<std::int64_t>(strumRestSeconds * sampleRate_);
        if (restarted)
            strumUpstroke_ = false;
        lastStrumSample_ = strumSample;
        // The stroke is timed by the strings it reaches: a note the tuning
        // cannot sound takes no place in it and no part in its speed.
        std::array<bool, sampleGroupCapacity> sounds {};
        std::array<int, sampleGroupCapacity> strings {};
        bool completePlan = true;
        int firstString = AcustraEngine::stringCount;
        int lastString = -1;
        int sounding = 0;
        float meanVelocity = 0.0f;
        for (int index = 0; index < pendingNoteOnCount_; ++index)
        {
            const auto& note = pendingNoteOns_[static_cast<std::size_t>(index)];
            sounds[static_cast<std::size_t>(index)]
                = engine_.canSound(note.note, note.channel);
            if (sounds[static_cast<std::size_t>(index)])
            {
                meanVelocity += note.velocity;
                ++sounding;
                const int string = engine_.plannedString(note.note, note.channel);
                strings[static_cast<std::size_t>(index)] = string;
                completePlan &= string >= 0;
                if (string >= 0)
                {
                    firstString = std::min(firstString, string);
                    lastString = std::max(lastString, string);
                }
            }
        }
        if (sounding > 0)
            meanVelocity /= static_cast<float>(sounding);
        // A complete fretted shape gives the physical string indices. A
        // harmonic outside it, a larger group or a controller-owned layout
        // has no complete plan: keep its existing note-by-note behaviour.
        // Duplicated keys share one string's place while noteOn retains its
        // ownership/re-pluck handling. The first reached string starts now.
        int strokeSpan = 0;
        for (int index = 0, reached = 0; index < pendingNoteOnCount_; ++index)
        {
            auto& note = pendingNoteOns_[static_cast<std::size_t>(index)];
            if (!sounds[static_cast<std::size_t>(index)])
            {
                note.pluckDelay = 0;
                continue;
            }
            const int string = strings[static_cast<std::size_t>(index)];
            const int rank = completePlan
                ? (strumUpstroke_ ? lastString - string : string - firstString)
                : (strumUpstroke_ ? reached : sounding - 1 - reached);
            ++reached;
            note.pluckDelay = engine_.strumDelaySamples(rank, meanVelocity);
            strokeSpan = std::max(strokeSpan, note.pluckDelay);
        }
        strumUpstroke_ = ! strumUpstroke_;
        engine_.beginStrum(strokeSpan, restarted ? 0 : static_cast<int>(interval));
    }
    for (int index = 0; index < pendingNoteOnCount_; ++index)
    {
        const auto& note = pendingNoteOns_[static_cast<std::size_t>(index)];
        engine_.noteOn(note.note, note.velocity, note.channel,
                       strum ? note.pluckDelay : 0, strum);
    }
    pendingNoteOnCount_ = 0;

    // Resolve a zero-duration Note On/Off at one sample in the same direction
    // regardless of insertion order. The On must establish ownership before
    // the Off can release it.
    std::sort(pendingNoteOffs_.begin(),
              pendingNoteOffs_.begin() + pendingNoteOffCount_,
              [](const PendingNoteOff& left, const PendingNoteOff& right)
              {
                  return left.note != right.note ? left.note > right.note
                                                 : left.channel < right.channel;
              });
    for (int index = 0; index < pendingNoteOffCount_; ++index)
    {
        const auto& note = pendingNoteOffs_[static_cast<std::size_t>(index)];
        if (note.pedalMoved)
            engine_.noteOffWithVelocity(note.note, note.channel,
                                        note.sustained, note.releaseVelocity);
        else
            engine_.noteOffWithVelocity(note.note, note.channel,
                                        note.releaseVelocity);
    }
    pendingNoteOffCount_ = 0;
}

// Renders up to the event, then groups it with the other Note Ons and Offs
// of its sample or dispatches it. Returns true when a Note On joined the
// group at its sample.
bool Performer::handleEvent(int eventSample, const std::uint8_t* data,
                            int size) noexcept
{
    if (groupedSample_ >= 0 && eventSample != groupedSample_)
        flushNoteGroup();
    renderTo(eventSample);
    groupedSample_ = eventSample;

    const auto status = size > 0 ? static_cast<unsigned>(data[0]) & 0xf0u : 0u;
    const int midiChannel = size > 0 ? static_cast<int>(data[0] & 0x0fu) + 1 : 1;
    const bool positiveNoteOn = status == 0x90u && size >= 3
        && (data[2] & 0x7fu) != 0u;
    const bool noteOff = size >= 2
        && (status == 0x80u
            || (status == 0x90u && size >= 3 && (data[2] & 0x7fu) == 0u));
    if (positiveNoteOn)
    {
        if (pendingNoteOnCount_ < sampleGroupCapacity)
        {
            pendingNoteOns_[static_cast<std::size_t>(pendingNoteOnCount_++)]
                = { static_cast<int>(data[1] & 0x7fu), midiChannel,
                    static_cast<float>(data[2] & 0x7fu) / 127.0f, 0 };
            return true;
        }
        ++droppedEvents_;
    }
    else if (noteOff && pendingNoteOffCount_ < sampleGroupCapacity)
    {
        // A key-up damps its note; its release velocity, when the Note Off
        // carries one, sets only how firmly the hand lands.
        const bool hasReleaseVelocity = status == 0x80u && size >= 3
            && (data[2] & 0x7fu) != 0u;
        pendingNoteOffs_[static_cast<std::size_t>(pendingNoteOffCount_++)] = {
            static_cast<int>(data[1] & 0x7fu), midiChannel,
            engine_.sustainHolds(midiChannel), false,
            hasReleaseVelocity
                ? static_cast<float>(data[2] & 0x7fu) / 127.0f : -1.0f
        };
    }
    else
    {
        // Every controller, All Notes/Sound Off (CC123/CC120) included, acts
        // at once, and so before its sample's Note Ons and Offs, which wait
        // for the sample to end (flushNoteGroup): whatever order a host
        // stored them in, a reset ends what was sounding and the notes that
        // start on its sample, a loop's first beat after it, still sound.
        // A key-up waits for its sample's Note Ons, but the pedal meets it
        // in the order the host sent them: one pressed after it does not
        // catch it, and one lifted after it lets it go, whatever the pedal
        // does next on this sample - unless another pedal still holds it (an
        // MPE member's key-up is held by its own pedal or the manager's).
        dispatchMidiData(data, size);
        if (status == 0xb0u && size >= 3
            && ((data[1] & 0x7fu) == 64u || (data[1] & 0x7fu) == 121u))
        {
            for (int index = 0; index < pendingNoteOffCount_; ++index)
            {
                auto& off = pendingNoteOffs_[static_cast<std::size_t>(index)];
                if (!channelIsInControllerScope(midiChannel, off.channel))
                    continue;
                off.pedalMoved = true;
                off.sustained = off.sustained
                    && engine_.sustainHolds(off.channel);
            }
        }
    }
    return false;
}

// A Note On that comes due takes along the Note Ons its channel received
// within the window after it (see endBlock).
void Performer::gatherChord(int first) noexcept
{
    const auto& lead = held_[static_cast<std::size_t>(first)];
    const int channel = static_cast<int>(lead.bytes[0] & 0x0fu) + 1;
    for (int index = first + 1; index < heldCount_; ++index)
    {
        auto& held = held_[static_cast<std::size_t>(index)];
        if (held.due > lead.due + gatherWindow_)
            return;
        if (held.gathered)
            continue;
        const auto status = static_cast<unsigned>(held.bytes[0]) & 0xf0u;
        if (status == 0xb0u && held.size >= 3)
        {
            // Reset, sound/notes off, mono/poly and the RPNs that lay out an
            // MPE zone all change the allocation.
            switch (held.bytes[1] & 0x7fu)
            {
                case 6: case 38: case 96: case 97: case 98:
                case 99: case 100: case 101: case 120: case 121:
                case 123: case 126: case 127:
                    return;
                default:
                    continue;
            }
        }
        if (status != 0x90u || held.size < 3 || (held.bytes[2] & 0x7fu) == 0u
            || static_cast<int>(held.bytes[0] & 0x0fu) + 1 != channel)
            continue;
        const int note = static_cast<int>(held.bytes[1] & 0x7fu);
        if (pendingNoteOnCount_ == sampleGroupCapacity
            || std::any_of(pendingNoteOns_.begin(),
                           pendingNoteOns_.begin() + pendingNoteOnCount_,
                           [&](const PendingNoteOn& pending)
                           {
                               return pending.note == note
                                   && pending.channel == channel;
                           }))
            return;
        pendingNoteOns_[static_cast<std::size_t>(pendingNoteOnCount_++)]
            = { note, channel, static_cast<float>(held.bytes[2] & 0x7fu) / 127.0f, 0 };
        held.gathered = true;
    }
}

void Performer::dispatchMidiData(const std::uint8_t* data, int size) noexcept
{
    if (data == nullptr || size < 1)
        return;

    const auto kind = static_cast<unsigned>(data[0]) & 0xf0u;
    const int midiChannel = static_cast<int>(data[0] & 0x0fu) + 1;
    if (kind == 0x90u && size >= 3)
    {
        const auto note = static_cast<int>(data[1] & 0x7fu);
        if ((data[2] & 0x7fu) != 0u)
            engine_.noteOn(note, static_cast<float>(data[2] & 0x7fu) / 127.0f,
                           midiChannel);
        else
            engine_.noteOff(note, midiChannel);
    }
    else if (kind == 0x80u && size >= 2)
    {
        const bool hasReleaseVelocity = size >= 3 && (data[2] & 0x7fu) != 0u;
        engine_.noteOffWithVelocity(static_cast<int>(data[1] & 0x7fu),
            midiChannel, hasReleaseVelocity
                ? static_cast<float>(data[2] & 0x7fu) / 127.0f : -1.0f);
    }
    else if (kind == 0xe0u && size >= 3)
    {
        const auto raw = static_cast<int>(data[1] & 0x7fu)
                       | (static_cast<int>(data[2] & 0x7fu) << 7);
        const float normalised = raw < 8192
            ? static_cast<float>(raw - 8192) / 8192.0f
            : static_cast<float>(raw - 8192) / 8191.0f;
        rawPitchWheels_[static_cast<std::size_t>(midiChannel - 1)] = normalised;
        refreshPitchBend(midiChannel);
    }
    else if (kind == 0xd0u && size >= 2)
    {
        // MPE channel pressure: the fretting hand's grip on this note's own
        // member channel. Forwarded unconditionally; the engine applies it
        // only on a channel the lower zone actually made a member (see
        // AcustraEngine::mpePressureFor), so it is inert without an MPE zone.
        engine_.setMpePressure(static_cast<float>(data[1] & 0x7fu) / 127.0f,
                               midiChannel);
    }
    else if (kind == 0xb0u && size >= 3)
    {
        const auto controller = data[1] & 0x7fu;
        const auto value = data[2] & 0x7fu;
        static_cast<void>(processRpnController(
            midiChannel, static_cast<int>(controller), static_cast<int>(value)));

        if (controller == 1u)
        {
            // The modulation wheel is the fretting hand's vibrato. Like the
            // bridge hand it is one gesture across the instrument rather than
            // a per-channel setting, and zero is an exact no-op.
            engine_.setVibrato(static_cast<float>(value) / 127.0f);
            vibratoChannel_ = midiChannel;
        }
        else if (controller == 2u)
        {
            // Bridge-hand pressure. It is a playing gesture rather than a
            // construction setting, so it stays a controller and the panel
            // keeps its ten controls. It is global to the instrument: one hand
            // rests across the strings, not per channel.
            engine_.setPalmMutePressure(static_cast<float>(value) / 127.0f);
        }
        else if (controller == 64u)
        {
            engine_.setSustainPedal(value >= 64u, midiChannel);
        }
        else if (controller == 74u)
        {
            // MPE Timbre: where this one note's own member channel met the
            // string. Forwarded unconditionally; the engine reads it only on a
            // lower-zone member channel, at that note's own pluck (see
            // AcustraEngine::initialisePluck), so it is inert without an MPE
            // zone.
            engine_.setMpeTimbre(static_cast<float>(value) / 127.0f, midiChannel);
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
            engine_.setStringPerChannelMode(
                value == static_cast<unsigned>(AcustraEngine::stringCount));
        }
        else if (controller == 127u && midiChannel == 1)
        {
            engine_.setStringPerChannelMode(false);
        }
        else if (controller == 120u)
        {
            engine_.allSoundOff(midiChannel);
        }
        else if (controller == 121u)
        {
            resetControllerScope(midiChannel);
        }
        else if (controller == 123u)
        {
            engine_.allNotesOff(midiChannel);
        }
    }
}

// RPN parsing follows MIDI 1.0 (and JUCE's MidiRPNDetector, which the
// plug-in used before this layer): CC101/100 select an RPN, CC99/98 an NRPN,
// CC6 sets the value's MSB and clears its LSB, CC38 sets the LSB; a value is
// delivered on every CC6 or CC38 once a parameter and an MSB are known.
bool Performer::processRpnController(int midiChannel, int controller,
                                     int value) noexcept
{
    if (midiChannel < 1 || midiChannel > 16)
        return false;
    auto& state = rpnStates_[static_cast<std::size_t>(midiChannel - 1)];
    const auto byte = static_cast<std::uint8_t>(value);
    switch (controller)
    {
        case 98: case 100:
            state.parameterLsb = byte;
            state.valueMsb = state.valueLsb = 0xff;
            state.nrpn = controller == 98;
            return false;
        case 99: case 101:
            state.parameterMsb = byte;
            state.valueMsb = state.valueLsb = 0xff;
            state.nrpn = controller == 99;
            return false;
        case 6:
            state.valueMsb = byte;
            state.valueLsb = 0xff;
            break;
        case 38:
            state.valueLsb = byte;
            break;
        default:
            return false;
    }
    if (state.parameterMsb >= 0x80 || state.parameterLsb >= 0x80
        || state.valueMsb >= 0x80 || state.nrpn)
        return false;

    const int parameterNumber = (state.parameterMsb << 7) + state.parameterLsb;
    const bool is14BitValue = state.valueLsb < 0x80;
    const int parsedValue = is14BitValue
        ? (state.valueMsb << 7) + state.valueLsb : state.valueMsb;

    const int wholeValue = is14BitValue ? parsedValue / 128 : parsedValue;
    if (parameterNumber == mpeZoneLayoutRpn && midiChannel == 1
        && wholeValue >= 0 && wholeValue <= 15)
    {
        setLowerZoneMemberCount(wholeValue);
        return true;
    }
    if (parameterNumber != 0)
        return false;

    const int cents = is14BitValue ? std::min(parsedValue % 128, 99) : 0;
    const float range = std::clamp(
        static_cast<float>(wholeValue) + 0.01f * static_cast<float>(cents),
        0.0f, 96.0f);
    if (lowerZoneMemberCount_ > 0 && midiChannel == 1)
    {
        lowerMasterPitchBendRange_ = range;
        for (int channel = 1; channel <= lowerZoneMemberCount_ + 1; ++channel)
            refreshPitchBend(channel);
    }
    else if (lowerZoneMemberCount_ > 0 && midiChannel >= 2
             && midiChannel <= lowerZoneMemberCount_ + 1)
    {
        lowerMemberPitchBendRange_ = range;
        for (int channel = 2; channel <= lowerZoneMemberCount_ + 1; ++channel)
            refreshPitchBend(channel);
    }
    else
    {
        conventionalPitchBendRanges_[static_cast<std::size_t>(midiChannel - 1)]
            = range;
        refreshPitchBend(midiChannel);
    }
    return true;
}

void Performer::setLowerZoneMemberCount(int memberCount) noexcept
{
    const int next = std::clamp(memberCount, 0, 15);
    if (next == lowerZoneMemberCount_)
    {
        if (next > 0)
        {
            lowerMasterPitchBendRange_ = 2.0f;
            lowerMemberPitchBendRange_ = 48.0f;
            for (int channel = 1; channel <= next + 1; ++channel)
                refreshPitchBend(channel);
        }
        return;
    }

    const int lastAffected = std::max(next, lowerZoneMemberCount_) + 1;
    for (int channel = 1; channel <= lastAffected; ++channel)
    {
        rawPitchWheels_[static_cast<std::size_t>(channel - 1)] = 0.0f;
        rpnStates_[static_cast<std::size_t>(channel - 1)] = RpnState {};
    }
    lowerMasterPitchBendRange_ = 2.0f;
    lowerMemberPitchBendRange_ = 48.0f;
    lowerZoneMemberCount_ = next;
    engine_.setLowerZoneMemberCount(next);
    for (int channel = 1; channel <= lastAffected; ++channel)
        refreshPitchBend(channel);
}

bool Performer::isLowerZoneMember(int midiChannel) const noexcept
{
    return lowerZoneMemberCount_ > 0 && midiChannel >= 2
        && midiChannel <= lowerZoneMemberCount_ + 1;
}

// The bend a channel's own wheel asks for, plus the master tune. A lower
// zone member's note already sounds the manager's bend on top of its own
// (AcustraEngine::configureVoice), so the tune rides on the manager and
// every channel outside the zone, and never twice.
float Performer::tunedBend(int midiChannel, float bend) const noexcept
{
    return isLowerZoneMember(midiChannel) || masterTuneSemitones_ == 0.0f
        ? bend : bend + masterTuneSemitones_;
}

void Performer::refreshPitchBend(int midiChannel) noexcept
{
    if (midiChannel < 1 || midiChannel > 16)
        return;
    float range = conventionalPitchBendRanges_[static_cast<std::size_t>(
        midiChannel - 1)];
    if (lowerZoneMemberCount_ > 0 && midiChannel == 1)
        range = lowerMasterPitchBendRange_;
    else if (isLowerZoneMember(midiChannel))
        range = lowerMemberPitchBendRange_;
    const float bend
        = rawPitchWheels_[static_cast<std::size_t>(midiChannel - 1)] * range;
    engine_.setPitchBend(tunedBend(midiChannel, bend), midiChannel);
}

bool Performer::channelIsInControllerScope(int controllerChannel,
                                           int targetChannel) const noexcept
{
    return lowerZoneMemberCount_ > 0 && controllerChannel == 1
        ? targetChannel >= 1 && targetChannel <= lowerZoneMemberCount_ + 1
        : targetChannel == controllerChannel;
}

void Performer::resetControllerScope(int midiChannel) noexcept
{
    for (int channel = 1; channel <= 16; ++channel)
    {
        if (! channelIsInControllerScope(midiChannel, channel))
            continue;
        rawPitchWheels_[static_cast<std::size_t>(channel - 1)] = 0.0f;
        rpnStates_[static_cast<std::size_t>(channel - 1)] = RpnState {};
        engine_.setPitchBend(tunedBend(channel, 0.0f), channel);
        engine_.setSustainPedal(false, channel);
        // Pressure back to "never received", the neutral grip, not to a
        // received zero, which is a light one.
        engine_.setMpePressure(-1.0f, channel);
    }
    // The wheel is one gesture across the instrument, so only a reset that
    // covers the channel which set it takes it back (RP-015 resets CC1).
    if (vibratoChannel_ > 0
        && channelIsInControllerScope(midiChannel, vibratoChannel_))
    {
        engine_.setVibrato(0.0f);
        vibratoChannel_ = 0;
    }
}

void Performer::queueMessage(int sampleOffset, std::uint8_t status, int data1,
                             int data2, int size) noexcept
{
    const std::array<std::uint8_t, 3> bytes {
        status, static_cast<std::uint8_t>(dataByte(data1)),
        static_cast<std::uint8_t>(dataByte(data2))
    };
    handleMidi(sampleOffset, bytes.data(), size);
}

void Performer::noteOn(int sampleOffset, int channel, int note,
                       int velocity) noexcept
{
    queueMessage(sampleOffset, statusByte(0x90u, channel), note, velocity, 3);
}

void Performer::noteOff(int sampleOffset, int channel, int note,
                        int releaseVelocity) noexcept
{
    queueMessage(sampleOffset, statusByte(0x80u, channel), note,
                 releaseVelocity, 3);
}

void Performer::controlChange(int sampleOffset, int channel, int controller,
                              int value) noexcept
{
    queueMessage(sampleOffset, statusByte(0xb0u, channel), controller, value, 3);
}

void Performer::pitchWheel(int sampleOffset, int channel, float position) noexcept
{
    // The inverse of dispatchMidiData's decoding: 8192 is centre, 0 is -1
    // and 16383 is +1. NaN reads as centre.
    const float clamped = position == position
        ? std::clamp(position, -1.0f, 1.0f) : 0.0f;
    const float scaled = 8192.0f
        + clamped * (clamped < 0.0f ? 8192.0f : 8191.0f);
    const int raw = std::clamp(static_cast<int>(scaled + 0.5f), 0, 16383);
    queueMessage(sampleOffset, statusByte(0xe0u, channel), raw & 0x7f,
                 raw >> 7, 3);
}

void Performer::channelPressure(int sampleOffset, int channel, int value) noexcept
{
    queueMessage(sampleOffset, statusByte(0xd0u, channel), value, 0, 2);
}

void Performer::setPitchBendRange(int sampleOffset, int channel, int semitones,
                                  int cents) noexcept
{
    controlChange(sampleOffset, channel, 101, 0);
    controlChange(sampleOffset, channel, 100, 0);
    controlChange(sampleOffset, channel, 6, semitones);
    controlChange(sampleOffset, channel, 38, cents);
    controlChange(sampleOffset, channel, 101, 127);
    controlChange(sampleOffset, channel, 100, 127);
}
} // namespace acustra
