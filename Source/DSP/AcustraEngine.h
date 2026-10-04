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

// Capacity of the measured banks: the largest bank a construction plays.
// That is the steel blend (SteelBodyBlend.h), g21's 132 radiation modes and
// the joint-pole body's 9 below its band, plus the 19 slots continuing the
// radiation above the fitted band (configureBody); the bridge is B's 47 modes
// and the joint body's 8. AcustraEngine.cpp static-asserts that every bank fits, so
// a regenerated header that grows fails to build rather than to sound.
// Slots past a construction's bank and its continuation cost no CPU, but
// every instance carries them as memory, the Bellido guitar too.
#if !defined(ACUSTRA_BRIDGE_MODE_COUNT)
#define ACUSTRA_BRIDGE_MODE_COUNT 56
#endif
#if !defined(ACUSTRA_BODY_MODE_COUNT)
#define ACUSTRA_BODY_MODE_COUNT 160
#endif

// Asks the compiler to inline a function at every call it can see, whatever
// its size (the hot string-loop and bridge-derivative recurrences).
#if defined(__clang__)
#define ACUSTRA_ALWAYS_INLINE __attribute__((always_inline))
#elif defined(_MSC_VER)
#define ACUSTRA_ALWAYS_INLINE __forceinline
#else
#define ACUSTRA_ALWAYS_INLINE
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
    Mahogany,
    Maple
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
    CaptureType capture { CaptureType::StereoMic };
    Tuning tuning { Tuning::Standard };
    PickingTechnique picking { PickingTechnique::Finger };
    GuitarModel guitarModel { GuitarModel::Original };
    float stringAge { 0.15f };       // 0 fresh, 1 worn/dead
    float pluckPosition { 0.28f };   // 0 bridgeward, 1 neckward
    float touch { 0.58f };           // 0 soft/dark, 1 hard/bright
    // The body's radiation against the direct sound at the contact (the
    // tool's click; the fitted bridge-local share ships at 0), so mostly a
    // level on the body.
    float bodyAmount { 0.82f };
    // Scales the stereo microphones' difference: 0 mono, 1 as measured.
    float stereoWidth { 0.62f };
    float outputGain { 0.42f };      // linear
    // The piezo mixed into Main under a microphone Capture, 0 none to 1 the
    // piezo at its full level (as Capture Piezo plays it) beside the
    // microphones. With Capture on Piezo the piezo is already all of Main,
    // so it adds nothing there.
    float piezoMix { 0.0f };
    // The key-up's own sound, 0 none to 1 twice the nominal level: the
    // damping finger or palm landing on the still-vibrating string, launched
    // into it from where it touches, and a faint brush of skin on a wound
    // string's winding (startReleaseNoise in AcustraEngine.cpp). 0.5 is the
    // nominal level. Read once at each key-up; zero is an exact no-op.
    float releaseNoise { 0.0f };
    // The room around the microphones, 0 none to 1 a microphone well out in
    // it: a small studio's early reflections and its 0.45 s reverberation
    // (RoomAmbience in AcustraEngine.cpp), on the microphone captures only.
    // At 0.5 the room's sound sits about 12.5 dB under a held chord's and
    // 10 dB under a released phrase's (whose own sound stops before its
    // room's), at the default controls, and 10 dB lower at each halving (its
    // send is 0.794 room^1.66). Zero is an exact no-op; a change lets the
    // sounding room ring out.
    float room { 0.0f };
};

struct AcustraEngineTestAccess;

class AcustraEngine
{
public:
    static constexpr int stringCount = 6;
    static constexpr int fretCount = 20;

    AcustraEngine() noexcept;

    // Models 8 kHz to 384 kHz: a finite rate outside that is clamped to the
    // nearer bound (and so plays off pitch), and one that is no rate at all
    // (NaN, infinite, zero or negative) falls back to 48 kHz.
    void prepare(double sampleRate, int maximumBlockSize);
    // The rate prepare() settled on, which every time constant follows.
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    // Main and the Piezo output are this many samples later than the
    // strings, at every rate and for every Capture: the piezo chain's
    // pipeline (renderPiezo), which the microphones wait out so the two
    // sensors blend on one time base. A front end reports it to the host.
    static constexpr int outputLatency = 7;
    [[nodiscard]] static constexpr int outputLatencySamples() noexcept
    {
        return outputLatency;
    }
    // The slots after a bank (and the steel blend's parts) that continue its
    // radiation above the fitted band (configureBody). Every construction of
    // a model at a rate holds the same number, the grid's modes past 18 kHz
    // or 0.45 fs silent, so a Shape or Wood change keeps each continuation
    // mode's state in its slot (sameBodyBank) whatever band the construction
    // in between had. At most 19 sound: the steel Jumbo in Spruce or
    // Mahogany, whose 8.1 kHz top is 18.4 grid steps under 18 kHz.
    static constexpr int radiationContinuationSlots = 19;
    void reset() noexcept;
    void setParameters(const EngineParameters& parameters) noexcept;

    // Setup/offline-fitting control. If already prepared, changing the
    // calibration resets the engine; do not call it from the audio thread.
    void setPhysicalCalibration(const PhysicalCalibration&) noexcept;

    // The MIDI notes of the open strings, low E string first, in a tuning:
    // what the engine tunes to and the lowest note each string can play.
    [[nodiscard]] static std::array<int, stringCount> openNotes(Tuning) noexcept;

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
    // The delay is bounded to [0, ten seconds]; a negative one plucks now.
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
    // The current chord plan's assignment (0 is the low E), or -1 for a
    // note the plan does not place. Read before noteOn consumes the plan,
    // so a player can time its stroke across the actual strings, including
    // strings skipped between them, rather than across pitches.
    [[nodiscard]] int plannedString(int midiNote, int midiChannel = 1) const noexcept;
    // Observer for displays and tests: the string (0 is the low E) whose key
    // is down for this note on this channel, or -1.
    [[nodiscard]] int heldString(int midiNote, int midiChannel = 1) const noexcept;
    // Samples after the first string that a strum's k-th string sounds, from
    // the pick's speed for this velocity and the string spacing.
    [[nodiscard]] int strumDelaySamples(int stringRank,
                                        float velocity) const noexcept;
    // Whether noteOn() would sound this note at all in the current tuning
    // and channel mode, rather than drop it (below the lowest string with no
    // harmonic to reach it, or off a string-per-channel string's frets).
    [[nodiscard]] bool canSound(int midiNote, int midiChannel = 1) const noexcept;
    // Key-up damps the note at every release velocity: lifting a key is the
    // fretting hand letting go, never a new stroke.
    void noteOff(int midiNote, int midiChannel = 1) noexcept;
    // The same key-up, told whether the sustain pedal held it when it was
    // made, for a caller that orders the key-ups of one sample after their
    // Note Ons but a pedal change on that sample after them (the Performer).
    void noteOff(int midiNote, int midiChannel, bool sustained) noexcept;
    // The same two key-ups with MIDI's release velocity (0-1). It damps the
    // note exactly as noteOff does and sets only how firmly the hand lands,
    // which the release noise follows (EngineParameters::releaseNoise); a
    // negative value is "not sent" and plays as the nominal key-up.
    void noteOffWithVelocity(int midiNote, int midiChannel,
                             float releaseVelocity) noexcept;
    void noteOffWithVelocity(int midiNote, int midiChannel, bool sustained,
                             float releaseVelocity) noexcept;
    // Whether a key-up on this channel now would be held by the sustain
    // pedal: its own, or, on an MPE lower-zone member, the manager's too.
    [[nodiscard]] bool sustainHolds(int midiChannel) const noexcept;
    void setSustainPedal(bool down, int midiChannel = 1) noexcept;
    // Continuous bridge-hand damping, 0 open to 1 fully muted. Exposed as a
    // controller rather than a panel control: it is a playing pressure, and
    // zero is an exact no-op.
    void setPalmMutePressure(float pressure) noexcept;
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
    // its own. 0-1; inert on a conventional or manager channel and inert with
    // no lower zone.
    void setMpePressure(float value, int midiChannel) noexcept;
    // Opt-in guitar-controller mode: channels 1-6 are the six strings
    // directly, bypassing chooseString's fret-distance guess. It is the
    // layout Roland's GK and Fishman's TriplePlay produce in "mono mode",
    // but neither is documented to transmit a message requesting it -- see
    // the CC126 handler in AcustraPerformer.cpp for what actually toggles
    // it. A note on channel 1-6 with no playable fret on that channel's
    // string is dropped rather than reassigned. Off, the default, is an
    // exact no-op.
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

