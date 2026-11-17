#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct Contact
    {
        double point, length, force, displacement;
        int delay;
        bool fired;
        std::array<double, 2> planeMetres;
        double period;
    };

    static void hand(AcustraEngine& engine, float position, bool active = true)
    {
        engine.pickingGesture_ = { position, 0.0f, active };
        engine.pickingGestureSeen_ = true;
        engine.pickingGestureSample_ = engine.sampleClock_;
    }

    static void seed(AcustraEngine& engine, std::uint32_t seed)
    {
        for (auto& voice : engine.voices_)
            voice.randomState = seed;
    }

    static std::array<float, 3> posture(AcustraEngine& engine, std::uint64_t sample)
    {
        engine.sampleClock_ = sample;
        const auto gesture = engine.nextPickingGesture();
        return { gesture.position, gesture.pressure, gesture.active ? 1.0f : 0.0f };
    }

    static bool seen(const AcustraEngine& engine) { return engine.pickingGestureSeen_; }

    static std::array<std::uint32_t, 3> otherDraws(const AcustraEngine& engine)
    {
        return { engine.strumRandomState_, engine.voices_[0].randomState,
                 engine.voices_[0].pluckForceState };
    }

    static Contact contact(const AcustraEngine& engine, int string)
    {
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        const double lengthSamples = std::round(voice.loops[0].currentDelay);
        // The released static saddle-force step independently records the
        // held displacement. Recover the hand force from T*y/[L*p*(1-p)],
        // rather than reading the initializer's compliance or force gains.
        const double displacement = voice.releaseStepRise
            * voice.releaseShapePosition[0] * lengthSamples
            / (AcustraEngine::releaseStepShare * std::sqrt(voice.polarisationMix));
        const double point = voice.pluckPoint;
        const double force = displacement
            * engine.physicalCalibration_.steelDisplacementScaleMetres
            * voice.tensionNewtons
            / (voice.speakingLengthMetres * point * (1.0 - point));
        std::array<double, 2> planes {};
        for (std::size_t plane = 0; plane < planes.size(); ++plane)
        {
            const int written = std::clamp(static_cast<int>(std::round(
                voice.loops[plane].currentDelay)), 8, engine.activeDelaySamples() - 3);
            planes[plane] = voice.releaseShapePosition[plane] * double(written)
                / voice.contactPeriodSamples * voice.speakingLengthMetres;
        }
        return { point, voice.speakingLengthMetres, force, displacement, voice.pluckDelay,
                 voice.attackFired, planes, voice.contactPeriodSamples };
    }

    // Natural harmonics have no public note-number shortcut. Use the same
    // explicit touched-node entry point as the string pitch suite.
    static void touchHarmonic(AcustraEngine& engine, int string)
    {
        engine.startNote(string, 2, engine.voices_[string].openMidi + 12,
                         0.72f, string + 1, 0, false);
    }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
using Picking = acustra::PickingTechnique;
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

auto fresh(double rate, Picking picking, float posture,
           std::uint32_t seed = 0x13579bdu, bool active = true)
{
    auto engine = std::make_unique<Engine>();
    acustra::EngineParameters parameters;
    parameters.picking = picking;
    // Put the Finger and Thumb about 175 mm from the saddle. The Pick, 0.40
    // of the Finger's distance since 1.9.63, reaches only 130 mm.
    parameters.pluckPosition = picking == Picking::Pick ? 1.0f
        : picking == Picking::Thumb ? 0.035f : 0.40f;
    parameters.room = 0.0f;
    engine->setParameters(parameters);
    engine->prepare(rate, 127);
    engine->setStringPerChannelMode(true);
    Access::seed(*engine, seed);
    Access::hand(*engine, posture, active);
    return engine;
}

std::vector<float> render(Engine& engine, int frames, int block = 127)
{
    std::vector<float> result(static_cast<std::size_t>(frames * 2));
    for (int start = 0; start < frames; start += block)
    {
        const int count = std::min(block, frames - start);
        engine.process(result.data() + start, result.data() + frames + start, count);
    }
    expect(std::all_of(result.begin(), result.end(), [](float sample)
        { return std::isfinite(sample) && std::abs(sample) <= 1.0f; }),
        "physical hand geometry produced nonfinite or unbounded audio");
    return result;
}

