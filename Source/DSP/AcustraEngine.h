// Acustra: a physically modelled acoustic-guitar instrument.
//
// Runtime output uses six stiff-string waveguides coupled through a shared
// passive bridge and a measurement-derived modal body and radiation model.

#pragma once

#include "FittedPhysicalData.h"

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>

// Capacity of the per-material measured banks: the larger of the two banks in
// each generated header. AcustraEngine.cpp static-asserts that both fit, so a
// regenerated header that grows fails to build rather than to sound.
#if !defined(ACUSTRA_BRIDGE_MODE_COUNT)
#define ACUSTRA_BRIDGE_MODE_COUNT 56
#endif
#if !defined(ACUSTRA_BODY_MODE_COUNT)
#define ACUSTRA_BODY_MODE_COUNT 141
#endif

namespace acustra
{

enum class BodyShape
{
    Parlor,
    Auditorium,
    Dreadnought,
    Jumbo
};

enum class BodyMaterial
{
    Spruce,
    Cedar,
    Mahogany,
    Maple
};

enum class StringMaterial
{
    Nylon,
    Steel
};

// Legacy numeric values remain valid for offline renderers; sanitisation maps
// retired microphones to MonoMic and retired pickups to loaded Piezo.
enum class CaptureType
{
    StereoMic,
    TrebleMic,
    BassMic,
    SaddlePiezo,
    Magnetic,
    UpperMic,
    LoadedPiezo,
    MonoMic,
    Piezo = LoadedPiezo
};

enum class Tuning
{
    Standard,
    DropD,
    Dadgad,
    OpenG,
    HalfStepDown
};

enum class PickingTechnique
{
    Finger,
    Pick,
    Thumb
};

enum class BridgeModel
{
    Original,
    FyldeSteel
};

// Values 2-4 were the Washburn 1897, Santa Cruz OM 2022 and Martin D18V 2007,
// fitted from Mark Rau's measurements, which carry no redistribution license.
// They are retired: sanitisation maps any of those values to Original.
enum class GuitarModel
{
    Original,
    Bellido1978
};

struct EngineParameters
{
    BodyShape shape { BodyShape::Dreadnought };
    BodyMaterial bodyMaterial { BodyMaterial::Spruce };
    StringMaterial stringMaterial { StringMaterial::Steel };
    CaptureType capture { CaptureType::StereoMic };
    Tuning tuning { Tuning::Standard };
    PickingTechnique picking { PickingTechnique::Finger };
    BridgeModel bridgeModel { BridgeModel::Original };
    GuitarModel guitarModel { GuitarModel::Original };
    float stringAge { 0.15f };       // 0 fresh, 1 worn/dead
    float pluckPosition { 0.28f };   // 0 bridgeward, 1 neckward
    float touch { 0.58f };           // 0 soft/dark, 1 hard/bright
    float bodyAmount { 0.82f };      // measurement-derived body radiation
    float stereoWidth { 0.62f };     // authored per-mode stereo gain spread
    float outputGain { 0.42f };      // linear
};

struct AcustraEngineTestAccess;

class AcustraEngine
{
public:
    static constexpr int stringCount = 6;
    static constexpr int fretCount = 20;

    AcustraEngine() noexcept;

    void prepare(double sampleRate, int maximumBlockSize);
    void reset() noexcept;
    void setParameters(const EngineParameters& parameters) noexcept;

    // Setup/offline-fitting control. If already prepared, changing the
    // calibration resets the engine; do not call it from the audio thread.
    void setPhysicalCalibration(const PhysicalCalibration&) noexcept;

