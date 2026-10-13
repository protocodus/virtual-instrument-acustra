// A released fretted note falls as the lifting fingertip becomes the
// string's end (AcustraEngine::beginReleaseFall): GuitarSet's measured
// curve of cents against the level the hand's damping has taken, 80 cents
// by 35 dB, where it stops, the period lengthened at the string's tension.
// Open strings do not fall.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct Fall
    {
        float now, scale, seconds, period, tension, slideSpeed, length;
        std::array<float, 2> added; bool played;
    };
    static Fall fall(const AcustraEngine& e, int string)
    {
        const auto& v = e.voices_[static_cast<std::size_t>(string)];
        return { v.releaseFallSemitones, v.releaseFallScale, v.releaseSeconds,
                 v.contactPeriodSamples, v.tensionNewtons, v.fretMotion.speed,
                 v.speakingLengthMetres,
                 { v.loops[0].targetDelay - v.loops[0].configuredDelay,
                   v.loops[1].targetDelay - v.loops[1].configuredDelay },
                 v.played };
    }
    static int controlPeriod() { return AcustraEngine::controlPeriod; }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

// The measured median fall (Tools/MeasureReleaseFall.py), cents against
// the level drop in dB, held from 35 dB.
double measuredFall(double dropDb)
{
    constexpr std::array<std::array<double, 2>, 9> curve {{
        { 0.0, 0.0 }, { 1.5, 0.4 }, { 4.5, 1.9 }, { 8.0, 15.7 }, { 12.5, 28.9 },
        { 17.5, 39.2 }, { 22.5, 49.8 }, { 27.5, 61.6 }, { 35.0, 80.5 } }};
    if (dropDb <= 0.0)
        return 0.0;
    for (std::size_t k = 1; k < curve.size(); ++k)
        if (dropDb <= curve[k][0])
            return curve[k - 1][1] + (curve[k][1] - curve[k - 1][1])
                * (dropDb - curve[k - 1][0]) / (curve[k][0] - curve[k - 1][0]);
    return curve.back()[1];
}

std::unique_ptr<Engine> engine(bool fall, double rate, bool isolate = false,
                               float releaseNoise = 0.0f)
{
    auto e = std::make_unique<Engine>();
    acustra::PerformanceRealism realism;
    realism.releasePitchFall = fall;
    e->setPerformanceRealism(realism);
    acustra::EngineParameters parameters;
    parameters.releaseNoise = releaseNoise;
    e->setParameters(parameters);
    e->prepare(rate, 64);
    e->setTempoBpm(120.0);
    e->setStringPerChannelMode(true);
    if (isolate)
    {
        e->setBridgeCouplingEnabled(false);
        e->setSympatheticStringsEnabled(false);
    }
    return e;
}

std::vector<float> advance(Engine& e, int samples, int block = 64)
{
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(samples));
    std::vector<float> left(static_cast<std::size_t>(block)), right(left.size());
    for (int done = 0; done < samples;)
    {
        const int count = std::min(block, samples - done);
        e.process(left.data(), right.data(), count);
        out.insert(out.end(), left.begin(), left.begin() + count);
        done += count;
    }
    return out;
}

// Plays a note on its string, holds it, lets it go and steps sample by
// sample until the hand's damping has begun (after the 1/32-note join
// window, 62.5 ms at 120 BPM), or a pedal holds it past that window.
void releaseAt(Engine& e, int note, int string, double rate)
{
    e.noteOn(note, 0.7f, string + 1);
    advance(e, static_cast<int>(0.4 * rate));
    e.noteOff(note, string + 1);
    const int window = static_cast<int>(std::ceil(0.0625 * rate));
    for (int n = 0; n < window + 4 && !(Access::fall(e, string).seconds > 0.0f); ++n)
        advance(e, 1, 1);
}

