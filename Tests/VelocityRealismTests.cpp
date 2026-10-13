// Finger uses the actual release slip; Thumb retains its nominal reference
// ratio. Both keep force-sensitive brightness without fitting new parameters.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static void clearRepeatMemory(AcustraEngine& engine)
    {
        // Negative control for the original exact-velocity residual switch:
        // suppressing repeat ownership removes its real force residual.
        engine.voices_[0].lastPluckMidiNote = -1;
    }
    struct Release
    {
        double pole, reference, position, displacement, brightness;
        double absolutePole;
    };
    static Release inspect(AcustraEngine& engine, int string = 0)
    {
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        const auto& loop = voice.loops[0];
        const int length = static_cast<int>(std::round(loop.currentDelay));
        const auto at = [&] (int n)
        { return double(loop.delay[static_cast<std::size_t>(AcustraEngine::maximumDelaySamples - n - 1)]); };
        double mean = 0.0, variance = 0.0, slopes = 0.0;
        for (int n = 0; n < length; ++n) mean += at(n);
        mean /= length;
        for (int n = 0; n < length; ++n)
        {
            variance += (at(n) - mean) * (at(n) - mean);
            const double d = at(n) - at((n + length - 1) % length);
            slopes += d * d;
        }
        // The release step is the static saddle force shed by the hand;
        // recover its held displacement using the triangle's steep flank.
        const double displacement = voice.releaseStepRise
            * voice.releaseShapePosition[0] * length
            / (AcustraEngine::releaseStepShare * std::sqrt(voice.polarisationMix));
        // Independently predict the default-Touch 0.2 mm contact's r/u
        // release from observed held force and string geometry, rather than
        // consulting plectrumSlipPole or the initializer's pole metadata.
        const double speakingLength = voice.speakingLengthMetres;
        const double a = voice.pluckPoint * speakingLength;
        const double waveSpeed = 2.0 * 0.648 * 440.0
            * std::exp2((voice.openMidi - 69.0) / 12.0)
            * voice.bendImpedanceScale;
        const double heldMetres = displacement
            * engine.physicalCalibration_.steelDisplacementScaleMetres;
        const double releaseSpeed = 0.5 * waveSpeed * heldMetres
            * (1.0 / a + 1.0 / (speakingLength - a));
        // Finger's authored effective release edge shrinks with nominal
        // playing force; it stays independent of same-velocity force draws.
        // Use observed MIDI velocity and independently reconstructed held
        // displacement, rather than the initializer's computed slip pole.
        const double radius = 0.2e-3
            * (engine.parameters_.picking == PickingTechnique::Finger
                ? std::pow(double(voice.velocity), 0.233125)
                : 1.0 / double(voice.velocity));
        const double absolutePole = std::exp(-releaseSpeed
            / (radius * engine.sampleRate_));
        return { voice.releaseSlipPole, voice.releaseReferencePole,
                 voice.pluckPoint, displacement,
                 length * length * slopes / std::max(variance, 1.0e-30),
                 absolutePole };
    }
    static void seed(AcustraEngine& engine, std::uint32_t seed)
    { engine.voices_[0].randomState = seed; }
    static void primeRepeatForce(AcustraEngine& engine, float velocity,
                                 std::uint32_t forceSeed)
    {
        // Supply only a preceding command and repeat-force stream. There
        // is no old wave to merge, and the contact-position/angle generator
        // is seeded independently, so the paired strokes differ in force.
        auto& voice = engine.voices_[0];
        voice.lastPluckMidiNote = 40;
        voice.lastPluckMidiChannel = 1;
        voice.lastPluckVelocity = velocity;
        voice.lastPluckSample = 0;
        voice.repeatPluckState = forceSeed;
        engine.sampleClock_ = 1;
        engine.performanceRealism_.coherentHand = false;
    }
    struct ContinuousContact
    {
        double force, touch, corner;
    };
    static ContinuousContact continuousContact(AcustraEngine& engine)
    {
        const auto& voice = engine.voices_[0];
        const auto release = inspect(engine);
        const double point = voice.pluckPoint;
        // Infer static force from the independently stored saddle-force
        // release, then use the actual string geometry. Never read the
        // initializer's velocity or repeat gain to manufacture the answer.
        const double force = release.displacement
            * engine.physicalCalibration_.steelDisplacementScaleMetres
            * voice.tensionNewtons
            / (voice.speakingLengthMetres * point * (1.0 - point));
        return { force, engine.effectiveTouch(voice.velocity),
                 voice.excitationReleaseCoefficient };
    }
};
}