    // Call once before the noteOn() calls for one strum's strings (not for a
    // single note): draws this stroke's own pick-speed variation, shared by
    // every string noteOn() schedules with strumMember set until the next
    // beginStrum() call. Scaling every string's delay by the same drawn
    // factor is what keeps a stroke's own strings in order even though its
    // total span varies stroke to stroke -- see noteOn.
    void beginStrum() noexcept;
    // A pluck can be scheduled: the string is taken and fretted now, the
    // fretting hand having formed the chord, and released this many samples
    // later, which is how a strum reaches its strings one after another.
    // strumMember marks a note as one string of a strum (including its
    // first, undelayed string): its scheduled delay is scaled by the
    // stroke's beginStrum() draw and its level draws its own jitter,
    // bounded to what repeated real strums vary by; a single note leaves it
    // false and is unaffected down to the bit.
    void noteOn(int midiNote, float velocity, int midiChannel = 1,
                int pluckDelaySamples = 0, bool strumMember = false) noexcept;
    // Call just before the noteOn() calls for notes that arrive together on
    // one sample and one channel (a sequenced chord, or one the plug-in has
    // gathered): the fretting hand forms them as one shape, one note per
    // string within one hand span, instead of fretting them one at a time
    // (see planChord in AcustraEngine.cpp). The plan belongs to this sample
    // only; notes it does not name, and every note when it is not called,
    // are placed one at a time as before. Inert for string-per-channel
    // controllers and MPE member channels, whose strings are their own.
    void planChord(const int* midiNotes, int count, int midiChannel = 1) noexcept;
    // Observer for displays and tests: the string (0 is the low E) whose key
    // is down for this note on this channel, or -1.
    [[nodiscard]] int heldString(int midiNote, int midiChannel = 1) const noexcept;
    // Samples after the first string that a strum's k-th string sounds, from
    // the pick's speed for this velocity and the string spacing.
    [[nodiscard]] int strumDelaySamples(int stringRank,
                                        float velocity) const noexcept;
    // Ordinary key-up damps the existing note at every release velocity.
    // With CC68 legato explicitly enabled, fingerLift requests an active
    // fretting-hand lift/pull-off; zero keeps the finger touching the string.
    // That articulation adds the velocity law's energy capped by the fret's
    // stored elastic energy, so it can audibly excite the target/open note.
    void noteOff(int midiNote, int midiChannel = 1,
                 float fingerLift = 0.0f) noexcept;
    void setSustainPedal(bool down, int midiChannel = 1) noexcept;
    // Continuous bridge-hand damping, 0 open to 1 fully muted. Exposed as a
    // controller rather than a panel control: it is a playing pressure, and
    // zero is an exact no-op.
    void setPalmMutePressure(float pressure) noexcept;
    // MIDI's Legato Footswitch, CC68. While it is down a note that a string
    // already sounding can reach is hammered on rather than replucked, and
    // releasing it pulls off to whatever that string is still holding. Up is
    // an exact no-op, which is the default.
    void setLegato(bool on) noexcept;
    void setPitchBend(float semitones, int midiChannel = 1) noexcept;
    // MIDI's Modulation Wheel, CC1, as the left hand's vibrato. Zero is an
    // exact no-op, which is the default; see the vibrato map in
    // AcustraEngine.cpp for what the wheel is bounded by.
    void setVibrato(float amount) noexcept;
    // Acustra implements the MPE lower zone only: channel 1 is its manager
    // and the contiguous channels above it are members. Zero restores
    // conventional, fully independent MIDI channels.
    void setLowerZoneMemberCount(int memberCount) noexcept;
    // MPE Timbre, CC74, on a lower-zone member channel: where the string is
    // met this note (Traube and Smith DAFx-00; Traube and Depalle DAFx-03),
    // in place of the panel Pluck Position for that one pluck. value is 0-1
    // MIDI CC74; a negative value clears it back to the panel control. Read
    // once, at the pluck, like the panel control it replaces; inert on a
    // conventional or manager channel and inert with no lower zone.
    void setMpeTimbre(float value, int midiChannel) noexcept;
    // MPE channel pressure, 0xD0, on a lower-zone member channel: the
    // fretting hand's grip. It biases how deep the wheel's vibrato reaches
    // (see vibratoSemitones) and nothing else; it adds no string energy of
    // its own. It does NOT reach a pull-off's lift -- liftFinger's own
    // comment records the energy discontinuity that took it back out.
    // 0-1; inert on a conventional or manager channel and inert with no
    // lower zone.
    void setMpePressure(float value, int midiChannel) noexcept;
    // Opt-in guitar-controller mode: channels 1-6 are the six strings
    // directly, bypassing chooseString's fret-distance guess. It is the
    // layout Roland's GK and Fishman's TriplePlay produce in "mono mode",
    // but neither is documented to transmit a message requesting it -- see
    // the CC126 handler in PluginProcessor.cpp for what actually toggles it. A note
    // on channel 1-6 with no playable fret on that channel's string is
    // dropped rather than reassigned. Off, the default, is an exact no-op.
    void setStringPerChannelMode(bool enabled) noexcept;
    [[nodiscard]] bool isStringPerChannelMode() const noexcept
    {
        return stringPerChannelMode_;
    }
    void allNotesOff(int midiChannel = 1) noexcept;
    void allSoundOff(int midiChannel = 1) noexcept;
    void setBridgeCouplingEnabled(bool enabled) noexcept;
    // Offline/test isolation only; omits idle-string ports from the junction.
    // Their delay lines still receive its return, and all six anchor springs
    // remain present. This changes bridge loading and the resulting motion.
    void setSympatheticStringsEnabled(bool enabled) noexcept;
    // The port observers - getLastBridgeTailForce and the three
    // getLastBridge*Power ledgers - and the moment histories behind them
    // cost a tenth of a held chord and never reach the output. A host that
    // does not read them (the plug-in, the Rack Extension) can turn them
    // off; the audio is bit-identical either way. On by default, for the
    // tests and tools that read them; switching restarts them from rest.
    void setPortObserversEnabled(bool enabled) noexcept;

    void process(float* left, float* right, int numSamples) noexcept;

    [[nodiscard]] int getActiveVoiceCount() const noexcept;
    [[nodiscard]] int getSympatheticStringCount() const noexcept;
    [[nodiscard]] float getLastBridgeVelocity() const noexcept;
    [[nodiscard]] float getLastBridgeReactionForce() const noexcept;
    [[nodiscard]] float getLastBridgeBodyForce() const noexcept;
    [[nodiscard]] float getLastBridgeTailForce() const noexcept;
    // Legacy separate-sympathy observer, now zero: idle-string reactions enter
    // the shared junction and are already included in the body force above.
    [[nodiscard]] float getLastSympatheticRadiationForce() const noexcept;
    // The played strings' axial wave, observed separately from the two-way
    // junction because its current radiation surrogate remains one-way.
    [[nodiscard]] float getLastLongitudinalForce() const noexcept;
    // Zero-state port-power observers retain the work that enters at note-on.
    // Acoustic derivatives re-reference a newly established pluck shape;
    // they cannot account for that initial state in a passivity ledger.
    [[nodiscard]] float getLastBridgePower() const noexcept;
    [[nodiscard]] float getLastBridgeBodyPower() const noexcept;
    [[nodiscard]] float getLastBridgeTailPower() const noexcept;

private:
    friend struct AcustraEngineTestAccess;

    static constexpr int maximumDelaySamples = 8192;
    static constexpr int bodyModeCount = ACUSTRA_BODY_MODE_COUNT;
    static constexpr int bridgeModeCount = ACUSTRA_BRIDGE_MODE_COUNT;
    static_assert(bridgeModeCount < 255, "BridgeLoad::activeModes holds a byte");
    static constexpr int controlPeriod = 32;
    static constexpr int midiChannelCount = 16;
    static constexpr int legatoHeldLimit = 8;

    struct OnePole
    {
        float state { 0.0f };
        float previousInput { 0.0f };
        float delayedInputGain { 0.0f };
        float ratePole { 0.0f };
        bool remapped { false };

        void configureRate(float referencePole, double sampleRate) noexcept;
        float process(float input, float coefficient) noexcept
        {
            if (remapped)
            {
                state = input + ratePole * (state - input)
                      + delayedInputGain * (previousInput - input);
                previousInput = input;
                return state;
            }
            state = input + coefficient * (state - input);
            return state;
        }
        void reset() noexcept { state = previousInput = 0.0f; }
    };

    struct SecondOrderAllpass
    {
        float x1 { 0.0f };
        float x2 { 0.0f };
        float y1 { 0.0f };
        float y2 { 0.0f };
        float process(float input, float a1, float a2) noexcept
        {
            const float output = a2 * input + a1 * x1 + x2
                               - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = input;
            y2 = y1;
            y1 = output;
            return output;
        }
        void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }
    };

    struct FixedDerivative
    {
        std::array<float, 10> history {};
        int index { 0 };

        void reset(float value = 0.0f) noexcept
        {
            history.fill(value);
            index = 0;
        }
        float process(float input, float sampleRateRatio) noexcept;
        // A released shape entering the junction moves the wave variable
        // without the bridge having moved: the shape was standing on the
        // string before the finger let go. Re-reference the history to the
        // new level so this sample reports the motion the bridge already had
        // and the samples after it are differences again.
        float processAcrossRelease(float input, float sampleRateRatio) noexcept;
    };