    // The separate output a host can take alongside Main. The pointer
    // receives numSamples samples; null means the output is not wanted and
    // nothing is written to it.
    //   piezo: the under-saddle piezo and its analog chain (renderPiezo in
    //     AcustraEngine.cpp), mono, whatever Capture selects.
    // It is at the level Main has when Capture selects Piezo (the same
    // construction loudness reference and Output gain) and passes its own
    // copy of Main's safety limiter; it shares no state with Main, so Main is
    // bit-for-bit what it would be without it. Main with Capture on Piezo
    // equals the Piezo output on both sides, once a Capture change's
    // crossfade (a 20 ms time constant) has settled.
    struct OutputBuses
    {
        float* piezo { nullptr };
    };

    // Main only: left and right follow Capture.
    void process(float* left, float* right, int numSamples) noexcept;
    // Main plus the requested output, in one pass.
    void process(float* left, float* right, const OutputBuses& buses,
                 int numSamples) noexcept;

    [[nodiscard]] int getActiveVoiceCount() const noexcept;
    [[nodiscard]] int getSympatheticStringCount() const noexcept;
    [[nodiscard]] float getLastBridgeVelocity() const noexcept;
    [[nodiscard]] float getLastBridgeReactionForce() const noexcept;
    [[nodiscard]] float getLastBridgeBodyForce() const noexcept;
    [[nodiscard]] float getLastBridgeTailForce() const noexcept;
    // The played strings' axial wave, observed separately from the two-way
    // junction because its current radiation surrogate remains one-way.
    [[nodiscard]] float getLastLongitudinalForce() const noexcept;
    // The under-saddle piezo's voltage at the jack, where it enters the
    // preamp: the element's charge on its own capacitance, the cable's and
    // the preamp's input network (renderPiezo). Read by
    // Tools/CalibratePiezo.py; the chain runs every sample whatever Capture
    // selects.
    [[nodiscard]] float getLastPiezoVoltage() const noexcept;
    // The same sample at every stage the chain can run out of room at, in
    // volts: the element's open-circuit voltage, the jack, U1A's input
    // (about its 4.5 V bias; the typical common-mode limit is 2.5 V
    // either side) and U1B's drive before its output swing clips it (about
    // 4.5 V; PiezoDesign::railHigh and railLow).
    struct PiezoProbe
    {
        float openCircuit { 0.0f };
        float jack { 0.0f };
        float bufferInput { 0.0f };
        float gainStageDrive { 0.0f };
    };
    [[nodiscard]] PiezoProbe getLastPiezoProbe() const noexcept;
    // Newtons per engine force unit on the piezo: the calibration's
    // displacement unit (the strings' own) per 48 kHz sample
    // (PiezoDesign item 2). Read by Tools/CalibratePiezo.py through
    // AcustraPerformanceRenderer --piezo-unit.
    [[nodiscard]] double getPiezoNewtonsPerUnit() const noexcept;
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

