// A common tone may need a different physical string in a new chord.
// The release-join window must retain its old wave without losing its new stroke.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct Wave
    {
        std::array<std::vector<float>, 2> samples;
        std::array<int, 2> writeIndex {};
        std::array<float, 2> delay {};
        bool operator==(const Wave& other) const
        {
            return samples == other.samples && writeIndex == other.writeIndex
                && delay == other.delay;
        }
    };

    static Wave wave(const AcustraEngine& engine, int string, bool tail)
    {
        Wave result;
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        for (int plane = 0; plane < 2; ++plane)
        {
            const auto& loop = tail
                ? (plane == 0 ? voice.tailLoop : voice.tailParallelLoop)
                : voice.loops[static_cast<std::size_t>(plane)];
            auto& samples = result.samples[static_cast<std::size_t>(plane)];
            samples.resize(static_cast<std::size_t>(engine.activeDelaySamples()));
            for (std::size_t index = 0; index < samples.size(); ++index)
                samples[index] = loop.delay[index];
            result.writeIndex[static_cast<std::size_t>(plane)] = loop.writeIndex;
            result.delay[static_cast<std::size_t>(plane)] = loop.currentDelay;
        }
        return result;
    }

    static const auto& voice(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)];
    }

    static float gripCost(const AcustraEngine& engine)
    {
        AcustraEngine::StringFrets frets {};
        frets.fill(-1);
        for (int string = 0; string < AcustraEngine::stringCount; ++string)
        {
            const auto& current = voice(engine, string);
            if (current.played && current.keyDown)
                frets[static_cast<std::size_t>(string)] = current.fret;
        }
        return engine.shapeCost(frets, 0u, engine.handWeights());
    }
};
}

