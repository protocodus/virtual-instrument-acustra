// Exact preservation of the current contact constraint when sparse tap events
// replace per-sample scans. This reference retains the direct functional and
// reduction order; it is not a second instrument model or an audio baseline.
#include "DSP/AcustraEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>

namespace acustra {
struct AcustraEngineTestAccess
{
    using Loop = AcustraEngine::StringLoop;
    static void condition(Loop& loop, float position, bool velocity)
    { AcustraEngine::conditionRepluckContact(loop, position, velocity); }
};
}
namespace {
using Access = acustra::AcustraEngineTestAccess;
using Loop = Access::Loop;

void directTapReference(Loop& loop,
                                            float position,
                                            bool stopVelocity) noexcept
{
    if (!(position > 0.0f && position < 1.0f))
        return;
    const float period = loop.currentDelay;
    const int length = std::clamp(static_cast<int>(std::ceil(period)) + 1,
                                  4, static_cast<int>(loop.delay.size()) - 2);
    const auto at = [&] (int age) -> float&
    {
        return loop.delay[static_cast<std::size_t>(
            (static_cast<unsigned>(loop.writeIndex - 1 - age) & (loop.delay.size() - 1)))];
    };
    // The same fractional taps as displacementAt: include their complete
    // read prehistory rather than rounding the period/contact to a grid.
    const float travel = 0.5f * period * position;
    const float reflected = period - travel;
    const int directAge = static_cast<int>(travel);
    const int reflectedAge = static_cast<int>(reflected);
    const double directFraction = travel - directAge;
    const double reflectedFraction = reflected - reflectedAge;
    std::array<std::array<int, 8>, 2> ages {};
    std::array<std::array<double, 8>, 2> weights {};
    ages[0] = { reflectedAge, reflectedAge + 1, directAge, directAge + 1 };
    weights[0] = { 1.0 - reflectedFraction, reflectedFraction,
                   -(1.0 - directFraction), -directFraction };
    // One ideal cyclic advance of the same field, minus its current point
    // observation, is the local velocity functional in wave/sample units.
    for (int tap = 0; tap < 4; ++tap)
    {
        ages[1][static_cast<std::size_t>(tap)]
            = (ages[0][static_cast<std::size_t>(tap)] + length - 1) % length;
        weights[1][static_cast<std::size_t>(tap)]
            = weights[0][static_cast<std::size_t>(tap)];
        ages[1][static_cast<std::size_t>(tap + 4)]
            = ages[0][static_cast<std::size_t>(tap)];
        weights[1][static_cast<std::size_t>(tap + 4)]
            = -weights[0][static_cast<std::size_t>(tap)];
    }
    const auto contactWeight = [&] (int axis, int age)
    {
        double result = 0.0;
        for (int tap = 0; tap < (axis == 0 ? 4 : 8); ++tap)
            if (age == ages[static_cast<std::size_t>(axis)][static_cast<std::size_t>(tap)])
                result += weights[static_cast<std::size_t>(axis)][static_cast<std::size_t>(tap)];
        return result;
    };
    // The static point-force triangle is the ideal string's Green function.
    // Use its discrete counterpart h: cyclic Laplacian(h) = contactWeight,
    // so <Ds,Dh> = displacementAt(p). Removing its slope-orthogonal component
    // makes the retained displacement zero under the hand and gives exactly
    // E_after = E_before - cross^2/norm before float rounding. The wrap-edge
    // difference belongs to this norm too. Integrating the sparse contact
    // weights twice solves h without allocating a delay-sized scratch array.
    // This is a preceding-hold approximation, not a resolved tool trajectory
    // or a full string/body energy ledger. Fractional-period discretisation
    // and copied filter memories remain outside that physical claim. The
    // ordinary intrinsic string loss still damps uncoupled modes; there is
    // no second string branch or artificial post-release tail on a merge.
    const int axes = stopVelocity ? 2 : 1;
    std::array<double, 2> firstSlope {}, cross {}, norm {}, cumulativeWeight {},
                          wave {}, waveSum {};
    for (int axis = 0; axis < axes; ++axis)
        for (int tap = 0; tap < (axis == 0 ? 4 : 8); ++tap)
            firstSlope[static_cast<std::size_t>(axis)]
                += (length - 1 - ages[static_cast<std::size_t>(axis)][static_cast<std::size_t>(tap)])
                    * weights[static_cast<std::size_t>(axis)][static_cast<std::size_t>(tap)] / length;
    double mixedNorm = 0.0;
    double previous = at(length - 1);
    for (int age = 0; age < length; ++age)
    {
        const double current = at(age);
        for (int axis = 0; axis < axes; ++axis)
        {
            const auto a = static_cast<std::size_t>(axis);
            const double slope = firstSlope[a] - cumulativeWeight[a];
            cross[a] += (current - previous) * slope;
            norm[a] += slope * slope;
            wave[a] += slope;
            waveSum[a] += wave[a];
        }
        mixedNorm += (firstSlope[0] - cumulativeWeight[0])
                   * (firstSlope[1] - cumulativeWeight[1]);
        for (int axis = 0; axis < axes; ++axis)
            cumulativeWeight[static_cast<std::size_t>(axis)] += contactWeight(axis, age);
        previous = current;
    }
    if (!(norm[0] > 0.0) || !std::isfinite(cross[0]))
        return;
    std::array<double, 2> removed { cross[0] / norm[0], 0.0 };
    const double determinant = norm[0] * norm[1] - mixedNorm * mixedNorm;
    if (stopVelocity && determinant > 1.0e-12 * norm[0] * norm[1])
    {
        removed[0] = (cross[0] * norm[1] - cross[1] * mixedNorm) / determinant;
        removed[1] = (cross[1] * norm[0] - cross[0] * mixedNorm) / determinant;
    }
    if (removed[0] == 0.0 && removed[1] == 0.0)
        return;
    cumulativeWeight = {};
    wave = {};
    for (int age = 0; age < length; ++age)
    {
        double correction = 0.0;
        for (int axis = 0; axis < axes; ++axis)
        {
            const auto a = static_cast<std::size_t>(axis);
            wave[a] += firstSlope[a] - cumulativeWeight[a];
            correction += removed[a] * (wave[a] - waveSum[a] / length);
            cumulativeWeight[a] += contactWeight(axis, age);
        }
        at(age) = static_cast<float>(at(age) - correction);
    }
    // The observer should retain the incoming wave's velocity trend rather
    // than count the imposed displacement constraint as an impact.
    loop.derivativeCrossesContact = true;
}

std::uint32_t bits(float value)
{
    std::uint32_t result;
    std::memcpy(&result, &value, sizeof result);
    return result;
}

void seed(Loop& loop, std::size_t size, int wave)
{
#if ACUSTRA_EXTENDED_SAMPLE_RATES
    loop.delay.prepareSize(size);
#else
    (void) size;
#endif
    std::uint32_t random = 0xabc37d19u;
    for (std::size_t index = 0; index < size; ++index)
    {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        loop.delay[index] = wave == 0 ? 0.375f : wave == 1
            ? static_cast<float>(static_cast<std::int32_t>(random)) * 0x1p-32f + 0.125f
            : (index % 2 == 0 ? -0.7f : 0.3f);
    }
    // Nonzero memories must survive the contact unchanged.
    loop.bendingLossX1 = 0.23f; loop.bendingLossX2 = -0.17f;
    loop.constantLossY1 = 0.31f; loop.constantLossY2 = -0.19f;
    loop.allpassY1 = 0.27f; loop.allpassY2 = -0.11f;
    loop.gestureContact.memory = 0.2f;
    loop.gestureContact.previousIncoming = {0.1f, -0.4f};
    loop.targetDelay = 73.25f;
    loop.loopGainTransitionSamples = 17;
    loop.intrinsicCoefficientSamples = 23;
}

bool same(const Loop& expected, const Loop& actual)
{
    if (expected.delay.size() != actual.delay.size()) return false;
    for (std::size_t index = 0; index < expected.delay.size(); ++index)
        if (bits(expected.delay[index]) != bits(actual.delay[index])) return false;
    return expected.derivativeCrossesContact == actual.derivativeCrossesContact
        && expected.writeIndex == actual.writeIndex
        && expected.currentDelay == actual.currentDelay
        && actual.targetDelay == 73.25f
        && actual.bendingLossX1 == 0.23f && actual.bendingLossX2 == -0.17f
        && actual.constantLossY1 == 0.31f && actual.constantLossY2 == -0.19f
        && actual.allpassY1 == 0.27f && actual.allpassY2 == -0.11f
        && actual.gestureContact.memory == 0.2f
        && actual.gestureContact.previousIncoming == std::array<float, 2>{0.1f, -0.4f}
        && actual.loopGainTransitionSamples == 17
        && actual.intrinsicCoefficientSamples == 23;
}
}

