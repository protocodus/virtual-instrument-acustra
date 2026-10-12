// Shape/Wood must describe the final construction, not the route through
// earlier selections. Compare coefficients before reset can rebuild them,
// then compare controlled strikes after silent, interrupted and released
// transitions. RNG normalization is test-only: panic intentionally keeps
// performance draws moving in production.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static void restartDraws(AcustraEngine& engine) { engine.restartRandomDraws(); }
    static std::uint64_t configurationGeneration(const AcustraEngine& engine)
    { return engine.voiceConfigurationGeneration_; }
    static bool idle(const AcustraEngine& engine)
    { return engine.idleFlushed_ && engine.getActiveVoiceCount() == 0; }
    static bool settled(const AcustraEngine& engine, const EngineParameters& target)
    {
        return !engine.bodyUpdatePending_ && !engine.bridgeUpdatePending_
            && engine.bodyModelFade_ == 1.0f && engine.bridgeLoadFade_ == 1.0f
            && engine.configuredBodyShape_ == target.shape
            && engine.configuredBodyMaterial_ == target.bodyMaterial
            && engine.configuredBridgeShape_ == target.shape
            && engine.configuredBridgeMaterial_ == target.bodyMaterial;
    }
    static bool delaysSettled(const AcustraEngine& engine)
    {
        for (const auto& voice : engine.voices_)
            for (const auto& loop : voice.loops)
                if (loop.currentDelay != loop.targetDelay)
                    return false;
        return true;
    }
    static std::vector<double> suspendedHistory(const AcustraEngine& engine)
    {
        std::vector<double> result { double(engine.sampleClock_), double(engine.noteOrder_),
            double(engine.controlCounter_), double(engine.strumRandomState_),
            double(engine.pickingGestureOriginSample_), engine.strumSpeedScale_, engine.strumParallelSign_,
            engine.palmMute_, engine.targetPalmMute_, engine.vibrato_, engine.vibratoPhase_,
            engine.vibratoOnset_, double(engine.pickingGestureSample_), double(engine.pickingGestureSeen_),
            engine.pickingGesture_.position, engine.pickingGesture_.pressure, double(engine.pickingGesture_.active),
            engine.strumGesture_.position, engine.strumGesture_.pressure, double(engine.strumGesture_.active) };
        result.insert(result.end(), engine.pitchBendSemitones_.begin(), engine.pitchBendSemitones_.end());
        result.insert(result.end(), engine.mpeTimbre_.begin(), engine.mpeTimbre_.end());
        result.insert(result.end(), engine.mpePressure_.begin(), engine.mpePressure_.end());
        for (bool down : engine.sustainPedals_) result.push_back(down);
        for (const auto& finger : engine.hand_)
            result.insert(result.end(), { double(finger.fret), double(finger.heldAt), double(finger.valid) });
        for (const auto& voice : engine.voices_)
        {
            result.insert(result.end(), { double(voice.randomState), double(voice.repeatPluckState),
                double(voice.releaseNoiseState), double(voice.legatoFrictionState), double(voice.excitationNoiseState) });
            for (const auto* loop : { &voice.loops[0], &voice.loops[1], &voice.tailLoop, &voice.tailParallelLoop })
            {
                result.insert(result.end(), loop->delay.begin(), loop->delay.end());
                result.insert(result.end(), { double(loop->writeIndex), loop->allpassY1, loop->allpassY2,
                    loop->bendingLossX1, loop->bendingLossX2, loop->bendingLossY1, loop->bendingLossY2,
                    loop->constantLossX1, loop->constantLossX2, loop->constantLossY1, loop->constantLossY2,
                    loop->broadLossFilter.state, loop->broadLossFilter.previousInput,
                    loop->lossFilter.state, loop->lossFilter.previousInput,
                    loop->dispersion.x1, loop->dispersion.x2, loop->dispersion.y1, loop->dispersion.y2,
                    loop->secondDispersion.x1, loop->secondDispersion.x2,
                    loop->secondDispersion.y1, loop->secondDispersion.y2 });
            }
        }
        for (const auto* bank : { &engine.bodyBank_, &engine.fadingBodyBank_ })
        {
            for (const auto* state : { &bank->real, &bank->imaginary, &bank->momentReal, &bank->momentImaginary })
                result.insert(result.end(), state->begin(), state->end());
            for (const auto* states : { &bank->captureFilter.stateLeft, &bank->captureFilter.stateRight })
                for (const auto& state : *states) result.insert(result.end(), state.begin(), state.end());
        }
        for (const auto* bridge : { &engine.bridgeLoad_, &engine.fadingBridgeLoad_ })
            for (std::size_t index = 0; index < bridge->heaveModes.size(); ++index)
                for (const auto* mode : { &bridge->heaveModes[index], &bridge->rockModes[index] })
                    result.insert(result.end(), { mode->input1, mode->output1, mode->output2 });
        result.insert(result.end(), engine.micDelayLeft_.begin(), engine.micDelayLeft_.end());
        result.insert(result.end(), engine.micDelayRight_.begin(), engine.micDelayRight_.end());
        result.insert(result.end(), engine.micDelayMono_.begin(), engine.micDelayMono_.end());
        result.push_back(engine.micDelayIndex_);
        return result;
    }
    // Deliberately omit ringing states, cache generations and smoothed level
    // references. None is a construction coefficient; the delivered audio
    // below independently includes their audible effects.
    static std::vector<double> coefficients(const AcustraEngine& engine)
    {
        std::vector<double> result;
        for (const auto& mode : engine.bodyModes_)
            for (float value : { mode.poleReal, mode.poleImaginary,
                mode.leftReal, mode.leftImaginary, mode.rightReal, mode.rightImaginary,
                mode.leftMomentReal, mode.leftMomentImaginary,
                mode.rightMomentReal, mode.rightMomentImaginary })
                result.push_back(value);
        const auto& bridge = engine.bridgeLoad_;
        for (std::size_t index = 0; index < bridge.heaveModes.size(); ++index)
        {
            for (const auto* mode : { &bridge.heaveModes[index], &bridge.rockModes[index] })
                for (double value : { mode->denominator1, mode->denominator2,
                                     mode->numerator1, mode->numerator2 })
                    result.push_back(value);
            result.push_back(bridge.residueHeave[index]);
            result.push_back(bridge.residueCross[index]);
            result.push_back(bridge.residueRock[index]);
            result.push_back(bridge.rocking[index]);
        }
        result.insert(result.end(), { bridge.immediateHeave, bridge.immediateCross,
                                     bridge.immediateRock, double(bridge.activeModeCount) });
        for (const auto& section : engine.bodyBank_.captureFilter.coefficients)
            result.insert(result.end(), section.begin(), section.end());
        result.push_back(engine.bodyBank_.captureFilter.gain);
        for (const auto& voice : engine.voices_)
        {
            result.push_back(voice.characteristicImpedance);
            result.push_back(voice.polarisationDetune);
            for (const auto& loop : voice.loops)
                result.insert(result.end(), { loop.currentDelay, loop.targetDelay,
                    loop.loopGain, loop.targetLoopGain, loop.lowpassCoefficient,
                    loop.highLossMix, loop.dispersionA1, loop.dispersionA2,
                    loop.secondDispersionA1, loop.secondDispersionA2,
                    loop.bendingLossGain, loop.bendingLossA1, loop.bendingLossA2,
                    loop.constantLossGain, loop.constantLossA1, loop.constantLossA2,
                    loop.constantLossN1, loop.constantLossN2 });
        }
        return result;
    }
};
} // namespace acustra

