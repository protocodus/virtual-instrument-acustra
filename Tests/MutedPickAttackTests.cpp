// The palm-muted Pick's attack against released 1.3.3, at engine level: the
// Rack device's saved-patch check (AllPatchRenderTest, "saved palm-muted
// pick"), kept here so an engine change meets it before the Rack pin moves.
// Restoring the wound strings' bending loss in 1.9.59 failed that check with
// every suite in this repository green.
//
// Protocol. One engine at 96 kHz (the patch's 48 kHz at 2x) in 64-frame
// blocks: Dreadnought, Mahogany, Pick, String Age 0.2, Pluck Position 0.16,
// Touch 0.7, Body 0.82, Width 0.62, Release Noise 0.1, Room 0.1, bridge hand
// at 72/127. The first six E2+B2 strikes of the Palm Muted Steel Pick phrase:
// one every 0.2419355 s, held 0.1935484 s, velocities 96 and 80 in turn.
// Stereo power in three 2048-sample Hann windows per strike, 0, 35 and 85 ms
// after it: as long as the Rack check's 1024 samples at 48 kHz, so the bins
// are its own, 11..21 (516 to 984 Hz, mids), 43..85 (2016 to 3984 Hz,
// presence) and 146..155 (6844 to 7266 Hz, the ring).
//
// The 1.3.3 figures below are this file built against the 1.3.3 engine
// (d01b477) and run with --report on 2026-11-16. The tolerances are the Rack
// check's.
#include "DSP/AcustraEngine.h"

#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
using namespace acustra;

constexpr double rate = 96000.0;
constexpr int block = 64, strikes = 6, window = 2048;
constexpr double strikeSeconds = 0.2419355, heldSeconds = 0.1935484;

struct Figures { double mids, presence, ring, decay; };
constexpr Figures released133 { -10.92, -12.95, -18.98, -57.89 };

int onset(int strike) { return static_cast<int>(std::lround(rate * strike * strikeSeconds)); }

Figures measure()
{
    auto engine = std::make_unique<AcustraEngine>();
    EngineParameters parameters;
    parameters.shape = BodyShape::Dreadnought;
    parameters.bodyMaterial = BodyMaterial::Mahogany;
    parameters.picking = PickingTechnique::Pick;
    parameters.stringAge = 0.2f;
    parameters.pluckPosition = 0.16f;
    parameters.touch = 0.7f;
    parameters.bodyAmount = 0.82f;
    parameters.stereoWidth = 0.62f;
    parameters.releaseNoise = 0.1f;
    parameters.room = 0.1f;
    engine->setParameters(parameters);
    engine->prepare(rate, block);
    engine->setParameters(parameters);
    engine->setPalmMutePressure(72.0f / 127.0f);

    const int total = onset(strikes - 1) + static_cast<int>(rate * 0.085) + window;
    std::vector<float> left(static_cast<std::size_t>(total + block));
    std::vector<float> right(left.size());
    for (int done = 0; done < total; done += block)
    {
        for (int strike = 0; strike < strikes; ++strike)
        {
            const int on = onset(strike) - done;
            const int off = static_cast<int>(std::lround(rate * (strike * strikeSeconds + heldSeconds))) - done;
            for (int note : { 40, 47 })
            {
                if (on >= 0 && on < block)
                    engine->noteOn(note, (strike % 2 ? 80 : 96) / 127.0f, 1, on);
                if (off >= 0 && off < block)
                    engine->noteOff(note, 1);
            }
        }
        engine->process(left.data() + done, right.data() + done, block);
    }

    // A one-sided spectrum has twice the power of its positive bins and a Hann
    // window's energy is 3(N - 1)/8 of a rectangular one's.
    const auto bandPower = [&] (int first, int firstBin, int lastBin)
    {
        double sum = 0.0;
        for (int bin = firstBin; bin <= lastBin; ++bin)
        {
            const double step = 2.0 * 3.14159265358979323846 * bin / window;
            std::complex<double> l, r;
            for (int n = 0; n < window; ++n)
            {
                const double hann = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * n / (window - 1));
                const std::complex<double> turn = std::polar(hann, -step * n);
                l += static_cast<double>(left[static_cast<std::size_t>(first + n)]) * turn;
                r += static_cast<double>(right[static_cast<std::size_t>(first + n)]) * turn;
            }
            sum += std::norm(l) + std::norm(r);
        }
        return sum * 16.0 / (3.0 * window * (window - 1.0));
    };
    const auto power = [&] (int first)
    {
        double sum = 0.0;
        for (int n = first; n < first + window; ++n)
            sum += static_cast<double>(left[static_cast<std::size_t>(n)]) * left[static_cast<std::size_t>(n)]
                 + static_cast<double>(right[static_cast<std::size_t>(n)]) * right[static_cast<std::size_t>(n)];
        return sum / window;
    };

    double full = 0.0, mid = 0.0, presence = 0.0, onsetFull = 0.0, onsetRing = 0.0, tailRing = 0.0;
    for (int strike = 0; strike < strikes; ++strike)
        for (double seconds : { 0.0, 0.035, 0.085 })
        {
            const int first = onset(strike) + static_cast<int>(std::lround(rate * seconds));
            const double here = power(first);
            full += here;
            mid += bandPower(first, 11, 21);
            presence += bandPower(first, 43, 85);
            if (seconds == 0.0)
            {
                onsetFull += here;
                onsetRing += bandPower(first, 146, 155);
            }
            else if (seconds == 0.085)
                tailRing += bandPower(first, 146, 155);
        }
    const auto db = [] (double a, double b) { return 10.0 * std::log10(a / b); };
    return { db(mid, full), db(presence, full), db(onsetRing, onsetFull), db(tailRing, onsetRing) };
}
}

int main(int argc, char** argv)
{
    const Figures now = measure();
    std::printf("palm-muted Pick E2+B2: mids %.2f dB, presence %.2f dB, initial 7 kHz %.2f dB, decay %.2f dB\n",
                now.mids, now.presence, now.ring, now.decay);
    if (argc > 1 && std::strcmp(argv[1], "--report") == 0)
        return EXIT_SUCCESS;

    int failures = 0;
    const auto expect = [&] (bool condition, const char* what, double value, double reference)
    {
        if (condition)
            return;
        ++failures;
        std::printf("FAIL: %s is %.2f dB; released 1.3.3 has %.2f dB\n", what, value, reference);
    };
    // The 2 to 4 kHz share is the pick's presence: within 1.25 dB of release.
    expect(std::abs(now.presence - released133.presence) <= 1.25, "presence", now.presence, released133.presence);
    // 4 dB for the later model's mid colour, 2 dB for its initial bite.
    expect(std::abs(now.mids - released133.mids) <= 4.0, "mids", now.mids, released133.mids);
    expect(std::abs(now.ring - released133.ring) <= 2.0, "initial 7 kHz", now.ring, released133.ring);
    // The muted ring has fallen at least 24 dB by 85 ms, and no more than 6 dB
    // past release, where the pitched tail left would be too weak.
    expect(now.decay <= -24.0 && now.decay >= released133.decay - 6.0, "7 kHz decay by 85 ms", now.decay, released133.decay);
    if (failures == 0)
        std::printf("The palm-muted Pick's attack is within released 1.3.3's windows\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