void testFallFollowsTheMeasuredCurve()
{
    // D string, fifth fret: G3. Its loops are configured as the held note
    // was; the fall adds the samples that lengthen the period by its ratio.
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto falling = engine(true, rate);
        releaseAt(*falling, 55, 2, rate);
        const auto start = Access::fall(*falling, 2);
        expect(start.scale == 1.0f, "a released fretted note does not take the whole measured fall");
        expect(start.seconds > 0.0f, "a released note has no hand time");
        // It stops when the hand's damping has taken 35 dB.
        const double stop = 35.0 / 60.0 * start.seconds;
        double worstTrajectory = 0.0, worstPitch = 0.0, previous = 0.0;
        bool monotonic = true;
        for (int step = 1; step <= static_cast<int>(0.3 * rate); ++step)
        {
            advance(*falling, 1, 1);
            const auto now = Access::fall(*falling, 2);
            if (!now.played)
                break;
            // Independent law: the measured curve against 60 dB per T60,
            // advanced a whole control period at each update of the control
            // grid, so it may lead or trail the continuous curve by one.
            const double t = static_cast<double>(step) / rate;
            const double period = Access::controlPeriod() / rate;
            const auto curve = [&](double time)
            {
                return -0.01 * measuredFall(60.0 * std::clamp(time, 0.0, stop + period)
                                            / start.seconds);
            };
            const double late = curve(t - period), early = curve(t + period);
            if (now.now > late + 1.0e-4 || now.now < early - 1.0e-4)
                worstTrajectory = std::max(worstTrajectory,
                    std::min(std::abs(now.now - late), std::abs(now.now - early)));
            monotonic &= now.now <= previous + 1.0e-7;
            previous = now.now;
            // The period P grows to P 2^(-fall/12): both planes' loops by
            // the same samples over what was configured for the held note.
            const double added = now.period * (std::exp2(-now.now / 12.0) - 1.0);
            for (const float loop : now.added)
                worstPitch = std::max(worstPitch, std::abs(loop - added) / now.period);
        }
        std::cout << rate << " Hz: trajectory outside one control period by "
                  << worstTrajectory << " semitones, added delay error "
                  << worstPitch << " of a period, final fall " << previous * 100.0 << " cents\n";
        expect(worstTrajectory < 1.0e-3, "the fall does not follow the measured curve");
        expect(monotonic, "the fall is not monotonic");
        expect(std::abs(previous * 100.0 + 80.5) < 0.05, "the fall did not stop at 80.5 cents");
        expect(worstPitch < 1.0e-5, "the loops' delays do not carry the fall");
    }
}

double measuredCents(const std::vector<float>& audio, std::size_t from,
                     std::size_t count, double rate, double target)
{
    constexpr double pi = 3.1415926535897932384626433832795;
    std::vector<double> windowed(count);
    for (std::size_t n = 0; n < count; ++n)
        windowed[n] = audio[from + n] * (0.5 - 0.5 * std::cos(2.0 * pi * n / (count - 1)));
    const auto power = [&] (double cents)
    {
        const auto rotation = std::polar(1.0, -2.0 * pi * target * std::exp2(cents / 1200.0) / rate);
        std::complex<double> phase { 1.0, 0.0 }, sum {};
        for (const double value : windowed) { sum += value * phase; phase *= rotation; }
        return std::norm(sum);
    };
    double peak = -200.0, maximum = -1.0;
    for (int step = 0; step <= 80; ++step)
    {
        const double cents = -200.0 + 5.0 * step;
        if (const double value = power(cents); value > maximum) { maximum = value; peak = cents; }
    }
    double low = peak - 5.0, high = peak + 5.0;
    for (int step = 0; step < 28; ++step)
    {
        const double a = low + (high - low) / 3.0, b = high - (high - low) / 3.0;
        if (power(a) < power(b)) low = a; else high = b;
    }
    return 0.5 * (low + high);
}

void testTheSoundingPitchFalls()
{
    // The rendered fundamental of the isolated string, against the same
    // string that does not fall: what the string sounds, not its settings.
    for (const double rate : { 48000.0, 96000.0 })
    {
        auto falling = engine(true, rate, true);
        auto steady = engine(false, rate, true);
        releaseAt(*falling, 57, 3, rate); // G string, 2nd fret: A3
        releaseAt(*steady, 57, 3, rate);
        const double seconds = Access::fall(*falling, 3).seconds;
        const auto a = advance(*falling, static_cast<int>(0.2 * rate));
        const auto b = advance(*steady, static_cast<int>(0.2 * rate));
        const double target = 440.0 * std::exp2((57 - 69) / 12.0);
        for (const double centre : { 0.03, 0.08 })
        {
            const auto from = static_cast<std::size_t>((centre - 0.025) * rate);
            const auto count = static_cast<std::size_t>(0.05 * rate);
            const double fallen = measuredCents(a, from, count, rate, target)
                - measuredCents(b, from, count, rate, target);
            // The fall over the window, weighted by the Hann window and by
            // the decaying note's energy.
            double weighted = 0.0, weights = 0.0;
            for (std::size_t n = 0; n < count; ++n)
            {
                // The damping began on the last sample releaseAt rendered.
                const double t = static_cast<double>(from + n) / rate;
                const double w = (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * n / (count - 1)))
                    * std::exp(-2.0 * 6.9078 * t / seconds);
                weighted += w * -measuredFall(60.0 * t / seconds);
                weights += w;
            }
            const double expected = weighted / weights;
            std::cout << rate << " Hz, " << centre * 1000.0 << " ms after damping: fell "
                      << fallen << " cents, expected about " << expected << '\n';
            expect(std::abs(fallen - expected) < 8.0, "the rendered pitch does not fall as the fingertip lifts");
        }
    }
}