    struct StringLoop
    {
        std::array<float, maximumDelaySamples> delay {};
        int writeIndex { 0 };
        float currentDelay { 128.0f };
        float targetDelay { 128.0f };
        float loopGain { 0.995f };
        float broadLossMix { 0.02f };
        float highLossMix { 0.1f };
        float broadLossCoefficient { 0.5f };
        float lowpassCoefficient { 0.5f };
        float dispersionA1 { 0.0f };
        float dispersionA2 { 0.0f };
        OnePole broadLossFilter {};
        OnePole lossFilter {};
        SecondOrderAllpass dispersion {};
        // The fractional-delay allpass's state: its two previous outputs.
        float allpassY1 { 0.0f };
        float allpassY2 { 0.0f };
        // Its coefficients for the last fraction read: once a delay has
        // slewed to its target the fraction repeats, and the two double
        // divisions that design it give the same coefficients again.
        // The fraction always lies in [1.1, 2.1), so -1 matches nothing.
        float thiranFraction { -1.0f };
        float thiranFirst { 0.0f };
        float thiranSecond { 0.0f };
        FixedDerivative bridgeDerivative {};
        bool derivativeNeedsPriming { true };
        bool derivativeCrossesRelease { false };
        // A hand's loss is a gain per round trip, and a contact settles over
        // one. Slewing the applied gain toward the requested one across a
        // round trip, in either direction, is what keeps the wave the
        // junction reads from stepping when a key is lifted.
        float appliedReleaseGain { 1.0f };
        float requestedReleaseGain { 1.0f };
        float releaseGainStep { 0.0f };

        void reset() noexcept;
        [[nodiscard]] float readDelay(float samples) noexcept;
        // Read-only point observation of both travelling waves; does not
        // advance the feedback allpass or alter the vibrating string.
        [[nodiscard]] float displacementAt(float fraction) const noexcept;
        float advance(float delaySmoothing, float releaseGain) noexcept;
        // A plucked string is released from rest, so the wave the bridge
        // reads was already standing there when the finger let go. Prime the
        // finite difference from the first value this loop actually produces
        // - which is the filtered, dispersed one advance() returns, not the
        // raw delay tap - or establishing the released shape reads as a
        // one-sample velocity impulse the size of the whole displacement.
        float bridgeVelocity(float incident, float sampleRateRatio) noexcept;
        void write(float value) noexcept;
    };

    // Double precision: a direct-form section's coefficients sit within
    // theta^2 of 2 and 1, and at 384 kHz a float held the bridge's air-mode
    // group's pole angles to only about 5%, which turned the heave's H1 by
    // 90 degrees against the rock.
    struct BridgeMode
    {
        double denominator1 { 0.0 };
        double denominator2 { 0.0 };
        double numerator1 { 0.0 };
        double numerator2 { 0.0 };
        double input1 { 0.0 };
        double output1 { 0.0 };
        double output2 { 0.0 };

        double processPast(double input) noexcept;
        void reset() noexcept
        {
            input1 = output1 = output2 = 0.0;
        }
    };

    // What the six strings and their anchors present to the saddle, in the
    // two coordinates the archive measures: sum over strings of Z*2a and of
    // u*Z*2a, of Z, u*Z and u^2*Z, and the same three moments of the anchor
    // stiffness. u is saddleLeverArm(string).
    struct BridgeDrive
    {
        float incidentHeave { 0.0f };
        float incidentRock { 0.0f };
        float impedance0 { 0.0f };
        float impedance1 { 0.0f };
        float impedance2 { 0.0f };
        float stiffness0 { 0.0f };
        float stiffness1 { 0.0f };
        float stiffness2 { 0.0f };
    };

    struct BridgeLoad
    {
        // The legacy names rotation/moment use normalized coordinates:
        // r = (x_treble - x_bass)/2 = a*theta and its conjugate load M/a,
        // where a is the assumed impact half-spacing. Rotation is therefore
        // a displacement, not radians; a height h transforms a horizontal
        // string port by h/a on both force and motion, not by h alone.
        // One slot past the measured modes carries the plate
        // conductance floor described in FittedPhysicalData.h. Every mode is
        // one pole pair carrying the residue matrix [[heave, cross],
        // [cross, rock]] of MeasuredBridgeData.h, so it needs two states:
        // one driven by the net force and one by the moment. A mode with no
        // rocking residue leaves its second state alone.
        std::array<BridgeMode, bridgeModeCount + 1> heaveModes {};
        std::array<BridgeMode, bridgeModeCount + 1> rockModes {};
        std::array<float, bridgeModeCount + 1> residueHeave {};
        std::array<float, bridgeModeCount + 1> residueCross {};
        std::array<float, bridgeModeCount + 1> residueRock {};
        std::array<bool, bridgeModeCount + 1> rocking {};
        // The slots process() runs, in index order: configureBridge drops
        // the ones whose section and residues are all zero (the padding past
        // a bank and modes above 0.45 fs), which add exactly zero.
        std::array<std::uint8_t, bridgeModeCount + 1> activeModes = [] {
            std::array<std::uint8_t, bridgeModeCount + 1> all {};
            for (std::size_t index = 0; index < all.size(); ++index)
                all[index] = static_cast<std::uint8_t>(index);
            return all;
        }();
        int activeModeCount { bridgeModeCount + 1 };
        float immediateHeave { 0.0f };
        float immediateCross { 0.0f };
        float immediateRock { 0.0f };
        float pastHeave { 0.0f };
        float pastRock { 0.0f };
        float tailIntegratedForce { 0.0f };
        float tailIntegratedMoment { 0.0f };
        float previousDisplacement { 0.0f };
        float previousRotation { 0.0f };
        float displacement { 0.0f };
        float rotation { 0.0f };
        float mainIntegratedForce { 0.0f };
        float mainIntegratedMoment { 0.0f };
        float bodyIntegratedForce { 0.0f };
        float bodyIntegratedMoment { 0.0f };

        void reset() noexcept;
        void process(const BridgeDrive& drive, float samplePeriod) noexcept;
    };

    struct BodyMode
    {
        float real {}, imaginary {}, momentReal {}, momentImaginary {};
        float poleReal {}, poleImaginary {};
        float leftReal {}, leftImaginary {}, rightReal {}, rightImaginary {};
        float upperReal {}, upperImaginary {};
        float leftMomentReal {}, leftMomentImaginary {};
        float rightMomentReal {}, rightMomentImaginary {};
        float upperMomentReal {}, upperMomentImaginary {};