void sameHandMovesEqualMillimetresAcrossFrets()
{
    constexpr std::array<int, 6> open { 40, 45, 50, 55, 59, 64 };
    // An isolated note's hand drifts 0.55 of the take spread, 0.03 of the open string for
    // the Pick since 1.9.7 and 0.02 otherwise (AcustraEngine::initialisePluck).
    double worstTravelError = 0.0, worstForceError = 0.0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (auto picking : { Picking::Finger, Picking::Pick, Picking::Thumb })
            for (int string : { 0, 2, 5 })
                for (int fret : { 0, 2, 3 })
                {
                    auto still = fresh(rate, picking, 0.0f);
                    auto moved = fresh(rate, picking, -0.5f);
                    const int note = open[static_cast<std::size_t>(string)] + fret;
                    still->noteOn(note, 0.72f, string + 1);
                    moved->noteOn(note, 0.72f, string + 1);
                    const auto a = Access::contact(*still, string);
                    const auto b = Access::contact(*moved, string);
                    const double physicalTravel = -0.5 * 0.55
                        * (picking == Picking::Pick ? 0.03 : 0.02) * 0.648;
                    const double travel = (b.point - a.point) * a.length;
                    const double travelError = std::abs(travel - physicalTravel);
                    const double forceError = std::abs(b.force / a.force - 1.0);
                    expect(a.fired && b.fired && a.length == b.length,
                           "physical travel fixture missed its selected string/fret");
                    expect(travelError < 4.0e-8,
                           "one shared hand moved different millimetres on a fretted string");
                    expect(a.force > 0.0 && forceError < 3.0e-6,
                           "physical hand travel changed commanded force instead of compliance");
                    worstTravelError = std::max(worstTravelError, travelError);
                    worstForceError = std::max(worstForceError, forceError);
                }
    std::cout << "Shared hand: worst travel error=" << worstTravelError
              << " metres, force error=" << worstForceError << '\n';
}

void independentTakeSpreadAndExplicitPositionRemainOwned()
{
    std::array<double, 2> independent {}, shared {};
    for (std::size_t i = 0; i < 2; ++i)
    {
        const auto seed = i == 0 ? 0x13579bdu : 0x6d2b79f5u;
        auto a = fresh(48000.0, Picking::Finger, 0.0f, seed, false);
        auto b = fresh(48000.0, Picking::Finger, 0.0f, seed, true);
        a->noteOn(40, 0.72f, 1);
        b->noteOn(40, 0.72f, 1);
        independent[i] = Access::contact(*a, 0).point;
        shared[i] = Access::contact(*b, 0).point;
    }
    expect(std::abs(independent[1] - independent[0]) > 1.0e-4,
           "independent take fixture did not move the contact point");
    expect(std::abs((shared[1] - shared[0])
                   - 0.45 * (independent[1] - independent[0])) < 4.0e-8,
           "shared millimetre mapping changed the independent take spread");

    for (auto picking : { Picking::Finger, Picking::Pick, Picking::Thumb })
    {
        auto left = fresh(48000.0, picking, -1.0f);
        auto right = fresh(48000.0, picking, 1.0f);
        for (auto* engine : { left.get(), right.get() })
        {
            engine->setLowerZoneMemberCount(15);
            engine->setMpeTimbre(0.65f, 2);
            engine->noteOn(57, 0.72f, 2);
        }
        expect(Access::contact(*left, 1).point == Access::contact(*right, 1).point,
               "shared physical hand displaced an explicit member CC74 position");
        expect(render(*left, 512) == render(*right, 512),
               "explicit member position inherited unrelated shared hand motion");
    }
}

