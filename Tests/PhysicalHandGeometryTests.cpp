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
        double point, length, force;
        int delay;
        bool fired;
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
        return { point, voice.speakingLengthMetres, force,
                 voice.pluckDelay, voice.attackFired };
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
    // Stay clear of the point band's reflections when measuring physical
    // travel. The existing upper-register nominal knee remains exercised.
    parameters.pluckPosition = picking == Picking::Pick ? 1.0f : 0.0f;
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
    // The hand drifts 0.70 of the take spread, 0.03 of the open string for
    // the Pick since 1.9.7 and 0.02 otherwise (AcustraEngine::initialisePluck).
    double worstTravelError = 0.0, worstForceError = 0.0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (auto picking : { Picking::Finger, Picking::Pick, Picking::Thumb })
            for (int string : { 0, 2, 5 })
                for (int fret : { 0, 5, 12, 20 })
                {
                    auto still = fresh(rate, picking, 0.0f);
                    auto moved = fresh(rate, picking, -0.5f);
                    const int note = open[static_cast<std::size_t>(string)] + fret;
                    still->noteOn(note, 0.72f, string + 1);
                    moved->noteOn(note, 0.72f, string + 1);
                    const auto a = Access::contact(*still, string);
                    const auto b = Access::contact(*moved, string);
                    const double physicalTravel = -0.5 * 0.70
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
                   - 0.30 * (independent[1] - independent[0])) < 4.0e-8,
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

void extremeVirtualLengthsStayInsideTheContactBand()
{
    for (double rate : { 8000.0, 48000.0, 192000.0 })
        for (float slide : { -96.0f, -24.0f, 0.0f, 24.0f, 96.0f })
            for (float posture : { -1.0f, 1.0f })
            {
                auto engine = fresh(rate, Picking::Finger, posture);
                engine->setPitchBend(slide, 1);
                engine->noteOn(40, 0.72f, 1);
                const auto contact = Access::contact(*engine, 0);
                expect(contact.fired && std::isfinite(contact.point)
                           && contact.point >= 0.05 && contact.point <= 0.36,
                       "virtual slide let physical hand travel escape the contact band");
                expect(std::isfinite(contact.force) && contact.force > 0.0,
                       "virtual slide made physical hand force invalid");
                render(*engine, 256);
            }
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
    extremeVirtualLengthsStayInsideTheContactBand();
    frettedPhysicalTravelReachesTheEmittedSignal();
    postureDependsOnPhysicalTimeInsteadOfNoteDensity();
    postureIsContinuousAcrossRestsAndMotionKnots();
    emptyQueriesResetAndPanicKeepPostureDeterministic();
    if (failures == 0)
        std::cout << "Physical hand geometry checks passed.\n";
    return failures == 0 ? 0 : 1;
}
