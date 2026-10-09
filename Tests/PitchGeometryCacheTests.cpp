#include "DSP/AcustraEngine.h"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static void invalidate(AcustraEngine& engine)
    {
        for (auto& voice : engine.voices_)
            voice.pitchGeometry.valid = false;
    }

    static void invalidateConfiguration(AcustraEngine& engine)
    {
        for (auto& voice : engine.voices_)
            voice.configurationKey.generation = 0;
    }

    static std::array<float, 3> memberGeometry(const AcustraEngine& engine)
    {
        for (const auto& voice : engine.voices_)
            if (voice.played && voice.midiChannel == 2)
                return { voice.pitchGeometry.value.frequency,
                         voice.speakingLengthMetres, voice.speakingFret };
        return {};
    }

    // A deliberately changed cached payload proves reuse without adding a
    // counter or a diagnostic branch to production's configuration path.
    static bool hitAndResetContract(AcustraEngine& engine)
    {
        auto& voice = engine.voices_[0];
        engine.configureVoice(voice, 0, voice.midiNote, false);
        voice.pitchGeometry.value.speakingFret = -123.0f;
        engine.configureVoice(voice, 0, voice.midiNote, false);
        if (voice.speakingFret != -123.0f)
            return false;
        voice.pitchGeometry.valid = false;
        engine.configureVoice(voice, 0, voice.midiNote, false);
        if (voice.speakingFret < 0.0f)
            return false;
        voice.contactTravelEnabled = true;
        voice.loops[0].delay[17] = 0.75f;
        voice.pitchGeometry.value.speakingFret = -124.0f;
        engine.configureVoice(voice, 0, voice.midiNote, true);
        if (voice.speakingFret != -124.0f || voice.contactTravelEnabled
            || voice.loops[0].delay[17] != 0.0f)
            return false;
        // A whole voice carries its complete key/value tuple to a saved copy.
        auto saved = std::make_unique<AcustraEngine::Voice>(voice);
        voice.pitchGeometry = {};
        voice = *saved;
        engine.configureVoice(voice, 0, voice.midiNote, false);
        if (voice.speakingFret != -124.0f)
            return false;
        invalidate(engine);
        engine.reset();
        return engine.voices_[0].speakingFret == 0.0f;
    }

    static bool rawInputMisses(AcustraEngine& engine)
    {
        auto& voice = engine.voices_[0];
        voice.played = voice.keyDown = true;
        voice.mpeMember = true;
        voice.midiChannel = 2;
        voice.harmonic = 1;
        int note = voice.openMidi + 5;
        int string = 0;
        const auto configure = [&] { engine.configureVoice(voice, string, note, false); };
        const auto proveMiss = [&] (auto change)
        {
            configure();
            voice.pitchGeometry.value.speakingFret = -125.0f;
            change();
            configure();
            return voice.speakingFret != -125.0f;
        };
        if (!proveMiss([&] { ++note; })
            || !proveMiss([&] { ++voice.openMidi; })
            || !proveMiss([&] { string = 1; })
            || !proveMiss([&] { voice.harmonic = 2; })
            || !proveMiss([&] { voice.harmonic = 1; })
            || !proveMiss([&] { engine.pitchBendSemitones_[0] = 0.25f; })
            // Keep the total performed bend fixed while changing its division
            // into the slide and tension gestures: frequency is insufficient.
            || !proveMiss([&] {
                engine.pitchBendSemitones_[1] = 0.125f;
                engine.pitchBendSemitones_[0] = 0.125f;
            })
            || !proveMiss([&] {
                engine.vibrato_ = 0.5f;
                engine.vibratoOnset_ = 0.75f;
                engine.vibratoPhase_ = 1.0f;
            })
            || !proveMiss([&] { voice.attackPitchCents = 0.1f; })
            || !proveMiss([&] {
                voice.attackPitchCents = std::nextafter(voice.attackPitchCents, 1.0f);
            })
            || !proveMiss([&] { engine.sampleRate_ = 44100.0; }))
            return false;
        // Signed zero is an exact-input distinction, despite equal arithmetic
        // pitch. This detects replacing a bit key with float equality.
        voice.attackPitchCents = 0.0f;
        if (!proveMiss([&] { voice.attackPitchCents = -0.0f; }))
            return false;
        // Sub-float changes to a double rate preserve every float-rate input
        // of this pure geometry. The full coefficient cache is separate.
        configure();
        voice.pitchGeometry.value.speakingFret = -126.0f;
        engine.sampleRate_ = std::nextafter(engine.sampleRate_, 48000.0);
        configure();
        return voice.speakingFret == -126.0f;
    }

    static bool sameState(const AcustraEngine& a, const AcustraEngine& b)
    {
        const auto same = [] (const auto& x, const auto& y)
        {
            return std::memcmp(&x, &y, sizeof(x)) == 0;
        };
        if (a.sampleClock_ != b.sampleClock_ || a.controlCounter_ != b.controlCounter_
            || a.voiceConfigurationGeneration_ != b.voiceConfigurationGeneration_)
            return false;
        for (std::size_t index = 0; index < a.voices_.size(); ++index)
        {
            const auto& x = a.voices_[index];
            const auto& y = b.voices_[index];
            const std::array<float, 9> sx { x.speakingLengthMetres, x.speakingFret,
                x.contactPeriodSamples, x.tensionNewtons, x.releaseDamping,
                x.appliedBendImpedanceScale, x.attackPitchCents,
                x.observedSlopeEnergy, x.appliedBridgeTailStiffness };
            const std::array<float, 9> sy { y.speakingLengthMetres, y.speakingFret,
                y.contactPeriodSamples, y.tensionNewtons, y.releaseDamping,
                y.appliedBendImpedanceScale, y.attackPitchCents,
                y.observedSlopeEnergy, y.appliedBridgeTailStiffness };
            if (!same(sx, sy) || !same(x.referencePickDelay, y.referencePickDelay)
                || !same(x.dispersionDesignFrequency, y.dispersionDesignFrequency)
                || !same(x.dispersionDesignInharmonicity, y.dispersionDesignInharmonicity)
                || !(x.configurationKey == y.configurationKey)
                || !(x.referencePickConfigurationKey == y.referencePickConfigurationKey)
                || x.pluckDelay != y.pluckDelay || x.returnSamples != y.returnSamples
                || x.tailActive != y.tailActive || x.played != y.played
                || x.keyDown != y.keyDown)
                return false;
            for (std::size_t plane = 0; plane < x.loops.size(); ++plane)
            {
                const auto& u = x.loops[plane];
                const auto& v = y.loops[plane];
                if (!same(u.delay, v.delay)
                    || !same(u.intrinsicCoefficientTarget, v.intrinsicCoefficientTarget)
                    || !same(u.intrinsicCoefficientStep, v.intrinsicCoefficientStep)
                    || u.intrinsicCoefficientSamples != v.intrinsicCoefficientSamples
                    || u.loopGainTransitionSamples != v.loopGainTransitionSamples
                    || !same(u.currentDelay, v.currentDelay)
                    || !same(u.targetDelay, v.targetDelay)
                    || !same(u.loopGain, v.loopGain))
                    return false;
            }
        }
        return true;
    }
};
}

