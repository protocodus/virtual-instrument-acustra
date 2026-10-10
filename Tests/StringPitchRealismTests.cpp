#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct State
    {
        double inharmonicity;
        double tension;
        double speakingLength;
        double attackCents;
        double fundamentalDecay;
        double retainedIntrinsicDecay;
    };

    struct Attack
    {
        double length, position, amplitude, slipPole, bendSpeed;
        double referencePole, upperContactFret;
        bool finite;
        std::array<double, 2> waveEnergy;
    };

    struct DelayedAttack
    {
        double position, releaseRise, slipPole, referencePole;
        std::array<float, 2> shapePosition;
        std::array<double, 2> waveEnergy;
        bool fired;
    };

    struct RepeatedReference
    {
        bool audioExact, wavePreserved, clearPreserved;
        std::array<float, 2> before, expected, actual;
    };

    static RepeatedReference repeatedPickReference(double rate, int gesture, bool queued)
    {
        std::array<std::unique_ptr<AcustraEngine>, 2> engines;
        const bool memberMode = gesture >= 2;
        const int string = memberMode ? 1 : 0;
        const int channel = string + 1;
        const int note = (memberMode ? 45 : 40) + 7;
        RepeatedReference result { true, true, true, {}, {}, {} };
        for (auto& e : engines)
        {
            e = std::make_unique<AcustraEngine>();
            e->prepare(rate, 128);
            EngineParameters parameters;
            parameters.picking = PickingTechnique::Pick;
            e->setParameters(parameters);
            e->setStringPerChannelMode(true);
            if (memberMode)
                e->setLowerZoneMemberCount(15);
            e->noteOn(note, 0.4f, channel);
            auto& voice = e->voices_[static_cast<std::size_t>(string)];
            const auto reference = voice.referencePickDelay;
            // A second fresh initialization of the identical attack may
            // reuse the reference solve, but must keep both valid delays.
            const auto saved = voice;
            voice.attackPitchCents = 0.0f;
            e->configureVoice(voice, string, note, true);
            result.clearPreserved &= voice.referencePickDelay == reference;
            voice = saved;
            std::array<float, 128> left {}, right {};
            for (int block = 0; block < 75; ++block)
                e->process(left.data(), right.data(), 128);
            if (queued)
                e->noteOn(note, 0.4f, channel, 1);
            if (gesture == 1)
                e->setPitchBend(12.0f, channel);
            else if (gesture == 2)
                e->setPitchBend(12.0f, 1);
            else if (gesture == 3)
                e->setPitchBend(2.0f, channel);
        }
        auto& expectedEngine = *engines[1];
        auto& expectedVoice = expectedEngine.voices_[static_cast<std::size_t>(string)];
        result.before = engines[0]->voices_[static_cast<std::size_t>(string)].referencePickDelay;
        // Independently configure an empty fresh attack at the current
        // gesture, then copy only its reference delays back onto the saved
        // ringing voice. The comparison never replaces its old waves.
        const auto saved = expectedVoice;
        expectedVoice.attackPitchCents = 0.0f;
        expectedEngine.configureVoice(expectedVoice, string, note, true);
        result.expected = expectedVoice.referencePickDelay;
        expectedVoice = saved;
        expectedVoice.referencePickDelay = result.expected;
        if (!queued)
            for (auto& e : engines)
                e->noteOn(note, 0.4f, channel);
        std::array<std::array<float, 128>, 2> left {}, right {};
        for (int block = 0; block < 24; ++block)
        {
            for (int variant = 0; variant < 2; ++variant)
                engines[static_cast<std::size_t>(variant)]->process(
                    left[static_cast<std::size_t>(variant)].data(),
                    right[static_cast<std::size_t>(variant)].data(), 128);
            result.audioExact &= left[0] == left[1] && right[0] == right[1];
        }
        auto& actualEngine = *engines[0];
        auto& actualVoice = actualEngine.voices_[static_cast<std::size_t>(string)];
        result.actual = actualVoice.referencePickDelay;
        // Force another reference refresh at the settled physical key and
        // check that this design operation leaves the vibrating histories.
        actualEngine.configureVoice(actualVoice, string, note, false);
        const auto oldLoops = actualVoice.loops;
        actualVoice.referencePickConfigurationKey.generation = 0;
        actualEngine.configureVoice(actualVoice, string, note, false, true);
        for (int plane = 0; plane < 2; ++plane)
        {
            const auto& old = oldLoops[static_cast<std::size_t>(plane)];
            const auto& loop = actualVoice.loops[static_cast<std::size_t>(plane)];
            result.wavePreserved &= loop.delay == old.delay && loop.writeIndex == old.writeIndex
                && loop.bendingLossY1 == old.bendingLossY1 && loop.bendingLossY2 == old.bendingLossY2
                && loop.dispersion.x1 == old.dispersion.x1 && loop.dispersion.x2 == old.dispersion.x2
                && loop.dispersion.y1 == old.dispersion.y1 && loop.dispersion.y2 == old.dispersion.y2;
        }
        return result;
    }

    static DelayedAttack controllerChangedBeforeAttack(
        int string, double rate, PickingTechnique technique,
        bool queued, bool memberMode, float slide, float member)
    {
        auto e = std::make_unique<AcustraEngine>();
        e->prepare(rate, 64);
        EngineParameters parameters;
        parameters.picking = technique;
        e->setParameters(parameters);
        e->setStringPerChannelMode(true);
        if (memberMode)
            e->setLowerZoneMemberCount(15);
        const int channel = string + 1;
        auto& voice = e->voices_[static_cast<std::size_t>(string)];
        voice.randomState = 0x9e3779b9u;
        // One sample of scheduling leaves the wheel change and attack
        // inside the same 32-sample control interval. A fresh note must
        // read the wheel at the actual stroke, as an immediate note does.
        if (queued)
            e->noteOn(voice.openMidi + 7, 0.4f, channel, 1);
        e->setPitchBend(slide, memberMode ? 1 : channel);
        if (memberMode)
            e->setPitchBend(member, channel);
        if (!queued)
            e->noteOn(voice.openMidi + 7, 0.4f, channel);
        std::array<float, 2> left {}, right {};
        e->process(left.data(), right.data(), queued ? 2 : 1);
        DelayedAttack result { voice.pluckPoint, voice.releaseStepRise,
                               voice.releaseSlipPole, voice.releaseReferencePole,
                               voice.releaseShapePosition, {},
                               voice.attackFired && voice.pluckDelay == 0 };
        for (int plane = 0; plane < 2; ++plane)
        {
            const auto& loop = voice.loops[static_cast<std::size_t>(plane)];
            const int count = std::clamp(static_cast<int>(std::round(loop.currentDelay)),
                                         8, AcustraEngine::maximumDelaySamples - 3);
            for (int age = 0; age < count; ++age)
            {
                const int index = (loop.writeIndex - 1 - age
                    + AcustraEngine::maximumDelaySamples) % AcustraEngine::maximumDelaySamples;
                const double value = loop.delay[static_cast<std::size_t>(index)];
                result.waveEnergy[static_cast<std::size_t>(plane)] += value * value;
            }
        }
        return result;
    }

    static Attack pluckedString(int string, int fret, double rate,
                               float slide, float member,
                               PickingTechnique technique = PickingTechnique::Pick,
                               int harmonic = 1, bool memberMode = false)
    {
        auto e = std::make_unique<AcustraEngine>();
        e->prepare(rate, 64);
        EngineParameters parameters;
        parameters.picking = technique;
        e->setParameters(parameters);
        e->setStringPerChannelMode(true);
        if (memberMode)
            e->setLowerZoneMemberCount(15);
        const int channel = string + 1;
        e->setPitchBend(slide, memberMode ? 1 : channel);
        if (memberMode)
            e->setPitchBend(member, channel);
        auto& voice = e->voices_[static_cast<std::size_t>(string)];
        voice.randomState = 0x9e3779b9u;
        const int note = voice.openMidi + fret;
        // Ordinary attacks use the public controller-owned string route;
        // the internal call selects an explicit touched node for the one
        // harmonic geometry fixture.
        if (harmonic > 1)
            e->startNote(string, harmonic, note, 0.4f, channel, 0, false);
        else
            e->noteOn(note, 0.4f, channel);
        const int samples = std::clamp(static_cast<int>(std::round(
            voice.loops[0].currentDelay)), 8, AcustraEngine::maximumDelaySamples - 3);
        const double amplitude = voice.releaseStepRise
            * voice.releaseShapePosition[0] * samples
            / (AcustraEngine::releaseStepShare * std::sqrt(voice.polarisationMix));
        Attack result { voice.speakingLengthMetres, voice.pluckPoint, amplitude,
                        voice.releaseSlipPole, voice.bendImpedanceScale,
                        voice.releaseReferencePole, voice.speakingFret,
                        true, {} };
        for (int plane = 0; plane < 2; ++plane)
        {
            const auto& loop = voice.loops[static_cast<std::size_t>(plane)];
            const int count = std::clamp(static_cast<int>(std::round(loop.currentDelay)),
                                         8, AcustraEngine::maximumDelaySamples - 3);
            for (int sample = 0; sample < count; ++sample)
            {
                const double value = loop.delay[static_cast<std::size_t>(
                    AcustraEngine::maximumDelaySamples - sample - 1)];
                result.finite &= std::isfinite(value);
                result.waveEnergy[static_cast<std::size_t>(plane)] += value * value;
            }
        }
        std::array<float, 256> left {}, right {};
        e->process(left.data(), right.data(), static_cast<int>(left.size()));
        for (std::size_t sample = 0; sample < left.size(); ++sample)
            result.finite &= std::isfinite(left[sample]) && std::isfinite(right[sample]);
        return result;
    }

    static State stringState(int string, int fret, double rate,
                             float slide, float member)
    {
        auto e = std::make_unique<AcustraEngine>();
        e->prepare(rate, 64);
        e->setLowerZoneMemberCount(15);
        auto& v = e->voices_[static_cast<std::size_t>(string)];
        v.played = v.keyDown = true;
        v.mpeMember = true;
        v.midiChannel = 2;
        v.midiNote = v.openMidi + fret;
        v.fret = fret;
        v.attackPitchCents = 0.0f;
        e->setPitchBend(slide, 1);
        e->setPitchBend(member, 2);
        e->configureVoice(v, string, v.midiNote, false);
        // Controlled wave slope gives the same geometric extension, without
        // a pluck's random shape or a bridge mode obscuring the comparison.
        v.attackSlopeEnergy = 0.001f;
        e->updateAttackPitch(v, string);
        const double fundamental = 440.0 * std::exp2(
            (v.midiNote + slide + member - 69.0) / 12.0);
        const double omega = 2.0 * std::acos(-1.0) * fundamental / rate;
        const double lossOmega = rate == 48000.0 ? omega
            : 2.0 * std::atan((rate / 48000.0) * std::tan(0.5 * omega));
        const auto& loop = v.loops[0];
        const auto shelfMagnitude = [lossOmega] (double coefficient, double mix)
        {
            const std::complex<double> low = (1.0 - coefficient)
                / (1.0 - coefficient * std::polar(1.0, -lossOmega));
            return std::abs((1.0 - mix) + mix * low);
        };
        const auto z = std::polar(1.0, -omega);
        const double bendingMagnitude = loop.bendingLossGain / std::abs(
            1.0 + static_cast<double>(loop.bendingLossA1) * z
                + static_cast<double>(loop.bendingLossA2) * z * z);
        const double gainPerTrip = loop.loopGain
            * shelfMagnitude(loop.broadLossCoefficient, loop.broadLossMix)
            * shelfMagnitude(loop.lowpassCoefficient, loop.highLossMix)
            * bendingMagnitude;
        const double decay = -3.0 / (fundamental * std::log10(gainPerTrip));
        v.level = 0.001f;
        e->captureTail(v);
        return { v.dispersionDesignInharmonicity, v.tensionNewtons,
                 v.speakingLengthMetres, v.attackPitchCents,
                 decay, v.tailHandIntrinsicT60 };
    }
};
}