namespace
{
using namespace acustra;
using Access = AcustraEngineTestAccess;
constexpr int block = 128;
int failures = 0, cases = 0;
double worstRelative = 0.0, worstAbsolute = 0.0;
int exactCases = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition && ++failures <= 30)
        std::cerr << "FAIL: " << message << '\n';
}

std::unique_ptr<AcustraEngine> fresh(const EngineParameters& p, double rate)
{
    auto engine = std::make_unique<AcustraEngine>();
    engine->setParameters(p);
    engine->prepare(rate, block);
    return engine;
}

double advance(AcustraEngine& engine, int samples)
{
    std::array<float, block> left {}, right {}, piezo {};
    AcustraEngine::OutputBuses buses;
    buses.piezo = piezo.data();
    double peak = 0.0;
    while (samples > 0)
    {
        const int count = std::min(block, samples);
        engine.process(left.data(), right.data(), buses, count);
        for (int index = 0; index < count; ++index)
            peak = std::max({ peak, double(std::abs(left[std::size_t(index)])),
                             double(std::abs(right[std::size_t(index)])),
                             double(std::abs(piezo[std::size_t(index)])) });
        samples -= count;
    }
    return peak;
}

// Always advance the direct reference by the same frames. This keeps the
// sample clock and control boundaries equal without resetting either side.
double advanceBoth(AcustraEngine& actual, AcustraEngine& reference, int samples)
{
    const double actualPeak = advance(actual, samples);
    return std::max(actualPeak, advance(reference, samples));
}

