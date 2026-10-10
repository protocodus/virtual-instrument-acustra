// Every construction a player can choose, played. Shape x Wood x Tuning x
// Capture is 180 settings; each one strums its
// tuning's six open strings at 48 kHz, with the Piezo output requested, and
// must stay finite, under full scale and audible, at a level near the
// default's. Between cells the routes and choices must agree:
//   - the Piezo output is the same instrument whatever Capture selects, bit
//     for bit, and on Capture = Piezo it is Main (as floats);
//   - the mono microphone is one signal on both channels;
//   - requesting the Piezo output leaves Main bit-identical.
// Then a reduced set, in which every value of every control appears, at
// 44.1, 88.2 and 192 kHz.
//
// Before this, the construction's own controls were tested one or two at a
// time from the default (audit F28): Open G and Half-step down were played
// by no test, and no test crossed Tuning and Capture with Shape and Wood.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
using acustra::AcustraEngine;
using acustra::BodyMaterial;
using acustra::BodyShape;
using acustra::CaptureType;
using acustra::EngineParameters;
using acustra::Tuning;

int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (condition)
        return;
    if (++failures <= 40)
        std::cerr << "FAIL: " << message << '\n';
}

constexpr std::array<CaptureType, 3> captures { CaptureType::StereoMic,
    CaptureType::MonoMic, CaptureType::Piezo };

std::string name(const EngineParameters& p)
{
    constexpr const char* shapes[] { "Parlor", "Auditorium", "Dreadnought", "Jumbo" };
    constexpr const char* woods[] { "Spruce", "Mahogany", "Maple" };
    constexpr const char* tunings[] { "Standard", "Drop D", "DADGAD", "Open G",
                                      "Half-step down" };
    const char* capture = p.capture == CaptureType::StereoMic ? "stereo mic"
        : p.capture == CaptureType::MonoMic ? "mono mic" : "piezo";
    return std::string(shapes[static_cast<int>(p.shape)]) + " "
        + woods[static_cast<int>(p.bodyMaterial)] + ", "
        + tunings[static_cast<int>(p.tuning)] + ", " + capture;
}

struct Hash
{
    std::uint64_t value = 1469598103934665603ull;
    void add(float sample)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &sample, sizeof bits);
        for (int byte = 0; byte < 4; ++byte)
        {
            value ^= (bits >> (8 * byte)) & 0xffu;
            value *= 1099511628211ull;
        }
    }
};

// One biquad in direct form I, double precision.
struct Biquad
{
    double b0, b1, b2, a1, a2;
    double x1 { 0.0 }, x2 { 0.0 }, y1 { 0.0 }, y2 { 0.0 };
    double process(double x) noexcept
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// BS.1770's K-weighting at 48 kHz (its own coefficients), for one channel.
struct KWeighting
{
    Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285,
                   -1.69065929318241, 0.73248077421585 };
    Biquad highPass { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };
    double process(double x) noexcept { return highPass.process(shelf.process(x)); }
};

struct Played
{
    std::uint64_t main = 0;
    std::uint64_t piezo = 0;
    double peak = 0.0;
    double piezoPeak = 0.0;
    double rmsDb = -400.0;
    // K-weighted mean power of both channels in dB (48 kHz only).
    double weightedDb = -400.0;
    bool finite = true;
    bool monoIsOneSignal = true;
    bool piezoIsMain = true;
    bool stereoDiffers = false;
};

// The tuning's open strings strummed down at velocity 0.8, 12 ms apart.
Played play(const EngineParameters& parameters, double rate, bool wantPiezo)
{
    constexpr int block = 128;
    auto engine = std::make_unique<AcustraEngine>();
    engine->setParameters(parameters);
    engine->prepare(rate, block);
    const auto open = AcustraEngine::openNotes(parameters.tuning);
    engine->beginStrum();
    for (int string = 0; string < AcustraEngine::stringCount; ++string)
        engine->noteOn(open[static_cast<std::size_t>(string)], 0.8f, 1,
                       static_cast<int>(string * 0.012 * rate), true);

    std::vector<float> left(block), right(block), piezo(block);
    AcustraEngine::OutputBuses buses;
    if (wantPiezo)
        buses.piezo = piezo.data();
    Played result;
    Hash main, line;
    double energy = 0.0;
    double weighted = 0.0;
    KWeighting weightLeft, weightRight;
    const int length = static_cast<int>(0.35 * rate);
    for (int start = 0; start < length; start += block)
    {
        const int count = std::min(block, length - start);
        engine->process(left.data(), right.data(), buses, count);
        for (int index = 0; index < count; ++index)
        {
            const float l = left[static_cast<std::size_t>(index)];
            const float r = right[static_cast<std::size_t>(index)];
            main.add(l);
            main.add(r);
            result.finite = result.finite && std::isfinite(l) && std::isfinite(r);
            result.peak = std::max({ result.peak, double(std::abs(l)), double(std::abs(r)) });
            energy += 0.5 * (double(l) * l + double(r) * r);
            const double kl = weightLeft.process(l);
            const double kr = weightRight.process(r);
            weighted += kl * kl + kr * kr;
            result.monoIsOneSignal = result.monoIsOneSignal && l == r;
            result.stereoDiffers = result.stereoDiffers || l != r;
            if (wantPiezo)
            {
                const float p = piezo[static_cast<std::size_t>(index)];
                line.add(p);
                result.finite = result.finite && std::isfinite(p);
                result.piezoPeak = std::max(result.piezoPeak, double(std::abs(p)));
                result.piezoIsMain = result.piezoIsMain && p == l && p == r;
            }
        }
    }
    result.main = main.value;
    result.piezo = line.value;
    result.rmsDb = 10.0 * std::log10(std::max(energy / length, 1.0e-40));
    result.weightedDb = 10.0 * std::log10(std::max(weighted / length, 1.0e-40));
    return result;
}

