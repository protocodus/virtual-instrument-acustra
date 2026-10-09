// The immutable Classic string release direction is authored; body/bridge measurements remain
// intact. Compare native strokes to the same geometry with only that selection
// bypassed, and measure stored fresh energy independently by Parseval's theorem.
#include "DSP/AcustraEngine.h"
#include "DSP/PluckPolarisationData.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace acustra {
struct AcustraEngineTestAccess {
    using Loop = AcustraEngine::StringLoop;
    static float share(const AcustraEngine& e, int string) {
        return e.voices_[static_cast<std::size_t>(string)].polarisationMix;
    }
    static std::uint32_t randomState(const AcustraEngine& e, int string) {
        return e.voices_[static_cast<std::size_t>(string)].randomState;
    }
    static auto loops(const AcustraEngine& e, int string) {
        return e.voices_[static_cast<std::size_t>(string)].loops;
    }
    static void start(AcustraEngine& e, int string, int harmonic, int note,
                      float velocity, bool bypassDirection = false) {
        e.startNote(string, harmonic, note, velocity, string + 1, 1, false);
        auto& voice = e.voices_[static_cast<std::size_t>(string)];
        voice.pluckDelay = 0;
        // Geometry was configured above against the actual measured model.
        // Bypass only its authored direction selection during initialization;
        // initialisePluck does not reconfigure mechanical modes or loop geometry.
        e.initialisePluck(voice, string, voice.velocity, false, !bypassDirection);
        voice.attackFired = true;
        e.bridgeDerivativesCrossRelease_ = true;
        e.configureVoice(voice, string, note, false);
    }
    static void directionBypassedNoteOn(AcustraEngine& e, int note, float velocity, int string) {
        // Exercise public ownership/scheduling, then preserve its geometry and
        // retained-wave path. Bypass only initialisePluck's direction eligibility;
        // configureVoice must continue to see the actual measured bridge model.
        e.noteOn(note, velocity, string + 1, 1);
        auto& voice = e.voices_[static_cast<std::size_t>(string)];
        voice.pluckDelay = 0;
        bool merge = false;
        if (voice.repluckPending) {
            voice.repluckPending = false;
            merge = voice.attackFired && voice.harmonic == 1
                && e.sampleClock_ > voice.lastPluckSample;
            voice.attackPitchCents = 0.0f; voice.attackPitchDecay = 1.0f;
            if (merge) {
                e.repluckOldLoops_ = voice.loops;
                e.retainRepluckArrivals(voice);
            }
            e.configureVoice(voice, string, note, !merge, true);
        }
        e.initialisePluck(voice, string, voice.velocity, merge, false);
        voice.attackFired = true;
        if (merge) e.bridgeDerivativesCrossConfigure_ = true;
        else e.bridgeDerivativesCrossRelease_ = true;
        e.configureVoice(voice, string, note, false);
    }
    static void forgetIncoming(AcustraEngine& e, int string) {
        auto& v = e.voices_[static_cast<std::size_t>(string)];
        v.level = 0.0f; v.contactTravel.active = false;
        v.tailActive = false;
        v.attackFired = false;
    }
    static std::array<Loop, 2> tails(const AcustraEngine& e, int string) {
        const auto& voice = e.voices_[static_cast<std::size_t>(string)];
        return { voice.tailLoop, voice.tailParallelLoop };
    }
    static bool tailActive(const AcustraEngine& e, int string) {
        return e.voices_[static_cast<std::size_t>(string)].tailActive;
    }
    static float contact(const AcustraEngine& e, int string) {
        return e.voices_[static_cast<std::size_t>(string)].pluckPoint;
    }
    static void condition(Loop& loop, float p) {
        AcustraEngine::conditionRepluckContact(loop, p, true);
    }
    static int configured(const AcustraEngine& e) {
        return static_cast<int>(e.configuredBridgeModel_);
    }
    static bool pending(const AcustraEngine& e) { return e.bridgeUpdatePending_; }
};
}
namespace {
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
constexpr double pi = 3.141592653589793238462643383279502884;
int failures = 0;
int freshCases = 0, exactCases = 0, identityCases = 0, repluckCases = 0, refretCases = 0, boundsCases = 0;
double maximumFreshRelativeError = 0.0, maximumShareError = 0.0, maximumRepluckBudgetRatio = 0.0;
constexpr std::array<float, 6> authoredOffsets { 0.0f, 0.0f, 0.0f, .1944f, .30f, .30f };
void expect(bool pass, const char* message) {
    if (!pass) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
std::unique_ptr<Engine> fresh(double rate, acustra::GuitarModel model,
                              acustra::PickingTechnique style,
                              float touch = .58f, acustra::Tuning tuning = acustra::Tuning::Standard) {
    auto e = std::make_unique<Engine>(); acustra::EngineParameters p;
    p.guitarModel = model; p.picking = style; p.touch = touch; p.room = 0.0f; p.tuning = tuning;
    e->setParameters(p); e->prepare(rate, 1); e->setStringPerChannelMode(true);
    return e;
}
int index(const Access::Loop& loop, int age) {
    const int size = static_cast<int>(loop.delay.size());
    return (loop.writeIndex - 1 - age + 2 * size) % size;
}
double freshEnergyByFourier(const std::array<Access::Loop, 2>& loops) {
    // N times the cyclic slope norm = sum_k |DFT(x)_k|^2 * |1-exp(iw_k)|^2.
    // This independent oracle never reads attackSlopeEnergy or the production
    // normalization helper, and includes the closing edge of each stored wave.
    double energy = 0.0;
    for (const auto& loop : loops) {
        const int n = static_cast<int>(std::round(loop.currentDelay));
        for (int k = 1; k < n; ++k) {
            const double omega = 2.0 * pi * k / n;
            const std::complex<double> step { std::cos(omega), std::sin(omega) };
            std::complex<double> phase { 1.0, 0.0 }, coefficient {};
            for (int age = 0; age < n; ++age) {
                coefficient += static_cast<double>(loop.delay[static_cast<std::size_t>(index(loop, age))]) * phase;
                phase *= step;
            }
            energy += std::norm(coefficient) * (2.0 - 2.0 * std::cos(omega));
        }
    }
    return energy;
}
double cyclicEnergy(const Access::Loop& loop) {
    const int n = static_cast<int>(std::ceil(loop.currentDelay)) + 1;
    double energy = 0.0;
    for (int age = 0; age < n; ++age) {
        const double d = static_cast<double>(loop.delay[static_cast<std::size_t>(index(loop, age))])
            - loop.delay[static_cast<std::size_t>(index(loop, (age + 1) % n))];
        energy += d * d;
    }
    return energy;
}
void advance(Engine& e, int samples) {
    for (int i = 0; i < samples; ++i) { float l = 0, r = 0; e.process(&l, &r, 1); }
}
bool compareNative(Engine& a, Engine& b, int samples) {
    bool same = true;
    for (int i = 0; i < samples; ++i) {
        float al = 0, ar = 0, ap = 0, bl = 0, br = 0, bp = 0;
        a.process(&al, &ar, Engine::OutputBuses { &ap }, 1);
        b.process(&bl, &br, Engine::OutputBuses { &bp }, 1);
        same = same && al == bl && ar == br && ap == bp
            && a.getLastBridgeBodyForce() == b.getLastBridgeBodyForce()
            && a.getLastBridgeVelocity() == b.getLastBridgeVelocity();
    }
    return same;
}
void testNativeShareAndEnergy() {
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (auto style : { acustra::PickingTechnique::Finger,
                            acustra::PickingTechnique::Pick,
                            acustra::PickingTechnique::Thumb })
            for (int string = 0; string < 6; ++string)
                for (int harmonic : { 1, 2, 3, 4 }) {
                    const auto open = Engine::openNotes(acustra::Tuning::Standard);
                    const int note = open[static_cast<std::size_t>(string)]
                        + (harmonic == 1 ? 12 : static_cast<int>(std::round(12.0 * std::log2(harmonic))));
                    const float touch = harmonic == 2 ? .1f : harmonic == 3 ? 1.0f : .58f;
                    const float velocity = harmonic == 2 ? .15f : harmonic == 3 ? .5f : 100.0f / 127.0f;
                    auto candidate = fresh(rate, acustra::GuitarModel::Bellido1978, style, touch);
                    auto directionReference = fresh(rate, acustra::GuitarModel::Bellido1978, style, touch);
                    Access::start(*candidate, string, harmonic, note, velocity);
                    Access::start(*directionReference, string, harmonic, note, velocity, true);
                    const double shareError = std::abs(Access::share(*candidate, string)
                        - Access::share(*directionReference, string) - authoredOffsets[static_cast<std::size_t>(string)]);
                    maximumShareError = std::max(maximumShareError, shareError);
                    expect(shareError < 1e-6, "Classic native stroke did not retain its immutable string offset");
                    expect(Access::randomState(*candidate, string) == Access::randomState(*directionReference, string),
                           "Classic direction adjustment consumed or changed an existing angle/noise draw");
                    const double a = freshEnergyByFourier(Access::loops(*candidate, string));
                    const double b = freshEnergyByFourier(Access::loops(*directionReference, string));
                    expect(a > 1e-12 && b > 1e-12, "fresh energy oracle contains no stored wave");
                    if (b > 1e-12) maximumFreshRelativeError = std::max(maximumFreshRelativeError, std::abs(a / b - 1.0));
                    expect(std::abs(a - b) <= 2e-6 * b,
                           "Classic direction rotation changed independent total fresh-stroke energy");
                    ++freshCases;
                }
}

void testOriginalAndBassDirectionAreExact() {
    for (double rate : { 44100.0, 96000.0 })
        for (auto style : { acustra::PickingTechnique::Finger,
                            acustra::PickingTechnique::Pick,
                            acustra::PickingTechnique::Thumb })
            for (auto model : { acustra::GuitarModel::Original, acustra::GuitarModel::Bellido1978 })
                for (int string = 0; string < 6; ++string) {
                    if (model == acustra::GuitarModel::Bellido1978 && string >= 3) continue;
                    for (int fret : { 0, 13, 20 }) {
                        const auto open = Engine::openNotes(acustra::Tuning::Standard);
                        const int note = open[static_cast<std::size_t>(string)] + fret;
                        auto actual = fresh(rate, model, style), directionReference = fresh(rate, model, style);
                        for (float velocity : { 100.f / 127.f, .52f }) {
                            actual->noteOn(note, velocity, string + 1);
                            Access::directionBypassedNoteOn(*directionReference, note, velocity, string);
                            expect(Access::share(*actual, string) == Access::share(*directionReference, string),
                                   "Original or first-three-string direction partition changed");
                            const bool same = compareNative(*actual, *directionReference, 512);

                            expect(same, "Original or first-three-string direction profile changed native microphone, pickup or mechanics");
                            ++exactCases;
                        }
                    }
                }
}

void testStringIdentityAcrossFretsAndTunings() {
    // Independent expected offsets are authored constants above, not the
    // production helper's Standard-pitch/smoothstep derivation.
    for (auto style : { acustra::PickingTechnique::Finger, acustra::PickingTechnique::Pick,
                        acustra::PickingTechnique::Thumb })
        for (int tuning = 0; tuning < 5; ++tuning)
            for (int string = 0; string < 6; ++string) {
                const auto value = static_cast<acustra::Tuning>(tuning);
                const auto open = Engine::openNotes(value);
                for (int fret : { 0, 7, 13, 20 }) {
                    const int note = open[static_cast<std::size_t>(string)] + fret;
                    auto actual = fresh(48000, acustra::GuitarModel::Bellido1978, style, .58f, value);
                    auto directionReference = fresh(48000, acustra::GuitarModel::Bellido1978, style, .58f, value);
                    actual->noteOn(note, .65f, string + 1);
                    Access::start(*directionReference, string, 1, note, .65f, true);
                    const double error = std::abs(Access::share(*actual, string)
                        - Access::share(*directionReference, string) - authoredOffsets[static_cast<std::size_t>(string)]);
                    maximumShareError = std::max(maximumShareError, error);
                    expect(error < 1e-6, "direction bias followed fretted pitch or tuning instead of physical string identity");
                    ++identityCases;
                }
            }
}
void testNativeTouchAndAngleBounds() {
    // There is no SourceAngle control: the native angle comes from the voice
    // noise/gesture stream. Repeated native strokes sample that existing stream.
    for (auto style : { acustra::PickingTechnique::Finger, acustra::PickingTechnique::Pick,
                        acustra::PickingTechnique::Thumb })
        for (float touch : { -10.0f, 0.0f, .58f, 1.0f, 10.0f })
            for (int string = 0; string < 6; ++string) {
                const auto open = Engine::openNotes(acustra::Tuning::Standard);
                const int note = open[static_cast<std::size_t>(string)] + 12;
                auto actual = fresh(48000, acustra::GuitarModel::Bellido1978, style, touch);
                auto directionReference = fresh(48000, acustra::GuitarModel::Bellido1978, style, touch);
                for (int repeat = 0; repeat < 4; ++repeat) {
                    actual->noteOn(note, .65f, string + 1);
                    Access::directionBypassedNoteOn(*directionReference, note, .65f, string);
                    const float a = Access::share(*actual, string), b = Access::share(*directionReference, string);
                    const double error = std::abs(a - b - authoredOffsets[static_cast<std::size_t>(string)]);
                    maximumShareError = std::max(maximumShareError, error);
                    expect(std::isfinite(a) && std::isfinite(b) && b >= .17f && b <= .35f
                        && a >= .17f && a <= .650001f, "Touch/native source-angle draw escaped the bounded normal share");
                    expect(error < 1e-6 && Access::randomState(*actual, string) == Access::randomState(*directionReference, string),
                           "Touch or native angle source lost its existing draw under the string bias");
                    advance(*actual, 29); advance(*directionReference, 29); ++boundsCases;
                }
            }
}

void testConfiguredModelAndSoundingPartition() {
    for (int string : { 3, 5 }) {
    const int note = string == 3 ? 60 : 69;
    auto actual = fresh(48000, acustra::GuitarModel::Original, acustra::PickingTechnique::Finger);
    auto directionReference = fresh(48000, acustra::GuitarModel::Original, acustra::PickingTechnique::Finger);
    for (auto* e : { actual.get(), directionReference.get() }) {
        e->noteOn(note, .7f, string + 1); advance(*e, 240);
        acustra::EngineParameters p; p.guitarModel = acustra::GuitarModel::Bellido1978;
        e->setParameters(p); advance(*e, 240);
        p.guitarModel = acustra::GuitarModel::Original; e->setParameters(p);
    }
    expect(Access::configured(*actual) == 1 && Access::pending(*actual),
           "configured-model test never entered the queued model state");
    actual->noteOn(note + 2, .7f, string + 1); Access::start(*directionReference, string, 1, note + 2, .7f, true);
    expect(std::abs(Access::share(*actual, string) - Access::share(*directionReference, string) - authoredOffsets[static_cast<std::size_t>(string)]) < 1e-6f,
           "queued requested model prematurely changed the active bridge's stroke profile");
    const float before = Access::share(*actual, string); advance(*actual, 2400);
    expect(Access::configured(*actual) == 0 && Access::share(*actual, string) == before,
           "model change repartitioned a wave already sounding");
    }
}
void testRepluckBudget() {
    for (double rate : { 44100.0, 96000.0 })
        for (auto style : { acustra::PickingTechnique::Finger, acustra::PickingTechnique::Pick,
                            acustra::PickingTechnique::Thumb })
            for (int string = 0; string < 6; ++string)
                {
            constexpr int pitchChange = 0;
            const auto open = Engine::openNotes(acustra::Tuning::Standard);
            const int note = open[static_cast<std::size_t>(string)] + 12;
            auto ringing = fresh(rate, acustra::GuitarModel::Bellido1978, style);
            auto source = fresh(rate, acustra::GuitarModel::Bellido1978, style);
            for (auto* e : { ringing.get(), source.get() }) { e->noteOn(note, .65f, string + 1); advance(*e, 509); }
            const auto old = Access::loops(*ringing, string);
            Access::forgetIncoming(*source, string);
            ringing->noteOn(note + pitchChange, .52f, string + 1); Access::start(*source, string, 1, note + pitchChange, .52f, true);
            const auto merged = Access::loops(*ringing, string), directionReferenceFresh = Access::loops(*source, string);
            double allowed = 0.0, actual = 0.0;
            for (int plane = 0; plane < 2; ++plane) {
                auto held = old[static_cast<std::size_t>(plane)];
                Access::condition(held, Access::contact(*ringing, string));
                auto increment = held; increment.delay.fill(0.0f);
                const auto& release = directionReferenceFresh[static_cast<std::size_t>(plane)];
                const int n = static_cast<int>(std::ceil(held.currentDelay)) + 1;
                for (int age = 0; age < n; ++age) {
                    const double sample = age * static_cast<double>(release.currentDelay) / held.currentDelay;
                    const int whole = static_cast<int>(sample); const double fraction = sample - whole;
                    const double a = release.delay[static_cast<std::size_t>(index(release, whole))];
                    const double b = release.delay[static_cast<std::size_t>(index(release, whole + 1))];
                    increment.delay[static_cast<std::size_t>(index(increment, age))] = static_cast<float>(a + fraction * (b - a));
                }
                allowed += cyclicEnergy(held) + cyclicEnergy(increment);
                actual += cyclicEnergy(merged[static_cast<std::size_t>(plane)]);
                expect(pitchChange != 0 || (merged[static_cast<std::size_t>(plane)].writeIndex == old[static_cast<std::size_t>(plane)].writeIndex
                           && merged[static_cast<std::size_t>(plane)].bendingLossY1 == old[static_cast<std::size_t>(plane)].bendingLossY1),
                       "profile normalization reset the incoming string clock or bending memory");
            }
            if (allowed > 1e-12) maximumRepluckBudgetRatio = std::max(maximumRepluckBudgetRatio, actual / allowed);
            ++repluckCases;
            expect(allowed > 1e-12 && actual > 1e-12 && actual <= allowed * (1.0 + 2e-5),
                   "Classic repluck exceeded its conditioned old wave plus direction-reference fresh-work budget");
        }
}
void testRefretKeepsRetainedTailAndFreshBudget() {
    for (double rate : { 44100.0, 96000.0 })
        for (auto style : { acustra::PickingTechnique::Finger, acustra::PickingTechnique::Pick,
                            acustra::PickingTechnique::Thumb })
            for (int string = 0; string < 6; ++string) {
                const auto open = Engine::openNotes(acustra::Tuning::Standard);
                const int note = open[static_cast<std::size_t>(string)] + 12;
                auto actual = fresh(rate, acustra::GuitarModel::Bellido1978, style);
                auto directionReference = fresh(rate, acustra::GuitarModel::Bellido1978, style);
                // The incoming wave is identical and already uses the authored
                // stroke. Only the new stroke's selection is bypassed below.
                for (auto* e : { actual.get(), directionReference.get() }) {
                    e->noteOn(note, .65f, string + 1); advance(*e, 509);
                }
                actual->noteOn(note + 2, .52f, string + 1);
                Access::directionBypassedNoteOn(*directionReference, note + 2, .52f, string);
                const auto a = Access::tails(*actual, string), b = Access::tails(*directionReference, string);
                expect(Access::tailActive(*actual, string) && Access::tailActive(*directionReference, string),
                       "changed-fret stroke did not preserve a sounding retained branch");
                for (int plane = 0; plane < 2; ++plane) {
                    const auto& x = a[static_cast<std::size_t>(plane)];
                    const auto& y = b[static_cast<std::size_t>(plane)];
                    expect(x.delay == y.delay && x.writeIndex == y.writeIndex
                        && x.currentDelay == y.currentDelay && x.bendingLossY1 == y.bendingLossY1,
                           "new authored stroke rescaled or reset its retained old-fret field");
                }
                const double actualFresh = freshEnergyByFourier(Access::loops(*actual, string));
                const double directionReferenceFresh = freshEnergyByFourier(Access::loops(*directionReference, string));
                expect(actualFresh > 1e-12 && directionReferenceFresh > 1e-12
                    && std::abs(actualFresh - directionReferenceFresh) <= 2e-6 * directionReferenceFresh,
                       "changed-fret stroke changed its independent direction-reference fresh-energy budget");
                if (directionReferenceFresh > 1e-12) maximumFreshRelativeError = std::max(
                    maximumFreshRelativeError, std::abs(actualFresh / directionReferenceFresh - 1.0));
                ++refretCases;
            }
}

}
int main() {
    testNativeShareAndEnergy(); testOriginalAndBassDirectionAreExact(); testStringIdentityAcrossFretsAndTunings(); testNativeTouchAndAngleBounds();
    testConfiguredModelAndSoundingPartition(); testRepluckBudget(); testRefretKeepsRetainedTailAndFreshBudget();
    std::cout << "fresh " << freshCases << " exact " << exactCases << " identity " << identityCases
              << " repluck " << repluckCases << " refret " << refretCases << " bounds " << boundsCases
              << " max_fresh_relative_error " << maximumFreshRelativeError
              << " max_share_error " << maximumShareError
              << " max_repluck_budget_ratio " << maximumRepluckBudgetRatio << "\n";
    if (failures) return 1;
    std::cout << "Pluck profile, independent fresh energy and native state invariants passed\n";
    return 0;
}
