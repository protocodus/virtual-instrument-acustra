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
    static double releaseSlipPole(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)].releaseSlipPole;
    }
};
} // namespace acustra

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
    // Open G's H8 was a second fixture until 1.9.9, when the owner chose by
    // ear to part the planes by 0.75 cent (normalPlaneLagPeriods) so that
    // partials swell and fade as the recordings' do: it beats there at
    // 0.68 Hz, 8-11 dB across this window, by design. E3's H5 stays, because
    // that split beats it at 0.36 Hz, which the window's straight decay
    // absorbs, while the per-plane end-delay chorus this guards against
    // beat it audibly.
    constexpr double referenceAmplitude[2][3] {
        { 0.000861821, 0.000319559, 0.000739774 },
        { 0.000816120, 0.000323796, 0.000718182 }
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
        engine->prepare(rate, 256);
        engine->setStringPerChannelMode(true);
        const int note = engine->openNotes(parameters.tuning)[string] + fret;
        engine->noteOn(note, 0.6f, string + 1);
        const double releasePole = acustra::AcustraEngineTestAccess::releaseSlipPole(*engine, string);
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
        // The references were observed under the earlier quadratic velocity
        // law. 1.6.3's linear law gives velocity 0.6 a force of 0.6 instead
        // of 0.36, and its release-edge scaling keeps the contact shape, so
        // every partial scales by that ratio (1.66-1.68x on 1.6.2's output).
        constexpr double linearForceScale = 0.6 / 0.36;
        // Since 1.9.5 the Thumb's release time follows the Finger's
        // v^-0.766875 rather than 1/v^2, so at velocity 0.6 it lets go
        // 0.6^-1.233125 = 1.88 times sooner than when these were observed,
        // and its slip, a one-pole on the periodic line, passes more of an
        // upper partial. Scale the Thumb's reference by that one-pole's gain
        // at the partial under its release time against the old one: 1.62
        // for E3's H5 and 1.73 for G3's retired H8, which grew 1.07 and 1.96-2.20
        // times from 1.9.4.
        double releaseScale = 1.0;
        if (technique == acustra::PickingTechnique::Thumb)
        {
            const double w = 2.0 * pi * frequency / rate;
            const auto gain = [w](double pole) {
                return (1.0 - pole) / std::sqrt(1.0 - 2.0 * pole * std::cos(w) + pole * pole);
            };
            const double oldPole = std::pow(releasePole, std::pow(0.6, 1.233125));
            releaseScale = releasePole > 0.0 ? gain(releasePole) / gain(oldPole) : 0.0;
            std::cout << "  Thumb release scale " << releaseScale << '\n';
        }
        const double reference = linearForceScale * releaseScale
            * referenceAmplitude[rate == 48000.0 ? 0 : 1]
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
