#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;

struct HarmonicEnvelope
{
    double modulationDb;
    double meanAmplitude;
};

// A short Hann-window sinusoidal observation contains both close partials,
// while rejecting adjacent guitar harmonics. Only the straight exponential
// decay is removed; a beating envelope remains in the logarithmic residual.
// This observes the audible microphone output, independently of loop delays,
// modal predictions, cached coefficients or a selected dominant FFT peak.
HarmonicEnvelope observe(const std::vector<float>& audio, double rate,
                         double frequency)
{
    const int halfWindow = static_cast<int>(std::lround(0.030 * rate));
    std::vector<double> times, levels;
    double amplitudeSum = 0.0;
    for (int observation = 0; observation <= 95; ++observation)
    {
        const double time = 0.30 + 0.01 * observation;
        const int centre = static_cast<int>(std::lround(time * rate));
        std::complex<double> projection {};
        double windowSum = 0.0;
        for (int offset = -halfWindow; offset <= halfWindow; ++offset)
        {
            const double window = 0.5 + 0.5 * std::cos(pi * offset / halfWindow);
            projection += double(audio[static_cast<std::size_t>(centre + offset)])
                * window * std::polar(1.0, -2.0 * pi * frequency * offset / rate);
            windowSum += window;
        }
        const double amplitude = 2.0 * std::abs(projection) / windowSum;
        amplitudeSum += amplitude;
        times.push_back(time);
        levels.push_back(20.0 * std::log10(std::max(amplitude, 1.0e-15)));
    }
    double timeMean = 0.0, levelMean = 0.0;
    for (std::size_t index = 0; index < times.size(); ++index)
    {
        timeMean += times[index];
        levelMean += levels[index];
    }
    timeMean /= times.size();
    levelMean /= times.size();
    double covariance = 0.0, variance = 0.0;
    for (std::size_t index = 0; index < times.size(); ++index)
    {
        covariance += (times[index] - timeMean) * (levels[index] - levelMean);
        variance += (times[index] - timeMean) * (times[index] - timeMean);
    }
    const double slope = covariance / variance;
    for (std::size_t index = 0; index < times.size(); ++index)
        levels[index] -= levelMean + slope * (times[index] - timeMean);
    std::sort(levels.begin(), levels.end());
    return { levels[90] - levels[5], amplitudeSum / times.size() };
}
}

int main()
{
    int failures = 0;
    // Observed on unchanged 8ea51f9 with this exact protocol. A six-decibel
    // energy bracket prevents suppressing a harmonic to hide its beating,
    // while allowing the requested pitch correction to move its comb level.
    constexpr double referenceAmplitude[2][3][2] {
        { { 0.000861821, 0.000185553 }, { 0.000319559, 0.000317302 },
          { 0.000739774, 0.0000667068 } },
        { { 0.000816120, 0.000184121 }, { 0.000323796, 0.000343050 },
          { 0.000718182, 0.0000865664 } }
    };
    for (const double rate : { 48000.0, 96000.0 })
    for (const auto technique : { acustra::PickingTechnique::Finger,
                                 acustra::PickingTechnique::Pick,
                                 acustra::PickingTechnique::Thumb })
    for (const int fixture : { 0, 1 })
    {
        const int string = fixture == 0 ? 0 : 3;
        const int fret = fixture == 0 ? 12 : 0;
        const int partial = fixture == 0 ? 5 : 8;
        auto engine = std::make_unique<acustra::AcustraEngine>();
        acustra::EngineParameters parameters;
        parameters.picking = technique;
        parameters.room = 0.0f;
        parameters.releaseNoise = 0.0f;
        // Default Detune is zero on both the pre-polish negative control
        // and the current instrument; no wheel or modulation is sent.
        engine->setParameters(parameters);
        engine->prepare(rate, 256);
        engine->setStringPerChannelMode(true);
        const int note = engine->openNotes(parameters.tuning)[string] + fret;
        engine->noteOn(note, 0.6f, string + 1);
        std::vector<float> left(static_cast<std::size_t>(2.0 * rate)), right(left.size());
        engine->process(left.data(), right.data(), static_cast<int>(left.size()));
        const double frequency = 440.0 * std::exp2((note - 69.0) / 12.0) * partial;
        const auto envelope = observe(left, rate, frequency);
        std::cout << "rate=" << rate << " technique=" << int(technique)
                  << " note=" << note << " H" << partial
                  << " modulationDb=" << envelope.modulationDb
                  << " meanAmplitude=" << envelope.meanAmplitude << '\n';
        if (!(std::isfinite(envelope.modulationDb) && envelope.modulationDb < 1.0))
        {
            std::cerr << "FAIL: upper partial acquired an audible beating envelope\n";
            ++failures;
        }
        const double reference = referenceAmplitude[rate == 48000.0 ? 0 : 1]
                                                  [static_cast<int>(technique)][fixture];
        if (!(envelope.meanAmplitude > 0.5 * reference
              && envelope.meanAmplitude < 2.0 * reference))
        {
            std::cerr << "FAIL: upper partial left its original energy bracket\n";
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}