        void process(float force, float moment, float& left,
                     float& right, float& upper) noexcept
        {
            const float nextReal = force + poleReal * real - poleImaginary * imaginary;
            const float nextImaginary = poleImaginary * real + poleReal * imaginary;
            const float nextMomentReal = moment + poleReal * momentReal
                                       - poleImaginary * momentImaginary;
            const float nextMomentImaginary = poleImaginary * momentReal
                                            + poleReal * momentImaginary;
            real = nextReal;
            imaginary = nextImaginary;
            momentReal = nextMomentReal;
            momentImaginary = nextMomentImaginary;
            left += 2.0f * (leftReal * real - leftImaginary * imaginary
                + leftMomentReal * momentReal - leftMomentImaginary * momentImaginary);
            right += 2.0f * (rightReal * real - rightImaginary * imaginary
                + rightMomentReal * momentReal - rightMomentImaginary * momentImaginary);
            upper += 2.0f * (upperReal * real - upperImaginary * imaginary
                + upperMomentReal * momentReal - upperMomentImaginary * momentImaginary);
        }
        // process() without the upper-bout sum: configureBody gives the
        // right channel and the mono microphone the same residues, so their
        // sums are the same numbers and renderBody forms that sum once. The
        // expressions are process()'s own, so every bit is what it returns.
        void processStereo(float force, float moment, float& left,
                           float& right) noexcept
        {
            const float nextReal = force + poleReal * real - poleImaginary * imaginary;
            const float nextImaginary = poleImaginary * real + poleReal * imaginary;
            const float nextMomentReal = moment + poleReal * momentReal
                                       - poleImaginary * momentImaginary;
            const float nextMomentImaginary = poleImaginary * momentReal
                                            + poleReal * momentImaginary;
            real = nextReal;
            imaginary = nextImaginary;
            momentReal = nextMomentReal;
            momentImaginary = nextMomentImaginary;
            left += 2.0f * (leftReal * real - leftImaginary * imaginary
                + leftMomentReal * momentReal - leftMomentImaginary * momentImaginary);
            right += 2.0f * (rightReal * real - rightImaginary * imaginary
                + rightMomentReal * momentReal - rightMomentImaginary * momentImaginary);
        }
        void reset() noexcept { real = imaginary = momentReal = momentImaginary = 0.0f; }
    };

    // Local-contact transport. Each tap is a fixed lossless
    // fractional delay; source samples enter once and the ordinary string
    // loop carries every subsequent round trip.
    struct ContactTravel
    {
        struct Tap
        {
            int whole { 0 };
            int order { 0 };
            double a1 { 0.0 }, a2 { 0.0 };
            double y1 { 0.0 }, y2 { 0.0 };
        };
        std::array<float, maximumDelaySamples> history {};
        std::array<Tap, 2> taps {};
        int writeIndex { 0 };
        int historyLength { 0 };
        int historyRemaining { 0 };
        bool active { false };
        void reset(float directDelay, float nutDelay) noexcept;
        std::array<float, 2> process(float source) noexcept;
    };

    // Everything configureVoice's result depends on, reduced to what it is
    // computed from: the engine-wide state configureVoice reads (calibration,
    // host rate, construction, tuning, bridge bank, every string's anchor
    // stiffness) enters as one generation count that changes whenever any of
    // it does; the voice's own pitch, tension, age and hand enter as their
    // exact bits. configureVoice writes the same values for an equal key, so
    // an equal key lets it keep what it already wrote.
    struct VoiceConfigurationKey
    {
        std::uint64_t generation { 0 }; // 0 matches nothing
        int stoppedMidi { 0 };
        int openMidi { 0 };
        std::uint32_t frequency { 0 };
        std::uint32_t tensionSemitones { 0 };
        std::uint32_t age { 0 };
        std::uint32_t palmMute { 0 };
        bool steel { false };

        bool operator==(const VoiceConfigurationKey& other) const noexcept
        {
            return generation == other.generation
                && stoppedMidi == other.stoppedMidi
                && openMidi == other.openMidi
                && frequency == other.frequency
                && tensionSemitones == other.tensionSemitones
                && age == other.age && palmMute == other.palmMute
                && steel == other.steel;
        }
    };