namespace
{
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void testSlideAndFretHaveTheSamePhysicalString()
{
    // Jarvelainen & Karjalainen (Acta Acustica 92, 2006): the same string's
    // B grows as 1/L^2. Moving the fret up an octave halves L and quadruples B.
    // A manager pitch wheel is documented as that same slide, so these
    // observables must agree with
    // stopping the string twelve frets further up, at unchanged tension.
    double worstB = 0.0;
    double worstLength = 0.0;
    double worstDecay = 0.0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int string : { 0, 1, 4, 5 })
            for (const int fret : { 0, 5 })
            {
                using Access = acustra::AcustraEngineTestAccess;
                const auto held = Access::stringState(string, fret, rate, 0.0f, 0.0f);
                const auto slid = Access::stringState(string, fret, rate, 12.0f, 0.0f);
                const auto stopped = Access::stringState(string, fret + 12, rate, 0.0f, 0.0f);
                worstB = std::max(worstB,
                    std::abs(slid.inharmonicity / stopped.inharmonicity - 1.0));
                expect(std::abs(slid.inharmonicity / held.inharmonicity - 4.0) < 0.01,
                       "octave slide did not quadruple the string's inharmonicity");
                expect(std::abs(slid.speakingLength / held.speakingLength - 0.5) < 0.002,
                       "octave slide did not halve the physical speaking length");
                expect(slid.tension == held.tension,
                       "a slide changed the string's tension");
                worstLength = std::max(worstLength,
                    std::abs(slid.speakingLength / stopped.speakingLength - 1.0));
                expect(std::abs(slid.attackCents / stopped.attackCents - 1.0) < 0.01,
                       "attack settling retained the pre-slide speaking length");
                // Read the actual normal-loop transfer at the fundamental,
                // so this compares realised round-trip decay as well as the
                // geometry with the independently configured physical fret.
                worstDecay = std::max(worstDecay,
                    std::abs(slid.fundamentalDecay / stopped.fundamentalDecay - 1.0));
                expect(std::abs(slid.retainedIntrinsicDecay
                        / stopped.retainedIntrinsicDecay - 1.0) < 0.002,
                       "retained tail forgot the slide's intrinsic fret decay");
            }
    std::cout << "Slide vs physical fret: B error " << 100.0 * worstB
              << "%, speaking length error " << 100.0 * worstLength << "%\n";
    expect(worstB < 0.002, "slide and fret disagreed on stiff-string dispersion");
    expect(worstLength < 0.002, "slide and fret disagreed on physical speaking length");
    std::cout << "Slide vs physical fret: fundamental decay error "
              << 100.0 * worstDecay << "%\n";
    expect(worstDecay < 0.002, "slide and fret disagreed on fundamental decay");
}

