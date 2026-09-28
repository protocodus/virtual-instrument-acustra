#pragma once

#include "AcustraEngine.h"

#include <array>
#include <cstdint>

namespace acustra
{
// The player: everything Acustra does between receiving a note or controller
// and calling the engine. It owns the engine and is the only thing a front
// end talks to while playing, so the plug-in and the Rack Extension share one
// behaviour instead of each porting it.
//
// It covers sample-accurate splitting of a block at its events, MIDI channel
// messages (notes, pitch bend per channel with its RPN 0 range, CC1 vibrato,
// CC2 bridge hand, CC64 sustain, CC74 and channel pressure for MPE,
// CC120/121/123, CC126/127 string-per-channel mode), the MPE lower zone
// (RPN 6 on channel 1) and its controller scope, same-sample note grouping
// (canonical order, strums, chord shapes) and the Gather Chords window with
// the latency it costs.
//
// Front-end neutral and real-time safe: no JUCE, no allocation after
// construction, no exceptions, fixed-size storage only, C++17.
//
// One block is played as
//
//     beginBlock (left, right, numSamples);
//     handleMidi (offset, bytes, size);   // any number, offsets non-decreasing
//     endBlock();                         // renders [0, numSamples)
//
// Events are channel messages exactly as MIDI 1.0 spells them. A front end
// without MIDI (the Rack Extension) spells its notes and controllers the same
// way, directly or through the helpers below, so it plays through the same
// logic byte for byte.
class Performer
{
public:
    // A hand's chord does not reach the keys at once. The melody note a
    // pianist voices louder strikes 20 to 30 ms before the rest, mostly
    // because a faster key travels sooner, which an electronic keyboard's
    // key-bottom contact shares; asynchronies played on purpose, a bass lead
    // or an enlarged melody lead, usually exceed 30 ms (Goebl 2001, "Melody
    // lead in piano performance: Expressive device or artifact?", JASA 110,
    // 563-572). Thirty milliseconds is where one chord ends and notes meant
    // apart begin.
    static constexpr double chordGatherSeconds = 0.030;

    // Gather Chords holds every event of one to three bytes for the window,
    // so the queue holds the window plus one block. A MIDI 1.0 cable carries
    // at most about one three-byte message per millisecond (31.25 kbit/s), so
    // the window plus a 4096-sample block at 44.1 kHz, 123 ms, is about 130
    // events; 1024 leaves eight times that for a host's denser streams. An
    // MPE controller streaming bend and pressure on many member channels at
    // once can still exceed it: an event that arrives with the queue full is
    // dropped and counted (droppedEventCount), and nothing ever allocates.
    static constexpr int heldEventCapacity = 1024;
    // Note Ons (and, separately, Note Offs) that one sample can group. A
    // 129th Note On on one sample is dropped and counted; a 129th Note Off
    // is applied at once, ahead of the group's Note Ons.
    static constexpr int sampleGroupCapacity = 128;

    // A strum after a rest longer than this starts again on a downstroke
    // instead of alternating from the last stroke. A front end that stops
    // rendering while silent (the Rack Extension idles) must not idle for
    // less than this after the last note, or the rest would go unmeasured.
    static constexpr double strumRestSeconds = 2.0;

    // The Gather Chords window, and so its latency, at this sample rate.
    [[nodiscard]] static int gatherWindowSamples(double sampleRate) noexcept;

    Performer() noexcept;

    // Not real-time. Prepares the engine and starts the performance clock
    // from zero. Channel layout the player was given (MPE zone, conventional
    // bend ranges, string-per-channel mode) is kept; bend wheels,
    // RPN selections and held events are cleared.
    void prepare(double sampleRate, int maximumBlockSize);

    // The construction and mix controls; call any time, typically once per
    // block before beginBlock.
    void setParameters(const EngineParameters& parameters) noexcept;

    // Gather Chords (off by default). While on, every event is held back by
    // latencySamples(), so the host must be told that latency. Switching it
    // off releases what is held at the start of the next block, in order.
    void setGatherChords(bool gather) noexcept { gatherChords_ = gather; }
    [[nodiscard]] bool isGatheringChords() const noexcept { return gatherChords_; }
    [[nodiscard]] int latencySamples() const noexcept
    {
        return gatherChords_ ? gatherWindow_ : 0;
    }

    // A pitch offset every channel's bend carries on top of its wheel, for a
    // host's global tuning (Reason's master tune). Zero, the default, is an
    // exact no-op. Applied at once, from the next rendered sample.
    void setMasterTuneCents(float cents) noexcept;

    // Panic: silences the engine and forgets held events, bend wheels and
    // RPN selections. The channel layout is kept (see prepare).
    void reset() noexcept;

