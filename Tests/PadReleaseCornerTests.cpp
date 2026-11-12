// Independent transfer-function witnesses for the pad's half-power match.
// Production filters a periodic sine; this test finds the original pole by
// bisection of its complex response, without repeating the production root.
#include "DSP/AcustraEngine.h"

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
    template <typename Input>
    static std::vector<float> filterLine(int length, Input input, double pole,
                                        double reference, bool matched)
    {
        auto loop = std::make_unique<AcustraEngine::StringLoop>();
        for (int sample = 0; sample < length; ++sample)
            loop->delay[static_cast<std::size_t>(
                AcustraEngine::maximumDelaySamples - sample - 1)]
                = input(sample);
        AcustraEngine::applyPadRelease(*loop, length, pole, reference, matched);
        std::vector<float> result(static_cast<std::size_t>(length));
        for (int sample = 0; sample < length; ++sample)
            result[static_cast<std::size_t>(sample)]
                = loop->delay[static_cast<std::size_t>(
                    AcustraEngine::maximumDelaySamples - sample - 1)];
        return result;
    }

    // A cosine: like a plucked line it turns at the bridge sample, where
    // the release keeps the period's start. A sine sits on a slope there and
    // is started at its own turning point, a quarter period on (1.9.45).
    static std::vector<float> filter(int length, int harmonic, double pole,
                                    double reference, bool matched)
    {
        return filterLine(length, [=] (int sample) {
            return static_cast<float>(std::cos(6.2831853071795864769
                * harmonic * sample / length));
        }, pole, reference, matched);
    }

    struct Contact
    {
        float fundamental;
        float speakingLength;
        float lossDesignFrequency;
        float inharmonicity;
        std::array<float, 8> intrinsicLoss;
        float attackPitchCents;
        double slipPole;
        double referencePole;
    };

    static Contact inspect(const AcustraEngine& engine, int string)
    {
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        return { engine.loopFundamental(voice), voice.speakingLengthMetres,
                 voice.dispersionDesignFrequency, voice.dispersionDesignInharmonicity,
                 { voice.bendingLossGain, voice.bendingLossA1, voice.bendingLossA2,
                   voice.constantLossGain, voice.constantLossA1, voice.constantLossA2,
                   voice.constantLossN1, voice.constantLossN2 },
                 voice.attackPitchCents,
                 voice.releaseSlipPole, voice.releaseReferencePole };
    }
};
}