void testBendingRaisesTensionWithoutShorteningTheFret()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (const int string : { 0, 4, 5 })
    {
        const auto held = Access::stringState(string, 5, 48000.0, 0.0f, 0.0f);
        const auto bent = Access::stringState(string, 5, 48000.0, 0.0f, 2.0f);
        expect(bent.tension > held.tension,
               "the member's lateral bend did not raise tension");
        expect(std::abs(bent.speakingLength / held.speakingLength - 1.0) < 0.002,
               "a tension bend moved the physical fret");
        // The same transverse extension makes a smaller relative tension
        // increment on an already bent string (Kirchhoff-Carrier law).
        expect(bent.attackCents < 0.90 * held.attackCents,
               "attack settling ignored the member bend's higher tension");
    }
}

void testSlideAttackMatchesItsPhysicalFret()
{
    using Access = acustra::AcustraEngineTestAccess;
    double worstPosition = 0.0, worstAmplitude = 0.0, worstEnergy = 0.0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int string : { 0, 1, 4, 5 })
            for (const int fret : { 0, 5, 8 })
                for (const auto technique : { acustra::PickingTechnique::Finger,
                                              acustra::PickingTechnique::Pick,
                                              acustra::PickingTechnique::Thumb })
                {
                    const auto slid = Access::pluckedString(string, fret, rate,
                                                           12.0f, 0.0f, technique);
                    const auto stopped = Access::pluckedString(string, fret + 12, rate,
                                                              0.0f, 0.0f, technique);
                    worstPosition = std::max(worstPosition,
                        std::abs(slid.position - stopped.position));
                    worstAmplitude = std::max(worstAmplitude,
                        std::abs(slid.amplitude / stopped.amplitude - 1.0));
                    for (int plane = 0; plane < 2; ++plane)
                        worstEnergy = std::max(worstEnergy, std::abs(
                            slid.waveEnergy[static_cast<std::size_t>(plane)]
                            / stopped.waveEnergy[static_cast<std::size_t>(plane)] - 1.0));
                    expect(slid.finite && stopped.finite, "slide/fret attack was nonfinite");
                }
    std::cout << "Slide attack vs physical fret: position error " << worstPosition
              << ", displacement error " << 100.0 * worstAmplitude
              << "%, wave-energy error " << 100.0 * worstEnergy << "%\n";
    expect(worstPosition < 1.0e-6, "slid attack kept the unbent pluck position");
    expect(worstAmplitude < 2.0e-5, "slid attack kept the unbent release-force geometry");
    expect(worstEnergy < 2.0e-4, "slid attack retained the old contact width or release shape");
}

