// A shelf specified as loss per second must not acquire a new loss law
// merely because a supported fret/bend passes two octaves above the open
// string. Check the asymptotic retention parameter independently of the
// full frequency-dependent shelf/bridge decay, which this is not a fit for.
#include "DSP/AcustraEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct Retention { double openFrequency, frequency, openMix, appliedMix, tailMix; };
    static bool boundedFitMatchesFreshSolve(double rate, int string, float slide)
    {
        auto engine = std::make_unique<AcustraEngine>();
        engine->prepare(rate, 64);
        engine->setStringPerChannelMode(true);
        auto& voice = engine->voices_[static_cast<std::size_t>(string)];
        voice.played = voice.keyDown = true;
        voice.midiChannel = string + 1;
        voice.midiNote = voice.openMidi + 20;
        voice.fret = 20;
        voice.attackPitchCents = voice.noteDetuneCents = 0.0f;
        engine->setPitchBend(slide, string + 1);
        engine->configureVoice(voice, string, voice.midiNote, true);
        const auto arguments = voice.dispersionDesignArguments;
        const auto cachedDecay = voice.dispersionDecayRatios;
        const auto cachedPole = voice.dispersionPoleRatios;
        // Rebuild the *same complete* phase-design request with no cache.
        // The running actual-pitch shelf is not silently substituted into
        // this bounded-frequency design, nor omitted from its complete key.
        for (auto& solve : engine->dispersionSolves_)
            solve.valid = false;
        const auto fresh = engine->solvedDispersion(arguments);
        const float base = std::clamp((0.035f + 0.42f * engine->parameters_.stringAge
            + 0.018f * static_cast<float>(5 - string))
            * engine->physicalCalibration_.steel.frequencyLossScale, 0.0f, 0.95f);
        const double exponent = engine->tunedMidiFrequency(voice.openMidi) / arguments[1];
        const double expected = std::clamp(-std::expm1(exponent * std::log1p(-base)),
                                          0.0, static_cast<double>(0.95f));
        return cachedDecay == fresh.first && cachedPole == fresh.second
            && std::abs(arguments[6] - expected) < 3.6e-7;
    }
    static Retention state(AcustraEngine& engine, int string, int fret,
                           float slide, float member)
    {
        auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        // A resting open reference has exactly the authored base shelf.
        voice.played = voice.keyDown = voice.mpeMember = true;
        voice.midiChannel = 2;
        voice.attackPitchCents = voice.noteDetuneCents = 0.0f;
        engine.setPitchBend(0.0f, 1);
        engine.setPitchBend(0.0f, 2);
        engine.configureVoice(voice, string, voice.openMidi, false);
        Retention result {};
        result.openFrequency = engine.tunedMidiFrequency(voice.openMidi);
        result.openMix = voice.loops[0].highLossMix;
        engine.setPitchBend(slide, 1);
        engine.setPitchBend(member, 2);
        engine.configureVoice(voice, string, voice.openMidi + fret, false);
        result.frequency = voice.pitchGeometry.value.lossDesignFrequency;
        result.appliedMix = voice.loops[0].highLossMix;
        voice.level = 0.01f;
        engine.captureTail(voice);
        result.tailMix = voice.tailHandIntrinsicHighLoss;
        return result;
    }
};
} // namespace acustra

int main()
{
    using Engine = acustra::AcustraEngine;
    using Access = acustra::AcustraEngineTestAccess;
    int cases = 0, failures = 0, beyondOldUpperPitch = 0, cappedLoss = 0;
    double worstMixError = 0.0, worstNormalRateError = 0.0;
    const auto check = [&](const Access::Retention& r) {
        const double exponent = r.openFrequency / r.frequency;
        const double unboundedMix = -std::expm1(exponent * std::log1p(-r.openMix));
        const double expected = std::clamp(unboundedMix, 0.0, static_cast<double>(0.95f));
        const double error = std::max(std::abs(r.appliedMix - expected),
                                      std::abs(r.tailMix - expected));
        worstMixError = std::max(worstMixError, error);
        beyondOldUpperPitch += exponent < 0.25;
        cappedLoss += unboundedMix > 0.95f;
        // Three float epsilons cover the ratio, powf and 1-retention
        // roundings. This bound is on numerical error, not a sound tolerance.
        if (!std::isfinite(error) || error > 3.6e-7
            || r.appliedMix < 0.0 || r.appliedMix > 0.95f)
        {
            if (failures < 8)
                std::fprintf(stderr, "Retention mismatch fopen=%.9g f=%.9g: %.9g expected %.9g\n",
                    r.openFrequency, r.frequency, r.appliedMix, expected);
            ++failures;
        }
        if (exponent >= 0.125 && exponent <= 2.0 && unboundedMix < 0.95)
        {
            const double referenceRate = -r.openFrequency * std::log1p(-r.openMix);
            const double observedRate = -r.frequency * std::log1p(-r.appliedMix);
            if (referenceRate > 0.0)
                worstNormalRateError = std::max(worstNormalRateError,
                    std::abs(observedRate / referenceRate - 1.0));
        }
        ++cases;
    };
    for (double rate : { 8000.0, 22050.0, 44100.0, 48000.0, 96000.0, 384000.0 })
        for (float master : { -100.0f, 0.0f, 100.0f })
            for (int tuning = 0; tuning < 4; ++tuning)
            {
                auto engine = std::make_unique<Engine>();
                acustra::EngineParameters parameters;
                parameters.tuning = static_cast<acustra::Tuning>(tuning);
                engine->setParameters(parameters);
                engine->setMasterTuneCents(master);
                engine->prepare(rate, 64);
                engine->setLowerZoneMemberCount(15);
                constexpr std::array<std::array<float, 2>, 7> gestures {{
                    {{-96.0f,-96.0f}}, {{-12.0f,0.0f}}, {{0.0f,-12.0f}},
                    {{0.0f,0.0f}}, {{0.0f,12.0f}}, {{12.0f,0.0f}}, {{96.0f,96.0f}}
                }};
                for (int string = 0; string < 6; ++string)
                    for (int fret : { 0, 20 })
                        for (const auto& gesture : gestures)
                            check(Access::state(*engine, string, fret, gesture[0], gesture[1]));
                // Continuous crossing of the former 24-semitone boundary,
                // including hosts whose finite delay band clips the request.
                for (int step = 0; step <= 32; ++step)
                    check(Access::state(*engine, 5, 20, 0.0f, 2.0f + 0.125f * step));
            }
    int freshFits = 0;
    for (double rate : { 8000.0, 22050.0, 48000.0, 96000.0 })
        for (int string : { 0, 5 })
            for (float slide : { 0.0f, 11.9f, 12.0f, 12.1f, 36.0f, 48.0f })
            {
                if (!Access::boundedFitMatchesFreshSolve(rate, string, slide))
                    ++failures;
                ++freshFits;
            }
    std::printf("Bounded dispersion: %d fresh-solve and complete-fit-shelf checks\n", freshFits);
    if (beyondOldUpperPitch == 0 || cappedLoss == 0 || worstNormalRateError > 2e-5)
        ++failures;
    std::printf("Retention: %d cases, %d beyond old upper-pitch cap, %d loss-ceiling cases; max mix error %.9g, normal per-second rate error %.9g, failures %d\n",
                cases, beyondOldUpperPitch, cappedLoss, worstMixError, worstNormalRateError, failures);
    return failures == 0 ? 0 : 1;
}