    struct Voice
    {
        std::array<StringLoop, 2> loops {};
        VoiceConfigurationKey configurationKey {};
        // A string taken for a new note is still vibrating. This carries that
        // vibration on under the hand damping the model already uses for a
        // stopped note, instead of deleting it, in both planes: the parallel
        // one holds most of a pluck and radiates through the rocking saddle.
        StringLoop tailLoop {};
        StringLoop tailParallelLoop {};
        ContactTravel tailContactTravel {};
        float tailDamping { 1.0f };
        // The retained virtual-string branch keeps the port it had at capture,
        // including its applied member bend, while the main voice is retuned.
        float tailCharacteristicImpedance { 0.0f };
        float tailLevel { 0.0f };
        int tailQuietSamples { 0 };
        bool tailActive { false };
        int openMidi { 40 };
        int midiNote { 40 };
        // 1 is a stopped note. Above that the string sounds open in its nth
        // mode, a natural harmonic, and the loop runs at the open pitch.
        int harmonic { 1 };
        int midiChannel { 1 };
        int fret { 0 };
        int ownerCount { 0 };
        // Notes the fretting hand is holding on this string, oldest first, so
        // that releasing the top one pulls off to the one under it. Empty
        // unless the legato footswitch is down, which is what keeps every
        // other path exactly as it was.
        std::array<int, legatoHeldLimit> legatoHeld {};
        int legatoHeldCount { 0 };
        bool played { false };
        bool keyDown { false };
        bool pedalHeld { false };
        bool mpeMember { false };
        bool memberPitchBendFrozen { false };
        std::uint64_t startOrder { 0 };
        std::uint32_t randomState { 1 };
        float velocity { 0.0f };
        float polarisationMix { 0.5f };
        float excitationEnvelope { 0.0f };
        float excitationDecay { 0.0f };
        float excitationColour { 0.0f };
        float excitationLowpass { 0.0f };
        // renderExcitation's lowpass coefficient for this colour and host
        // rate, which a burst keeps throughout: a powf per sample otherwise.
        std::uint32_t excitationCoefficientColour { 0xffffffffu };
        std::uint32_t excitationCoefficientRate { 0xffffffffu };
        float excitationCoefficient { 0.0f };
        // The Pick technique's contact transient is an impact and enters
        // broadband, bypassing the Finger burst's colour filter.
        bool excitationWhite { false };
        ContactTravel contactTravel {};
        float contactPeriodSamples { 0.0f };
        // Routing identity survives transport retirement: a drained contact
        // must not fall back to the former bridge-boundary source write.
        bool contactTravelEnabled { false };
        float characteristicImpedance { 0.5f };
        // A bend is a tension change at fixed length, so the port the string
        // presents moves with it: Z = sqrt(T mu) = Z0 times the frequency
        // ratio (see configureVoice). The junction sums impedances every
        // sample, so the requested scale is followed at the delay's own 6 ms
        // rate rather than stepping once per control period. Both are 1
        // without a bend, and a scale of exactly 1 leaves every product
        // bit-identical to the unbent engine.
        float bendImpedanceScale { 1.0f };
        float appliedBendImpedanceScale { 1.0f };
        // The string's tension now, in newtons, bend included. Kept so the
        // bend can be checked against Grimes' law rather than inferred.
        float tensionNewtons { 0.0f };
        float bridgeTailStiffness { 10000.0f };
        float attackPitchCents { 0.0f };
        float attackPitchDecay { 1.0f };
        float frozenMemberPitchBendSemitones { 0.0f };
        float attackSlopeEnergy { 0.0f };
        // Transverse motion stretches the string, and the tension it adds is
        // carried by the string's own longitudinal modes. Fixed-fixed axial
        // modes lie at integer multiples of c_long/(2L); the first two modes
        // with nonzero projection under an integrated extension drive retain
        // the measured-band cost of a compact real-time model while avoiding
        // the unphysical single-pole truncation.
        static constexpr int longitudinalModeCount = 2;
        std::array<float, longitudinalModeCount> longitudinalY1 {};
        std::array<float, longitudinalModeCount> longitudinalY2 {};
        std::array<float, longitudinalModeCount> longitudinalA1 {};
        std::array<float, longitudinalModeCount> longitudinalA2 {};
        std::array<float, longitudinalModeCount> longitudinalB0 {};
        float longitudinalDrive { 0.0f };
        float observedSlopeEnergy { 0.0f };
        float dispersionDesignFrequency { 0.0f };
        float dispersionDesignInharmonicity { -1.0f };
        float dispersionDesignAge { -1.0f };
        float dispersionDesignFrequencyLossScale { -1.0f };
        // Exact arguments of the last completed dispersion solve. Frequency
        // is positive, so the zero-initialized key cannot be a valid hit.
        std::array<double, 7> dispersionDesignArguments {};
        // The fraction both polarisation loops were lengthened by so that the
        // pair of modes they form through a rocking saddle is heard at the
        // requested pitch (coupledPolarisationDetune); zero elsewhere.
        float polarisationDetune { 0.0f };
        float dispersionDecayRatio { 10.0f };
        float dispersionPoleRatio { 4.0f };
        float level { 0.0f };
        float releaseDamping { 1.0f };
        float fingerLift { 0.0f };
        // The lifting finger still touches the string until it has risen
        // clear of it; that contact is the hand loss for this many samples.
        float touchDamping { 1.0f };
        int touchSamples { 0 };
        // Samples until a released string is handed back to the allocator.
        int returnSamples { 0 };
        // Samples until a scheduled pluck is released; zero when none waits.
        int pluckDelay { 0 };
        // A held string keeps its wave until this scheduled re-pluck fires.
        bool repluckPending { false };
        // Where this pluck landed, as a fraction of the sounding length.
        float pluckPoint { 0.0f };
        // Set by noteOn's strumMember argument and read once by
        // initialisePluck for its own level jitter; noteOn itself reads it
        // to scale this string's delay by the stroke's shared
        // strumSpeedScale_ (see beginStrum). Both are on top of the pluck
        // point every note already draws. A single note leaves this false,
        // so it draws exactly as it did before and stays bit-identical.
        bool strumming { false };
        // The engine's sample clock when this note was fretted; a chord
        // still forming is the run of notes whose onsets are close together.
        std::uint64_t onsetSample { 0 };
    };

    struct BodyOutput
    {
        float left { 0.0f };
        float right { 0.0f };
        float upper { 0.0f };
    };

    // The body bank as renderBody runs it: each BodyMode field as its own
    // array, so four modes advance as one vector operation where the
    // compiler has vector extensions (the Rack toolchain emits no vectors of
    // its own). bodyModes_ stays the configured record; load() copies its
    // coefficients here. The states live here only.
    struct BodyBank
    {
        static constexpr int lanes = 4;
        static constexpr int capacity = (bodyModeCount + lanes - 1) / lanes * lanes;
        using Lanes = std::array<float, capacity>;
        alignas(16) Lanes real {}, imaginary {}, momentReal {}, momentImaginary {};
        alignas(16) Lanes poleReal {}, poleImaginary {};
        alignas(16) Lanes leftReal {}, leftImaginary {}, rightReal {}, rightImaginary {};
        alignas(16) Lanes leftMomentReal {}, leftMomentImaginary {};
        alignas(16) Lanes rightMomentReal {}, rightMomentImaginary {};
        int count { 0 };

        // The first modeCount modes' coefficients; with resetStates their
        // states restart from rest, as configureBody's mode.reset() did.
        // Slots past the bank are zero, as configureBody's mode = {} was.
        void load(const std::array<BodyMode, bodyModeCount>& modes,
                  int modeCount, bool resetStates) noexcept;
        void reset() noexcept
        {
            real.fill(0.0f);
            imaginary.fill(0.0f);
            momentReal.fill(0.0f);
            momentImaginary.fill(0.0f);
        }
        // BodyMode::processStereo for every mode in index order, each sum
        // accumulated in that order, then the same flush of tiny states.
        BodyOutput render(float force, float moment) noexcept;
    };

    struct RadiationDelay
    {
        // Largest supplied delay is 1 ms; capacity covers 384 kHz plus interpolation.
        std::array<BodyOutput, 512> history {};
        float samples {};
        std::array<float, 6> weights { 0, 0, 1, 0, 0, 0 };
        int firstTap {}, writeIndex {};
        void configure(float delaySamples) noexcept;
        BodyOutput process(BodyOutput input) noexcept;
        void reset() noexcept { history.fill({}); writeIndex = 0; }
    };

