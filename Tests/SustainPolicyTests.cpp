#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct State
    {
        float frequency {}, completedScale {};
        std::uint32_t keyScale {}, referenceScale {};
        std::uint64_t generation {};
        std::array<double, 9> design {};
        std::array<float, 2> pole {}, referenceDelay {}, targetDelay {};
        int solveCursor {}, configuredModel {};
    };
    static State state(const AcustraEngine& e, int string)
    {
        const auto& v = e.voices_[static_cast<std::size_t>(string)];
        State out;
        out.frequency = v.pitchGeometry.value.frequency;
        out.completedScale = v.dispersionDesignBroadLossCornerScale;
        out.keyScale = v.configurationKey.broadLossCornerScale;
        out.referenceScale = v.referencePickConfigurationKey.broadLossCornerScale;
        out.generation = e.voiceConfigurationGeneration_;
        out.design = v.dispersionDesignArguments;
        out.referenceDelay = v.referencePickDelay;
        out.solveCursor = e.nextDispersionSolve_;
        out.configuredModel = static_cast<int>(e.configuredBridgeModel_);
        for (int plane = 0; plane < 2; ++plane)
        {
            out.pole[plane] = v.loops[plane].broadLossCoefficient;
            out.targetDelay[plane] = v.loops[plane].targetDelay;
        }
        return out;
    }
    static void fixture(AcustraEngine& e, int string, int note, bool clear = true)
    {
        auto& v = e.voices_[static_cast<std::size_t>(string)];
        v.played = v.keyDown = true;
        v.midiChannel = string + 1;
        v.mpeMember = false;
        v.harmonic = 1;
        v.attackPitchCents = 0;
        e.configureVoice(v, string, note, clear, true);
    }
    static void configure(AcustraEngine& e, int string)
    {
        auto& v = e.voices_[static_cast<std::size_t>(string)];
        e.configureVoice(v, string, v.midiNote, false);
    }
    static bool flipActiveModelKeepingGeneration(AcustraEngine& e, int string, GuitarModel model)
    {
        const auto generation = e.voiceConfigurationGeneration_;
        const auto pitch = e.voices_[static_cast<std::size_t>(string)].pitchGeometry.key;
        e.configuredBridgeModel_ = model;
        configure(e, string);
        return generation == e.voiceConfigurationGeneration_
            && pitch == e.voices_[static_cast<std::size_t>(string)].pitchGeometry.key;
    }
    static void smallSlide(AcustraEngine& e, int string)
    {
        e.pitchBendSemitones_[static_cast<std::size_t>(string)] = .001f;
        configure(e, string);
    }
    static bool pending(const AcustraEngine& e) { return e.bridgeUpdatePending_; }
    static void invalidateConfiguration(AcustraEngine& e)
    {
        for (auto& v : e.voices_)
            v.configurationKey.generation = 0;
    }
    static bool sameProfileState(const AcustraEngine& a, const AcustraEngine& b)
    {
        for (int string = 0; string < 6; ++string)
        {
            const auto x = state(a, string), y = state(b, string);
            if (x.frequency != y.frequency || x.completedScale != y.completedScale
                || x.keyScale != y.keyScale || x.referenceScale != y.referenceScale
                || x.design != y.design || x.pole != y.pole
                || x.targetDelay != y.targetDelay || x.referenceDelay != y.referenceDelay)
                return false;
        }
        return true;
    }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
using Model = acustra::GuitarModel;
using Style = acustra::PickingTechnique;
constexpr double pi = 3.141592653589793238462643383279502884;
int failures = 0, mathCases = 0, flipCases = 0, queueCases = 0;
int styleCases = 0, toleranceCases = 0, parityCases = 0, referenceCases = 0;
double maxCutoffRelativeError = 0, maxReferenceDelayError = 0;

