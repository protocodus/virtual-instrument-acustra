// A softer thumb is a darker thumb.
//
// The thumb's pad lets go of the string over a time that grows as the stroke
// softens, as the Finger's pad does: its contact grows with force, so its
// release time goes as v^-0.767 (AcustraEngine::initialisePluck). From
// velocity 127 to 16 an A2 lets go over 13.6 to 72 samples, a ratio of 5.3.
// Before 1.9.5 the thumb's edge shrank as it pressed harder, its release
// time went as 1/v^2, and the A2 let go over 928 samples at velocity 16, two
// of its periods: a ratio of 68.
//
// Then on Mahogany Steel Thumb an A2's and an E4's share of their sound
// between 2.5 and 6 kHz falls at every step from velocity 127 to 48, by 10.6
// and 7.1 dB in all. With the old edge the A2's rose 5.0 dB again from
// velocity 80 to 48 and the E4's 2.0 dB from 64 to 48: the slip, shifted so
// the line starts at rest in a loop longer than the line, put back the
// harmonics the slow release had taken.
//
// One note on channel 1, a fresh Performer for each velocity, the preset's
// controls, 48 kHz in 256-sample blocks. The share is the energy through a
// 4th-order 2.5 kHz high-pass and 6 kHz low-pass over the note's first
// 5-155 ms, against the energy of the whole note there.

#include "DSP/AcustraPerformer.h"
#include "DSP/AcustraPresets.h"


#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static double releaseSlipPole(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)].releaseSlipPole;
    }
};
} // namespace acustra

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;

int failures = 0;

void check(bool condition, const char* what)
{
    if (! condition)
    {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++failures;
    }
}

// RBJ biquad, Q 1/sqrt(2), high- or low-pass.
struct Biquad
{
    double b0, b1, b2, a1, a2;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    Biquad(double frequency, bool highPass)
    {
        const double w = 2.0 * 3.14159265358979323846 * frequency / rate;
        const double alpha = std::sin(w) / (2.0 * std::sqrt(0.5));
        const double a0 = 1.0 + alpha;
        const double c = std::cos(w);
        b0 = (highPass ? (1.0 + c) : (1.0 - c)) / 2.0 / a0;
        b1 = (highPass ? -(1.0 + c) : (1.0 - c)) / a0;
        b2 = b0;
        a1 = -2.0 * c / a0;
        a2 = (1.0 - alpha) / a0;
    }

    double process(double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

struct Note
{
    double releaseTau = 0.0;
    double share = 0.0;
};

acustra::EngineParameters thumbPreset()
{
    for (const auto& preset : acustra::factoryPresets)
        if (std::strcmp(preset.slug, "mahogany-steel-thumb") == 0)
            return acustra::engineParameters(preset);
    std::fprintf(stderr, "FAIL Mahogany Steel Thumb is not a factory preset\n");
    std::exit(EXIT_FAILURE);
}

Note play(int midiNote, int velocity)
{
    const auto parameters = thumbPreset();
    auto performer = std::make_unique<acustra::Performer>();
    performer->setParameters(parameters);
    performer->prepare(rate, block);
    performer->setParameters(parameters);

    Note note;
    std::vector<float> left(block), right(block), output;
    const int blocks = static_cast<int>(0.4 * rate / block);
    for (int index = 0; index < blocks; ++index)
    {
        performer->beginBlock(left.data(), right.data(), block);
        if (index == 4)
        {
            const std::uint8_t on[3] { 0x90, static_cast<std::uint8_t>(midiNote),
                                       static_cast<std::uint8_t>(velocity) };
            performer->handleMidi(0, on, 3);
        }
        performer->endBlock();
        // Only the plucked string has a release pole.
        if (index == 4)
            for (int string = 0; string < 6; ++string)
            {
                const double pole = acustra::AcustraEngineTestAccess::releaseSlipPole(
                    performer->engine(), string);
                if (pole > 0.0)
                    note.releaseTau = -1.0 / std::log(pole);
            }
        output.insert(output.end(), left.begin(), left.end());
    }

    float peak = 0.0f;
    for (const float sample : output)
        peak = std::max(peak, std::abs(sample));
    std::size_t onset = 0;
    while (onset < output.size() && std::abs(output[onset]) <= 0.02f * peak)
        ++onset;
    Biquad high1(2500.0, true), high2(2500.0, true), low1(6000.0, false), low2(6000.0, false);
    const std::size_t from = onset + static_cast<std::size_t>(0.005 * rate);
    const std::size_t to = onset + static_cast<std::size_t>(0.155 * rate);
    double band = 0.0, whole = 0.0;
    for (std::size_t sample = onset; sample < to && sample < output.size(); ++sample)
    {
        const double x = output[sample];
        const double y = low2.process(low1.process(high2.process(high1.process(x))));
        if (sample >= from)
        {
            band += y * y;
            whole += x * x;
        }
    }
    note.share = 10.0 * std::log10((band + 1e-30) / (whole + 1e-30));
    return note;
}
} // namespace

int main()
{
    for (const int midiNote : { 45, 64 })
    {
        const Note loud = play(midiNote, 127);
        const Note soft = play(midiNote, 16);
        const double ratio = soft.releaseTau / loud.releaseTau;
        std::printf("MIDI %d lets go over %.1f samples at velocity 127, %.1f at 16: ratio %.1f\n",
                    midiNote, loud.releaseTau, soft.releaseTau, ratio);
        check(loud.releaseTau > 0.0 && ratio > 3.0 && ratio < 8.0,
              "a thumb's release time grows as a pad's does");

        double previous = loud.share;
        std::printf("velocity 127: 2.5-6 kHz share %.1f dB\n", loud.share);
        for (const int velocity : { 112, 96, 80, 64, 48 })
        {
            const double share = play(midiNote, velocity).share;
            std::printf("velocity %3d: 2.5-6 kHz share %.1f dB (%+.1f)\n", velocity, share,
                        share - previous);
            check(share < previous, "a softer thumb is darker");
            previous = share;
        }
    }
    if (failures > 0)
    {
        std::fprintf(stderr, "%d thumb dynamics checks failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("a softer thumb lets go more slowly, as a pad does, and sounds darker");
    return EXIT_SUCCESS;
}