    static EngineParameters sanitise(const EngineParameters&) noexcept;
    static PhysicalCalibration sanitise(const PhysicalCalibration&) noexcept;
    static std::array<int, stringCount> openNotes(Tuning) noexcept;
    static float midiFrequency(int midiNote) noexcept;
    static float clamp(float value, float low, float high) noexcept;
    static float phaseDelayForOnePoleMix(float coefficient, float mix,
                                         float omega) noexcept;
    static float magnitudeForOnePoleMix(float coefficient, float mix,
                                        float omega) noexcept;
    static float registeredPluckAperture(float apertureSamples,
                                         float apertureScale,
                                         float referenceDelay,
                                         float currentReferenceLength,
                                         float exponent) noexcept;
    void applyDiscreteParameters(bool force) noexcept;
    void updateControlState() noexcept;
    void configureBody() noexcept;
    void configureBridge() noexcept;
    float bridgePhaseDelay(float frequency, int stringIndex) const noexcept;
    // The saddle's mobility at one string's two ports, bridge and anchors in
    // parallel, at a frequency: the normal port at its lever arm, the
    // parallel port on the rocking (see saddleHeightRatio), and the transfer
    // between them. The last two are zero wherever rocking was not measured.
    struct PortMobility
    {
        std::complex<float> normal {};
        std::complex<float> transfer {};
        std::complex<float> parallel {};
        bool valid { false };
    };
    [[nodiscard]] PortMobility bridgePortMobility(float frequency,
                                                  int stringIndex) const noexcept;
    // bridgePortMobility's per-mode terms that do not depend on the string's
    // frequency - each included mode's prewarped omega (a tanf), its damping
    // and residues, and the plate floor's - for the bank, shape, host rate
    // and calibration in the key. configureVoice asks for them six times per
    // control update; they change only when one of those does.
    struct BridgeMobilityTable
    {
        struct Mode
        {
            float omega { 0.0f };
            float damping { 0.0f };
            float heave { 0.0f };
            float cross { 0.0f };
            float rock { 0.0f };
        };
        const void* bank { nullptr };
        std::array<std::uint32_t, 9> key {};
        bool valid { false };
        int count { 0 };
        std::array<Mode, bridgeModeCount> modes {};
        float scale { 1.0f };
        bool plate { false };
        float plateOmega { 0.0f };
        float plateDamping { 0.0f };
        float plateWeight { 0.0f };
    };
    const BridgeMobilityTable& bridgeMobilityTable() const noexcept;
    mutable BridgeMobilityTable bridgeMobilityTable_ {};
    float bridgePhaseDelay(const PortMobility& port, float frequency,
                           int stringIndex) const noexcept;
    // How far, as a fraction of the request, the pair of modes the two
    // polarisations form through a rocking saddle sits from the pitch the
    // normal loop was tuned to alone (see AcustraEngine.cpp).
    [[nodiscard]] float coupledPolarisationDetune(
        const PortMobility& port, float impedance, float bentImpedance,
        float frequency, float parallelExtraDelay, float normalGain,
        float parallelGain) const noexcept;
    // The saddle crown's height over the measured body's rocking axis, over
    // the normalized rocking coordinate's half-spacing: what projects a
    // string's horizontal (soundboard-parallel) force onto the rocking
    // moment, and the rocking displacement back onto its horizontal motion.
    [[nodiscard]] float saddleHeightRatio() const noexcept;
    // The six anchor stubs as the three moments of one stiffness matrix in
    // the saddle's two coordinates: sum K, sum uK, sum u^2 K.
    void bridgeAnchorMoments(float& stiffness0, float& stiffness1,
                             float& stiffness2) const noexcept;
    void configureVoice(Voice& voice, int stringIndex, int midiNote,
                        bool clearDelay) noexcept;
    void updateAttackPitch(Voice& voice, int stringIndex) noexcept;
    float effectiveTouch(float velocity) const noexcept;
    // MPE channel pressure for this voice's own member channel, -1 with no
    // lower zone, no CC message received yet on that channel, or off a
    // member channel; 0 is a real received value (light grip), not the
    // sentinel.
    [[nodiscard]] float mpePressureFor(const Voice& voice) const noexcept;
    [[nodiscard]] float vibratoSemitones(const Voice& voice,
                                         int fret) const noexcept;
    void initialisePluck(Voice& voice, int stringIndex, float velocity) noexcept;
    void returnToOpenString(Voice& voice, int stringIndex,
                            bool clearDelay) noexcept;
    void firePluck(Voice& voice, int stringIndex) noexcept;
    void beginRelease(Voice& voice, int stringIndex) noexcept;
    void captureTail(Voice& voice) noexcept;
    [[nodiscard]] float actionHeight(int stringIndex,
                                     float nutDistance) const noexcept;
    [[nodiscard]] float frettingClearance(int stringIndex, float fretDistance,
                                          float heldDistance) const noexcept;
    [[nodiscard]] float handContactGain(float frequency) const noexcept;
    [[nodiscard]] float pluckEnergy(float velocity, float soundingLength,
                                    float tension) const noexcept;
    void addReleasedTriangle(StringLoop& loop, float height,
                             float apexFraction, float sign) noexcept;
    void addUniformVelocity(StringLoop& loop, float plateau,
                            float extentFraction, float sign) noexcept;
    void addTriangleVelocity(StringLoop& loop, float scale,
                             float apexFraction, float sign) noexcept;
    // The Pick technique's released state (FittedPhysicalData.h): a rest
    // triangle of this height with its apex at position, a fraction of the
    // sounding length from the bridge, smoothed by the contact aperture (in
    // loop phase), plus the velocity the string leaves the tip with,
    // localised over that same aperture and carrying releaseShare of the
    // triangle's stored energy. Both are projected onto the nth-harmonic
    // node like the plucked shape. Replaces the line's first length samples.
    void writePickRelease(StringLoop& loop, int length, float height,
                          float position, float aperture, int modes,
                          float releaseShare, double slipPole) noexcept;
    double plectrumSlipPole(const Voice& voice, float releasedAmplitude,
                            float heldDistance, float soundingLength,
                            float scaleLength) const noexcept;
    static void applyPlectrumSlip(StringLoop& loop, int length,
                                  double slipPole) noexcept;
    void liftFinger(Voice& voice, int stringIndex, int targetMidi) noexcept;
    void hammerString(Voice& voice, int stringIndex, int previousMidi,
                      float velocity) noexcept;
    void resetSoundState() noexcept;
    void freezeMemberPitchBend(Voice& voice) noexcept;
    [[nodiscard]] bool isLowerZoneMaster(int midiChannel) const noexcept;
    [[nodiscard]] bool isLowerZoneMember(int midiChannel) const noexcept;
    [[nodiscard]] bool channelControlsVoice(int midiChannel,
                                            const Voice& voice) const noexcept;
    [[nodiscard]] bool sustainIsDown(const Voice& voice) const noexcept;
    [[nodiscard]] int chooseLegatoString(int midiNote,
                                        int midiChannel) const noexcept;
    bool releaseLegatoNote(int midiNote, int midiChannel,
                           float fingerLift) noexcept;
    int chooseString(int midiNote) const noexcept;
    int chooseStringWithoutHand(int midiNote) const noexcept;
    // The fretting hand (see chooseString). Each string remembers the last
    // fretted note it sounded and when a finger last held it there.
    struct HandFinger
    {
        int fret { 0 };
        std::uint64_t heldAt { 0 };
        bool valid { false };
    };
    using StringFrets = std::array<int, stringCount>;
    using HandWeights = std::array<float, stringCount>;
    [[nodiscard]] HandWeights handWeights() const noexcept;
    [[nodiscard]] static bool handKnown(const HandWeights& weights) noexcept;
    [[nodiscard]] float shapeCost(const StringFrets& shapeFrets,
                                  unsigned movableStrings,
                                  const HandWeights& weights) const noexcept;
    struct ShapeNote
    {
        int midiNote { 0 };
        // The string this note sounds on now (a chord member that may move),
        // or -1 for a note still to be placed.
        int current { -1 };
        // A string still ringing this pitch, which a repeat prefers.
        int ringing { -1 };
        // Only its current string: a key re-struck while it is held.
        bool fixed { false };
    };
    struct ShapeScore
    {
        int steals { 0 };
        int impossible { 0 };
        int moves { 0 };
        float cost { 0.0f };
        int misses { 0 };
        int opens { 0 };
        int fretSum { 0 };
        int ringing { 0 };
    };
    struct ShapeSearch
    {
        std::array<ShapeNote, stringCount> notes {};
        int count { 0 };
        unsigned movable { 0 };
        HandWeights weights {};
        StringFrets frets {};
        std::array<int, stringCount> strings {};
        std::array<int, stringCount> bestStrings {};
        ShapeScore best {};
        bool found { false };
    };
    [[nodiscard]] static bool betterShape(const ShapeScore& candidate,
                                          const ShapeScore& incumbent) noexcept;
    void searchShape(ShapeSearch& search, int index, unsigned used) const noexcept;
    [[nodiscard]] int reshapeFormingChord(int midiNote, int midiChannel,
                                          int chosenString) noexcept;
    void startNote(int stringIndex, int harmonic, int midiNote, float velocity,
                   int midiChannel, int delaySamples, bool strumMember) noexcept;
    void muteVacatedString(Voice& voice, int stringIndex) noexcept;
    void rememberFinger(int stringIndex) noexcept;
    void releaseFinger(int stringIndex) noexcept;
    struct HarmonicChoice
    {
        int string { -1 };
        int harmonic { 1 };
    };
    [[nodiscard]] HarmonicChoice chooseHarmonic(int midiNote) const noexcept;
    float renderExcitation(Voice& voice) noexcept;
    void finishVoice(Voice& voice, int stringIndex, float verticalIncident,
                     float horizontalIncident, float excitation,
                     float tailIncident, float tailParallelIncident,
                     float bridgeDisplacement,
                     float bridgeVelocity, float horizontalBridgeDisplacement,
                     float& directLeft,
                     float& directRight, float& sympatheticForce,
                     float& longitudinalForce) noexcept;
    BodyOutput renderBody(float bridgeInput, float bodyMoment) noexcept;
    float renderLoadedPiezo(float force) noexcept;
    float nextNoise(Voice& voice) noexcept;