namespace
{
using Access = acustra::AcustraEngineTestAccess;
int failures = 0;
void expect(bool condition, const char* message)
{ if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }

auto fresh(double rate, acustra::PickingTechnique picking)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    engine->prepare(rate, 128);
    engine->setStringPerChannelMode(true);
    acustra::EngineParameters parameters;
    parameters.picking = picking;
    engine->setParameters(parameters);
    return engine;
}

void testContactLawAndForceSensitiveBrightness()
{
    for (const auto technique : { acustra::PickingTechnique::Finger,
                                  acustra::PickingTechnique::Thumb })
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            double smallestReference = 1.0e30, largestReference = 0.0;
            double smallestBrightnessRatio = 1.0e30;
            for (unsigned take = 1; take <= 24; ++take)
            {
                std::array<Access::Release, 2> releases {};
                for (int force = 0; force < 2; ++force)
                {
                    auto engine = fresh(rate, technique);
                    Access::seed(*engine, 0x6d2b79f5u * take);
                    // These independent repeat-stream seeds yield opposite
                    // ends of the bounded one-dB residual. Position, angle,
                    // velocity, Touch and gesture remain identical in each
                    // pair; random-position groups cannot obscure whether
                    // increased held force actually speeds the contact up.
                    Access::primeRepeatForce(*engine, 0.445f,
                        force == 0 ? 42284u : 7943u);
                    engine->noteOn(40, 0.445f, 1);
                    const auto state = Access::inspect(*engine);
                    const bool finger = technique == acustra::PickingTechnique::Finger;
                    if (finger)
                        expect(state.pole > 0.0 && state.reference == 0.0
                                   && std::abs(state.pole - state.absolutePole) < 1.0e-6,
                               "Finger did not apply its physical release slip in full");
                    else
                        expect(state.pole > state.reference && state.reference > 0.0,
                               "soft Thumb did not have a slower release than its reference");
                    // tau*y/[p(1-p)] stays constant at this velocity's
                    // authored Finger radius. Thumb's nominal full-force
                    // reference is independent of its actual force draw.
                    const double observedPole = finger ? state.pole : state.reference;
                    const double nominal = -1.0 / std::log(observedPole)
                        * (finger ? state.displacement
                            / (state.position * (1.0 - state.position)) : 1.0);
                    smallestReference = std::min(smallestReference, nominal);
                    largestReference = std::max(largestReference, nominal);
                    releases[std::size_t(force)] = state;
                }
                const auto& weak = releases[0];
                const auto& strong = releases[1];
                expect(weak.position == strong.position
                           && strong.displacement > 1.20 * weak.displacement,
                       "paired force probe failed to isolate a stronger stroke at one position");
                expect(strong.pole < weak.pole,
                       "firmer same-velocity stroke failed to speed the release contact");
                const double brightnessRatio = strong.brightness / weak.brightness;
                expect(brightnessRatio > 1.01,
                       "firmer same-velocity stroke did not excite a brighter string spectrum");
                smallestBrightnessRatio = std::min(smallestBrightnessRatio, brightnessRatio);
            }
            expect(largestReference / smallestReference < 1.00002,
                   "release time violated its force/geometry invariant");
            std::cout << "Force-sensitive paired release rate=" << rate
                      << " force/geometry spread=" << largestReference / smallestReference
                      << " minimum strong/weak spectral moment=" << smallestBrightnessRatio << '\n';
        }
}