namespace
{
constexpr double pi = 3.14159265358979323846;
using Access = acustra::AcustraEngineTestAccess;
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

double onePassMagnitude(double pole, double radians)
{
    return std::abs((1.0 - pole)
        / (1.0 - pole * std::polar(1.0, -radians)));
}

double poleAtCorner(double radians)
{
    double lower = 0.0, upper = 1.0;
    for (int iteration = 0; iteration < 80; ++iteration)
    {
        const double middle = 0.5 * (lower + upper);
        if (onePassMagnitude(middle, radians) > std::sqrt(0.5))
            lower = middle;
        else
            upper = middle;
    }
    return 0.5 * (lower + upper);
}

std::complex<double> coefficient(const std::vector<float>& signal, int harmonic)
{
    std::complex<double> result {};
    for (std::size_t sample = 0; sample < signal.size(); ++sample)
        result += static_cast<double>(signal[sample]) * std::polar(1.0,
            -2.0 * pi * harmonic * static_cast<double>(sample) / signal.size());
    return result * (2.0 / signal.size());
}

void testHalfPowerAndZeroPhase()
{
    double worstMatched = 0.0, worstLegacy = 0.0, worstPhase = 0.0;
    int cases = 0;
    for (const int length : { 64, 257, 2048 })
        for (const int harmonic : { 1, 3, length / 8, length / 4, length / 2 - 1 })
        {
            const double radians = 2.0 * pi * harmonic / length;
            const double pole = poleAtCorner(radians);
            const auto matched = Access::filter(length, harmonic, pole, 0.0, true);
            const auto legacy = Access::filter(length, harmonic, pole, 0.0, false);
            const auto transfer = coefficient(matched, harmonic);
            worstMatched = std::max(worstMatched,
                std::abs(std::abs(transfer) - std::sqrt(0.5)));
            worstLegacy = std::max(worstLegacy,
                std::abs(std::abs(coefficient(legacy, harmonic)) - std::sqrt(0.5)));
            // The original cosine has DFT coefficient 1. A zero-phase pair
            // preserves this orientation, including after bridge-rest repair.
            worstPhase = std::max(worstPhase, std::abs(transfer.imag()));
            expect(matched.front() == 0.0f, "pad release no longer starts at bridge rest");
            ++cases;
        }
    std::cout << "Pad corner cases=" << cases << " matched amplitude error="
              << worstMatched << " legacy=" << worstLegacy
              << " imaginary coefficient=" << worstPhase << '\n';
    expect(worstMatched < 2.0e-6, "two-pass pad did not retain the original half-power corner");
    expect(worstLegacy > 0.02, "corner fixture did not expose the old approximation");
    expect(worstPhase < 2.0e-6, "two-pass pad introduced a phase rotation");
}

void testReferenceInverseBypassAndEdges()
{
    constexpr int length = 257, harmonic = 17;
    const auto bypass = Access::filter(length, harmonic, 0.0, 0.0, true);
    const auto legacyBypass = Access::filter(length, harmonic, 0.0, 0.0, false);
    expect(bypass == legacyBypass, "zero release pole changed the bypass samples");
    double worstInverse = 0.0;
    for (const double pole : { 0.2, 0.7, 0.95, 0.995 })
    {
        const auto restored = Access::filter(length, harmonic, pole, pole, true);
        for (int sample = 0; sample < length; ++sample)
            worstInverse = std::max(worstInverse,
                std::abs(static_cast<double>(restored[static_cast<std::size_t>(sample)])
                    - (bypass[static_cast<std::size_t>(sample)] - bypass.front())));
    }
    expect(worstInverse < 2.0e-5, "matching the reference pole failed to cancel its release");
    for (const double pole : { 1.0e-200, 1.0e-12, 0.99999999 })
    {
        const auto signal = Access::filter(length, harmonic, pole, 0.0, true);
        expect(std::all_of(signal.begin(), signal.end(), [](float sample)
            { return std::isfinite(sample); }), "edge pole produced nonfinite audio");
    }
    std::cout << "Pad reference cancellation max sample error=" << worstInverse << '\n';
}

// The released line meets the loop's empty loss and dispersion sections at
// the bridge sample, so it must start there at rest and level: a plucked
// line, an asymmetric triangle with its corner at the bridge, comes back as
// the cyclic slip of itself started at the rounded corner's turning point.
// To 1.9.44 the bridge sample was brought to rest by a raised cosine instead
// and this checked that cosine's width; the line it left met the sections
// on a slope, a kink in every period (AcustraEngine::applyPadRelease).
void testReleasedLineStartsAtRestAtItsTurningPoint()
{
    double worstError = 0.0;
    int moved = 0, cases = 0;
    for (const int length : { 8, 9, 19, 64, 257, 4096 })
        for (const double apex : { 0.12, 0.3 })
        for (const double corner : { 0.01, 0.1, 0.5, 1.0, 2.0, 3.0 })
        {
            const double originalPole = poleAtCorner(corner);
            double lo = 0.0, hi = 1.0;
            for (int i = 0; i < 80; ++i)
            {
                const double mid = 0.5 * (lo + hi);
                if (onePassMagnitude(mid, corner) > std::sqrt(std::sqrt(0.5)))
                    lo = mid;
                else
                    hi = mid;
            }
            const double pole = 0.5 * (lo + hi);
            const auto triangle = [=] (int sample)
            {
                const double phase = static_cast<double>(sample) / length;
                return static_cast<float>(phase < apex ? phase / apex
                                                       : (1.0 - phase) / (1.0 - apex));
            };
            const auto response = Access::filterLine(length, triangle, originalPole, 0.0, true);
            std::vector<double> slipped(static_cast<std::size_t>(length));
            double mass = 0.0;
            for (int distance = 0; distance < length; ++distance)
                mass += std::pow(pole, distance) + std::pow(pole, length - distance);
            for (int sample = 0; sample < length; ++sample)
                for (int distance = 0; distance < length; ++distance)
                    slipped[static_cast<std::size_t>(sample)]
                        += (std::pow(pole, distance) + std::pow(pole, length - distance)) / mass
                         * triangle((sample + distance) % length);
            const int lowest = static_cast<int>(std::min_element(slipped.begin(), slipped.end())
                                                - slipped.begin());
            for (int sample = 0; sample < length; ++sample)
            {
                const double expected = slipped[static_cast<std::size_t>((sample + lowest) % length)]
                    - slipped[static_cast<std::size_t>(lowest)];
                worstError = std::max(worstError,
                    std::abs(response[static_cast<std::size_t>(sample)] - expected));
            }
            const double ahead = response[1] - response.front();
            const double behind = response.front() - response.back();
            expect(response.front() == 0.0f, "the released line left the bridge sample off rest");
            expect(ahead * behind <= 0.0, "the released line met the bridge sample on a slope");
            moved += lowest != 0 ? 1 : 0;
            ++cases;
        }
    expect(worstError < 2.0e-5, "the released line is not its cyclic slip started at the turning point");
    expect(moved > cases / 3, "no slip moved the turning point off the bridge sample");
    std::cout << "Released line against its cyclic slip, worst sample error=" << worstError
              << " (" << moved << " of " << cases << " started off the bridge sample)\n";
}

auto note(int rate, int string, int fret, acustra::PickingTechnique style, bool matched)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    auto realism = engine->performanceRealism();
    realism.matchedPadRelease = matched;
    engine->setPerformanceRealism(realism);
    engine->prepare(rate, 128);
    engine->setStringPerChannelMode(true);
    acustra::EngineParameters parameters;
    parameters.picking = style;
    parameters.room = 0.0f;
    engine->setParameters(parameters);
    engine->noteOn((string == 4 ? 59 : 64) + fret, 48.0f / 127.0f, string + 1);
    return engine;
}

