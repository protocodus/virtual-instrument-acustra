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
    // References observed on 1.9.26 with this exact protocol (first on
    // unchanged 8ea51f9; re-observed when 1.9.26 put E3 on the low E in
    // tune, 10 cents up, and the earlier velocity-law and Thumb-release
    // rescalings of the old references fell away with them). A six-decibel
    // energy bracket prevents suppressing a harmonic to hide its beating,
    // while allowing the requested pitch correction to move its comb level.
    // Open G's H8 was a second fixture until 1.9.9, when the owner chose by
    // ear to part the planes by 0.75 cent (normalPlaneLagPeriods) so that
    // partials swell and fade as the recordings' do: it beats there at
    // 0.68 Hz, 8-11 dB across this window, by design. E3's H5 stays: that
    // split beats it at 0.36 Hz, which the window's straight decay absorbs.
    // In tune, its H5 (824 Hz) sits a few cents under a dreadnought body
    // mode that answers 19 dB down and beats it at 2 Hz, up to 1.7 dB
    // across this window (1.9.26, the pair tuned on its loudness centre;
    // 2.6 dB when it was tuned on its energy centre); the per-plane
    // end-delay chorus this guards against beat it well beyond the 2 dB
    // allowed. The Pick's references were re-observed on 1.9.42, which
    // moved its hand from 60 mm to 141 mm from the saddle: the fifth
    // partial's place on the pluck's comb moved with it, 7.5 dB up from
    // 0.000492141 and 0.000494366.
    constexpr double referenceAmplitude[2][3] {
        { 0.00134199, 0.00117303, 0.00108866 },
        { 0.00125877, 0.00113555, 0.00102439 }
    };
    for (const double rate : { 48000.0, 96000.0 })
    for (const auto technique : { acustra::PickingTechnique::Finger,
                                 acustra::PickingTechnique::Pick,
                                 acustra::PickingTechnique::Thumb })
    {
        const int string = 0;
        const int fret = 12;
        const int partial = 5;
        auto engine = std::make_unique<acustra::AcustraEngine>();
        acustra::EngineParameters parameters;
        parameters.picking = technique;
        parameters.room = 0.0f;
        parameters.releaseNoise = 0.0f;
        // Default Detune is zero on both the pre-polish negative control
        // and the current instrument; no wheel or modulation is sent.
        engine->setParameters(parameters);
        // The partial's level and beating depend on where it meets the
        // body; the fretting finger's own offset (frettingPitchCents) is a
        // performance draw, not this fixture.
        auto realism = engine->performanceRealism();
        realism.frettingPitch = false;
        engine->setPerformanceRealism(realism);
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
        if (!(std::isfinite(envelope.modulationDb) && envelope.modulationDb < 2.0))
        {
            std::cerr << "FAIL: upper partial acquired an audible beating envelope\n";
            ++failures;
        }
        const double reference = referenceAmplitude[rate == 48000.0 ? 0 : 1]
                                                   [static_cast<int>(technique)];
        if (!(envelope.meanAmplitude > 0.5 * reference
              && envelope.meanAmplitude < 2.0 * reference))
        {
            std::cerr << "FAIL: upper partial left its original energy bracket\n";
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}