    // --- one block -----------------------------------------------------------
    // left and right receive numSamples samples; endBlock overwrites all of
    // them. The pointers must stay valid until endBlock.
    void beginBlock(float* left, float* right, int numSamples) noexcept
    {
        beginBlock(left, right, AcustraEngine::OutputBuses {}, numSamples);
    }
    // Main plus the separate outputs (see AcustraEngine::OutputBuses): each
    // non-null bus pointer also receives numSamples samples, all rendered in
    // the same pass as Main; a null one is not rendered. Main is the same
    // whichever are wanted.
    void beginBlock(float* left, float* right,
                    const AcustraEngine::OutputBuses& buses,
                    int numSamples) noexcept;
    // One MIDI message at sampleOffset in this block (clamped to
    // [0, numSamples]). Offsets must not decrease within a block, as a
    // MidiBuffer's do; one earlier than an event already handled plays at
    // that event's time. Messages longer than three bytes (SysEx) only mark
    // their sample.
    void handleMidi(int sampleOffset, const std::uint8_t* data, int size) noexcept;
    void endBlock() noexcept;
    // A block without events.
    void process(float* left, float* right, int numSamples) noexcept
    {
        beginBlock(left, right, numSamples);
        endBlock();
    }
    void process(float* left, float* right,
                 const AcustraEngine::OutputBuses& buses,
                 int numSamples) noexcept
    {
        beginBlock(left, right, buses, numSamples);
        endBlock();
    }

    // --- MIDI spelt for front ends without MIDI ------------------------------
    // Channels are 1-16, data values 0-127; out-of-range values are clamped.
    void noteOn(int sampleOffset, int channel, int note, int velocity) noexcept;
    // Release velocity is spelt into the Note Off and read by nothing: a
    // key-up damps its note however fast it is lifted.
    void noteOff(int sampleOffset, int channel, int note,
                 int releaseVelocity = 64) noexcept;
    void controlChange(int sampleOffset, int channel, int controller,
                       int value) noexcept;
    // -1 (full down) to +1 (full up), 0 centred, as 14-bit pitch bend. The
    // bend in semitones is this times the channel's RPN 0 range.
    void pitchWheel(int sampleOffset, int channel, float position) noexcept;
    void channelPressure(int sampleOffset, int channel, int value) noexcept;
    // RPN 0 (pitch bend sensitivity) followed by the null RPN.
    void setPitchBendRange(int sampleOffset, int channel, int semitones,
                           int cents = 0) noexcept;

    // --- engine and observers ------------------------------------------------
    // For setup the player does not cover (setPortObserversEnabled) and for
    // observers (getActiveVoiceCount, heldString). Playing the engine
    // directly bypasses the player's state.
    [[nodiscard]] AcustraEngine& engine() noexcept { return engine_; }
    [[nodiscard]] const AcustraEngine& engine() const noexcept { return engine_; }
    // Events lost to a full queue since prepare (see the capacities above).
    [[nodiscard]] std::uint32_t droppedEventCount() const noexcept
    {
        return droppedEvents_;
    }

private:
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
    };

    // A due time counts samples since prepare.
    struct HeldEvent
    {
        std::int64_t due { 0 };
        std::array<std::uint8_t, 3> bytes {};
        int size { 0 };
        bool gathered { false };
    };

    // MIDI 1.0 RPN/NRPN selection state for one channel, as JUCE's
    // MidiRPNDetector keeps it: 0xff is "not received".
    struct RpnState
    {
        std::uint8_t parameterMsb { 0xff };
        std::uint8_t parameterLsb { 0xff };
        std::uint8_t valueMsb { 0xff };
        std::uint8_t valueLsb { 0xff };
        bool nrpn { false };
    };

    bool handleEvent(int eventSample, const std::uint8_t* data, int size) noexcept;
    void flushNoteGroup() noexcept;
    void gatherChord(int first) noexcept;
    void renderTo(int sample) noexcept;
    void dispatchMidiData(const std::uint8_t* data, int size) noexcept;
    bool processRpnController(int midiChannel, int controller, int value) noexcept;
    void setLowerZoneMemberCount(int memberCount) noexcept;
    void refreshPitchBend(int midiChannel) noexcept;
    void resetControllerScope(int midiChannel) noexcept;
    [[nodiscard]] bool channelIsInControllerScope(int controllerChannel,
                                                  int targetChannel) const noexcept;
    void queueMessage(int sampleOffset, std::uint8_t status, int data1,
                      int data2, int size) noexcept;

    AcustraEngine engine_;

    // Playing state that outlives a block.
    // A strum's stroke alternates; the sample clock tells a rest from a beat.
    bool strumUpstroke_ { false };
    bool gatherChords_ { false };
    double sampleRate_ { 48000.0 };
    std::int64_t processedSamples_ { 0 };
    std::int64_t lastStrumSample_ { -1 };
    std::array<RpnState, 16> rpnStates_ {};
    std::array<float, 16> rawPitchWheels_ {};
    std::array<float, 16> conventionalPitchBendRanges_ {};
    float lowerMasterPitchBendRange_ { 2.0f };
    float lowerMemberPitchBendRange_ { 48.0f };
    int lowerZoneMemberCount_ { 0 };
    float masterTuneSemitones_ { 0.0f };
    std::array<HeldEvent, heldEventCapacity> held_ {};
    int heldCount_ { 0 };
    int gatherWindow_ { 0 };
    std::uint32_t droppedEvents_ { 0 };

    // The block being played.
    float* left_ { nullptr };
    float* right_ { nullptr };
    AcustraEngine::OutputBuses buses_ {};
    int blockSamples_ { 0 };
    int renderedTo_ { 0 };
    bool direct_ { true };
    int groupedSample_ { -1 };
    std::array<PendingNoteOn, sampleGroupCapacity> pendingNoteOns_ {};
    int pendingNoteOnCount_ { 0 };
    std::array<PendingNoteOff, sampleGroupCapacity> pendingNoteOffs_ {};
    int pendingNoteOffCount_ { 0 };
    std::array<bool, 16> cancelledNoteOns_ {};
};
} // namespace acustra