// How far a construction's K-weighted level may sit from the default
// construction's on the same tuning and capture: a guard against a
// construction that plays silent, blows up or takes the wrong route, not a
// loudness match. Tools/CalibrateConstructionLoudness.py levels every
// construction within 1 LU on its phrase set in Standard tuning, and
// ConstructionLoudnessTests holds its own phrase within 3 LU. One open strum
// of 0.35 s in five tunings meets the body's resonances note by note: on
// 2026-09-29 it spread from -5.6 dB to +5.5 dB (the Original, Jumbo Spruce,
// Half-step down, whose low E-flat sits on the Jumbo's air mode).
constexpr double levelTolerance = 9.0;

// What every played cell must be on its own; the level is checked when a
// reference is given (48 kHz, where the K-weighting is the standard's).
void checkCell(const EngineParameters& p, const Played& played, double rate,
               double referenceDb)
{
    const std::string label = name(p) + " at " + std::to_string(int(rate)) + " Hz";
    expect(played.finite, label + ": non-finite output");
    expect(played.peak < 1.0 && played.piezoPeak < 1.0,
           label + ": reached full scale (peak " + std::to_string(played.peak)
               + ", Piezo output " + std::to_string(played.piezoPeak) + ")");
    expect(played.rmsDb > -50.0 && played.piezoPeak > 1.0e-3,
           label + ": silent (" + std::to_string(played.rmsDb) + " dBFS)");
    if (std::isfinite(referenceDb))
        expect(std::abs(played.weightedDb - referenceDb) < levelTolerance,
               label + ": " + std::to_string(played.weightedDb - referenceDb)
                   + " dB from the default construction's level");
    if (p.capture == CaptureType::MonoMic)
        expect(played.monoIsOneSignal, label + ": the mono mic differs between channels");
    if (p.capture == CaptureType::StereoMic)
        expect(played.stereoDiffers, label + ": the stereo mic is one signal");
    if (p.capture == CaptureType::Piezo)
        expect(played.piezoIsMain, label + ": the Piezo output is not Main on Capture = Piezo");
}

// The default construction's level on the same capture and tuning.
double referenceLevel(CaptureType capture, Tuning tuning, double rate)
{
    EngineParameters p;
    p.capture = capture;
    p.tuning = tuning;
    return play(p, rate, true).weightedDb;
}

void testEveryConstruction()
{
    constexpr double rate = 48000.0;
    std::array<std::array<double, 3>, 5> reference {};
    for (int tuning = 0; tuning < 5; ++tuning)
        for (std::size_t c = 0; c < captures.size(); ++c)
            reference[std::size_t(tuning)][c]
                = referenceLevel(captures[c], static_cast<Tuning>(tuning), rate);

    double lowest = 1.0e9, highest = -1.0e9, loudestPeak = 0.0;
    int cells = 0;
    for (int shape = 0; shape < 4; ++shape)
    for (int wood = 0; wood < 3; ++wood)
    for (int tuning = 0; tuning < 5; ++tuning)
    {
        EngineParameters p;
        p.shape = static_cast<BodyShape>(shape);
        p.bodyMaterial = static_cast<BodyMaterial>(wood);
        p.tuning = static_cast<Tuning>(tuning);
        std::array<Played, 3> played;
        for (std::size_t c = 0; c < captures.size(); ++c)
        {
            p.capture = captures[c];
            played[c] = play(p, rate, true);
            const double level = reference[std::size_t(tuning)][c];
            checkCell(p, played[c], rate, level);
            lowest = std::min(lowest, played[c].weightedDb - level);
            highest = std::max(highest, played[c].weightedDb - level);
            loudestPeak = std::max(loudestPeak, played[c].peak);
            ++cells;
        }
        p.capture = CaptureType::StereoMic;
        const std::string label = name(p);
        for (std::size_t c = 1; c < captures.size(); ++c)
            expect(played[c].piezo == played[0].piezo,
                   label + ": the Piezo output depends on what Capture selects");
        // Main without the Piezo output, once per construction.
        if ((shape + wood) % 5 == tuning)
            expect(play(p, rate, false).main == played[0].main,
                   label + ": requesting the Piezo output changed Main");
    }
    std::cout << "Construction matrix: " << cells << " settings at 48 kHz, level "
              << lowest << " to " << highest
              << " dB from the default's on the same capture, loudest peak "
              << loudestPeak << '\n';
}

// Every value of every construction control at least once per rate.
void testOtherRates()
{
    for (const double rate : { 44100.0, 88200.0, 192000.0 })
    {
        for (int index = 0; index < 20; ++index)
        {
            EngineParameters p;
            p.shape = static_cast<BodyShape>(index % 4);
            p.bodyMaterial = static_cast<BodyMaterial>((index / 4 + index) % 3);
            p.tuning = static_cast<Tuning>(index % 5);
            const std::size_t c = static_cast<std::size_t>(index % 3);
            p.capture = captures[c];
            checkCell(p, play(p, rate, true), rate,
                      std::numeric_limits<double>::quiet_NaN());
        }
    }
}
} // namespace

int main()
{
    testEveryConstruction();
    testOtherRates();
    if (failures != 0)
    {
        std::cerr << failures << " Acustra construction matrix check(s) failed\n";
        return 1;
    }
    std::cout << "All Acustra construction matrix tests passed\n";
    return 0;
}