    // The under-saddle piezo and the analog chain behind it (renderPiezo in
    // AcustraEngine.cpp, Docs/decisions.md 2026-09-29 "Accurate piezo
    // chain"), in signal order: a real, documented chain at component level.
    // Tools/PiezoReference.py simulates the same parts as a circuit, and
    // Tests/PiezoCircuitTests.cpp holds this implementation to it. Values
    // marked "chosen" were picked from a documented range.
    struct PiezoDesign
    {
        // 1. Each string presses on its own stretch of the element, and a
        // real saddle never seats evenly: a good install is within about
        // +-1-2 dB string to string (Martin Keith, "Troubleshooting imbalance
        // between strings on common acoustic guitar pickup systems",
        // Acoustic Guitar; US 6,822,156 B1 on saddle bowing and air gaps).
        // Chosen pattern, low E to high E: -0.8, +0.4, +0.9, -0.3, +0.6,
        // -1.0 dB, scaled so the six sum to six.
        static constexpr std::array<float, 6> stringWeights {{
            0.912446483f, 1.04762873f, 1.10970464f,
            0.966512336f, 1.07203114f, 0.891676665f }};
        // The axial force reaches the saddle through the strings' break
        // angle behind it: sin 25 degrees (chosen, a typical 20-30 degrees).
        // Zero while longitudinalGain ships at 0.
        static constexpr float axialShare = 0.422618262f;
        // 2. The force in newtons: an engine force unit is a string's wave
        // impedance times one displacement unit per 48 kHz sample
        // (FixedDerivative differences over the 48 kHz period at every
        // rate), and a displacement unit is the strings' own,
        // PhysicalCalibration::steelDisplacementScaleMetres - the shipped
        // fit's 7.74 mm, 0.00773577847 * 48000 = 371.3 N per unit. It is
        // read at run time (piezoNewtonsPerUnit_), so a calibration set by
        // setPhysicalCalibration moves the piezo with the strings
        // (Docs/decisions.md 2026-09-30).
        // The saddle on its element, driven one way by the rigid-saddle
        // force: F_p / F_r = Zk Q / (1 + Zk Q), Zk = k/s + c_m,
        // Q = 1/(s M + SZ) + G. M: 3.8 g, chosen within 2.7-5.1 g (a bone
        // saddle 72 x 3 x 9.5 mm at 1.95 g/cc is 4.0 g). k: the element and its seat
        // put M at 6 kHz (chosen within the documented 5-7 kHz). c_m: the
        // element's loss factor 1/18, Zollner's rig Q of 18 taken as the
        // bound on material loss (M. Zollner, Physics of the Electric
        // Guitar, 2005, ch.6). G: the bridge's conductance under the saddle,
        // the mean of Re(Y) over 5-7 kHz of the measured Fylde mobility the
        // steel bridge's level is fitted to (the Fylde Falstaff, Carcagno et
        // al. 2018), saddle removed. SZ: the six strings' summed wave
        // impedance at their standard open notes (stringImpedance).
        static constexpr double saddleMass = 3.8e-3;
        static constexpr double elementHz = 6000.0;
        static constexpr double elementLoss = 1.0 / 18.0;
        static constexpr double bridgeConductance = 1.59e-3;
        // 3. The element: a charge source across its capacitance, 0.2 V/N
        // open-circuit at 1.45 nF (Zollner ch.6, Ovation EA-68).
        static constexpr double voltsPerNewton = 0.2;
        static constexpr double elementCapacitance = 1.45e-9;
        // 4. The cable: 3 m of Mogami 2524 at 130 pF/m (length chosen), and
        // a stray picofarad at each input node (chosen).
        static constexpr double cableCapacitance = 390.0e-12;
        static constexpr double strayCapacitance = 1.0e-12;
        // 5. The preamp: ESP Project 202 Fig. 1 (R. Elliott,
        // sound-au.com/project202.htm) on one 9 V battery, OPA2134 halves
        // (TI SBOS058B). U1A is a follower whose input network R1-R3 is
        // bootstrapped through C2 and R4; D1/D2 (1N4148) guard its input;
        // C3 into R5 || R6 couples U1B, a gain of 1 + R7/R8 falling to one
        // below C4's corner; R9 and C5 feed the volume pot, at full, into a
        // Radial PZ-DI's 1 MOhm input. R7 is 6.2 kOhm, not the figure's
        // 10 kOhm: the article sets the gain with R7 and R8 ("simply reduce
        // the value of R8 and/or increase the value of R7" for more), and at
        // the strings' fitted unit (item 2) the figure's gain of two put the
        // player's velocity-127 Pick strums 0.8 dB past U1B's negative swing.
        // 1.62 (E24, 1.83 dB less) leaves them 1.0 dB, about the 1.25 dB
        // they had at the 6.1 mm unit; U1A's input range, which R7 does not
        // move, keeps 1.2 dB there (Docs/decisions.md 2026-09-30).
        static constexpr double c1 = 4.7e-9;
        static constexpr double r1 = 1.0e6, r2 = 1.0e6, r3 = 1.0e6;
        static constexpr double c2 = 33.0e-6, r4 = 3.9e3;
        static constexpr double c3 = 220.0e-9, r5 = 47.0e3, r6 = 47.0e3;
        static constexpr double r7 = 6.2e3, r8 = 10.0e3, c4 = 33.0e-6;
        static constexpr double r9 = 100.0, c5 = 10.0e-6;
        static constexpr double volume = 10.0e3, diInput = 1.0e6;
        // The OPA2134's common-mode input capacitance (6 pF), and its
        // limits about the 4.5 V bias: the typical input common-mode range,
        // 2 V inside each rail (+-13 V at +-15 V), and the output swing for
        // U1B's 6.18 kOhm load ((R7 + R8) || (R9 + pot || DI)), the
        // datasheet's guaranteed 10 kOhm and 2 kOhm rows interpolated in
        // conductance: V+ - 1.2463 V, V- + 0.6080 V.
        // The OPA2134 has no output phase reversal.
        static constexpr double inputCapacitance = 6.0e-12;
        static constexpr double commonModeLimit = 2.5;
        static constexpr double railHigh = 3.2537111;
        static constexpr double railLow = -3.8919926;
        // D1/D2, 1N4148s: IS 2.52 nA, N 1.752 (the widely reposted SPICE
        // model), at 25 C: N kT/q = 45.0 mV.
        static constexpr double diodeSaturation = 2.52e-9;
        static constexpr double diodeThermalVoltage = 1.752 * 0.025692579;
        // 6. The level match to the stereo microphones
        // (Tools/CalibratePiezo.py): the median BS.1770 loudness difference.
        // It sits on the output reference, so a later change to the
        // strings' loudness moves both sensors together, and the
        // construction loudness table (ConstructionLoudnessData.h) refines
        // it per construction and Picking.
        static constexpr float trim = 2.4417f;
    };
    // The saddle filter at the host rate: matched
    // poles and least-squares zeros (renderPiezo).
    struct PiezoSaddleFilter
    {
        std::array<float, 5> b {};
        float a1 { 0.0f }, a2 { 0.0f };
    };
    static PiezoSaddleFilter designPiezoSaddle(double sampleRate,
                                               double stringImpedanceSum) noexcept;

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
        // The longest reference delay needs nine samples of history. A
        // power-of-two ring makes both reads and the write wrap one mask.
        static constexpr unsigned historyMask = 15;
        std::array<float, historyMask + 1> history {};
        int index { 0 };