void queuedStrokeKeepsItsPhysicalPostureThroughASlide()
{
    auto early = fresh(48000.0, Picking::Finger, -0.5f);
    auto later = fresh(48000.0, Picking::Finger, -0.5f);
    for (auto* engine : { early.get(), later.get() })
    {
        engine->beginStrum();
        engine->noteOn(52, 0.72f, 1, 150, true);
        engine->setPitchBend(5.0f, 1);
    }
    Access::hand(*later, 0.9f);
    later->beginStrum();
    const int wait = Access::contact(*early, 0).delay + 1;
    expect(wait > 1, "queued physical hand fixture fired before its slide");
    render(*early, wait);
    render(*later, wait);
    const auto a = Access::contact(*early, 0);
    const auto b = Access::contact(*later, 0);
    expect(a.fired && b.fired && a.length < 0.324,
           "queued physical hand fixture did not reach its shortened string");
    expect(a.point == b.point && a.force == b.force,
           "later hand posture replaced a queued strum's captured physical position");
}

// 1.9.52 to 1.9.62 held every strike neckward of its body's soundhole rim;
// 1.9.63 frees the hand again. The strike now stays inside the hand's own
// band of the sounding string (AcustraEngine::initialisePluck: 0.05 to 0.36
// from the panel, to 0.46 from a member's CC74, a take reflected at either
// edge), and each written plane sits its authored offset from that centre.
constexpr double lowestPoint = 0.05, panelHighest = 0.36, memberHighest = 0.46;
constexpr std::array<double, 2> planeOffsets { -0.006, 0.009 };

void checkStrike(const Access::Contact& contact, double highest, bool checkPlanes = true)
{
    constexpr double rounding = 1.0e-6; // float band edges and their reflection
    expect(contact.fired, "the requested physical strike did not fire");
    expect(contact.point >= lowestPoint - rounding && contact.point <= highest + rounding,
           "the strike centre left the hand's band of the sounding string");
    if (checkPlanes)
        for (std::size_t plane = 0; plane < planeOffsets.size(); ++plane)
        {
            const double distance = contact.planeMetres[plane];
            expect(distance >= (lowestPoint + planeOffsets[plane] - rounding) * contact.length
                       && distance <= (highest + planeOffsets[plane] + rounding) * contact.length,
                   "the written transverse wave left the hand's band or passed the stop");
        }
}

void allStringsRespectTheirBodyAtBothPositionEndpoints()
{
    const auto open = Engine::openNotes(acustra::Tuning::Standard);
    constexpr std::size_t shapes = 4;
    std::array<double, shapes> closest { 1.0, 1.0, 1.0, 1.0 };
    // Since 1.9.63 the body no longer moves the strike: every shape must
    // place each endpoint stroke exactly where the first body did.
    std::vector<double> firstBodyPoints;
    int strikes = 0;
    for (std::size_t shape = 0; shape < shapes; ++shape)
    {
        std::size_t index = 0;
        for (auto picking : { Picking::Finger, Picking::Pick, Picking::Thumb })
        {
            auto engine = fresh(48000.0, picking, 0.0f);
            acustra::EngineParameters parameters;
            parameters.shape = static_cast<acustra::BodyShape>(shape);
            parameters.picking = picking;
            parameters.room = 0.0f;
            for (int string = 0; string < Engine::stringCount; ++string)
                for (int fret : { 0, 12, Engine::fretCount })
                    for (bool member : { false, true })
                    {
                        double lower = 0.0;
                        for (float control : { 0.0f, 1.0f })
                        {
                            engine->reset();
                            parameters.pluckPosition = member ? 1.0f - control : control;
                            engine->setParameters(parameters);
                            engine->setStringPerChannelMode(true);
                            engine->setLowerZoneMemberCount(member ? 15 : 0);
                            // Channel one manages the lower zone. To strike
                            // low E with its own CC74, use a free member and
                            // slide its uniquely playable open note to the fret.
                            const bool lowMember = member && string == 0;
                            const int channel = lowMember ? 7 : string + 1;
                            if (lowMember)
                                engine->setPitchBend(static_cast<float>(fret), 1);
                            if (member)
                                engine->setMpeTimbre(control, channel);
                            Access::seed(*engine, 0x13579bdu);
                            Access::hand(*engine, fret == 0 ? -1.0f : 1.0f);
                            const int note = open[string] + (lowMember ? 0 : fret);
                            engine->noteOn(note, 0.72f, channel);
                            const auto contact = Access::contact(*engine, string);
                            expect(engine->heldString(note, channel) == string
                                       && std::abs(contact.length - 0.648 * std::exp2(-fret / 12.0)) < 1e-6,
                                   "the position endpoint missed its intended string and fret");
                            checkStrike(contact, member ? memberHighest : panelHighest);
                            expect(std::isfinite(contact.force) && contact.force > 0.0,
                                   "a position endpoint dropped the fresh note's excitation");
                            closest[shape] = std::min(closest[shape], contact.planeMetres[0]);
                            if (shape == 0)
                                firstBodyPoints.push_back(contact.point);
                            else
                                expect(index < firstBodyPoints.size()
                                           && contact.point == firstBodyPoints[index],
                                       "the body shape moved the strike");
                            ++index;
                            if (control == 0.0f)
                                lower = contact.point;
                            else if (member || fret == 0)
                                expect(contact.point > lower,
                                       "open-string position or member CC74 reversed its endpoints");
                            else
                                // Shared hand motion reflects at its upper limit
                                // on short strings. A single draw can return
                                // bridgeward while keeping distinct endpoints.
                                expect(std::abs(contact.point - lower) > 1e-5,
                                       "a fretted position control lost its endpoint travel");
                            ++strikes;
                        }
                    }
        }
    }
    std::cout << strikes << " endpoint strikes; closest normal contact by shape (mm):";
    for (double distance : closest)
        std::cout << ' ' << 1000.0 * distance;
    std::cout << '\n';
}