void testUpperNotesAndPickingIsolation()
{
    int changedPads = 0, cases = 0;
    double largestAttackPitchChange = 0.0;
    for (const int rate : { 44100, 48000, 96000 })
        for (const int string : { 4, 5 })
            for (int fret = 9; fret <= 20; ++fret)
                for (const auto style : { acustra::PickingTechnique::Finger,
                                         acustra::PickingTechnique::Thumb,
                                         acustra::PickingTechnique::Pick })
                {
                    auto baseline = note(rate, string, fret, style, false);
                    auto candidate = note(rate, string, fret, style, true);
                    const auto before = Access::inspect(*baseline, string);
                    const auto after = Access::inspect(*candidate, string);
                    // The new released shape changes slope energy and thus
                    // the existing attack-tension pitch surrogate. Its initial
                    // target delay may legitimately differ. The performed
                    // pitch/length and intrinsic loss design must remain exact.
                    expect(before.fundamental == after.fundamental
                           && before.speakingLength == after.speakingLength
                           && before.lossDesignFrequency == after.lossDesignFrequency
                           && before.inharmonicity == after.inharmonicity
                           && before.intrinsicLoss == after.intrinsicLoss,
                           "corner correction changed performed pitch or intrinsic string loss");
                    expect(std::isfinite(after.attackPitchCents)
                           && after.attackPitchCents >= 0.0f && after.attackPitchCents <= 20.0f,
                           "corner correction exceeded the existing attack-pitch bound");
                    largestAttackPitchChange = std::max(largestAttackPitchChange,
                        std::abs(static_cast<double>(after.attackPitchCents)
                            - before.attackPitchCents));
                    expect(before.slipPole == after.slipPole
                           && before.referencePole == after.referencePole,
                           "corner correction changed the physical release time");
                    std::array<float, 128> aLeft {}, aRight {}, bLeft {}, bRight {};
                    bool changed = false;
                    for (int block = 0; block < 16; ++block)
                    {
                        baseline->process(aLeft.data(), aRight.data(), 128);
                        candidate->process(bLeft.data(), bRight.data(), 128);
                        changed |= aLeft != bLeft || aRight != bRight;
                        for (std::size_t sample = 0; sample < aLeft.size(); ++sample)
                            expect(std::isfinite(bLeft[sample]) && std::isfinite(bRight[sample]),
                                   "upper-note corner correction produced nonfinite output");
                    }
                    if (style == acustra::PickingTechnique::Pick)
                        expect(!changed, "pad corner correction changed the plectrum path");
                    else
                        changedPads += changed ? 1 : 0;
                    ++cases;
                }
    std::cout << "Upper-note cases=" << cases << " changed pad renders=" << changedPads
              << " max attack-pitch change=" << largestAttackPitchChange << " cents\n";
    expect(changedPads == 144, "pad corner correction did not reach every upper-note fixture");
}
}

int main()
{
    testHalfPowerAndZeroPhase();
    testReferenceInverseBypassAndEdges();
    testReleasedLineStartsAtRestAtItsTurningPoint();
    testUpperNotesAndPickingIsolation();
    return failures == 0 ? 0 : 1;
}