        void reset(float value = 0.0f) noexcept
        {
            history.fill(value);
            index = 0;
        }
        ACUSTRA_ALWAYS_INLINE float process(float input, float sampleRateRatio) noexcept;
        // A released shape entering the junction moves the wave variable
        // without the bridge having moved: the shape was standing on the
        // string before the finger let go. Re-reference the history to the
        // new level so this sample reads no step - at 48 kHz exactly zero
        // motion, at other rates the history's own interpolated remainder -
        // and the samples after it are differences again.
        float processAcrossRelease(float input, float sampleRateRatio) noexcept;
        // A construction change (strings, tuning, the bridge) steps the
        // junction's wave variables while the bridge keeps moving.
        // Re-reference the history to the step's linear extrapolation, so
        // this sample reports the motion the bridge already had - the
        // previous sample's - at every rate, and the samples after it are
        // differences again. processAcrossRelease reports none, a one-sample
        // hole in every bridge force that sounded as a tick (audit F14).
        float processAcrossStep(float input, float sampleRateRatio) noexcept;
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
        // The dispersion's two allpass sections (calibrateDispersion in
        // AcustraEngine.cpp), in cascade. The second runs only on the
        // stiffer notes that need it; inactive, it is bypassed and leaves
        // the loop exactly as it was.
        float dispersionA1 { 0.0f };
        float dispersionA2 { 0.0f };
        bool secondDispersionActive { false };
        float secondDispersionA1 { 0.0f };
        float secondDispersionA2 { 0.0f };
        // The string's own bending loss (bendingLossSection in
        // AcustraEngine.cpp): g / (1 + a1 z^-1 + a2 z^-2), unit gain at DC,
        // designed at the host rate from the loss law rather than mapped
        // from 48 kHz. Inactive leaves the loop exactly as it was.
        bool bendingLossActive { false };
        bool bendingLossSeed { false };
        float bendingLossGain { 1.0f };
        float bendingLossA1 { 0.0f };
        float bendingLossA2 { 0.0f };
        float bendingLossY1 { 0.0f };
        float bendingLossY2 { 0.0f };
        OnePole broadLossFilter {};
        OnePole lossFilter {};
        SecondOrderAllpass dispersion {};
        SecondOrderAllpass secondDispersion {};
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
        // Switches the second dispersion section in or out under a sounding
        // wave (see its definition).
        void switchSecondDispersion(bool active) noexcept;
        // Read-only point observation of both travelling waves; does not
        // advance the feedback allpass or alter the vibrating string.
        [[nodiscard]] float displacementAt(float fraction) const noexcept;
        // Inlined into the voice loop whatever its size. The Rack Extension's
        // build inlined it until the bending section grew it past its limit;
        // called instead, four times per string per sample, it cost about a
        // tenth of the engine there, and inlining gains a few percent on the
        // host, which never did.
        ACUSTRA_ALWAYS_INLINE float advance(float delaySmoothing,
                                            float releaseGain) noexcept;
        // A plucked string is released from rest, so the wave the bridge
        // reads was already standing there when the finger let go. Prime the
        // finite difference from the first value this loop actually produces
        // - which is the filtered, dispersed one advance() returns, not the
        // raw delay tap - or establishing the released shape reads as a
        // one-sample velocity impulse the size of the whole displacement.
        ACUSTRA_ALWAYS_INLINE float bridgeVelocity(float incident, float sampleRateRatio) noexcept;
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
        // The same, with the mobility crossfading from `fading`'s modes to
        // this load's: weight is this load's share of the immediate and past
        // mobility, 1 - weight fading's. Both mode sets are driven by the
        // same body force, the anchor stubs are this load's alone.
        void process(const BridgeDrive& drive, float samplePeriod,
                     BridgeLoad& fading, float weight) noexcept;
        // Advance the modes by one sample of body force and moment, leaving
        // their past mobility response in pastHeave and pastRock.
        void advanceModes(float bodyForce, float bodyMoment) noexcept;
    };

    // One radiation mode's coefficients, as configureBody places them:
    // BodyBank holds the states and renders every mode (BodyBank::render).
    // The mono microphone's residues are the right channel's, so the upper
    // bout needs none of its own.
    struct BodyMode
    {
        float poleReal {}, poleImaginary {};
        float leftReal {}, leftImaginary {}, rightReal {}, rightImaginary {};
        float leftMomentReal {}, leftMomentImaginary {};
        float rightMomentReal {}, rightMomentImaginary {};
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

        bool operator==(const VoiceConfigurationKey& other) const noexcept
        {
            return generation == other.generation
                && stoppedMidi == other.stoppedMidi
                && openMidi == other.openMidi
                && frequency == other.frequency
                && tensionSemitones == other.tensionSemitones
                && age == other.age && palmMute == other.palmMute;
        }
    };

    // What a string's bridge anchor holds before configureVoice first sets
    // it; prepare() restores it (see restartRandomDraws).
    static constexpr float initialBridgeTailStiffness = 10000.0f;

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
        // Intrinsic loss and pitch belong to the captured string. The bridge
        // hand remains live CC2 expression while that old wave is retained.
        float tailHandFrequency { 1.0f };
        float tailHandUnbentFrequency { 1.0f };
        float tailHandIntrinsicT60 { 1.0f };
        float tailHandIntrinsicHighLoss { 0.0f };
        float tailHandPressure { 0.0f };
        float tailCapturedHandPressure { 0.0f };
        std::array<float, 2> tailCapturedLoopGain {};
        std::array<float, 2> tailCapturedHighLoss {};
        // The retained virtual-string branch keeps the port it had at capture,
        // including its applied member bend, while the main voice is retuned.
        float tailCharacteristicImpedance { 0.0f };
        float tailLevel { 0.0f };
        int tailQuietSamples { 0 };
        bool tailActive { false };
        // A tail being let go: its port fades out of the junction on the
        // delay's time constant before the branch is dropped (finishVoice).
        bool tailRetiring { false };
        int openMidi { 40 };
        int midiNote { 40 };
        // 1 is a stopped note. Above that the string sounds open in its nth
        // mode, a natural harmonic, and the loop runs at the open pitch.
        int harmonic { 1 };
        int midiChannel { 1 };
        int fret { 0 };
        int ownerCount { 0 };
        bool played { false };
        bool keyDown { false };
        bool pedalHeld { false };
        bool mpeMember { false };
        bool memberPitchBendFrozen { false };
        std::uint64_t startOrder { 0 };
        std::uint32_t randomState { 1 };
        // The release burst's noise, handed over from randomState at each
        // pluck (initialisePluck).
        std::uint32_t excitationNoiseState { 1 };
        float velocity { 0.0f };
        float polarisationMix { 0.5f };
        float excitationEnvelope { 0.0f };
        float excitationDecay { 0.0f };
        float excitationColour { 0.0f };
        float excitationLowpass { 0.0f };
        float excitationLowpass2 { 0.0f };
        bool excitationSoft { false };
        // renderExcitation's lowpass coefficient for this colour and host
        // rate, which a burst keeps throughout: a powf per sample otherwise.
        std::uint32_t excitationCoefficientColour { 0xffffffffu };
        std::uint32_t excitationCoefficientRate { 0xffffffffu };
        float excitationCoefficient { 0.0f };
        // The Pick technique's contact transient is an impact and enters
        // broadband, bypassing the Finger burst's colour filter.
        bool excitationWhite { false };
        // renderContactNoise's state for the pluck in progress: the drive's
        // amplitude in the loop's per-sample wave units and its per-sample
        // decay; the coefficients of the three one-pole stages that shape
        // the launched displacement (two at the contact's corner, one at the
        // fundamental), their states, and the gain that makes the force they
        // imply unit RMS; the samples left to run; the shares of
        // the stroke's direction normal and parallel to the top; its own
        // generator, so that switching the noise on leaves every other draw
        // as it was; and its travel from the contact point to the bridge,
        // direct and by the nut, with the retained tail's copy at a repluck
        // (those buffers, and the click's flight line, end the voice).
        float contactNoiseAmplitude { 0.0f };
        float contactNoiseDecay { 0.0f };
        float contactNoiseCoefficient { 0.0f };
        float contactNoiseStage1 { 0.0f };
        float contactNoiseStage2 { 0.0f };
        float contactNoiseStage3 { 0.0f };
        float contactNoiseLowCoefficient { 0.0f };
        // Its string-borne and airborne levels for this pluck, the last
        // launched displacement and force at unit level, the click's
        // radiated term and its per-reference-sample scale.
        float contactNoiseString { 0.0f };
        float contactNoiseClick { 0.0f };
        float contactNoiseLaunched { 0.0f };
        float contactNoiseForce { 0.0f };
        float contactNoiseAir { 0.0f };
        float contactNoiseAirScale { 1.0f };
        // The click's radiation corner (a one-pole low-pass on dF/dt) and
        // its flight to the microphone, in whole samples.
        float contactNoiseAirCoefficient { 1.0f };
        float contactNoiseAirLowpass { 0.0f };
        int contactNoiseAirDelay { 1 };
        int contactNoiseAirWrite { 0 };
        float contactNoiseGain { 0.0f };
        int contactNoiseSamples { 0 };
        float contactNoiseNormal { 0.0f };
        float contactNoiseParallel { 0.0f };
        float tailContactNoiseNormal { 0.0f };
        float tailContactNoiseParallel { 0.0f };
        std::uint32_t contactNoiseState { 1 };
        ContactTravel contactTravel {};
        float contactPeriodSamples { 0.0f };
        // Where each plane's release shape put its kink, as a share of the
        // line it was written on (initialisePluck).
        std::array<float, 2> releaseShapePosition {};
        // A Finger or Thumb release's slip and the full-velocity slip it is
        // taken as a ratio to (initialisePluck); zero when the release is
        // the written shape itself.
        double releaseSlipPole { 0.0 };
        double releaseReferencePole { 0.0 };
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
        // Fretting point after a conventional/manager slide. A lateral
        // member bend keeps it fixed. The attack's extension uses the same
        // geometry as dispersion and the string's axial modes.
        float speakingLengthMetres { 0.648f };
        // Fractional fret position used by the fitted fret-decay law. Slides
        // remain within its measured/playable fret range; a tension bend
        // keeps this position. Zero slide retains the exact MIDI fret.
        float speakingFret { 0.0f };
        float bridgeTailStiffness { initialBridgeTailStiffness };
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
        // finishVoice's follower coefficient for the normal loop's current
        // delay: an expf per sample until the delay has slewed to its target.
        std::uint32_t observedSlopeDelay { 0xffffffffu };
        float observedSlopeAlpha { 0.0f };
        // The performed fundamental, including slide/bend/vibrato but
        // excluding the small energy-dependent attack transient; intrinsic
        // partial loss is timed by this physical round-trip frequency.
        float dispersionDesignFrequency { 0.0f };
        float dispersionDesignInharmonicity { -1.0f };
        float dispersionDesignAge { -1.0f };
        float dispersionDesignFrequencyLossScale { -1.0f };
        // Exact arguments of the last completed dispersion solve. Its fit
        // frequency is bounded beyond the playable fretboard plus the
        // documented 12-semitone panel/Reason bend range; above that the
        // inexpensive bending section still follows the physical pitch.
        // Frequency is positive, so an all-zero key cannot be a valid hit.
        std::array<double, 9> dispersionDesignArguments {};
        // The bending-loss section at the physical playing frequency,
        // including beyond the bounded phase-fit band above; both
        // polarisations carry it.
        float bendingLossGain { 1.0f };
        float bendingLossA1 { 0.0f };
        float bendingLossA2 { 0.0f };
        // The fraction both polarisation loops were lengthened by so that the
        // pair of modes they form through a rocking saddle is heard at the
        // requested pitch (coupledPolarisationDetune); zero elsewhere.
        float polarisationDetune { 0.0f };
        // The two dispersion sections' pole pairs relative to the
        // fundamental; a zero decay ratio is an unused section.
        std::array<float, 2> dispersionDecayRatios { 0.0f, 0.0f };
        std::array<float, 2> dispersionPoleRatios { 0.0f, 0.0f };
        // Each loop's delay as configureVoice tuned it at 48 kHz, in 48 kHz
        // samples, for a Pick release at another rate (writePickRelease);
        // zero when not tuned for one.
        std::array<float, 2> referencePickDelay {};
        float level { 0.0f };
        float releaseDamping { 1.0f };
        // The hand's T60 for the release under way (beginRelease), from
        // which releaseDamping follows the loop's period; zero when none.
        float releaseSeconds { 0.0f };
        // Samples until a released string is handed back to the allocator.
        int returnSamples { 0 };
        // Samples until a scheduled pluck is released; zero when none waits.
        int pluckDelay { 0 };
        // Once this note's first release has fired, live chord formation
        // cannot move its physical string or manufacture another attack.
        bool attackFired { false };
        // A held string keeps its wave until this scheduled re-pluck fires.
        bool repluckPending { false };
        // A strum member whose key came up before the pick reached it: the
        // pick still arrives and the key-up is applied right after it, held
        // by the pedal if the pedal held it at key-up and still does.
        bool releaseAfterPluck { false };
        bool pedalHeldAtKeyUp { false };
        // Where this pluck landed, as a fraction of the sounding length.
        float pluckPoint { 0.0f };
        // The static force the hand held the string aside with, let go at
        // the pluck (initialisePluck): the normal plane's steep-flank rise
        // per sample still to reach the junction, the two first-order
        // stages whose cascade gives it (n + 1) d^n there (process), and
        // the samples since the latest release (-1: none sounding).
        float releaseStepRise { 0.0f };
        float releaseStepForce { 0.0f };
        float releaseStepLevel { 0.0f };
        int releaseStepAge { -1 };
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
        // The contact noise's buffers (renderContactNoise), last: at zero
        // levels nothing reads them, and between the per-sample fields they
        // would put 66 KB between the excitation's and the contact's.
        std::array<float, 512> contactNoiseAirLine {};
        ContactTravel contactNoiseTravel {};
        ContactTravel tailContactNoiseTravel {};
        // The key-up's sound (startReleaseNoise): the release velocity the
        // key came up with (negative when not sent), the largest bridge force
        // since the pluck, and the release noise's generator state - the
        // touch's white amplitude and its rise and fall, the brush's, two
        // one-pole stages at the contact's corner, the brush's band-pass,
        // the leaky integrator that launches the displacement, the samples
        // left, the stroke's normal and parallel shares, its own draws and
        // its travel from the damping point.
        float releaseVelocity { -1.0f };
        float peakLevel { 0.0f };
        float releaseNoiseTouch { 0.0f };
        float releaseNoiseFall { 0.0f };
        float releaseNoiseRise { 0.0f };
        float releaseNoiseFallDecay { 0.0f };
        float releaseNoiseRiseDecay { 0.0f };
        float releaseNoiseBrush { 0.0f };
        float releaseNoiseBrushFall { 0.0f };
        float releaseNoiseBrushRise { 0.0f };
        float releaseNoiseBrushFallDecay { 0.0f };
        float releaseNoiseBrushRiseDecay { 0.0f };
        float releaseNoiseCoefficient { 0.0f };
        float releaseNoiseStage1 { 0.0f };
        float releaseNoiseStage2 { 0.0f };
        float releaseNoiseBandA1 { 0.0f };
        float releaseNoiseBandA2 { 0.0f };
        float releaseNoiseBandA3 { 0.0f };
        float releaseNoiseBandK { 0.0f };
        float releaseNoiseBand1 { 0.0f };
        float releaseNoiseBand2 { 0.0f };
        float releaseNoiseLeak { 0.0f };
        float releaseNoiseLaunched { 0.0f };
        float releaseNoiseNormal { 0.0f };
        float releaseNoiseParallel { 0.0f };
        int releaseNoiseSamples { 0 };
        std::uint32_t releaseNoiseState { 1 };
        ContactTravel releaseNoiseTravel {};
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
        // The first `ordered` modes (a bank's own) are summed in index order;
        // the rest (the steel blend's parallel parts, starting on a group of
        // four) in vector accumulators.
        int ordered { 0 };

        // The first modeCount modes' coefficients; with resetStates their
        // states restart from rest. Slots past the bank are zero.
        void load(const std::array<BodyMode, bodyModeCount>& modes,
                  int modeCount, int orderedCount, bool resetStates) noexcept;
        void reset() noexcept
        {
            real.fill(0.0f);
            imaginary.fill(0.0f);
            momentReal.fill(0.0f);
            momentImaginary.fill(0.0f);
        }
        // Every mode's two-pole state advanced by the force and moment and
        // summed into the left and right residues, then a flush of tiny
        // states. The ordered modes' sums accumulate in index order, as ever;
        // with vector extensions the modes after them gather four lanes
        // across their parts and are added to those sums once per sample.
        BodyOutput render(float force, float moment) noexcept;
    };

    static EngineParameters sanitise(const EngineParameters&) noexcept;
    static PhysicalCalibration sanitise(const PhysicalCalibration&) noexcept;
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
    // keepModalState keeps the sounding modes' state (a live reconfigure
    // of the same measured bank: Shape, Wood); otherwise every mode starts
    // from rest.
    void configureBridge(bool keepModalState = false) noexcept;
    // The frequency and Q configureBody gives radiation mode `index` of the
    // bank these parameters select, before the host-rate clamp: the pole
    // steel's own bridge modes share (tests).
    static std::array<float, 2> radiationModePole(
        const EngineParameters& parameters,
        const PhysicalCalibration& calibration, int index) noexcept;
    // Body Material's factors on frequency, Q, brightness and radiation for
    // these parameters, relative to the wood the model's measured bank was
    // built of (tests).
    static std::array<float, 4> bodyWoodFactors(
        const EngineParameters& parameters) noexcept;
    // The Original's capture voicing (CaptureVoicingData.h) as an amplitude
    // gain at a frequency, its level included; configureBody multiplies
    // each of the Original's radiation modes by it (tests).
    static float captureVoicingGain(float frequency) noexcept;
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
        std::array<std::uint32_t, 12> key {};
        bool valid { false };
        int count { 0 };
        // The first `ordered` modes are evaluated as ever; the steel blend's
        // parallel parts after them (SteelBodyBlend.h) by a real-arithmetic
        // form of the same section (bridgePortMobility).
        int ordered { 0 };
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
    // Scale every state a string's two loops store of the travelling wave -
    // the delay line and each filter's memory - by gain.
    static void scaleStoredWaves(Voice& voice, float gain) noexcept;
    void returnToOpenString(Voice& voice, int stringIndex,
                            bool clearDelay) noexcept;
    void firePluck(Voice& voice, int stringIndex) noexcept;
    void beginRelease(Voice& voice, int stringIndex) noexcept;
    void completeKeyUp(Voice& voice, int stringIndex, bool pedalHeld) noexcept;
    void releaseKey(int midiNote, int midiChannel, bool sustainGiven,
                    bool sustained, float releaseVelocity = -1.0f) noexcept;
    void captureTail(Voice& voice) noexcept;
    void updateTailHandLoss(Voice& voice) noexcept;
    // The Pick technique's released state (FittedPhysicalData.h): a rest
    // triangle of this height with its apex at position, a fraction of the
    // sounding length from the bridge, smoothed by the contact aperture (in
    // loop phase), plus the velocity the string leaves the tip with,
    // localised over that same aperture and carrying releaseShare of the
    // triangle's stored energy. Both are projected onto the nth-harmonic
    // node like the plucked shape. Replaces the line's first length samples.
    // The share is solved on the grid of referenceDelay, the loop's period
    // in 48 kHz samples, at any other host rate.
    void writePickRelease(StringLoop& loop, int length, float height,
                          float position, float aperture, int modes,
                          float releaseShare, double slipPole,
                          float referenceDelay) noexcept;
    double plectrumSlipPole(const Voice& voice, float releasedAmplitude,
                            float heldDistance, float soundingLength,
                            float scaleLength, float edgeRadius) const noexcept;
    static void applyPlectrumSlip(StringLoop& loop, int length,
                                  double slipPole) noexcept;
    void resetSoundState() noexcept;
    struct VoiceBend
    {
        float performed { 0.0f }; // semitones the loop is tuned by
        float member { 0.0f };    // an MPE member's own share, a tension bend
    };
    [[nodiscard]] VoiceBend voiceBend(const Voice& voice) const noexcept;
    [[nodiscard]] float loopFundamental(const Voice& voice) const noexcept;
    [[nodiscard]] static float handDamping(float t60Seconds,
                                           float fundamental) noexcept;
    void restartRandomDraws() noexcept;
    void freezeMemberPitchBend(Voice& voice) noexcept;
    [[nodiscard]] bool isLowerZoneMaster(int midiChannel) const noexcept;
    [[nodiscard]] bool isLowerZoneMember(int midiChannel) const noexcept;
    [[nodiscard]] bool channelControlsVoice(int midiChannel,
                                            const Voice& voice) const noexcept;
    [[nodiscard]] bool sustainIsDown(const Voice& voice) const noexcept;
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
    void initialiseContactNoise(Voice& voice, float velocity, float position,
                                float contactDistance, float releasedAmplitude,
                                float contactWidthRatio) noexcept;
    float renderContactNoise(Voice& voice) noexcept;
    // The contact noise's per-sample work, kept out of the voice loop
    // (process): only a voice whose noise or its travel is running calls it.
    void addContactNoise(Voice& voice, float& verticalIncident,
                         float& horizontalIncident) noexcept;
    void addTailContactNoise(Voice& voice, float& tailIncident,
                             float& tailParallelIncident) noexcept;
    // The key-up's sound (EngineParameters::releaseNoise): started by
    // beginRelease and muteVacatedString, rendered and launched from the
    // damping contact like the contact noise.
    void startReleaseNoise(Voice& voice, int stringIndex,
                           bool fretSide) noexcept;
    float renderReleaseNoise(Voice& voice) noexcept;
    void addReleaseNoise(Voice& voice, float& verticalIncident,
                         float& horizontalIncident) noexcept;
    void finishVoice(Voice& voice, int stringIndex, float verticalIncident,
                     float horizontalIncident, float excitation,
                     float tailIncident, float tailParallelIncident,
                     float bridgeDisplacement,
                     float bridgeVelocity, float horizontalBridgeDisplacement,
                     float& directLeft,
                     float& directRight, float& longitudinalForce) noexcept;
    BodyOutput renderBody(float bridgeInput, float bodyMoment) noexcept;
    float renderPiezo(float force) noexcept;
    void resetPiezo() noexcept;
    void configurePiezoUnit() noexcept;
    float nextNoise(Voice& voice) noexcept;

    EngineParameters targetParameters_ {};
    EngineParameters parameters_ {};
    PhysicalCalibration physicalCalibration_ { fittedPhysicalCalibration };
    std::array<Voice, stringCount> voices_ {};
    std::array<BodyMode, bodyModeCount> bodyModes_ {};
    // What renderBody runs: the two banks above as BodyBank lanes, with
    // their states. Slots past a bank's own modes are all-zero padding
    // (configureBody); their contribution to every sum is exactly zero, so
    // BodyBank::count stops before them.
    BodyBank bodyBank_ {}, fadingBodyBank_ {};
    GuitarModel configuredGuitarModel_ { GuitarModel::Original };
    GuitarModel fadingBodyModel_ { GuitarModel::Original };
    BodyShape fadingBodyShape_ { BodyShape::Dreadnought };
    BodyMaterial fadingBodyMaterial_ { BodyMaterial::Spruce };
    BodyShape configuredBodyShape_ { BodyShape::Dreadnought };
    BodyMaterial configuredBodyMaterial_ { BodyMaterial::Spruce };
    bool bodyUpdatePending_ { false };
    // Completed dispersion solves by their exact arguments, shared by the
    // six strings: a chord change asks for a handful of designs a playing
    // hand keeps returning to, and each solve is an iterative 3x3 fit that
    // made the note-on batch several times a normal one (with a
    // least-squares refit on the stiffer notes). The solve is a pure
    // function of its arguments, so a hit is the same result.
    struct DispersionSolve
    {
        std::array<double, 9> arguments {};
        std::array<float, 2> decayRatios { 0.0f, 0.0f };
        std::array<float, 2> poleRatios { 0.0f, 0.0f };
        bool valid { false };
    };
    std::array<DispersionSolve, 64> dispersionSolves_ {};
    // writePickRelease's two released waves at every sample of the loop: its
    // energy pass evaluates them (ten Gaussian edges and ten corner lookups
    // a sample), and its write pass writes the same samples from them.
    std::array<float, maximumDelaySamples> pickReleaseDisplacement_ {};
    std::array<float, maximumDelaySamples> pickReleaseVelocity_ {};
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
    // The piezo chain (renderPiezo). Each string presses on its own stretch
    // of the element at its own sensitivity; the weighted sums below mirror
    // the junction's, and like its port they are held while no string is
    // summed.
    std::array<float, stringCount> piezoStringWeights_ {};
    float lastPiezoImpedanceSum_ { 0.0f };
    float lastPiezoImpedanceMoment_ { 0.0f };
    FixedDerivative piezoForceDerivative_ {};
    float lastPiezoWave_ { 0.0f };
    float lastPiezoForce_ { 0.0f };
    // The saddle filter, in direct form I; histories are force units.
    PiezoSaddleFilter piezoSaddle_ {};
    std::array<float, 5> piezoSaddleInput_ {};
    std::array<float, 2> piezoSaddleOutput_ {};
    // The preamp's input section, trapezoidal, as deviations from its
    // operating point: w = V_IN - kin V_oc and C2's voltage.
    double piezoFrontA00_ {}, piezoFrontA01_ {}, piezoFrontA10_ {}, piezoFrontA11_ {};
    double piezoFrontB0_ {}, piezoFrontB1_ {}, piezoFrontE0_ {}, piezoFrontE1_ {};
    double piezoInputShare_ {}, piezoJackInput_ {}, piezoJackElement_ {};
    double piezoFrontW_ {}, piezoFrontC2_ {};
    double piezoLastOpen_ {}, piezoLastClamp_ {};
    // D1/D2: Ceq fs / IS, the IN node's charge per volt and sample over
    // the diodes' saturation current, and the charge (as volts at IN) they
    // take in one sample at 0 to 8 V past U1A's range, with its slope.
    static constexpr int piezoDiodePoints = 1025;
    static constexpr double piezoDiodeStep = 8.0 / (piezoDiodePoints - 1);
    double piezoDiodeScale_ {};
    std::array<double, piezoDiodePoints> piezoDiodeDump_ {};
    std::array<double, piezoDiodePoints> piezoDiodeSlope_ {};
    // C3, C4 and C5 as trapezoidal one-poles: state = pole * state
    // + gain * (input + previous input).
    double piezoC3Pole_ {}, piezoC3Gain_ {}, piezoC4Pole_ {}, piezoC4Gain_ {};
    double piezoC5Pole_ {}, piezoC5Gain_ {};
    double piezoC3_ {}, piezoC4_ {}, piezoC5_ {};
    double piezoLastBuffer_ {}, piezoLastStage_ {};
    // U1B's drive for the four samples a corner is found on, and its
    // clipped output for the twelve the BLAMP spans (seven held back, the
    // newest, and four ahead that only hold corrections), oldest first; the
    // chain's output is seven samples behind its input.
    std::array<double, 4> piezoDrive_ {};
    std::array<double, 12> piezoStage_ {};
    double piezoOutputVolts_ { 0.0 };
    double piezoOutputScale_ { 0.0 };
    // PiezoDesign item 2 at run time: newtons per engine force unit, the
    // calibration's displacement unit per 48 kHz sample; the mid-band gain
    // from the element's open-circuit voltage to the DI that the level match
    // divides out with it; and the force floors below which renderPiezo
    // reads none (5 pN) and zeroes the saddle's histories (50 nN), in force
    // units (configurePiezoUnit).
    double piezoNewtonsPerUnit_ { 0.0 };
    double piezoMidbandGain_ { 0.0 };
    float piezoForceFloor_ { 0.0f };
    float piezoSaddleFloor_ { 0.0f };
    // Observers (getLastPiezoProbe).
    float lastPiezoOpen_ { 0.0f };
    float lastPiezoVoltage_ { 0.0f };
    float lastPiezoInput_ { 0.0f };
    float lastPiezoDrive_ { 0.0f };
    // The piezo's whole level over radiationReferenceGain: its match to the
    // microphones (PiezoDesign::trim) times its reference for the
    // construction and Picking (piezoReferenceFor), smoothed like the
    // microphones' reference.
    float piezoTrim_ { 1.0f };
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
    // The released static force's high-pass, a double pole at 10 ms, and
    // how long it runs (initialisePluck).
    float releaseStepPole_ { 0.0f };
    int releaseStepSamples_ { 1 };
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
    float lastLongitudinalForce_ { 0.0f };
    float lastBridgePower_ { 0.0f };
    float lastBridgeBodyPower_ { 0.0f };
    float lastBridgeTailPower_ { 0.0f };
    float bodyAmount_ { 0.82f };
    float width_ { 0.62f };
    float outputGain_ { 0.42f };
    float piezoMix_ { 0.0f };
    // A small room's sound at the microphones (EngineParameters::room): its
    // early reflections from a tapped line and its late field from an
    // eight-line feedback delay network, run at the host rate divided by the
    // whole factor that brings it to 64 kHz or under, so its memory does not
    // grow with the rate. The input is the microphones' mid signal; the
    // output a stereo pair, energy-normalised at prepare so its
    // direct-to-reverberant ratio is the same at every rate.
    struct RoomAmbience
    {
        static constexpr int lineCount = 8;
        static constexpr int lineCapacity = 4096;
        static constexpr int earlyCapacity = 2048;
        static constexpr int tapCount = 10;
        std::array<std::array<float, lineCapacity>, lineCount> lines {};
        std::array<int, lineCount> lengths {};
        std::array<int, lineCount> heads {};
        std::array<float, lineCount> absorptionState {};
        std::array<float, lineCount> absorptionGain {};
        std::array<float, lineCount> absorptionPole {};
        std::array<float, earlyCapacity> early {};
        int earlyHead { 0 };
        // Four Schroeder allpasses diffusing what enters the late field.
        static constexpr int diffuserCapacity = 512;
        std::array<std::array<float, diffuserCapacity>, 4> diffusers {};
        std::array<int, 4> diffuserLengths {};
        std::array<int, 4> diffuserHeads {};
        std::array<int, tapCount> tapDelays {};
        std::array<float, tapCount> tapLeft {};
        std::array<float, tapCount> tapRight {};
        int lateDelay { 1 };
        // The late field's input gain, set at prepare to its share.
        float lateInput { 1.0f };
        int longest { 1 };
        float inputCoefficient { 1.0f };
        float inputState { 0.0f };
        float earlyCoefficient { 1.0f };
        float earlyLeft { 0.0f };
        float earlyRight { 0.0f };
        float outputScale { 1.0f };
        int decimation { 1 };
        float inverseDecimation { 1.0f };
        int phase { 0 };
        float accumulator { 0.0f };
        float previousLeft { 0.0f };
        float previousRight { 0.0f };
        float currentLeft { 0.0f };
        float currentRight { 0.0f };
        // Internal samples since anything but an exact zero went in or came
        // out; past the longest path every state is zero again.
        int quietSamples { 0 };
        bool active { false };

        void prepare(double hostRate) noexcept;
        void reset() noexcept;
        // One host sample of the mid signal in (already scaled by the send),
        // the room's left and right out.
        void process(float input, float& left, float& right) noexcept;
        void step(float input) noexcept;
    };
    RoomAmbience room_ {};
    float roomAmount_ { 0.0f };
    float roomSendFor_ { 0.0f };
    float roomSend_ { 0.0f };
    // The microphones, held back by the piezo chain's pipeline (renderPiezo's
    // output is seven samples behind its input at every rate), so the two
    // sensors meet on the instrument's one time base.
    static constexpr int piezoPipelineSamples = outputLatency;
    std::array<float, piezoPipelineSamples> micDelayLeft_ {};
    std::array<float, piezoPipelineSamples> micDelayRight_ {};
    std::array<float, piezoPipelineSamples> micDelayMono_ {};
    int micDelayIndex_ { 0 };
    // The output reference's per-material and per-construction factor
    // (outputReferenceFor in AcustraEngine.cpp), smoothed like the output
    // control, and the mono microphone's own (monoReferenceFor).
    float outputReference_ { 1.0f };
    float monoReference_ { 1.0f };
    float palmMute_ { 0.0f };
    float targetPalmMute_ { 0.0f };
    float palmMuteSmoothing_ { 0.5f };
    // An idle instrument settles on a rounding-level residue (about 1e-12 at
    // the output) that its strings and bridge hold between them; after
    // idleFlushSeconds with nothing played and everything under
    // idleFloor it is cleared once, as All Sound Off clears it, so an idle
    // instrument reaches exact zero (processIdleFlush).
    int idleQuietSamples_ { 0 };
    bool idleFlushed_ { true };
    void processIdleFlush(float samplePeak) noexcept;
    float bodyModelFade_ { 1.0f };
    float bodyModelFadeStep_ { 1.0f / 1920.0f };
    // A live bridge rebuild crossfades the mobility from the modes that were
    // sounding (fadingBridgeLoad_) to the new ones over 20 ms, as the body
    // crossfades its radiation: bridgeLoadFade_ is the new modes' share.
    BridgeLoad fadingBridgeLoad_ {};
    float bridgeLoadFade_ { 1.0f };
    float bridgeLoadFadeStep_ { 1.0f / 960.0f };
    // A rebuild asked for while that fade runs waits for it to end, as the
    // body's does (bodyUpdatePending_): restarting the fade from a mix of two
    // banks, or rebuilding under it, stepped the bridge.
    bool bridgeUpdatePending_ { false };
    GuitarModel configuredBridgeModel_ { GuitarModel::Original };
    GuitarModel fadingBridgeModel_ { GuitarModel::Original };
    BodyShape fadingBridgeShape_ { BodyShape::Dreadnought };
    BodyMaterial fadingBridgeMaterial_ { BodyMaterial::Spruce };
    BodyShape configuredBridgeShape_ { BodyShape::Dreadnought };
    BodyMaterial configuredBridgeMaterial_ { BodyMaterial::Spruce };
    void applyPendingBridge(bool fade) noexcept;
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
    // Off only where a test audits the strings' own waves at the junction:
    // the released static force (initialisePluck) is an external force,
    // outside their wave-norm identity and the piezo's weighting.
    bool releaseStepEnabled_ { true };
    // The strings do not leave the bridge when a note ends, so the junction
    // keeps the port they present rather than switching it out from under a
    // body that is still ringing.
    float lastImpedanceSum_ { 0.0f };
    float lastImpedanceMoment_ { 0.0f };
    float lastImpedanceInertia_ { 0.0f };
    bool bridgeDerivativesNeedPriming_ { true };
    bool bridgeDerivativesCrossRelease_ { false };
    // A live construction change stepped the junction's wave variables on
    // the next sample (FixedDerivative::processAcrossStep).
    bool bridgeDerivativesCrossConfigure_ { false };
    bool prepared_ { false };
    bool bodyConfigured_ { false };
    std::uint64_t noteOrder_ { 0 };
    // Samples rendered since reset(); the hand's memory and the chord window
    // are timed on it.
    std::uint64_t sampleClock_ { 0 };
    std::array<HandFinger, stringCount> hand_ {};
    std::array<std::uint64_t, midiChannelCount> lastNoteOnSample_ {};
    std::array<std::uint64_t, midiChannelCount> chordStartSample_ {};
    std::array<bool, midiChannelCount> noteOnSeen_ {};
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
    // Each pluck's contact noise may draw its own level within this many dB
    // either way (initialiseContactNoise). It draws none: across the picked
    // archtop's three takes of a note the recordings' 0-12 ms energy between
    // partials ranges a median 1.6 dB (pooled SD 1.2 dB), and a noise of its
    // own on every pluck already ranges a median 1.1-2.9 dB over three
    // repeats of a note at MIDI 112 and 16 (Docs/decisions.md, 2026-09-28).
    static constexpr float contactNoiseTakeSpreadDb = 0.0f;
};

} // namespace acustra