void select(AcustraEngine& engine, EngineParameters& current,
            const EngineParameters& target, bool shapeFirst)
{
    if (shapeFirst)
    {
        current.shape = target.shape;
        engine.setParameters(current);
        current.bodyMaterial = target.bodyMaterial;
    }
    else
    {
        current.bodyMaterial = target.bodyMaterial;
        engine.setParameters(current);
        current.shape = target.shape;
    }
    engine.setParameters(current);
}

std::vector<float> strike(AcustraEngine& engine, double rate)
{
    Access::restartDraws(engine);
    const std::array<int, 4> notes { 45, 52, 59, 64 };
    engine.beginStrum();
    for (std::size_t rank = 0; rank < notes.size(); ++rank)
        engine.noteOn(notes[rank], 0.75f, 1, int(rank * .012 * rate), true);
    std::vector<float> audio;
    std::array<float, block> left {}, right {}, piezo {};
    AcustraEngine::OutputBuses buses;
    buses.piezo = piezo.data();
    for (int start = 0; start < int(.18 * rate); start += block)
    {
        if (start <= int(.1 * rate) && start + block > int(.1 * rate))
            for (int note : notes)
                engine.noteOff(note);
        const int count = std::min(block, int(.18 * rate) - start);
        engine.process(left.data(), right.data(), buses, count);
        for (int index = 0; index < count; ++index)
            audio.insert(audio.end(), { left[std::size_t(index)], right[std::size_t(index)],
                                       piezo[std::size_t(index)] });
    }
    return audio;
}

void compare(AcustraEngine& actual, AcustraEngine& reference,
             const EngineParameters& p, double rate, const std::string& label,
             bool exact)
{
    ++cases;
    expect(Access::settled(actual, p), label + ": final bank/fade did not settle");
    expect(Access::delaysSettled(actual) && Access::delaysSettled(reference),
           label + ": an open-string delay retained its approach history");
    expect(Access::coefficients(actual) == Access::coefficients(reference),
           label + ": construction coefficients depend on selection history before reset");
    const auto audio = strike(actual, rate);
    const auto baseline = strike(reference, rate);
    double signal = 0.0, difference = 0.0, maximum = 0.0;
    bool finite = true;
    for (std::size_t index = 0; index < audio.size(); ++index)
    {
        const double delta = double(audio[index]) - baseline[index];
        signal += double(baseline[index]) * baseline[index];
        difference += delta * delta;
        maximum = std::max(maximum, std::abs(delta));
        finite = finite && std::isfinite(audio[index]) && std::isfinite(baseline[index]);
    }
    const double relative = std::sqrt(difference / std::max(signal, 1.0e-30));
    worstRelative = std::max(worstRelative, relative);
    worstAbsolute = std::max(worstAbsolute, maximum);
    exactCases += audio == baseline ? 1 : 0;
    expect(finite && signal > 1.0e-8, label + ": non-finite or silent comparison strike");
    // Entirely silent selection cycles and cleared-wave comparisons must
    // recover every sample exactly. A released history can retain physical
    // wave/contact energy even below the measured output floor; report it
    // separately instead of equating output tail amplitude with stored energy.
    expect(!exact || audio == baseline,
           label + ": audio history error=" + std::to_string(relative)
               + ", max sample difference=" + std::to_string(maximum));
    std::cout << label << ": relative=" << relative << " max=" << maximum
              << " exact=" << (audio == baseline) << '\n';
}

enum class Route { SameTick, Settled, Interrupted };

void testIdleCycles(const EngineParameters& final, double rate, bool shapeFirst, Route route)
{
    auto actual = fresh(final, rate), reference = fresh(final, rate);
    auto current = final;
    // Three full traversals, alternating traversal direction, revisit both
    // visible woods and the retained legacy Maple in every shape.
    for (int round = 0; round < 3; ++round)
        for (int cell = 0; cell < 12; ++cell)
        {
            const int index = round % 2 ? 11 - cell : cell;
            auto target = final;
            target.shape = static_cast<BodyShape>(index / 3);
            target.bodyMaterial = static_cast<BodyMaterial>(index % 3);
            select(*actual, current, target, shapeFirst);
            if (route != Route::SameTick)
                advanceBoth(*actual, *reference,
                            route == Route::Settled ? int(.06 * rate) : 17);
        }
    select(*actual, current, final, shapeFirst);
    advanceBoth(*actual, *reference, int(.4 * rate));
    const std::string routeName = route == Route::SameTick ? "same-tick"
        : route == Route::Settled ? "settled" : "interrupted";
    const std::string label = std::to_string(int(rate)) + " shape="
        + std::to_string(int(final.shape)) + " wood=" + std::to_string(int(final.bodyMaterial))
        + (shapeFirst ? " shape-first " : " wood-first ") + routeName;
    expect(Access::idle(*actual) && Access::idle(*reference), label + ": silent setup was not idle");
    compare(*actual, *reference, final, rate, label, true);
    actual->reset();
    reference->reset();
    compare(*actual, *reference, final, rate, label + " reset", true);
}