void changingHandsAndBodiesRefreshTheNextStrike()
{
    auto engine = fresh(48000.0, Picking::Pick, -1.0f);
    acustra::EngineParameters parameters;
    parameters.shape = acustra::BodyShape::Dreadnought;
    parameters.picking = Picking::Pick;
    parameters.pluckPosition = 0.0f;
    engine->setParameters(parameters);
    engine->setLowerZoneMemberCount(15);
    engine->setMpeTimbre(1.0f, 2);
    engine->noteOn(45, 0.75f, 2);
    const auto first = Access::contact(*engine, 1);
    checkStrike(first, memberHighest);
    render(*engine, 4800);
    parameters.shape = acustra::BodyShape::Parlor;
    engine->setParameters(parameters);
    engine->setMpeTimbre(0.0f, 2);
    Access::hand(*engine, 1.0f);
    engine->beginStrum(160, 0, true, 5, 0);
    engine->noteOn(45, 0.75f, 2, 80, true);
    const int delay = Access::contact(*engine, 1).delay;
    expect(delay > 0, "the return stroke was never queued");
    render(*engine, delay);
    const auto second = Access::contact(*engine, 1);
    checkStrike(second, memberHighest);
    expect(first.point - second.point > 0.10,
           "re-plucking a ringing string reused the previous contact position");
    const auto audio = render(*engine, 1600);
    expect(std::any_of(audio.begin(), audio.end(), [](float sample) { return std::abs(sample) > 1e-5f; }),
           "re-plucking near the bridge silenced the ringing string");

    engine->reset();
    engine->setParameters(parameters);
    Access::touchHarmonic(*engine, 5);
    const auto harmonic = Access::contact(*engine, 5);
    expect(std::abs(harmonic.length - 0.648) < 1e-6
               && engine->getStringActivity()[5].harmonic == 2,
           "the touched node lost the open string's physical length");
    checkStrike(harmonic, panelHighest);
}

