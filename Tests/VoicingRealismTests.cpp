#include "DSP/AcustraEngine.h"

#include <array>
#include <iostream>
#include <memory>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static float gripCost(const AcustraEngine& engine,
                          const std::array<int, 6>& frets)
    {
        return engine.shapeCost(frets, 0u, engine.handWeights());
    }
    static bool impossible(const AcustraEngine& engine,
                           const std::array<int, 6>& frets)
    {
        return gripCost(engine, frets) >= AcustraEngine::impossibleShapeCost;
    }
    static std::array<std::uint32_t, 6> attackStates(const AcustraEngine& engine)
    {
        std::array<std::uint32_t, 6> result {};
        for (std::size_t string = 0; string < result.size(); ++string)
            result[string] = engine.voices_[string].randomState;
        return result;
    }
};
}

namespace
{
int failures = 0;
void expect(bool passes, const char* description)
{
    if (!passes)
    {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}
}

int main()
{
    using acustra::AcustraEngine;
    using Access = acustra::AcustraEngineTestAccess;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto engine = std::make_unique<AcustraEngine>();
        engine->prepare(rate, 64);
        expect(Access::impossible(*engine, { 1, 2, 3, 4, 5, 0 }),
               "five distinct stopped frets require more than four fingers");
        expect(!Access::impossible(*engine, { 1, 2, 3, 4, 4, 0 }),
               "adjacent equal-fret stops may share a finger");
        expect(Access::impossible(*engine, { 2, 0, 2, 3, 4, 5 }),
               "a barre cannot cross an intervening open string");
        expect(!Access::impossible(*engine, { 2, -1, 2, 3, 4, 5 }),
               "a muted string may lie under a barre");
        expect(!Access::impossible(*engine, { 2, 3, 2, 4, 4, 5 }),
               "a higher stopped string may sit above a barre");
        expect(!Access::impossible(*engine, { -1, 1, 3, 3, 3, 1 }),
               "the ordinary B-flat major barre must remain playable");

        // The former planner chose A13 D12 G10 B12 e11: the two fret-12
        // stops cannot share a barre across G10, making five independent
        // fingers despite only a three-fret span. E18 A17 D15 G16 B16
        // holds the same pitches with four fingers and the same span.
        constexpr std::array<int, 5> notes { 75, 71, 65, 62, 58 };
        constexpr std::array<int, 5> expected { 4, 3, 2, 1, 0 };
        engine->planChord(notes.data(), static_cast<int>(notes.size()), 1);
        for (std::size_t index = 0; index < notes.size(); ++index)
            expect(engine->plannedString(notes[index]) == expected[index],
                   "the chord planner chose a five-finger grip over a four-finger grip");
        for (const int note : notes)
            engine->noteOn(note, 0.7f);
        for (std::size_t index = 0; index < notes.size(); ++index)
            expect(engine->heldString(notes[index]) == expected[index],
                   "the planned physical-string voicing did not reach the attacks");

        // A chord whose keys arrive a sample apart is still forming: the grip
        // that fits one hand moves the note already heard and plucks it again
        // (the owner's choice, Docs/decisions.md, 2026-11-26). With attacks
        // kept, the rule of 2026-10-02 to 1.9.67, a better grip never
        // reattacks a live note already heard. Either way no held pitch drops.
        for (const bool movesSounded : { true, false })
        {
            auto live = std::make_unique<AcustraEngine>();
            acustra::PerformanceRealism realism {};
            realism.formingChordMovesSounded = movesSounded;
            live->setPerformanceRealism(realism);
            live->prepare(rate, 64);
            live->noteOn(60, 0.7f);
            const int originalString = live->heldString(60);
            const auto fired = Access::attackStates(*live);
            std::array<float, 64> left {}, right {};
            live->process(left.data(), right.data(), 1);
            live->noteOn(64, 0.7f);
            live->process(left.data(), right.data(), 1);
            live->noteOn(67, 0.7f);
            if (movesSounded)
            {
                const int moved = live->heldString(60);
                expect(moved >= 0 && moved != originalString,
                       "a forming live chord kept a grip no hand holds");
                expect(moved >= 0 && Access::attackStates(*live)[static_cast<std::size_t>(moved)]
                           != fired[static_cast<std::size_t>(moved)],
                       "a note moved by a forming live chord was not plucked on its new string");
            }
            else
            {
                expect(live->heldString(60) == originalString,
                       "with attacks kept, grip feasibility moved a sounded live-chord note");
                expect(Access::attackStates(*live)[static_cast<std::size_t>(originalString)]
                           == fired[static_cast<std::size_t>(originalString)],
                       "with attacks kept, grip feasibility reattacked a sounded live-chord note");
            }
            for (const int note : { 60, 64, 67 })
                expect(live->heldString(note) >= 0,
                       "an infeasible live grip dropped a held pitch instead of falling back");
        }
    }
    if (failures == 0)
        std::cout << "Four-finger voicing and live-chord grips passed.\n";
    return failures == 0 ? 0 : 1;
}
