#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct Contact
    {
        double position, length, tension, displacement, force, waveSpeed;
        double slip, reference;
        float colourCorner, releaseCorner;
    };

    static void seed(AcustraEngine& engine, std::uint32_t seed)
    {
        for (auto& voice : engine.voices_)
            voice.randomState = seed;
    }

    static Contact inspect(const AcustraEngine& engine, int note)
    {
        for (const auto& voice : engine.voices_)
            if (voice.played && voice.midiNote == note)
            {
                // Recover actual held displacement from its independent
                // saddle-force release state, rather than a new gain field.
                const double samples = std::round(voice.loops[0].currentDelay);
                const double displacement = voice.releaseStepRise
                    * voice.releaseShapePosition[0] * samples
                    / (AcustraEngine::releaseStepShare * std::sqrt(voice.polarisationMix));
                const double metres = displacement
                    * engine.physicalCalibration_.steelDisplacementScaleMetres;
                const double a = voice.pluckPoint * voice.speakingLengthMetres;
                const double force = voice.tensionNewtons * metres
                    * (1.0 / a + 1.0 / (voice.speakingLengthMetres - a));
                const double speed = 2.0 * 0.648 * 440.0
                    * std::exp2((voice.openMidi - 69.0) / 12.0)
                    * voice.bendImpedanceScale;
                return { voice.pluckPoint, voice.speakingLengthMetres,
                         voice.tensionNewtons, metres, force, speed,
                         voice.releaseSlipPole, voice.releaseReferencePole,
                         voice.excitationCoefficient, voice.excitationReleaseCoefficient };
            }
        return {};
    }

    static bool sameReleasedString(const AcustraEngine& a, const AcustraEngine& b)
    {
        for (std::size_t string = 0; string < a.voices_.size(); ++string)
            for (std::size_t plane = 0; plane < 2; ++plane)
                if (a.voices_[string].loops[plane].delay
                    != b.voices_[string].loops[plane].delay)
                    return false;
        return true;
    }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
using Picking = acustra::PickingTechnique;
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

auto make(double rate, Picking picking, float handPosition, bool contact = true)
{
    auto engine = std::make_unique<Engine>();
    auto realism = engine->performanceRealism();
    realism.coherentHand = false;
    realism.contactRelease = contact;
    engine->setPerformanceRealism(realism);
    acustra::EngineParameters parameters;
    parameters.picking = picking;
    parameters.pluckPosition = handPosition;
    engine->setParameters(parameters);
    engine->prepare(rate, 127);
    return engine;
}

void testPositionChangesComplianceWithoutChangingHandForce()
{
    double worstSpread = 1.0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (auto picking : { Picking::Finger, Picking::Pick, Picking::Thumb })
            for (float hand : { 0.0f, 0.28f, 1.0f })
                for (int fret : { 0, 12 })
                {
                    double lowest = 1.0e30, highest = 0.0;
                    double lowPoint = 1.0, highPoint = 0.0;
                    for (unsigned take = 1; take <= 12; ++take)
                    {
                        auto engine = make(rate, picking, hand);
                        engine->setStringPerChannelMode(true);
                        Access::seed(*engine, 0x6d2b79f5u * take);
                        engine->noteOn(40 + fret, 0.72f, 1);
                        const auto contact = Access::inspect(*engine, 40 + fret);
                        lowest = std::min(lowest, contact.force);
                        highest = std::max(highest, contact.force);
                        lowPoint = std::min(lowPoint, contact.position);
                        highPoint = std::max(highPoint, contact.position);
                    }
                    expect(lowPoint < highPoint - 0.005,
                           "force fixture did not exercise different physical contact points");
                    expect(lowest > 0.0 && highest / lowest < 1.00001,
                           "contact-point variation silently changed the prescribed hand force");
                    worstSpread = std::max(worstSpread, highest / lowest);
                }
    std::cout << "Point-compliance force spread: " << worstSpread << '\n';
}