    EngineParameters targetParameters_ {};
    EngineParameters parameters_ {};
    PhysicalCalibration physicalCalibration_ { fittedPhysicalCalibration };
    std::array<Voice, stringCount> voices_ {};
    std::array<BodyMode, bodyModeCount> bodyModes_ {};
    std::array<BodyMode, bodyModeCount> fadingBodyModes_ {};
    // What renderBody runs: the two banks above as BodyBank lanes, with
    // their states. Slots past a bank's own modes are all-zero padding
    // (configureBody); their contribution to every sum is exactly zero, so
    // BodyBank::count stops before them.
    BodyBank bodyBank_ {}, fadingBodyBank_ {};
    RadiationDelay bodyRadiationDelay_ {}, fadingBodyRadiationDelay_ {};
    GuitarModel configuredGuitarModel_ { GuitarModel::Original };
    BodyShape configuredBodyShape_ { BodyShape::Dreadnought };
    BodyMaterial configuredBodyMaterial_ { BodyMaterial::Spruce };
    StringMaterial configuredBodyStringMaterial_ { StringMaterial::Steel };
    bool bodyUpdatePending_ { false };
    // Completed dispersion solves by their exact arguments, shared by the
    // six strings: a chord change asks for a handful of designs a playing
    // hand keeps returning to, and each solve is an iterative 3x3 fit that
    // made the note-on batch several times a normal one. The solve is a pure
    // function of its arguments, so a hit is the same result.
    struct DispersionSolve
    {
        std::array<double, 7> arguments {};
        float decayRatio { 10.0f };
        float poleRatio { 4.0f };
        bool valid { false };
    };
    std::array<DispersionSolve, 64> dispersionSolves_ {};
    int nextDispersionSolve_ { 0 };
    // Advanced whenever engine state that configureVoice reads changes; see
    // VoiceConfigurationKey. Starts past the keys' never-matching zero.
    std::uint64_t voiceConfigurationGeneration_ { 1 };
    // The coupled body-shape factors configureBridge applies to the bridge
    // bank's A0 group, its modes up to T1 and the plate modes above; all
    // exactly 1 at each bank's anchor shape.
    float bridgeShapeA0_ { 1.0f };
    float bridgeShapeT1_ { 1.0f };
    float bridgeShapePlate_ { 1.0f };
    float bridgeShapeT1UpperHz_ { 0.0f };
    BridgeLoad bridgeLoad_ {};
    // Motion plus total/body/tail loads, each in heave and normalized rock.
    // Unlike acoustic histories these never re-prime at a note boundary.
    std::array<FixedDerivative, 8> bridgePowerDerivatives_ {};
    FixedDerivative bridgeVelocityDerivative_ {};
    std::array<float, 8> captureMix_ { 1.0f };
    float piezoLoadPole_ {}, piezoLoadGain_ {};
    float piezoLoadInput_ {}, piezoLoadOutput_ {};
    FixedDerivative bridgeRotationDerivative_ {};
    // The junction's power is the sum over both coordinates, so the moments
    // are differenced alongside the forces; the passivity tests read it.
    FixedDerivative bridgeForceMomentDerivative_ {};
    FixedDerivative bridgeBodyMomentDerivative_ {};
    FixedDerivative bridgeTailMomentDerivative_ {};
    FixedDerivative bridgeForceDerivative_ {};
    FixedDerivative bridgeBodyForceDerivative_ {};
    FixedDerivative bridgeTailForceDerivative_ {};
    double sampleRate_ { 48000.0 };
    float inverseSampleRate_ { 1.0f / 48000.0f };
    float delaySmoothing_ { 0.001f };
    float parameterSmoothing_ { 0.002f };
    float levelSmoothing_ { 0.0025f };
    std::array<float, midiChannelCount> pitchBendSemitones_ {};
    float vibrato_ { 0.0f };
    float vibratoPhase_ { 0.0f };
    float vibratoOnset_ { 0.0f };
    float lastBridgeVelocity_ { 0.0f };
    float lastBridgeReactionForce_ { 0.0f };
    float lastBridgeBodyForce_ { 0.0f };
    float lastBridgeTailForce_ { 0.0f };
    float lastSympatheticRadiationForce_ { 0.0f };
    float lastLongitudinalForce_ { 0.0f };
    float lastBridgePower_ { 0.0f };
    float lastBridgeBodyPower_ { 0.0f };
    float lastBridgeTailPower_ { 0.0f };
    float bodyAmount_ { 0.82f };
    float width_ { 0.62f };
    float outputGain_ { 0.42f };
    // The output reference's per-material factor (steelParallelPluckReference
    // in AcustraEngine.cpp), smoothed like the output control.
    float materialReference_ { 1.0f };
    float palmMute_ { 0.0f };
    float targetPalmMute_ { 0.0f };
    float palmMuteSmoothing_ { 0.5f };
    float bodyModelFade_ { 1.0f };
    float bodyModelFadeStep_ { 1.0f / 1920.0f };
    int controlCounter_ { 0 };
    int lowerZoneMemberCount_ { 0 };
    // -1 means no CC74 (mpeTimbre_) or channel pressure (mpePressure_) has
    // ever been received on that channel; the panel Pluck Position control
    // applies as it always did. Both persist across notes on the channel,
    // the way MPE per-channel controllers do, until the lower zone is
    // reconfigured or the engine is reset.
    std::array<float, midiChannelCount> mpeTimbre_ {};
    std::array<float, midiChannelCount> mpePressure_ {};
    bool stringPerChannelMode_ { false };
    std::array<bool, midiChannelCount> sustainPedals_ {};
    bool bridgeCouplingEnabled_ { true };
    bool sympatheticStringsEnabled_ { true };
    bool portObserversEnabled_ { true };
    // The strings do not leave the bridge when a note ends, so the junction
    // keeps the port they present rather than switching it out from under a
    // body that is still ringing.
    float lastImpedanceSum_ { 0.0f };
    float lastImpedanceMoment_ { 0.0f };
    float lastImpedanceInertia_ { 0.0f };
    bool bridgeDerivativesNeedPriming_ { true };
    bool bridgeDerivativesCrossRelease_ { false };
    bool legato_ { false };
    bool prepared_ { false };
    bool bodyConfigured_ { false };
    std::uint64_t noteOrder_ { 0 };
    // Samples rendered since reset(); the hand's memory and the chord window
    // are timed on it.
    std::uint64_t sampleClock_ { 0 };
    std::array<HandFinger, stringCount> hand_ {};
    std::uint64_t lastNoteOnSample_ { 0 };
    std::uint64_t chordStartSample_ { 0 };
    bool noteOnSeen_ { false };
    // planChord's shape for the notes of one sample.
    std::array<int, stringCount> plannedNotes_ {};
    std::array<int, stringCount> plannedStrings_ {};
    int plannedCount_ { 0 };
    int plannedChannel_ { 1 };
    std::uint64_t plannedSample_ { 0 };
    // Heijink and Meulenbroek, "On the Complexity of Classical Guitar
    // Playing: Functional Adaptations to Task Constraints", J. Motor
    // Behavior 34(4), 339-351 (2002): the scale fingering they call a small
    // span has the index and little fingers four frets apart counted
    // inclusively, the large span five, and the large one was rated more
    // complex; asked to finger note sequences themselves, six professional
    // guitarists moved the hand in only 2 of 31 fingerings, and then by a
    // single fret. So a hand in position covers four frets (the highest
    // fret it holds at most three above the lowest), a stretch reaches a
    // fifth, and moving the hand costs more than stretching it.
    static constexpr int handPositionSpan = 3;
    static constexpr int handStretchSpan = 4;
    // A fret held outside the four-fret position, against a fret the hand
    // must move by at full memory weight (1 per fret). Half is an ordering
    // choice -- stretching beats moving, as the study found -- not a figure.
    static constexpr float handStretchCost = 0.5f;
    // The listener's direction, not a measurement: a note released less
    // than handMemorySeconds ago still places the hand, weighted by
    // exp(-age / handMemoryTimeConstantSeconds); held notes weigh 1. With
    // nothing held and nothing that recent the hand is forgotten and the
    // allocator is exactly the handless one. Two seconds is the same rest
    // the plug-in already takes to restart a strum on a downstroke.
    static constexpr float handMemorySeconds = 2.0f;
    static constexpr float handMemoryTimeConstantSeconds = 1.0f;
    // Onsets closer than this are one chord still forming (Goebl, JASA
    // 110(1), 2001: a chord's unintended melody lead is 20-30 ms; the same
    // window the plug-in's Gather Chords uses).
    static constexpr float chordWindowSeconds = 0.030f;
    static constexpr float impossibleShapeCost = 1000.0f;
    // Shared across every string of one strum: drawn once by beginStrum(),
    // read by noteOn's strumMember path. A per-string draw here (rather than
    // each voice's own generator) is what keeps the pick's speed for the
    // whole stroke coherent, so a slower or faster draw scales every
    // string's delay together and never reorders them.
    std::uint32_t strumRandomState_ { 0x9e3779b9u };
    float strumSpeedScale_ { 1.0f };
    // Half-width of the uniform draw beginStrum() applies to strumSpeedScale_.
    // GuitarSet's comping tracks (Tools/MeasureStrums.py), pooled over runs
    // of >=3 repeats of one chord and direction, put a stroke's own total
    // span at a 36.47% standard deviation relative to its run's own mean
    // span -- a stroke that repeats at k times its run's usual pick speed
    // scales every string's delay by 1/k, so this relative figure, not the
    // corpus's absolute-ms one, is what a single per-stroke speed draw
    // should match. std of h*U(-1,1) is h/sqrt(3), so h = 0.3647*sqrt(3).
    static constexpr float strumSpeedJitterHalfWidth = 0.6317f;
};

} // namespace acustra