void testMemberAttackUsesBentWaveSpeedAtFixedLength()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int string : { 1, 4, 5 })
            for (const float interval : { -2.0f, 2.0f, 12.0f })
            {
                const auto held = Access::pluckedString(string, 5, rate,
                    0.0f, 0.0f, acustra::PickingTechnique::Finger, 1, true);
                const auto bent = Access::pluckedString(string, 5, rate,
                    0.0f, interval, acustra::PickingTechnique::Finger, 1, true);
                expect(bent.length == held.length && bent.position == held.position,
                       "member attack moved the stopped length or the picking hand");
                expect(std::abs(bent.amplitude / held.amplitude - 1.0) < 2.0e-5,
                       "member attack changed the nominal displacement map");
                // The edge release lasts r/u. At fixed y and contact
                // position, u follows the bent string's physical wave speed.
                const double heldTau = -1.0 / std::log(held.slipPole);
                const double bentTau = -1.0 / std::log(bent.slipPole);
                expect(std::abs(bentTau / heldTau * bent.bendSpeed - 1.0) < 2.0e-5,
                       "member attack release time ignored the bent wave speed");
                expect(bent.finite, "member attack was nonfinite");
            }
}

void testHarmonicAndExtremeSlideAttackGeometry()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto open = Access::pluckedString(5, 0, rate, 0.0f, 0.0f);
        const auto harmonic = Access::pluckedString(5, 24, rate, 0.0f, 0.0f,
                                                  acustra::PickingTechnique::Pick, 4);
        expect(harmonic.length == open.length && harmonic.position == open.position,
               "harmonic attack used its requested pitch as the speaking length");
        for (const int string : { 0, 5 })
            for (const float slide : { -192.0f, -48.0f, -12.0f, 12.0f, 48.0f, 192.0f })
                for (const auto technique : { acustra::PickingTechnique::Finger,
                                              acustra::PickingTechnique::Pick,
                                              acustra::PickingTechnique::Thumb })
                {
                    const auto attack = Access::pluckedString(string, 0, rate,
                                                             slide, 0.0f, technique);
                    expect(attack.finite && std::isfinite(attack.amplitude),
                           "extreme slide attack was nonfinite");
                    expect(attack.amplitude >= 0.0 && attack.amplitude <= 0.24001,
                           "virtual long string amplified the nominal release displacement");
                    expect(attack.position >= 0.05 && attack.position <= 0.36,
                           "extreme slide attack left the playable contact band");
                }
    }
}