void expect(bool passed, const char* message)
{
    if (!passed) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
std::uint32_t bits(float value)
{
    std::uint32_t out;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}
acustra::EngineParameters parameters(Model model, Style style)
{
    acustra::EngineParameters p;
    p.guitarModel = model;
    p.shape = acustra::BodyShape::Auditorium;
    p.bodyMaterial = acustra::BodyMaterial::Mahogany;
    p.picking = style;
    p.room = 0;
    p.touch = .58f;
    p.pluckPosition = .28f;
    return p;
}
std::unique_ptr<Engine> fresh(double rate, Model model, Style style)
{
    auto e = std::make_unique<Engine>();
    e->setParameters(parameters(model, style));
    e->prepare(rate, 16);
    e->setStringPerChannelMode(true);
    return e;
}
float expectedScale(Model model) { return model == Model::Bellido1978 ? .75f : 1.f; }
void poleOracle(double pole, double frequency, float scale)
{
    // Independent inverse-pole oracle: infer its physical cutoff rather than
    // call a production policy/design helper or import its constant.
    const double recovered = -48000*std::log(pole)/(2*pi);
    const double expected = std::clamp(14.3*scale*frequency, 500., .44*48000);
    const double error = std::abs(recovered/expected-1);
    maxCutoffRelativeError = std::max(maxCutoffRelativeError, error);
    expect(std::isfinite(recovered) && error < 2e-6,
           "stored broad-loss pole disagrees with independent cutoff/clamp oracle");
}
void stateOracle(const Access::State& state, Model model)
{
    const float scale = expectedScale(model);
    expect(state.keyScale == bits(scale), "full configuration key lost exact active profile bits");
    expect(state.completedScale == scale, "completed dispersion metadata has stale active profile");
    for (const float pole : state.pole)
        poleOracle(pole, state.frequency, scale);
    poleOracle(state.design[3], state.design[1], scale);
    expect(state.pole[0] == state.pole[1], "the broad-loss corner differs between vibration planes");
}
void advance(Engine& e, int frames)
{
    for (int frame = 0; frame < frames; ++frame)
    {
        float l = 0, r = 0;
        e.process(&l, &r, 1);
        expect(std::isfinite(l) && std::isfinite(r), "profile transition produced non-finite audio");
    }
}

void mathAndIdleCoverage()
{
    for (const double rate : {8000., 44100., 48000., 96000., 384000.})
    for (const auto model : {Model::Original, Model::Bellido1978})
    for (const auto style : {Style::Finger, Style::Pick, Style::Thumb})
    {
        auto e = fresh(rate, model, style);
        // Prepare configures every unplayed/sympathetic string too.
        for (int string = 0; string < 6; ++string)
            stateOracle(Access::state(*e, string), model);
        for (const int string : {0, 3, 5})
        for (const int fret : {0, 5, 12, 24})
        {
            Access::fixture(*e, string, Engine::openNotes(acustra::Tuning::Standard)[string]+fret);
            stateOracle(Access::state(*e, string), model);
            ++mathCases;
        }
    }
}

void activeOwnershipAndDesignCache()
{
    for (const double rate : {44100., 48000., 96000.})
    for (const auto initial : {Model::Original, Model::Bellido1978})
    {
        const auto other = initial == Model::Original ? Model::Bellido1978 : Model::Original;
        auto e = fresh(rate, initial, Style::Thumb);
        Access::fixture(*e, 5, 69);
        const auto original = Access::state(*e, 5);
        expect(Access::flipActiveModelKeepingGeneration(*e, 5, other),
               "active-model fixture changed pure pitch key or target generation");
        const auto changed = Access::state(*e, 5);
        stateOracle(changed, other);
        expect(changed.pole != original.pole && changed.design[3] != original.design[3],
               "active profile flip did not refresh both runtime and phase-design poles");
        expect(changed.solveCursor != original.solveCursor,
               "new active loss profile reused a solve with different intrinsic arguments");
        expect(Access::flipActiveModelKeepingGeneration(*e, 5, initial),
               "returning profile changed pure pitch key or target generation");
        const auto returned = Access::state(*e, 5);
        stateOracle(returned, initial);
        expect(returned.design == original.design && returned.pole == original.pole,
               "returning active profile did not recover its exact prior intrinsic design");
        expect(returned.solveCursor == changed.solveCursor,
               "returning profile failed to reuse the existing exact nine-argument solve");
        ++flipCases;
    }
    for (const double rate : {44100., 48000., 96000.})
    for (const auto initial : {Model::Original, Model::Bellido1978})
    for (const auto style : {Style::Pick, Style::Thumb})
    {
        const auto active = initial == Model::Original ? Model::Bellido1978 : Model::Original;
        auto e = fresh(rate, initial, style);
        e->noteOn(69, .7f, 6);
        advance(*e, 240);
        e->setParameters(parameters(active, style));
        advance(*e, 240);
        e->setParameters(parameters(initial, style));
        expect(Access::pending(*e) && Access::state(*e, 5).configuredModel == static_cast<int>(active),
               "queued model fixture did not retain a different active bridge");
        for (int string = 0; string < 6; ++string)
        {
            Access::configure(*e, string);
            stateOracle(Access::state(*e, string), active);
        }
        e->noteOn(71, .7f, 6);
        stateOracle(Access::state(*e, 5), active);
        ++queueCases;
    }
}

void styleInvarianceAndRequestTolerances()
{
    for (const double rate : {44100., 48000., 96000.})
    for (const auto model : {Model::Original, Model::Bellido1978})
    {
        auto e = fresh(rate, model, Style::Thumb);
        Access::fixture(*e, 5, 69);
        const auto held = Access::state(*e, 5);
        for (const auto style : {Style::Finger, Style::Pick, Style::Thumb})
        {
            e->setParameters(parameters(model, style));
            Access::configure(*e, 5);
            const auto changed = Access::state(*e, 5);
            expect(changed.keyScale == held.keyScale && changed.completedScale == held.completedScale
                && changed.pole == held.pole && changed.design == held.design
                && changed.targetDelay == held.targetDelay && changed.solveCursor == held.solveCursor,
                   "live technique selection changed intrinsic model profile or phase solve");
            ++styleCases;
        }
        const auto before = Access::state(*e, 5);
        Access::smallSlide(*e, 5);
        const auto shifted = Access::state(*e, 5);
        expect(shifted.frequency != before.frequency && shifted.pole != before.pole,
               "small-gesture fixture did not change inexpensive pitch/loss tuning");
        expect(shifted.design == before.design && shifted.solveCursor == before.solveCursor,
               "sub-threshold gesture invalidated the retained dispersion request tolerance");
        expect(shifted.completedScale == expectedScale(model),
               "small gesture lost completed profile ownership");
        ++toleranceCases;
    }
}

void copyResetAndForcedCacheParity()
{
    for (const double rate : {44100., 48000., 96000.})
    for (const auto model : {Model::Original, Model::Bellido1978})
    for (const auto style : {Style::Finger, Style::Pick, Style::Thumb})
    {
        auto cached = fresh(rate, model, style);
        cached->noteOn(69, .8f, 6);
        advance(*cached, 240);
        auto forced = std::make_unique<Engine>(*cached);
        expect(Access::sameProfileState(*cached, *forced), "engine copy lost loss/profile cache metadata");
        for (int block = 0; block < 96; ++block)
        {
            std::array<float, 16> a {}, b {}, c {}, d {};
            Access::invalidateConfiguration(*forced);
            cached->process(a.data(), b.data(), 16);
            forced->process(c.data(), d.data(), 16);
            expect(Access::sameProfileState(*cached, *forced)
                && std::memcmp(a.data(), c.data(), sizeof(a)) == 0
                && std::memcmp(b.data(), d.data(), sizeof(b)) == 0,
                   "full-cache reuse changed profile state or audio against forced computation");
        }
        cached->reset();
        for (int string = 0; string < 6; ++string)
            stateOracle(Access::state(*cached, string), model);
        ++parityCases;
    }
}

void pickReferenceProfile()
{
    for (const double rate : {44100., 96000., 192000.})
    for (const auto model : {Model::Original, Model::Bellido1978})
    for (const int string : {0, 3, 5})
    {
        const int note = Engine::openNotes(acustra::Tuning::Standard)[string]+12;
        auto native = fresh(rate, model, Style::Pick);
        auto reference = fresh(48000., model, Style::Pick);
        Access::fixture(*native, string, note);
        Access::fixture(*reference, string, note);
        const auto a = Access::state(*native, string), b = Access::state(*reference, string);
        expect(a.referenceScale == bits(expectedScale(model)),
               "non-48k Pick reference key lost the effective model loss profile");
        for (int plane = 0; plane < 2; ++plane)
        {
            const double error = std::abs(a.referenceDelay[plane]-b.targetDelay[plane]);
            maxReferenceDelayError = std::max(maxReferenceDelayError, error);
            expect(a.referenceDelay[plane] > 0 && error < .0011,
                   "non-48k Pick release reference disagrees with its actual 48k intrinsic design");
        }
        ++referenceCases;
    }
}
}

int main()
{
    mathAndIdleCoverage();
    activeOwnershipAndDesignCache();
    styleInvarianceAndRequestTolerances();
    copyResetAndForcedCacheParity();
    pickReferenceProfile();
    std::cout << "Sustain policy: " << mathCases << " physical corner fixtures, " << flipCases
              << " unchanged-generation active flips, " << queueCases << " queued owners, "
              << styleCases << " technique controls, " << toleranceCases << " request tolerances, "
              << parityCases << " copy/reset/audio-cache controls, " << referenceCases
              << " Pick reference fixtures; cutoff relative error=" << maxCutoffRelativeError
              << ", reference delay error=" << maxReferenceDelayError << " samples\n";
    return failures == 0 ? 0 : 1;
}