void testPreBendUsesSteelTensionForTheSameHandForce()
{
    double worstForceError = 0.0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (auto picking : { Picking::Finger, Picking::Pick, Picking::Thumb })
            for (int note : { 45, 69 })
            {
                std::array<Access::Contact, 4> contacts {};
                constexpr std::array<float, 4> bends { 0.0f, 2.0f, 6.0f, -48.0f };
                for (std::size_t i = 0; i < bends.size(); ++i)
                {
                    auto engine = make(rate, picking, 0.28f);
                    engine->setLowerZoneMemberCount(15);
                    Access::seed(*engine, 0x13579bdu);
                    engine->setPitchBend(bends[i], 2);
                    engine->noteOn(note, 0.72f, 2);
                    contacts[i] = Access::inspect(*engine, note);
                }
                const auto& base = contacts[0];
                expect(base.force > 0.0, "pre-bend fixture did not excite a string");
                for (std::size_t i : { 1u, 2u })
                {
                    const auto& bent = contacts[i];
                    expect(bent.tension > base.tension && bent.length == base.length,
                           "pre-bend fixture did not change tension at fixed speaking length");
                    expect(bent.displacement < base.displacement,
                           "stiffer pre-bent string did not resist the same plucking force");
                    const double error = std::abs(bent.force / base.force - 1.0);
                    expect(error < 2.0e-5,
                           "pre-bend changed actual static hand force instead of compliance");
                    worstForceError = std::max(worstForceError, error);
                }
                expect(contacts[3].displacement <= base.displacement * 1.00001,
                       "virtual downward bend amplified excitation through the tension floor");
            }
    std::cout << "Pre-bend same-force error: " << worstForceError << '\n';
}

void testThumbRoughnessHasAFinitePhysicalRelease()
{
    int affected = 0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int note : { 40, 64 })
            for (float velocity : { 0.25f, 0.5f, 0.75f, 1.0f })
            {
                auto engine = make(rate, Picking::Thumb, 0.28f);
                auto before = make(rate, Picking::Thumb, 0.28f, false);
                engine->noteOn(note, velocity);
                before->noteOn(note, velocity);
                const auto contact = Access::inspect(*engine, note);
                const auto old = Access::inspect(*before, note);
                const double a = contact.position * contact.length;
                const double releaseSpeed = 0.5 * contact.waveSpeed * contact.displacement
                    * (1.0 / a + 1.0 / (contact.length - a));
                const double pole = std::exp(-releaseSpeed / (0.2e-3 * rate));
                // Two smoothing stages have mean time 2q/(1-q). Equate
                // that to the independently predicted contact b/(1-b).
                const double expected = std::min(double(contact.colourCorner),
                    1.0 / (1.0 + 0.5 * pole / (1.0 - pole)));
                expect(std::abs(contact.releaseCorner - expected) < 2.0e-6,
                       "Thumb roughness ignored its independently predicted finite contact time");
                expect(old.releaseCorner == old.colourCorner,
                       "contact ablation failed to restore the previous roughness corner");
                expect(Access::sameReleasedString(*engine, *before)
                           && contact.slip == old.slip && contact.reference == old.reference,
                       "Thumb roughness change altered its approved periodic string release");
                if (contact.releaseCorner < 0.9 * old.releaseCorner)
                    ++affected;
                if (velocity == 1.0f)
                    expect(contact.slip == 0.0 && contact.reference == 0.0,
                           "full-force Thumb lost its selected reference-normalised string shape");
            }
    expect(affected >= 12, "Thumb physical contact had no meaningful release-time effect");
    std::cout << "Thumb contacts with at least 10% slower roughness corner: " << affected << '\n';
}

void testEmittedThumbAttackAndBlockDeterminism()
{
    const auto render = [] (bool contact, int block)
    {
        auto engine = make(48000.0, Picking::Thumb, 0.28f, contact);
        engine->noteOn(40, 0.75f);
        std::vector<float> signal(24000 * 2);
        std::array<float, 127> left {}, right {};
        for (int start = 0; start < 24000; start += block)
        {
            const int count = std::min(block, 24000 - start);
            engine->process(left.data(), right.data(), count);
            for (int n = 0; n < count; ++n)
            {
                signal[2 * (start + n)] = left[n];
                signal[2 * (start + n) + 1] = right[n];
            }
        }
        return signal;
    };
    const auto old = render(false, 127);
    const auto corrected = render(true, 127);
    expect(corrected == render(true, 17), "physical hand changed across process block partitions");
    double energy = 0.0, difference = 0.0, peak = 0.0;
    for (std::size_t i = 0; i < corrected.size(); ++i)
    {
        expect(std::isfinite(corrected[i]), "physical Thumb produced nonfinite audio");
        energy += double(corrected[i]) * corrected[i];
        const double delta = corrected[i] - old[i];
        difference += delta * delta;
        peak = std::max(peak, std::abs(double(corrected[i])));
    }
    expect(energy > 1.0e-7 && difference > 1.0e-8,
           "physical Thumb change was silent or absent in emitted audio");
    expect(peak < 1.0, "physical Thumb exceeded output headroom");
    std::cout << "Thumb output difference/RMS=" << std::sqrt(difference / energy)
              << ", peak=" << peak << '\n';
}
}

int main()
{
    testPositionChangesComplianceWithoutChangingHandForce();
    testPreBendUsesSteelTensionForTheSameHandForce();
    testThumbRoughnessHasAFinitePhysicalRelease();
    testEmittedThumbAttackAndBlockDeterminism();
    return failures == 0 ? 0 : 1;
}