void testOpenStringsDoNotFall()
{
    auto falling = engine(true, 48000.0);
    auto steady = engine(false, 48000.0);
    releaseAt(*falling, 50, 2, 48000.0);
    releaseAt(*steady, 50, 2, 48000.0);
    expect(Access::fall(*falling, 2).scale == 0.0f, "an open string fell");
    expect(advance(*falling, 24000) == advance(*steady, 24000),
           "an open string's release changed with the fall");
}

void testRefrettingReturnsWithoutASlide()
{
    // A same-pitch stroke after the damping began presses the fallen string
    // back onto its fret: the pitch returns, and with Release Noise up the
    // wound string makes no sliding scrape for a stop it did not travel.
    auto falling = engine(true, 48000.0, false, 0.7f);
    auto steady = engine(false, 48000.0, false, 0.7f);
    releaseAt(*falling, 45, 0, 48000.0); // low E, 5th fret
    releaseAt(*steady, 45, 0, 48000.0);
    // 100 ms into the damping: 19 dB down, about 42 cents fallen.
    advance(*falling, 4800);
    advance(*steady, 4800);
    expect(Access::fall(*falling, 0).now < -0.2f, "the fall had not begun before the re-fret");
    falling->noteOn(45, 0.7f, 1);
    steady->noteOn(45, 0.7f, 1);
    const auto refretted = Access::fall(*falling, 0);
    expect(refretted.now == 0.0f && refretted.scale == 0.0f
               && refretted.added[0] == 0.0f && refretted.added[1] == 0.0f,
           "a new stroke left the string fallen");
    float scrape = 0.0f;
    for (int n = 0; n < 4800; ++n)
    {
        advance(*falling, 1, 1);
        scrape = std::max(scrape, std::abs(Access::fall(*falling, 0).slideSpeed));
    }
    advance(*steady, 4800);
    expect(scrape == 0.0f, "pressing a fallen string back onto its fret was taken for a slide");
    expect(Access::fall(*falling, 0).length == Access::fall(*steady, 0).length,
           "the fall moved the string's stopping point");
}

void testPedalHandBackAndPanic()
{
    auto e = engine(true, 48000.0);
    e->setSustainPedal(true, 3);
    releaseAt(*e, 57, 2, 48000.0); // D string, 7th fret, on channel 3
    expect(Access::fall(*e, 2).scale == 0.0f, "a pedalled note fell before the pedal lifted");
    e->setSustainPedal(false, 3);
    advance(*e, 64);
    expect(Access::fall(*e, 2).scale == 1.0f && Access::fall(*e, 2).now < 0.0f,
           "lifting the pedal did not let the fretted note fall");
    advance(*e, static_cast<int>(0.6 * 48000.0));
    const auto handedBack = Access::fall(*e, 2);
    expect(!handedBack.played && handedBack.now == 0.0f && handedBack.scale == 0.0f,
           "a string handed back kept its fall");
    releaseAt(*e, 57, 2, 48000.0);
    advance(*e, 1200);
    e->allSoundOff(3);
    const auto cleared = Access::fall(*e, 2);
    expect(cleared.now == 0.0f && cleared.scale == 0.0f, "panic left a string fallen");
}