// At either finite-waveguide pitch limit, a manager slide can move the
// physical fret without changing the rounded final frequency or tension.
// The full configuration cache must still refresh its length/fret-dependent
// dispersion and loss coefficients. Compare against an observer
// that bypasses only that cache, keeping the geometry cache and all histories.
bool clampedSlideConfigurationContract()
{
    using Engine = acustra::AcustraEngine;
    using Access = acustra::AcustraEngineTestAccess;
    for (const double rate : {44100.0, 48000.0, 96000.0})
    for (const auto model : {acustra::GuitarModel::Original, acustra::GuitarModel::Bellido1978})
    for (const auto picking : {acustra::PickingTechnique::Finger, acustra::PickingTechnique::Pick})
    for (const float memberBend : {-48.0f, 96.0f})
    {
        auto cached = std::make_unique<Engine>();
        acustra::EngineParameters parameters;
        parameters.guitarModel = model;
        parameters.picking = picking;
        parameters.room = 0.0f;
        cached->setParameters(parameters);
        cached->prepare(rate, 32);
        cached->setLowerZoneMemberCount(2);
        cached->noteOn(memberBend < 0.0f ? 40 : 52, 0.8f, 2);
        std::array<float, 32> left {}, right {}, forcedLeft {}, forcedRight {};
        for (int block = 0; block < 150; ++block)
            cached->process(left.data(), right.data(), 32);
        cached->setPitchBend(memberBend, 2);
        for (int block = 0; block < 4; ++block)
            cached->process(left.data(), right.data(), 32);
        const auto before = Access::memberGeometry(*cached);
        auto forced = std::make_unique<Engine>(*cached);
        // The lower-limit case uses the default MPE +/-48 member range and
        // only half a semitone of the manager's ordinary +/-2 range.
        cached->setPitchBend(0.5f, 1);
        forced->setPitchBend(0.5f, 1);
        for (int block = 0; block < 200; ++block)
        {
            Access::invalidateConfiguration(*forced);
            cached->process(left.data(), right.data(), 32);
            forced->process(forcedLeft.data(), forcedRight.data(), 32);
            if (block == 0)
            {
                const auto after = Access::memberGeometry(*cached);
                if (before[0] != after[0] || !(after[1] < before[1])
                    || !(after[2] > before[2]))
                {
                    std::cerr << "FAIL: clamped-slide fixture did not isolate changed geometry\n";
                    return false;
                }
            }
            if (!Access::sameState(*cached, *forced)
                || std::memcmp(left.data(), forcedLeft.data(), sizeof(left)) != 0
                || std::memcmp(right.data(), forcedRight.data(), sizeof(right)) != 0)
            {
                std::cerr << "FAIL: clamped-slide configuration parity at " << rate
                          << " Hz, model " << static_cast<int>(model)
                          << ", picking " << static_cast<int>(picking)
                          << ", member bend " << memberBend << ", block " << block << '\n';
                return false;
            }
        }
    }
    return true;
}