void extremeVirtualLengthsRespectTheFretboardAndRetainPitch()
{
    for (double rate : { 8000.0, 48000.0, 192000.0 })
        for (float slide : { -96.0f, -24.0f, 0.0f, 24.0f, 96.0f })
            for (float posture : { -1.0f, 1.0f })
            {
                auto engine = fresh(rate, Picking::Finger, posture);
                engine->setPitchBend(slide, 1);
                engine->noteOn(40, 0.72f, 1);
                const auto contact = Access::contact(*engine, 0);
                // A wave with only a few samples per period cannot resolve two
                // represented kinks. Its hand, force and emitted audio still exist.
                checkStrike(contact, panelHighest, contact.period >= 32.0);
                if (slide > Engine::fretCount)
                {
                    const double lastFret = 0.648 * std::exp2(-Engine::fretCount / 12.0);
                    const double requested = std::min(0.24 * rate,
                        440.0 * std::exp2((40.0 + slide - 69.0) / 12.0));
                    expect(std::abs(contact.length - lastFret) < 1e-6,
                           "an upward virtual slide moved the stop beyond the fretboard");
                    expect(std::abs(rate / contact.period / requested - 1.0) < 0.01,
                           "the fretboard limit detuned the requested virtual pitch");
                }
                const auto audio = render(*engine, static_cast<int>(0.025 * rate));
                expect(std::isfinite(contact.force) && contact.force > 0.0
                           && std::any_of(audio.begin(), audio.end(), [](float sample)
                               { return std::abs(sample) > 1e-9f; }),
                       "a virtual-slide strike lost its excitation or rendered silence");
            }
}

void downwardSlideKeepsTheOpenStrokeContinuousAndBounded()
{
    const auto strike = [](float slide, std::uint32_t seed)
    {
        // Each fresh engine repeats the same force, angle and contact draws.
        // MIDI's zero-based channel 0 is engine channel 1.
        auto engine = fresh(48000.0, Picking::Pick, 0.0f, seed);
        acustra::EngineParameters parameters;
        parameters.picking = Picking::Pick;
        parameters.pluckPosition = 0.0f;
        engine->setParameters(parameters);
        engine->setPitchBend(slide, 1);
        engine->noteOn(40, 0.72f, 1);
        return Access::contact(*engine, 0);
    };

    double largestBoundaryStep = 0.0, largestLongStroke = 0.0;
    for (auto seed : { 0x13579bdu, 0x6d2b79f5u })
    {
        const auto open = strike(0.0f, seed);
        expect(open.fired && open.displacement > 0.0, "missing open Pick reference stroke");
        for (float slide : { -0.001f, 0.001f })
        {
            const auto crossing = strike(slide, seed);
            const double change = std::abs(crossing.displacement / open.displacement - 1.0);
            expect(crossing.fired && (slide < 0.0f ? crossing.length > open.length
                                                     : crossing.length < open.length),
                   "the fractional pitch wheel did not cross open-string length");
            expect(change < 1e-3, "crossing the open string stepped the Pick's release amplitude");
            largestBoundaryStep = std::max(largestBoundaryStep, change);
        }
        for (float slide : { -2.0f, -12.0f, -24.0f, -48.0f, -96.0f })
        {
            const auto extended = strike(slide, seed);
            const double ratio = extended.displacement / open.displacement;
            // The independent Pick draw moves at most 0.45 * 0.03 of the
            // string here. At a nominal point >= 0.05 it can change the
            // point-compliance factor by up to 27%. Allow the open
            // reference's own small draw too; extra virtual metres must
            // not multiply the stroke.
            expect(extended.fired && extended.length > open.length && ratio > 0.0 && ratio < 1.4,
                   "the longer virtual string amplified or dropped the release");
            largestLongStroke = std::max(largestLongStroke, ratio);
        }
    }
    std::cout << "Open Pick slide: maximum fractional boundary step " << largestBoundaryStep
              << ", largest long-string release/open " << largestLongStroke << '\n';
}

void frettedPhysicalTravelReachesTheEmittedSignal()
{
    double smallestDifference = 1.0;
    for (auto picking : { Picking::Finger, Picking::Pick, Picking::Thumb })
    {
        auto physical = fresh(48000.0, picking, -0.5f);
        // At the octave, the old fractional-string law moved half as far.
        // Halving the input posture reconstructs that old displacement
        // while preserving every force, pressure and independent draw.
        auto oldTravel = fresh(48000.0, picking, -0.25f);
        physical->noteOn(52, 0.72f, 1);
        oldTravel->noteOn(52, 0.72f, 1);
        const auto signal = render(*physical, 12000);
        const auto reference = render(*oldTravel, 12000);
        double energy = 0.0, difference = 0.0;
        for (std::size_t i = 0; i < signal.size(); ++i)
        {
            energy += double(signal[i]) * signal[i];
            const double d = signal[i] - reference[i];
            difference += d * d;
        }
        const double relative = std::sqrt(difference / std::max(energy, 1.0e-30));
        expect(energy > 1.0e-8 && relative > 0.005,
               "fret-dependent physical travel did not change emitted articulation");
        smallestDifference = std::min(smallestDifference, relative);
    }
    std::cout << "Physical vs old octave travel: minimum audio difference/RMS="
              << smallestDifference << '\n';
}