void testScheduledFreshAttackReadsCurrentControllers()
{
    using Access = acustra::AcustraEngineTestAccess;
    double worstPosition = 0.0, worstEnergy = 0.0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const auto technique : { acustra::PickingTechnique::Finger,
                                      acustra::PickingTechnique::Pick,
                                      acustra::PickingTechnique::Thumb })
            // Conventional slide, MPE manager slide, MPE member tension
            // bend and their combination exercise the two distinct routes.
            for (int gesture = 0; gesture < 4; ++gesture)
            {
                const bool memberMode = gesture != 0;
                const int string = memberMode ? 1 : 0;
                const float slide = gesture == 2 ? 0.0f : 12.0f;
                const float member = gesture >= 2 ? 2.0f : 0.0f;
                const auto direct = Access::controllerChangedBeforeAttack(
                    string, rate, technique, false, memberMode, slide, member);
                const auto queued = Access::controllerChangedBeforeAttack(
                    string, rate, technique, true, memberMode, slide, member);
                expect(direct.fired && queued.fired,
                       "scheduled controller-change fixture did not fire its attack");
                worstPosition = std::max(worstPosition,
                    std::abs(queued.position - direct.position));
                expect(queued.shapePosition == direct.shapePosition,
                       "scheduled fresh attack initialized the old controller geometry");
                expect(queued.releaseRise == direct.releaseRise,
                       "scheduled fresh attack initialized the old release-force geometry");
                expect(queued.slipPole == direct.slipPole
                    && queued.referencePole == direct.referencePole,
                       "scheduled fresh attack initialized the old member release speed");
                for (int plane = 0; plane < 2; ++plane)
                    worstEnergy = std::max(worstEnergy, std::abs(
                        queued.waveEnergy[static_cast<std::size_t>(plane)]
                        / direct.waveEnergy[static_cast<std::size_t>(plane)] - 1.0));
            }
    std::cout << "Scheduled controller-change attack: position error " << worstPosition
              << ", wave-energy error " << 100.0 * worstEnergy << "%\n";
    expect(worstPosition == 0.0, "scheduled fresh attack used a stale pluck position");
    expect(worstEnergy < 1.0e-6, "scheduled fresh attack wrote a stale released string shape");
}