int main()
{
    using Engine = acustra::AcustraEngine;
    using Access = acustra::AcustraEngineTestAccess;
    auto probe = std::make_unique<Engine>();
    probe->prepare(48000.0, 128);
    if (!Access::hitAndResetContract(*probe) || !Access::rawInputMisses(*probe))
    {
        std::cerr << "FAIL: exact pitch-geometry hit/miss/reset/copy contract\n";
        return 1;
    }
    if (!clampedSlideConfigurationContract())
        return 1;
    std::uint64_t frames = 0;
    for (const double rate : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0})
    for (const auto model : {acustra::GuitarModel::Original, acustra::GuitarModel::Bellido1978})
    for (const auto picking : {acustra::PickingTechnique::Finger, acustra::PickingTechnique::Pick})
    {
        auto cached = std::make_unique<Engine>();
        auto forced = std::make_unique<Engine>();
        acustra::EngineParameters parameters;
        parameters.guitarModel = model;
        parameters.picking = picking;
        parameters.room = 0.0f;
        for (auto* engine : {cached.get(), forced.get()})
        {
            engine->setParameters(parameters);
            engine->prepare(rate, 128);
            engine->setStringPerChannelMode(true);
            engine->setLowerZoneMemberCount(6);
        }
        std::array<float, 3> a {}, b {};
        bool sounded = false;
        for (int sample = 0; sample < 8192; ++sample)
        {
            Access::invalidate(*forced);
            for (auto* engine : {cached.get(), forced.get()})
            {
                if (sample == 0) { engine->noteOn(52, 0.8f, 2); engine->noteOn(67, 0.6f, 6); }
                if (sample == 31) engine->setPitchBend(0.3f, 1);
                if (sample == 32) engine->setPitchBend(0.7f, 2);
                if (sample == 97) { engine->setVibrato(0.8f); engine->setMpePressure(0.4f, 2); }
                if (sample == 299) engine->noteOff(52, 2);
                if (sample == 400) engine->noteOn(52, 0.5f, 2, 31);
                if (sample == 431) engine->setPitchBend(-0.4f, 2);
                if (sample == 733) engine->setPalmMutePressure(0.4f);
                if (sample == 911) { engine->setSustainPedal(true); engine->noteOff(67, 6); }
                if (sample == 1003) engine->setSustainPedal(false);
                if (sample == 1201 || sample == 1213)
                {
                    auto changed = parameters;
                    changed.tuning = sample == 1201 ? acustra::Tuning::Dadgad : acustra::Tuning::Standard;
                    changed.stringAge = sample == 1201 ? 0.55f : 0.25f;
                    engine->setParameters(changed);
                }
                if (sample == 1703)
                {
                    auto calibration = acustra::fittedPhysicalCalibration;
                    calibration.steel.stiffnessScale *= 1.01f;
                    engine->setPhysicalCalibration(calibration);
                    engine->noteOn(52, 0.6f, 3);
                }
                if (sample == 2001) engine->allSoundOff();
                if (sample == 2017) { engine->reset(); engine->noteOn(57, 0.9f, 4); }
                if (sample == 3001) { engine->prepare(rate * 1.001, 17); engine->noteOn(59, 0.4f, 4); }
                if (sample == 4101) engine->noteOff(59, 4);
            }
            cached->process(&a[0], &a[1], Engine::OutputBuses { &a[2] }, 1);
            forced->process(&b[0], &b[1], Engine::OutputBuses { &b[2] }, 1);
            if (std::memcmp(a.data(), b.data(), sizeof(a)) != 0
                || !std::isfinite(a[0]) || !std::isfinite(a[1]) || !std::isfinite(a[2])
                || (sample % 97 == 0 && !Access::sameState(*cached, *forced)))
            {
                std::cerr << "FAIL: forced-geometry parity at rate " << rate << ", sample " << sample << '\n';
                return 1;
            }
            sounded |= a[0] != 0.0f || a[1] != 0.0f;
            ++frames;
        }
        if (!sounded || !Access::sameState(*cached, *forced))
        {
            std::cerr << "FAIL: silent or inconsistent geometry-cache scenario\n";
            return 1;
        }
    }
    std::cout << "Pitch geometry: exact raw-key hits/misses, reset/copy, 24 clamped-slide cases and " << frames
              << " forced-uncached audio/state frames passed\n";
}