int main()
{
    auto direct = std::make_unique<Loop>();
    auto sparse = std::make_unique<Loop>();
    std::size_t cases = 0;
#if ACUSTRA_EXTENDED_SAMPLE_RATES
    constexpr std::array<std::size_t, 5> sizes {256u, 1024u, 4096u, 16384u, 32768u};
#else
    constexpr std::array<std::size_t, 1> sizes {8192u};
#endif
    for (const std::size_t size : sizes)
        for (const float period : {4.0f, 4.25f, 7.999f, 31.125f, 87.5f,
                                  256.25f, 1164.713f, static_cast<float>(size) - 2.01f})
        {
            if (period >= size - 2.0f) continue;
            for (const float position : {0.00001f, 0.05f, 0.28317f, 0.5f,
                    0.73129f, 0.99999f, 0.0f, 1.0f,
                    std::numeric_limits<float>::quiet_NaN()})
                for (const int write : {0, 1, static_cast<int>(size / 2), static_cast<int>(size) - 1})
                    for (const int wave : {0, 1, 2})
                        for (const bool velocity : {false, true})
                        {
                            seed(*direct, size, wave);
                            direct->currentDelay = period;
                            direct->writeIndex = write;
                            direct->derivativeCrossesContact = false;
                            *sparse = *direct;
                            for (int repeat = 0; repeat < 2; ++repeat)
                            {
                                directTapReference(*direct, position, velocity);
                                Access::condition(*sparse, position, velocity);
                                if (!same(*direct, *sparse))
                                {
                                    std::cerr << "Sparse contact differs: size=" << size
                                        << " period=" << period << " position=" << position
                                        << " write=" << write << " wave=" << wave
                                        << " velocity=" << velocity << " repeat=" << repeat << '\n';
                                    return 1;
                                }
                                ++cases;
                            }
                        }
        }
    std::cout << "Sparse contact exactly matches direct functional: " << cases
              << " fractional/wrapped/overlapping/repeated cases; all wave words and retained memories match\n";
    return 0;
}