void testReleasedCycles(const EngineParameters& final, bool shapeFirst)
{
    constexpr double rate = 48000.0;
    auto actual = fresh(final, rate), reference = fresh(final, rate);
    for (auto* engine : { actual.get(), reference.get() })
        engine->noteOn(57, .7f);
    advanceBoth(*actual, *reference, int(.04 * rate));
    auto current = final;
    for (int round = 0; round < 3; ++round)
    {
        auto alternate = final;
        alternate.shape = static_cast<BodyShape>((int(final.shape) + round + 1) % 4);
        alternate.bodyMaterial = static_cast<BodyMaterial>((int(final.bodyMaterial) + 1) % 3);
        select(*actual, current, alternate, shapeFirst);
        advanceBoth(*actual, *reference, 17);
        select(*actual, current, final, shapeFirst);
        advanceBoth(*actual, *reference, 31);
    }
    for (auto* engine : { actual.get(), reference.get() })
        engine->noteOff(57);
    int elapsed = 0;
    const int step = int(.1 * rate);
    double tailPeak = 1.0;
    while (tailPeak > 1.0e-7 && elapsed < int(12 * rate))
    {
        tailPeak = advanceBoth(*actual, *reference, step);
        elapsed += step;
    }
    tailPeak = advanceBoth(*actual, *reference, int(.4 * rate));
    const std::string label = "48000 shape=" + std::to_string(int(final.shape))
        + " wood=" + std::to_string(int(final.bodyMaterial))
        + (shapeFirst ? " shape-first natural-decay" : " wood-first natural-decay");
    expect(tailPeak < 1.0e-7, label + ": retained acoustic tail above comparison floor");
    std::cout << label << ": decay seconds=" << elapsed / rate
              << " tail peak=" << tailPeak << '\n';
    expect(Access::coefficients(*actual) == Access::coefficients(*reference)
               && Access::delaysSettled(*actual) && Access::delaysSettled(*reference),
           label + ": construction changed before clearing retained waves");
    // Copy the already configured engines, then clear only their sounding
    // history through the public panic path. The prior coefficient/endpoint
    // assertion prevents this clear from concealing stale construction.
    auto clearedActual = std::make_unique<AcustraEngine>(*actual);
    auto clearedReference = std::make_unique<AcustraEngine>(*reference);
    clearedActual->allSoundOff();
    clearedReference->allSoundOff();
    compare(*clearedActual, *clearedReference, final, rate, label + " cleared-wave clone", true);
    // This comparison happens before reset/allSoundOff/re-prepare, after the
    // measured tail fell below -140 dBFS; retained vibration cannot be
    // mistaken for persistent construction-coefficient history.
    compare(*actual, *reference, final, rate, label + " retained-wave diagnostic", false);
}