auto trajectory(double rate)
{
    auto engine = std::make_unique<Engine>();
    engine->prepare(rate, 127);
    return engine;
}

void postureDependsOnPhysicalTimeInsteadOfNoteDensity()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto sparse = trajectory(rate), ornamented = trajectory(rate);
        expect(Access::posture(*sparse, 0)[2] == 0.0f,
               "first contact lost the nominal calibration exemption");
        expect(Access::posture(*ornamented, 0)[2] == 0.0f,
               "ornament fixture has no nominal first contact");
        const auto otherDraws = Access::otherDraws(*ornamented);
        std::uint64_t last = 0;
        bool densityInvariant = true, snapshotStable = true;
        for (std::uint64_t n = 1; n <= 160; ++n)
        {
            const auto at = n * 997 + 37;
            for (auto between = last + 31; between < at; between += 31)
                Access::posture(*ornamented, between);
            const auto expected = Access::posture(*sparse, at);
            const auto actual = Access::posture(*ornamented, at);
            densityInvariant &= actual == expected;
            snapshotStable &= Access::posture(*ornamented, at) == actual;
            last = at;
        }
        expect(densityInvariant,
               "intervening contacts moved the absolute-time hand trajectory");
        expect(snapshotStable, "same-sample contact changed the shared posture snapshot");
        expect(Access::otherDraws(*ornamented) == otherDraws,
               "continuous posture consumed unrelated excitation/strum/repeat draws");
        const auto afterSilence = Access::posture(*sparse,
            static_cast<std::uint64_t>(86400.0 * rate));
        expect(afterSilence[2] == 1.0f && std::abs(afterSilence[0]) <= 1.0f
                   && std::abs(afterSilence[1]) <= 1.0f,
               "long silence invalidated a bounded established hand posture");
    }
}

