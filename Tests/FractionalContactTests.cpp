#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>

namespace acustra
{
struct AcustraEngineTestAccess
{
    using Loop = AcustraEngine::StringLoop;
    static bool geometry()
    {
        auto loop = std::make_unique<Loop>();
        for (float length : { 8.0f, 27.3f, 73.5f, 201.7f, 601.1f, 2400.8f })
            for (float position : { 0.02f, 0.173f, 0.499f, 0.763f, 0.98f })
            {
                loop->currentDelay = length;
                loop->beginGestureContact(position, 0.22f, 288);
                const auto& c = loop->gestureContact;
                const double expected = std::clamp(0.5 * length * position,
                    1.0, 0.5 * length - 1.0);
                if (std::abs(c.firstAge + c.firstFraction - expected) > 1.0e-4
                    || std::abs(c.secondAge + c.secondFraction
                        - (length - expected)) > 1.5e-4)
                    return false;
                double norm = 0.0;
                for (float w : c.spatialWeights) norm += w * w;
                if (std::abs(norm - 2.0) > 5.0e-7) return false;
            }
        return true;
    }

    static std::array<int, 4> cells(const Loop& loop)
    {
        const auto& c = loop.gestureContact;
        const int mask = AcustraEngine::maximumDelaySamples - 1;
        return { (-1-c.firstAge)&mask, (-2-c.firstAge)&mask,
                 (-1-c.secondAge)&mask, (-2-c.secondAge)&mask };
    }

    static bool passive()
    {
        // Four independent incident waves exercise the spatial operator's
        // nullspace as well as its contact axis. Compare actual increments
        // of every outgoing cell, not the interpolated signal alone.
        auto loop = std::make_unique<Loop>();
        for (int rate : { 8000, 44100, 48000, 96000, 192000, 384000 })
            for (float length : { 8.2f, 31.7f, 256.0f, 582.3f })
                for (float position : { 0.02f, 0.1397f, 0.5f, 0.8361f, 0.98f })
                    for (float strength : { 0.001f, 0.10f, 0.22f })
                    {
                        loop->reset();
                        loop->currentDelay = length;
                        loop->beginGestureContact(position, strength,
                            static_cast<int>(0.006 * rate));
                        const auto index = cells(*loop);
                        std::array<double, 4> previousIn {}, previousOut {};
                        for (int sample = 0; sample < 1024; ++sample)
                        {
                            std::array<double, 4> incoming {};
                            for (int j = 0; j < 4; ++j)
                            {
                                const float x = 0.001f * static_cast<float>(
                                    std::sin((0.071 + 0.053*j)*sample + 0.7*j)
                                    + 0.3*std::cos((0.317-0.037*j)*sample));
                                incoming[j] = x;
                                loop->delay[index[j]] = x;
                            }
                            const double oldMemory = loop->gestureContact.memory;
                            loop->applyGestureContact();
                            double energyIn = 0.5 * oldMemory * oldMemory;
                            const double memory = loop->gestureContact.memory;
                            double energyOut = 0.5 * memory * memory;
                            for (int j = 0; j < 4; ++j)
                            {
                                const double output = loop->delay[index[j]];
                                if (sample == 0 && output != incoming[j]) return false;
                                const double vi = incoming[j] - previousIn[j];
                                const double vo = output - previousOut[j];
                                energyIn += vi*vi; energyOut += vo*vo;
                                previousIn[j] = incoming[j]; previousOut[j] = output;
                            }
                            if (sample > 0 && energyOut > energyIn * 1.0001 + 1.0e-18)
                            {
                                std::cerr << "energy " << rate << ' ' << length << ' '
                                          << position << ' ' << strength << ' '
                                          << energyOut/energyIn << '\n';
                                return false;
                            }
                        }
                    }
        return true;
    }

    static std::array<float, 128> reaction(float length, float position)
    {
        auto loop = std::make_unique<Loop>();
        loop->currentDelay = length;
        loop->beginGestureContact(position, 0.2f, 288);
        // Seed at rest, then give all spatial cells the same smooth physical
        // waveform. Observe the entire reaction when a contact crosses a
        // grid boundary: the old rounded operator has a finite jump here.
        loop->applyGestureContact();
        std::array<float, 128> incoming {}, result {};
        const int mask = AcustraEngine::maximumDelaySamples - 1;
        for (int age = 0; age < 128; ++age)
        {
            incoming[age] = static_cast<float>(std::sin(0.137*age)
                                             + 0.3*std::cos(0.43*age));
            loop->delay[(-1-age)&mask] = incoming[age];
        }
        loop->applyGestureContact();
        for (int age = 0; age < 128; ++age)
            result[age] = loop->delay[(-1-age)&mask] - incoming[age];
        return result;
    }

    static bool continuous()
    {
        for (float boundary : { 20.0f, 20.5f, 21.0f })
        {
            const auto lower = reaction(100.0f, (boundary-0.0001f)/50.0f);
            const auto upper = reaction(100.0f, (boundary+0.0001f)/50.0f);
            double change = 0.0, signal = 0.0;
            for (int i=0; i<128; ++i)
            { change += std::pow(upper[i]-lower[i], 2); signal += std::pow(upper[i], 2); }
            if (signal < 1.0e-9 || change > signal * 1.0e-5) return false;
        }
        // The period is also continuous; it must not round before mapping
        // the two arrivals (pitch bends cross these half-cell boundaries).
        const auto a = reaction(100.4999f, 0.417f);
        const auto b = reaction(100.5001f, 0.417f);
        double change = 0.0, signal = 0.0;
        for (int i=0; i<128; ++i)
        { change += std::pow(a[i]-b[i], 2); signal += std::pow(a[i], 2); }
        return signal > 1.0e-9 && change < signal * 1.0e-5;
    }
};
}

int main()
{
    using Access = acustra::AcustraEngineTestAccess;
    const bool geometry = Access::geometry();
    const bool passive = Access::passive();
    const bool continuous = Access::continuous();
    if (!geometry || !passive || !continuous)
    {
        std::cerr << "fractional contact geometry=" << geometry
                  << " passive=" << passive << " continuous=" << continuous << '\n';
        return 1;
    }
    std::cout << "Fractional contact geometry, adjoint passivity and grid continuity passed\n";
}