void testSuspendedAdoption()
{
    constexpr double rate = 48000.0;
    EngineParameters initial;
    auto engine = fresh(initial, rate);
    engine->noteOn(57, .7f);
    advance(*engine, int(.05 * rate));
    engine->noteOff(57);
    double tail = 1.0;
    int elapsed = 0;
    while ((tail > 1.0e-9 || engine->getActiveVoiceCount() != 0) && elapsed < int(16 * rate))
    {
        tail = advance(*engine, int(.1 * rate));
        elapsed += int(.1 * rate);
    }
    expect(tail < 1.0e-9 && engine->getActiveVoiceCount() == 0,
           "suspended adoption: setup did not reach inaudible inactive sleep");
    engine->setPitchBend(.35f);
    engine->setVibrato(.4f);
    engine->setPalmMutePressure(.3f);
    engine->setSustainPedal(true);
    engine->setMpeTimbre(.6f, 2);
    engine->setMpePressure(.7f, 2);
    auto target = initial;
    target.shape = BodyShape::Parlor;
    target.bodyMaterial = BodyMaterial::Mahogany;
    engine->setParameters(target);
    const auto history = Access::suspendedHistory(*engine);
    engine->settleSuspendedParameters();
    expect(history == Access::suspendedHistory(*engine),
           "suspended adoption changed clocks/RNG/controllers/hand or stored waves/filters");
    expect(Access::settled(*engine, target) && Access::delaysSettled(*engine),
           "suspended adoption did not finish construction banks and delay endpoints");
    const auto coefficients = Access::coefficients(*engine);
    const auto generation = Access::configurationGeneration(*engine);
    engine->settleSuspendedParameters();
    expect(coefficients == Access::coefficients(*engine)
               && history == Access::suspendedHistory(*engine)
               && generation == Access::configurationGeneration(*engine),
           "suspended adoption is not idempotent");
    target.outputGain = .23f;
    target.stereoWidth = .31f;
    engine->setParameters(target);
    engine->settleSuspendedParameters();
    expect(coefficients == Access::coefficients(*engine)
               && history == Access::suspendedHistory(*engine)
               && generation == Access::configurationGeneration(*engine),
           "suspended mix-only edit invalidated construction caches or performance history");

    auto active = fresh(initial, rate);
    active->noteOn(57, .7f);
    advance(*active, int(.02 * rate));
    active->setParameters(target);
    auto untouched = std::make_unique<AcustraEngine>(*active);
    active->settleSuspendedParameters();
    expect(Access::coefficients(*active) == Access::coefficients(*untouched)
               && Access::suspendedHistory(*active) == Access::suspendedHistory(*untouched),
           "suspended adoption did not reject an active performance");
    const double first = advance(*active, int(.1 * rate));
    const double second = advance(*untouched, int(.1 * rate));
    expect(first == second && Access::suspendedHistory(*active) == Access::suspendedHistory(*untouched),
           "rejected active adoption changed subsequent performance");
    auto planned = fresh(initial, rate);
    planned->setParameters(target);
    const std::array<int, 3> notes { 45, 52, 59 };
    planned->planChord(notes.data(), int(notes.size()));
    const auto plannedCoefficients = Access::coefficients(*planned);
    const auto plannedHistory = Access::suspendedHistory(*planned);
    planned->settleSuspendedParameters();
    expect(plannedCoefficients == Access::coefficients(*planned)
               && plannedHistory == Access::suspendedHistory(*planned)
               && !Access::settled(*planned, target),
           "suspended adoption did not reject a planned chord");
    // The plan's inventory persists after its notes have been consumed and
    // after the sample has passed. It is a cache, not a queue of future notes.
    auto consumed = fresh(initial, rate);
    consumed->planChord(notes.data(), int(notes.size()));
    for (int note : notes) consumed->noteOn(note, .7f);
    consumed->allSoundOff();
    consumed->setParameters(target);
    const auto consumedHistory = Access::suspendedHistory(*consumed);
    consumed->settleSuspendedParameters();
    expect(Access::settled(*consumed, target) && Access::delaysSettled(*consumed)
               && consumedHistory == Access::suspendedHistory(*consumed),
           "consumed current-sample plan blocked suspended construction or changed performance");
    auto stale = fresh(initial, rate);
    stale->planChord(notes.data(), int(notes.size()));
    advance(*stale, 1);
    stale->setParameters(target);
    const auto staleHistory = Access::suspendedHistory(*stale);
    stale->settleSuspendedParameters();
    expect(Access::settled(*stale, target) && Access::delaysSettled(*stale)
               && staleHistory == Access::suspendedHistory(*stale),
           "stale chord-plan cache blocked suspended construction or changed performance");
    std::cout << "Suspended construction adoption: controller/clock/RNG/hand/wave/filter preservation, "
                 "idempotence and active/planned guards passed; setup tail=" << tail << '\n';
}
} // namespace

int main(int argc, char** argv)
{
    testSuspendedAdoption();
    if (argc == 2 && std::string(argv[1]) == "--suspended-adoption-only")
        return failures == 0 ? 0 : 1;
    for (int shape = 0; shape < 4; ++shape)
        for (int wood = 0; wood < 3; ++wood)
        {
            EngineParameters final;
            final.shape = static_cast<BodyShape>(shape);
            final.bodyMaterial = static_cast<BodyMaterial>(wood);
            final.bodyAmount = .7f;
            final.piezoMix = .2f;
            final.releaseNoise = .7f;
            final.outputGain = .12f;
            for (bool shapeFirst : { true, false })
            {
                for (double rate : { 44100.0, 48000.0, 96000.0 })
                    for (Route route : { Route::SameTick, Route::Settled, Route::Interrupted })
                        testIdleCycles(final, rate, shapeFirst, route);
                testReleasedCycles(final, shapeFirst);
            }
        }
    std::cout << "Construction history: " << cases << " comparisons, " << exactCases
              << " sample-exact; worst relative=" << worstRelative
              << " worst absolute=" << worstAbsolute << '\n';
    if (failures)
    {
        std::cerr << failures << " construction history check(s) failed\n";
        return 1;
    }
    std::cout << "All construction history tests passed\n";
    return 0;
}