void postureIsContinuousAcrossRestsAndMotionKnots()
{
    double worstRestStep = 0.0, worstVelocity = 0.0, worstKnotVelocityJump = 0.0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        // Separate, identically prepared histories expose the former rest
        // gate; querying one engine consecutively would itself hide that gate.
        std::array<std::array<float, 3>, 3> aroundRest {};
        for (int side = -1; side <= 1; ++side)
        {
            auto engine = trajectory(rate);
            Access::posture(*engine, 0);
            Access::posture(*engine, static_cast<std::uint64_t>(0.2 * rate));
            aroundRest[static_cast<std::size_t>(side + 1)] = Access::posture(*engine,
                static_cast<std::uint64_t>(2.2 * rate) + side);
            expect(aroundRest[static_cast<std::size_t>(side + 1)][2] == 1.0f,
                   "crossing a two-second rest abruptly replaced the shared hand");
        }
        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            const double step = std::abs(double(aroundRest[2][axis]) - aroundRest[0][axis]);
            // A shape-preserving cubic's endpoint tangents are harmonic
            // means of bounded target secants. For one-second targets in
            // [-1,1], its speed is at most 4 normalized units per second.
            expect(step <= 8.0 / rate + 2.0e-7,
                   "rest-neighbour posture change exceeded finite physical velocity");
            worstRestStep = std::max(worstRestStep, step);
        }

        auto engine = trajectory(rate);
        const auto stride = static_cast<std::uint64_t>(std::round(rate / 1000.0));
        auto before = Access::posture(*engine, 0);
        double maximumSpeed = 0.0;
        for (std::uint64_t n = 1; n <= 10000; ++n)
        {
            const auto current = Access::posture(*engine, n * stride);
            for (std::size_t axis = 0; axis < 2; ++axis)
            {
                const double speed = std::abs(double(current[axis]) - before[axis])
                    * rate / stride;
                expect(std::isfinite(current[axis]) && std::abs(current[axis]) <= 1.0f,
                       "continuous posture escaped its physical amplitude bounds");
                maximumSpeed = std::max(maximumSpeed, speed);
                worstVelocity = std::max(worstVelocity, speed);
            }
            before = current;
        }
        expect(maximumSpeed <= 4.001,
               "continuous posture exceeded its finite physical speed bound");

        // Use a fresh path for each knot so time never travels backwards.
        const auto radius = static_cast<std::uint64_t>(std::round(rate / 10000.0));
        for (std::uint64_t second = 1; second <= 12; ++second)
        {
            auto atKnot = trajectory(rate);
            Access::posture(*atKnot, 0);
            const auto centre = static_cast<std::uint64_t>(second * rate);
            const auto left = Access::posture(*atKnot, centre - radius);
            const auto middle = Access::posture(*atKnot, centre);
            const auto right = Access::posture(*atKnot, centre + radius);
            for (std::size_t axis = 0; axis < 2; ++axis)
            {
                const double leftVelocity = (double(middle[axis]) - left[axis]) * rate / radius;
                const double rightVelocity = (double(right[axis]) - middle[axis]) * rate / radius;
                const double jump = std::abs(rightVelocity - leftVelocity);
                expect(jump < 0.02, "hand trajectory velocity jumped at a target knot");
                worstKnotVelocityJump = std::max(worstKnotVelocityJump, jump);
            }
        }
    }
    std::cout << "Continuous posture: rest step=" << worstRestStep
              << ", maximum speed=" << worstVelocity
              << "/s, knot velocity mismatch=" << worstKnotVelocityJump << "/s\n";
}

void emptyQueriesResetAndPanicKeepPostureDeterministic()
{
    auto engine = trajectory(48000.0);
    static_cast<void>(engine->canSound(40, 1));
    static_cast<void>(engine->plannedString(40, 1));
    render(*engine, 128);
    expect(!Access::seen(*engine), "empty playback/query activated the picking hand");
    const auto first = Access::posture(*engine, 128);
    expect(first[2] == 0.0f && Access::posture(*engine, 128) == first,
           "same-sample queries activated a nominal first contact");
    Access::posture(*engine, 48000);
    engine->reset();
    auto freshEngine = trajectory(48000.0);
    for (std::uint64_t at : { 0ull, 137ull, 17011ull, 96001ull })
        expect(Access::posture(*engine, at) == Access::posture(*freshEngine, at),
               "reset did not restart the deterministic physical hand path");

    engine->allSoundOff(1);
    expect(!Access::seen(*engine), "panic retained an active hand snapshot");
    freshEngine->reset();
    for (std::uint64_t offset : { 0ull, 137ull, 17011ull, 96001ull })
        expect(Access::posture(*engine, 96001 + offset)
                   == Access::posture(*freshEngine, offset),
               "panic did not restart posture at its new physical-time origin");
}
}

int main()
{
    sameHandMovesEqualMillimetresAcrossFrets();
    independentTakeSpreadAndExplicitPositionRemainOwned();
    queuedStrokeKeepsItsPhysicalPostureThroughASlide();
    allStringsRespectTheirBodyAtBothPositionEndpoints();
    changingHandsAndBodiesRefreshTheNextStrike();
    extremeVirtualLengthsRespectTheFretboardAndRetainPitch();
    downwardSlideKeepsTheOpenStrokeContinuousAndBounded();
    frettedPhysicalTravelReachesTheEmittedSignal();
    postureDependsOnPhysicalTimeInsteadOfNoteDensity();
    postureIsContinuousAcrossRestsAndMotionKnots();
    emptyQueriesResetAndPanicKeepPostureDeterministic();
    if (failures == 0)
        std::cout << "Physical hand geometry checks passed.\n";
    return failures == 0 ? 0 : 1;
}