void testNoNewAttackAtTheRelease()
{
    // A G chord let go: the damped, falling strings never rise above what
    // the same chord, still held, sounds over the same time.
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto e = engine(true, rate), held = engine(true, rate);
        const std::array<int, 6> chord { 43, 47, 50, 55, 59, 67 };
        for (auto* instrument : { e.get(), held.get() })
            for (int string = 0; string < 6; ++string)
                instrument->noteOn(chord[static_cast<std::size_t>(string)], 0.8f, string + 1);
        advance(*e, static_cast<int>(0.6 * rate));
        advance(*held, static_cast<int>(0.6 * rate));
        for (int string = 0; string < 6; ++string)
            e->noteOff(chord[static_cast<std::size_t>(string)], string + 1);
        const int window = static_cast<int>(std::ceil(0.0625 * rate)) + 1;
        advance(*e, window);
        advance(*held, window);
        const auto released = advance(*e, static_cast<int>(0.5 * rate));
        const auto sustained = advance(*held, static_cast<int>(0.5 * rate));
        float after = 0.0f, still = 0.0f;
        for (const float x : released)
            after = std::max(after, std::abs(x));
        for (const float x : sustained)
            still = std::max(still, std::abs(x));
        expect(std::isfinite(after) && after <= still,
               "a falling release peaked above the chord it ended");
    }
}

void testRatesAndBlocksAgree()
{
    std::array<float, 3> at50ms {};
    int index = 0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto e = engine(true, rate);
        releaseAt(*e, 62, 4, rate); // B string, 3rd fret: D4
        advance(*e, static_cast<int>(0.05 * rate));
        at50ms[static_cast<std::size_t>(index++)] = Access::fall(*e, 4).now;
    }
    const float spread = *std::max_element(at50ms.begin(), at50ms.end())
        - *std::min_element(at50ms.begin(), at50ms.end());
    expect(spread < 0.015f, "the fall's time course depends on the sample rate");
    const auto renderWith = [](int block)
    {
        auto e = engine(true, 48000.0);
        e->noteOn(60, 0.7f, 3);
        auto out = advance(*e, 19000, block);
        e->noteOff(60, 3);
        const auto rest = advance(*e, 24000, block);
        out.insert(out.end(), rest.begin(), rest.end());
        return out;
    };
    const auto reference = renderWith(64);
    for (const int block : { 1, 37, 512 })
        expect(renderWith(block) == reference, "the fall depends on the host's block size");
}

void testFirmKeyUpFallsFaster()
{
    const auto fallAfter = [](float releaseVelocity)
    {
        auto e = engine(true, 48000.0);
        e->noteOn(55, 0.7f, 3);
        advance(*e, 19200);
        e->noteOffWithVelocity(55, 3, releaseVelocity);
        advance(*e, 3001 + 1440);
        return Access::fall(*e, 2).now;
    };
    const float firm = fallAfter(1.0f), nominal = fallAfter(64.0f / 127.0f),
                gentle = fallAfter(0.1f);
    expect(firm < nominal && nominal < gentle,
           "the fall does not follow how firmly the key let go");
}

void testMemberTensionBendIsKept()
{
    // An MPE member's bend is tension at its fret; the fall moves the stop.
    auto e = engine(true, 48000.0);
    e->setStringPerChannelMode(false);
    e->setLowerZoneMemberCount(15);
    e->setPitchBend(1.0f, 2);
    e->noteOn(57, 0.7f, 2);
    advance(*e, 19200);
    const int string = e->heldString(57, 2);
    const float tension = Access::fall(*e, string).tension;
    e->noteOff(57, 2);
    // The join window, then 100 ms of damping: about 42 cents fallen.
    advance(*e, 3001 + 4800);
    const auto now = Access::fall(*e, string);
    expect(now.now < -0.3f, "a member note did not fall");
    expect(std::abs(now.tension / tension - 1.0f) < 1.0e-6f,
           "the fall changed the member's bent tension");
}
} // namespace

int main()
{
    std::cout << std::unitbuf;
    testFallFollowsTheMeasuredCurve();
    testTheSoundingPitchFalls();
    testOpenStringsDoNotFall();
    testRefrettingReturnsWithoutASlide();
    testPedalHandBackAndPanic();
    testNoNewAttackAtTheRelease();
    testRatesAndBlocksAgree();
    testFirmKeyUpFallsFaster();
    testMemberTensionBendIsKept();
    if (failures != 0)
    {
        std::cerr << failures << " release fall check(s) failed\n";
        return 1;
    }
    std::cout << "release fall checks passed\n";
    return 0;
}
