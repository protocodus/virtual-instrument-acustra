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
// CC2 bridge hand, CC64 sustain, explicit CC65+CC84 connected articulations,
// CC74 and channel pressure for MPE,
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
    // once can still exceed it. A full queue loses its oldest continuous
    // gesture (bend, pressure, an ordinary controller), whose later value
    // supersedes it, so a wheel's last value is the one that survives; it
    // never loses a message that starts or ends a note or sets a mode, the
    // oldest queued continuous event making room for one of those, so a
    // key-up is never lost. Messages the Performer ignores are never queued.
    // What is lost is counted (droppedEventCount), and nothing ever
    // allocates.
    static constexpr int heldEventCapacity = 1024;
    // Note Ons (and, separately, Note Offs) that one sample can group. A
    // 129th Note On on one sample is dropped and counted; a 129th Note Off
    // is applied at once, ahead of the group's Note Ons.
    static constexpr int sampleGroupCapacity = 128;

    // Stroke direction. Automatic: a strum within strumAlternationBeats of
    // the previous one, a dotted eighth at the host tempo, is the strumming
    // hand's return and alternates its stroke; after a longer gap it is a
    // downstroke again, as an isolated chord is. The dotted eighth takes in
    // eighth-note and sixteenth-note patterns, swung eighths and triplets,
    // and leaves quarter-note chords all downstrokes. A rest longer than
    // strumRestSeconds restarts whatever the tempo; a front end that stops
    // rendering while silent (the Rack Extension idles) must not idle for
    // less than that after the last note, or the rest would go unmeasured.
    // The keyswitches, below the guitar's range, latch a direction on every
    // channel until the next keyswitch, reset or prepare: C0 makes every
    // strum a downstroke, C#0 an upstroke, D0 returns to automatic. They
    // never sound and take no string; their key-ups are consumed with them.
    enum class StrokeMode { Automatic, Down, Up };
    static constexpr double strumAlternationBeats = 0.75;
    static constexpr double strumRestSeconds = 2.0;
    static constexpr int keyswitchDownstroke = 24; // C0
    static constexpr int keyswitchUpstroke = 25;   // C#0
    static constexpr int keyswitchAutomatic = 26;  // D0
    [[nodiscard]] static constexpr bool isKeyswitch(int note) noexcept
    {
        return note >= keyswitchDownstroke && note <= keyswitchAutomatic;
    }
    // The keyswitches' effect, for a front end's own control; it takes
    // effect at once, like setSustainHold.
    void setStrokeMode(StrokeMode mode) noexcept { strokeMode_ = mode; }
    [[nodiscard]] StrokeMode strokeMode() const noexcept { return strokeMode_; }

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
    // Supply the host's current quarter-note tempo before this block's MIDI.
    // The engine keeps it across reset/prepare; invalid values use 120 BPM.
    void setTempoBpm(double bpm) noexcept;
    // A tempo notification within the current block. Audio before its
    // sample keeps the previous tempo; pending notes on that sample use
    // the new tempo. Host tempo is not delayed by Gather Chords.
    void setTempoBpmAt(int sampleOffset, double bpm) noexcept;

    // Gather Chords (off by default). While on, every event is held back by
    // the window (gatherWindowSamples at the engine's rate). Switching it
    // off releases what is held at the start of the next block, in order.
    void setGatherChords(bool gather) noexcept { gatherChords_ = gather; }
    [[nodiscard]] bool isGatheringChords() const noexcept { return gatherChords_; }
    // Sustain, the panel switch (AcustraEngine::setSustainHold): while on,
    // every key-up on every channel rings on as under its pedal, until the
    // switch is turned off; CC64 adds nothing while it is on and keeps its
    // own notes when it goes off. Call before beginBlock, like
    // setGatherChords; it takes effect at once and is never held back by
    // Gather Chords. Off, the default, is an exact no-op.
    void setSustainHold(bool hold) noexcept { engine_.setSustainHold(hold); }
    [[nodiscard]] bool isHoldingSustain() const noexcept
    {
        return engine_.isSustainHeld();
    }
    // Everything the host must be told: the engine's own fixed output
    // latency (AcustraEngine::outputLatencySamples) plus, while gathering,
    // the window.
    [[nodiscard]] int latencySamples() const noexcept
    {
        return latencySamples(gatherChords_, gatherWindow_);
    }
    [[nodiscard]] static constexpr int latencySamples(bool gather,
                                                      int gatherWindow) noexcept
    {
        return AcustraEngine::outputLatencySamples() + (gather ? gatherWindow : 0);
    }

    // A host's global reference tuning (Reason's master tune). Retensions
    // every string at its existing fret, independently of conventional/MPE
    // wheels, and survives prepare/reset. Clamped to +/-100 cents. Zero is an
    // exact no-op; nonfinite input uses zero. The existing delay/port followers
    // smooth live changes.
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
    // Main plus the separate Piezo output (see AcustraEngine::OutputBuses):
    // a non-null buses.piezo also receives numSamples samples, rendered in
    // the same pass as Main and split at events with it; a null one is not
    // rendered. Main is the same whether or not it is wanted.
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
    // Finish only this prefix when a front end splits for a control change.
    // The unrendered suffix is untouched, and the clock advances by exactly
    // the prefix. Start the suffix with a new beginBlock after the change.
    void endBlockAt(int sampleOffset) noexcept;
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
    // Release velocity is spelt into the Note Off: a key-up damps its note
    // however fast it is lifted. An explicit firm contact shortens its
    // damping tail and sets the optional release noise; 64 is nominal.
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
        // Whether the pedal held this key-up when it arrived, and whether a
        // pedal change on its sample came after it (see handleEvent).
        bool sustained { false };
        bool pedalMoved { false };
        // This key-up was held, then the last sustaining pedal came up on
        // its sample. The explicit pedal contact must bypass ordinary grace.
        bool pedalReleased { false };
        // MIDI's release velocity, 0-1, or -1 when the key-up carried none
        // (a Note On at velocity 0, or a Note Off at 0).
        float releaseVelocity { -1.0f };
    };

    // A due time counts samples since prepare.
    struct HeldEvent
    {
        std::int64_t due { 0 };
        std::array<std::uint8_t, 3> bytes {};
        int size { 0 };
        bool gathered { false };
        bool tempo { false };
        double tempoBpm { 120.0 };
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
    void handleTempoEvent(int eventSample, double bpm) noexcept;
    void flushNoteGroup() noexcept;
    void gatherChord(int first) noexcept;
    void renderTo(int sample) noexcept;
    void dispatchMidiData(const std::uint8_t* data, int size) noexcept;
    bool processRpnController(int midiChannel, int controller, int value) noexcept;
    void setLowerZoneMemberCount(int memberCount) noexcept;
    void refreshPitchBend(int midiChannel) noexcept;
    void playNote(int note, float velocity, int channel,
                  int delay = 0, bool strumming = false,
                  bool unambiguousTransition = true) noexcept;
    [[nodiscard]] bool isLowerZoneMember(int midiChannel) const noexcept;
    void resetControllerScope(int midiChannel) noexcept;
    [[nodiscard]] bool channelIsInControllerScope(int controllerChannel,
                                                  int targetChannel) const noexcept;
    void queueMessage(int sampleOffset, std::uint8_t status, int data1,
                      int data2, int size) noexcept;
    // Drops the oldest queued continuous gesture to make room in a full
    // queue; false when nothing continuous is queued.
    bool evictContinuousHeldEvent() noexcept;

    AcustraEngine engine_;

    // Playing state that outlives a block.
    // A strum's stroke alternates; the sample clock and the tempo tell a
    // hand's return from a new stroke, unless a keyswitch has latched one.
    bool strumUpstroke_ { false };
    StrokeMode strokeMode_ { StrokeMode::Automatic };
    double tempoBpm_ { 120.0 };
    bool gatherChords_ { false };
    double sampleRate_ { 48000.0 };
    std::int64_t processedSamples_ { 0 };
    std::int64_t lastStrumSample_ { -1 };
    std::array<RpnState, 16> rpnStates_ {};
    std::array<float, 16> rawPitchWheels_ {};
    // Both messages are required: CC65 never infers legato from overlap.
    // CC84 names one source note for one subsequent target on its channel.
    std::array<bool, 16> portamentoSwitches_ {};
    std::array<int, 16> portamentoSources_ {};
    std::array<float, 16> conventionalPitchBendRanges_ {};
    float lowerMasterPitchBendRange_ { 2.0f };
    float lowerMemberPitchBendRange_ { 48.0f };
    int lowerZoneMemberCount_ { 0 };
    // The channel whose CC1 set the wheel's vibrato now; 0 for none.
    int vibratoChannel_ { 0 };
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
};
} // namespace acustra