void testRepeatedPickRefreshesReferenceWithoutReplacingTheWave()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (int gesture = 0; gesture < 4; ++gesture)
            for (const bool queued : { false, true })
            {
                const auto repeat = Access::repeatedPickReference(rate, gesture, queued);
                expect(repeat.clearPreserved,
                       "fresh cached Pick configuration discarded its valid reference delays");
                expect(repeat.wavePreserved,
                       "Pick reference refresh replaced the retained wave or filter memories");
                expect(repeat.actual == repeat.expected,
                       "repeated Pick retained reference geometry from an earlier controller gesture");
                expect(repeat.audioExact,
                       "repeated Pick differs from its independently configured attack reference");
                if (gesture == 0 || rate == 48000.0)
                    expect(repeat.actual == repeat.before,
                           "unchanged Pick repeat unnecessarily changed its reference geometry");
                else
                    expect(repeat.actual != repeat.before,
                           "changed Pick gesture never refreshed the reference-rate release");
            }
}
}

int main()
{
    testSlideAndFretHaveTheSamePhysicalString();
    testBendingRaisesTensionWithoutShorteningTheFret();
    testSlideAttackMatchesItsPhysicalFret();
    testMemberAttackUsesBentWaveSpeedAtFixedLength();
    testHarmonicAndExtremeSlideAttackGeometry();
    testScheduledFreshAttackReadsCurrentControllers();
    testRepeatedPickRefreshesReferenceWithoutReplacingTheWave();
    if (failures == 0)
        std::cout << "All string pitch realism tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