void testFullVelocityFingerStillSlipsAndThumbKeepsItsShape()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int string : { 0, 5 })
    {
        const int note = string == 0 ? 40 : 64;
        auto finger = fresh(rate, acustra::PickingTechnique::Finger);
        finger->noteOn(note, 1.0f, string + 1);
        const auto state = Access::inspect(*finger, string);
        expect(state.pole > 0.0 && state.reference == 0.0
                   && std::abs(state.pole - state.absolutePole) < 1.0e-6,
               "full-velocity Finger cancelled its absolute physical slip");
        auto thumb = fresh(rate, acustra::PickingTechnique::Thumb);
        thumb->noteOn(note, 1.0f, string + 1);
        const auto thumbState = Access::inspect(*thumb, string);
        expect(thumbState.pole == 0.0 && thumbState.reference == 0.0,
               "nominal full-velocity Thumb no longer keeps its approved shape");
    }
    expect(acustra::AcustraEngine::outputLatencySamples() == 7,
           "release correction changed output latency");
}

double renderedLevel(acustra::PickingTechnique technique,
                     acustra::CaptureType capture, int midi, int velocity, double rate)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    acustra::EngineParameters parameters;
    parameters.picking = technique;
    parameters.capture = capture;
    engine->setParameters(parameters);
    engine->prepare(rate, 128);
    engine->setStringPerChannelMode(true);
    const int string = midi < 64 ? 1 : 6;
    engine->noteOn(midi, float(velocity) / 127.0f, string);
    std::array<float, 128> left {}, right {};
    double energy = 0.0;
    int samples = 0;
    const int duration = int(0.3 * rate);
    for (int start = 0; start < duration; start += 128)
    {
        const int count = std::min(128, duration - start);
        engine->process(left.data(), right.data(), count);
        for (int n = 0; n < count; ++n)
        {
            const double l = left[std::size_t(n)], r = right[std::size_t(n)];
            expect(std::isfinite(l) && std::isfinite(r), "velocity probe produced non-finite audio");
            energy += 0.5 * (l * l + r * r);
        }
        samples += count;
    }
    expect(energy > 1.0e-18, "a playable velocity lost its string signal");
    return 10.0 * std::log10(std::max(energy / samples, 1.0e-30));
}

void testPlayableAudibleVelocityRange()
{
    // Emitted audio, not the initializer's formula: softer strokes remain
    // present while their natural contact spectra retain audible dynamics.
    // Both ends of each RMS span are bounded: a fixed-velocity substitute
    // and the old over-wide v² force curve must both fail. Pick's slowest
    // contacts naturally lose more upper-band energy than Finger/Thumb.
    constexpr std::array<int, 5> velocities { 16, 32, 64, 96, 127 };
    constexpr std::array<double, 4> minimumSpan { 14.0, 9.0, 4.0, 1.0 };
    constexpr std::array<double, 4> maximumSpan { 26.0, 18.0, 9.0, 4.0 };
    constexpr std::array<double, 4> pickMaximumSpan { 36.0, 20.0, 9.0, 4.0 };
    int cases = 0;
    for (double rate : { 44100.0, 96000.0 })
        for (auto technique : { acustra::PickingTechnique::Finger,
                                acustra::PickingTechnique::Pick,
                                acustra::PickingTechnique::Thumb })
            for (auto capture : { acustra::CaptureType::StereoMic,
                                  acustra::CaptureType::MonoMic,
                                  acustra::CaptureType::Piezo })
                for (int midi : { 40, 52, 64, 76 })
                {
                    std::array<double, velocities.size()> levels {};
                    for (std::size_t v = 0; v < velocities.size(); ++v)
                        levels[v] = renderedLevel(technique, capture, midi, velocities[v], rate);
                    for (std::size_t v = 1; v < levels.size(); ++v)
                        expect(levels[v] > levels[v - 1] + 1.0,
                               "velocity probes do not rise monotonically in emitted audio");
                    const auto& upper = technique == acustra::PickingTechnique::Pick
                        ? pickMaximumSpan : maximumSpan;
                    for (std::size_t v = 0; v < minimumSpan.size(); ++v)
                    {
                        const double span = levels.back() - levels[v];
                        expect(span >= minimumSpan[v] && span <= upper[v],
                               "emitted velocity contrast left the playable RMS bounds");
                    }
                    ++cases;
                }
    std::cout << "Playable emitted velocity range: " << cases << " paths x "
              << velocities.size() << " probes; bounded soft/middle contrast and monotonicity\n";
}