namespace
{
using acustra::Performer;
using Access = acustra::AcustraEngineTestAccess;
using Stereo = std::array<std::vector<float>, 2>;
using Notes = std::vector<int>;
const Notes bMinor { 66, 62, 59, 54, 47 };
const Notes eMajor { 64, 59, 56, 52, 47, 40 };
const Notes aMinor { 64, 60, 57, 52, 45 };
int failures = 0;

void expect(bool condition, const std::string& description)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

auto fresh(double rate)
{
    auto performer = std::make_unique<Performer>();
    performer->prepare(rate, 127);
    performer->setTempoBpm(120.0);
    acustra::EngineParameters parameters;
    parameters.room = 0.1f;
    parameters.piezoMix = 0.2f;
    parameters.releaseNoise = 0.1f;
    performer->setParameters(parameters);
    return performer;
}

void group(Performer& performer, const Notes& notes, bool on)
{
    // Flush exactly one public same-sample MIDI group before rendering its
    // sample, allowing an observation of the caught wave at the handoff.
    std::array<float, 1> left {}, right {};
    performer.beginBlock(left.data(), right.data(), 0);
    for (const int note : notes)
        if (on) performer.noteOn(0, 1, note, 92);
        else performer.noteOff(0, 1, note, 64);
    performer.endBlock();
}

void advance(Performer& performer, int frames)
{
    std::array<float, 127> left {}, right {};
    while (frames > 0)
    {
        const int count = std::min(frames, 127);
        performer.process(left.data(), right.data(), count);
        frames -= count;
    }
}

void completeEChord(const acustra::AcustraEngine& engine, bool fired,
                    const std::string& label)
{
    // The conventional open E shape requires B3 on the B string, leaving
    // the G string for G-sharp3. This is a physical chord oracle, not an
    // assertion about the implementation's particular search order.
    for (const int note : eMajor)
    {
        const int string = engine.heldString(note);
        expect(string >= 0, label + ": lost newly held MIDI " + std::to_string(note));
        if (string >= 0 && fired)
        {
            const auto& current = Access::voice(engine, string);
            expect(current.attackFired && current.pluckDelay == 0
                       && current.lastPluckMidiNote == note,
                   label + ": the queued stroke never reached MIDI "
                       + std::to_string(note));
        }
    }
    expect(engine.heldString(59) == 4 && engine.heldString(56) == 3,
           label + ": B3/G-sharp3 did not reach separate physical strings");
    expect(Access::gripCost(engine) == 0.0f,
           label + ": the refingered open E chord became an impossible grip");
}

void changedChordPreservesOldWave(double rate, bool beyondJoin)
{
    auto performer = fresh(rate);
    group(*performer, bMinor, true);
    advance(*performer, static_cast<int>(0.12 * rate) + 17);
    expect(performer->engine().heldString(59) == 3,
           "Bm fixture did not place B3 on G4");
    group(*performer, bMinor, false);
    advance(*performer, static_cast<int>((beyondJoin ? 0.08 : 0.02) * rate));
    const auto& oldVoice = Access::voice(performer->engine(), 3);
    expect(oldVoice.releaseJoinPending != beyondJoin,
           "chord change did not reach the intended side of the join deadline");
    const auto oldWave = Access::wave(performer->engine(), 3, false);
    expect(std::any_of(oldWave.samples[0].begin(), oldWave.samples[0].end(),
                      [](float sample) { return std::abs(sample) > 1.0e-6f; }),
           "retained-wave witness was silent");
    group(*performer, eMajor, true);
    const std::string label = "Bm->E " + std::to_string(rate)
        + (beyondJoin ? " beyond join" : " inside join");
    completeEChord(performer->engine(), false, label);
    expect(Access::voice(performer->engine(), 3).tailActive
               && Access::wave(performer->engine(), 3, true) == oldWave,
           label + ": the old B3 wave or its transport phase was discarded");
    advance(*performer, static_cast<int>(0.06 * rate));
    completeEChord(performer->engine(), true, label);
}

void changedChordBeforeOldPickArrives(double rate)
{
    auto performer = fresh(rate);
    group(*performer, bMinor, true);
    advance(*performer, 17);
    group(*performer, bMinor, false);
    advance(*performer, static_cast<int>(0.002 * rate));
    expect(Access::voice(performer->engine(), 3).releaseAfterPluck
               && Access::voice(performer->engine(), 3).pluckDelay > 0,
           "short-stab fixture did not reach an unfired old B3 stroke");
    group(*performer, eMajor, true);
    completeEChord(performer->engine(), false, "queued-stab Bm->E");
    advance(*performer, static_cast<int>(0.10 * rate));
    completeEChord(performer->engine(), true, "queued-stab Bm->E");
    for (const int note : eMajor)
    {
        const int string = performer->engine().heldString(note);
        if (string >= 0)
            expect(!Access::voice(performer->engine(), string).releaseAfterPluck,
                   "an old queued key-up remained attached to the new chord");
    }
}

Stereo unchangedJoin(double rate, const Notes& before, const Notes& after,
                     bool keepCommonKeys)
{
    auto performer = fresh(rate);
    const int offAt = static_cast<int>(0.12 * rate) + 17;
    const int onAt = offAt + static_cast<int>(0.02 * rate);
    const int end = onAt + static_cast<int>(0.10 * rate);
    Stereo audio { std::vector<float>(static_cast<std::size_t>(end)),
                   std::vector<float>(static_cast<std::size_t>(end)) };
    const auto render = [&](int start, int finish)
    {
        while (start < finish)
        {
            const int count = std::min(127, finish - start);
            performer->process(audio[0].data() + start, audio[1].data() + start, count);
            start += count;
        }
    };
    group(*performer, before, true);
    render(0, offAt);
    Notes released;
    for (const int note : before)
        if (!keepCommonKeys || std::find(after.begin(), after.end(), note) == after.end())
            released.push_back(note);
    group(*performer, released, false);
    render(offAt, onAt);
    group(*performer, after, true);
    render(onAt, end);
    for (const int note : after)
        expect(performer->engine().heldString(note) >= 0,
               "unchanged join lost a held key");
    return audio;
}

void negativeControls(double rate)
{
    // Within grace, ordinary same-string joins must be the same physical
    // wave/attack as leaving the old key held until its repeated stroke.
    expect(unchangedJoin(rate, { 52 }, { 52 }, false)
               == unchangedJoin(rate, { 52 }, { 52 }, true),
           "no-plan E3 join changed the continuing string or attack");

    auto performer = fresh(rate);
    group(*performer, eMajor, true);
    advance(*performer, static_cast<int>(0.12 * rate) + 17);
    group(*performer, eMajor, false);
    advance(*performer, static_cast<int>(0.02 * rate));
    const auto oldE3 = Access::wave(performer->engine(), 2, false);
    const auto oldE4 = Access::wave(performer->engine(), 5, false);
    const auto oldE4Pluck = Access::voice(performer->engine(), 5).lastPluckSample;
    group(*performer, aMinor, true);
    expect(performer->engine().heldString(52) == 2
               && performer->engine().heldString(64) == 5,
           "same-plan E->Am moved a common tone off its continuing string");
    // The automatic return stroke reaches E4 immediately, so its fresh
    // release must change the wave while retaining its continuing phase.
    // E3 is still waiting for the pick and must keep its entire old wave.
    expect(Access::voice(performer->engine(), 2).pluckDelay > 0
               && Access::wave(performer->engine(), 2, false) == oldE3
               && Access::voice(performer->engine(), 5).pluckDelay == 0
               && Access::voice(performer->engine(), 5).lastPluckSample > oldE4Pluck
               && Access::wave(performer->engine(), 5, false).writeIndex == oldE4.writeIndex
               && !Access::voice(performer->engine(), 2).tailActive
               && !Access::voice(performer->engine(), 5).tailActive,
           "same-plan E->Am cleared a continuing wave or created a second branch");
    advance(*performer, static_cast<int>(0.06 * rate));
    for (const int note : aMinor)
    {
        const int string = performer->engine().heldString(note);
        expect(string >= 0, "same-plan E->Am lost a newly held note");
        if (string >= 0)
        {
            const auto& current = Access::voice(performer->engine(), string);
            expect(current.pluckDelay == 0 && current.lastPluckMidiNote == note,
                   "same-plan E->Am skipped a queued stroke");
        }
    }
    changedChordPreservesOldWave(rate, true);
}

Stereo ordinaryBlocks(double rate, int block, bool gather)
{
    auto performer = fresh(rate);
    performer->setGatherChords(gather);
    const int offAt = static_cast<int>(0.12 * rate) + 17;
    const int onAt = offAt + static_cast<int>(0.02 * rate);
    const int end = onAt + Performer::gatherWindowSamples(rate)
        + static_cast<int>(0.10 * rate);
    Stereo audio { std::vector<float>(static_cast<std::size_t>(end)),
                   std::vector<float>(static_cast<std::size_t>(end)) };
    for (int start = 0; start < end;)
    {
        const int count = std::min(block, end - start);
        performer->beginBlock(audio[0].data() + start, audio[1].data() + start, count);
        const auto feed = [&](int at, const Notes& notes, bool on)
        {
            if (at < start || at >= start + count)
                return;
            for (const int note : notes)
                if (on) performer->noteOn(at - start, 1, note, 92);
                else performer->noteOff(at - start, 1, note, 64);
        };
        feed(0, bMinor, true);
        feed(offAt, bMinor, false);
        feed(onAt, eMajor, true);
        performer->endBlock();
        start += count;
    }
    completeEChord(performer->engine(), true,
                   gather ? "gathered Bm->E" : "ordinary-block Bm->E");
    expect(performer->droppedEventCount() == 0,
           "the ordinary-block chord fixture dropped MIDI messages");
    return audio;
}
}

int main(int argc, char** argv)
{
    const bool controlsOnly = argc > 1 && std::string(argv[1]) == "--controls-only";
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        negativeControls(rate);
        if (controlsOnly)
            continue;
        changedChordPreservesOldWave(rate, false);
        changedChordBeforeOldPickArrives(rate);
        for (const bool gather : { false, true })
            expect(ordinaryBlocks(rate, 64, gather) == ordinaryBlocks(rate, 127, gather),
                   "common-tone refingering depended on block partition");
    }
    if (failures == 0)
        std::cout << "Chord-plan joins, retained waves and unchanged-join controls passed.\n";
    return failures == 0 ? 0 : 1;
}