void testDenseFloatingVelocityResponse()
{
    constexpr int probes = 257;
    double worstForceError = 0.0;
    for (const auto technique : { acustra::PickingTechnique::Finger,
                                  acustra::PickingTechnique::Pick,
                                  acustra::PickingTechnique::Thumb })
    {
        double reference = 0.0, previousForce = 0.0, previousCorner = 0.0;
        for (int index = 0; index < probes; ++index)
        {
            // Finer than MIDI 1.0, passed directly as a float: this would
            // expose either a handful of dynamics or hidden 7-bit rounding.
            const float velocity = 0.01f + 0.99f * static_cast<float>(index) / (probes - 1);
            auto engine = fresh(48000.0, technique);
            Access::seed(*engine, 0x13579bdu);
            engine->noteOn(40, velocity, 1);
            const auto contact = Access::continuousContact(*engine);
            const double normalized = contact.force
                / ((0.92 + 0.08 * contact.touch) * double(velocity));
            if (index == 0)
                reference = normalized;
            const double error = std::abs(normalized / reference - 1.0);
            worstForceError = std::max(worstForceError, error);
            expect(std::isfinite(contact.force) && contact.force > previousForce,
                   "floating velocity acquired a plateau or reversed the prescribed force");
            expect(error < 3.0e-6,
                   "floating velocity departed from the continuous linear force law");
            expect(std::isfinite(contact.corner) && contact.corner > 0.0
                       && contact.corner >= previousCorner - 2.0e-7,
                   "increasing floating velocity reversed soft-contact release speed");
            previousForce = contact.force;
            previousCorner = contact.corner;
        }
    }
    std::cout << "Continuous float velocity: 3 techniques x " << probes
              << " probes, worst force-law error=" << worstForceError << '\n';
}

void testRepeatedContactConvergesAcrossExactVelocity()
{
    struct Probe
    {
        Access::ContinuousContact contact;
        std::array<float, 1024> audio;
    };
    const auto probe = [](acustra::PickingTechnique technique, float velocity, int witness = 0)
    {
        auto engine = fresh(48000.0, technique);
        Access::seed(*engine, 0x13579bdu);
        engine->noteOn(40, 0.5f, 1);
        std::array<float, 512> left {}, right {};
        engine->process(left.data(), right.data(), 512);
        if (witness == 2)
            Access::clearRepeatMemory(*engine);
        engine->noteOn(40, velocity, 1);
        Probe result { Access::continuousContact(*engine), {} };
        engine->process(result.audio.data(), result.audio.data() + 512, 512);
        return result;
    };
    double worstFineForce = 0.0, worstFineAudio = 0.0;
    for (const auto technique : { acustra::PickingTechnique::Finger,
                                  acustra::PickingTechnique::Pick,
                                  acustra::PickingTechnique::Thumb })
    {
        const auto exact = probe(technique, 0.5f);
        // Quantized force, release, energy and coefficient inputs move together
        // at the smallest representable command. Measure that whole-system
        // float resolution directly, independently of the larger test step.
        const auto resolutionUp = probe(technique, std::nextafter(0.5f, 1.0f));
        const auto resolutionDown = probe(technique, std::nextafter(0.5f, 0.0f));
        const auto negative = probe(technique, 0.5f, 2);
        double upEnergy = 0.0, downEnergy = 0.0;
        double negativeEnergy = 0.0, referenceEnergy = 0.0;
        for (std::size_t i = 0; i < exact.audio.size(); ++i)
        {
            const double up = double(resolutionUp.audio[i]) - exact.audio[i];
            const double disabled = double(negative.audio[i]) - exact.audio[i];
            const double down = double(resolutionDown.audio[i]) - exact.audio[i];
            upEnergy += up * up;
            downEnergy += down * down;
            negativeEnergy += disabled * disabled;
            referenceEnergy += double(exact.audio[i]) * exact.audio[i];
        }
        const double resolutionFloor = std::sqrt(std::max(upEnergy, downEnergy)
            / std::max(referenceEnergy, 1.0e-30));
        const double negativeError = std::sqrt(negativeEnergy / std::max(referenceEnergy, 1.0e-30));
        const double precisionFloor = std::max(2.0e-5, resolutionFloor);
        expect(20.0 * resolutionFloor < negativeError,
               "float-input resolution approached a real repeat-memory discontinuity");
        for (const auto* adjacent : { &resolutionUp, &resolutionDown })
        {
            expect(std::abs(adjacent->contact.force / exact.contact.force - 1.0) < 2.0e-6,
                   "one-ULP velocity witness crossed a real repeat-force discontinuity");
            expect(std::abs(adjacent->contact.corner - exact.contact.corner) < 2.0e-7,
                   "one-ULP velocity witness crossed a real release-contact discontinuity");
        }
        std::cout << "Repeat float resolution: technique " << int(technique)
                  << ", reference RMS " << std::sqrt(referenceEnergy / exact.audio.size())
                  << ", one-ULP input RMS up/down " << std::sqrt(upEnergy / exact.audio.size())
                  << "/" << std::sqrt(downEnergy / exact.audio.size())
                  << ", relative floor " << resolutionFloor
                  << ", repeat-memory negative control " << negativeError << '\n';
        for (const float direction : { -1.0f, 1.0f })
        {
            double coarseForce = 0.0, coarseAudio = 0.0, coarseCorner = 0.0;
            for (const int exponent : { 8, 12, 16, 20 })
            {
                const float epsilon = std::ldexp(1.0f, -exponent);
                const auto nearby = probe(technique, 0.5f + direction * epsilon);
                const double forceError = std::abs(nearby.contact.force / exact.contact.force - 1.0);
                const double cornerError = std::abs(nearby.contact.corner - exact.contact.corner);
                double difference = 0.0, energy = 0.0;
                for (std::size_t sample = 0; sample < exact.audio.size(); ++sample)
                {
                    const double delta = double(nearby.audio[sample]) - exact.audio[sample];
                    difference += delta * delta;
                    energy += double(exact.audio[sample]) * exact.audio[sample];
                }
                const double audioError = std::sqrt(difference / std::max(energy, 1.0e-30));
                expect(std::isfinite(forceError) && std::isfinite(cornerError)
                           && std::isfinite(audioError),
                       "nearby floating velocity produced nonfinite contact or audio");
                if (exponent == 8)
                {
                    coarseForce = forceError;
                    coarseCorner = cornerError;
                    coarseAudio = audioError;
                }
                if (exponent == 20)
                {
                    // The interval is 4096 times smaller. Preserve the same
                    // force, contact and 1%-coarse audio convergence guards;
                    // admit only independently measured whole-instrument float-input resolution.
                    expect(forceError < 0.01 * coarseForce + 2.0e-6,
                           "repeat force jumped at the exact preceding velocity");
                    expect(cornerError < 0.01 * coarseCorner + 2.0e-7,
                           "repeat release speed jumped at the exact preceding velocity");
                    expect(audioError < 0.01 * coarseAudio + precisionFloor,
                           "repeat audio jumped at the exact preceding velocity");
                    expect(negativeError > 0.01 * coarseAudio + precisionFloor,
                           "float-resolution allowance concealed the repeat-memory discontinuity");
                    worstFineForce = std::max(worstFineForce, forceError);
                    worstFineAudio = std::max(worstFineAudio, audioError);
                }
            }
        }
    }
    std::cout << "Sub-MIDI repeat continuity: fine force error=" << worstFineForce
              << ", fine audio difference/RMS=" << worstFineAudio << '\n';
}
}

int main()
{
    testContactLawAndForceSensitiveBrightness();
    testFullVelocityFingerStillSlipsAndThumbKeepsItsShape();
    testPlayableAudibleVelocityRange();
    testDenseFloatingVelocityResponse();
    testRepeatedContactConvergesAcrossExactVelocity();
    return failures == 0 ? 0 : 1;
}
