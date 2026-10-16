// A crossed string's measured dead stroke is unchanged; its light contact
// survives only the originating complete chord's physical-key lifetimes.
#include "DSP/AcustraPerformer.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <vector>

namespace { std::atomic<long> allocations { 0 }; }
void* operator new(std::size_t size)
{
    ++allocations;
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc {};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace acustra
{
struct AcustraEngineTestAccess
{
    static const auto& voice(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)]; }
    static void replace(AcustraEngine& e, int string, int note)
    { e.startNote(string, 1, note, 0.8f, 1, 0, false); }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Performer = acustra::Performer;
using Access = acustra::AcustraEngineTestAccess;
int failures = 0;
void expect(bool pass, const char* message)
{ if (!pass) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }

std::unique_ptr<Performer> player(bool enabled = true, double rate = 48000.0,
    acustra::PickingTechnique picking = acustra::PickingTechnique::Pick,
    bool mutedStrings = true)
{
    auto p = std::make_unique<Performer>();
    acustra::PerformanceRealism realism;
    realism.heldShapeMute = enabled;
    realism.mutedStrings = mutedStrings;
    p->engine().setPerformanceRealism(realism);
    acustra::EngineParameters parameters;
    parameters.picking = picking;
    parameters.outputGain = 1.0f;
    parameters.piezoMix = 0.2f;
    parameters.room = 0.1f;
    parameters.releaseNoise = 0.1f;
    p->setParameters(parameters);
    p->prepare(rate, 127);
    p->setGatherChords(false);
    return p;
}

void advance(Engine& engine, int samples)
{
    std::array<float, 127> left {}, right {};
    while (samples > 0)
    {
        const int count = std::min(samples, 127);
        engine.process(left.data(), right.data(), count);
        for (int i = 0; i < count; ++i)
            expect(std::isfinite(left[i]) && std::isfinite(right[i]),
                   "nonfinite held-shape output");
        samples -= count;
    }
}

void chord(Performer& p, std::initializer_list<int> notes = {40, 57, 64},
           int channel = 1)
{
    float left = 0.0f, right = 0.0f;
    p.beginBlock(&left, &right, 1);
    for (int note : notes)
        p.noteOn(0, channel, note, 100);
    p.endBlock();
}

unsigned owners(const Engine& e, int channel = 1)
{
    unsigned mask = 0u;
    for (int note : {40, 57, 64})
    {
        const int string = e.heldString(note, channel);
        if (string >= 0)
            mask |= 1u << string;
    }
    return mask;
}

bool noCohorts(const Engine& e)
{
    for (int string = 0; string < 6; ++string)
        if (Access::voice(e, string).heldShapeOwners != 0u)
            return false;
    return true;
}

void lifetimeAndRates()
{
    for (double rate : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0})
    {
        auto p = player(true, rate);
        chord(*p);
        auto& e = p->engine();
        const unsigned cohort = owners(e);
        expect(cohort == ((1u << 0) | (1u << 3) | (1u << 5)),
               "public witness no longer occupies E/G/E strings");
        advance(e, static_cast<int>(0.7 * rate));
        const auto& mute = Access::voice(e, 1);
        expect(mute.mutedStroke && mute.played && !mute.keyDown
                   && mute.ownerCount == 0 && mute.heldShapeOwners == cohort
                   && mute.returnSamples == 1,
               "held shape lost its ownerless contact after the old deadline");
        expect(std::abs(mute.releaseSeconds - 43.0f / 110.0f) < 1.0e-5f,
               "held shape changed the measured damping law");
        e.noteOff(57);
        expect(mute.heldShapeOwners == (cohort & ~(1u << 3)),
               "partial release did not remove precisely its original owner");
        e.noteOn(57, 0.8f);
        expect(mute.heldShapeOwners == (cohort & ~(1u << 3)),
               "a later matching note rejoined an expired owner lifetime");
        e.noteOff(40);
        expect(mute.heldShapeOwners == (1u << 5), "outer held key lost its contact");
        e.noteOff(64);
        expect(mute.heldShapeOwners == 0u, "last original owner left a stuck contact");
        advance(e, 1);
        expect(!mute.mutedStroke && !mute.played,
               "expired contact did not return after its last owner lifted");
    }
}

void duplicateOwnersAndReplacement()
{
    auto p = player();
    chord(*p);
    auto& e = p->engine();
    const unsigned cohort = owners(e);
    e.noteOn(40, 0.8f); // same physical key/string, two MIDI owners
    expect(Access::voice(e, 1).heldShapeOwners == cohort,
           "same-note re-pluck lifted the original hand");
    e.noteOff(40);
    expect(Access::voice(e, 1).heldShapeOwners == cohort,
           "one duplicate key-up removed the surviving owner");
    e.noteOff(40);
    expect(Access::voice(e, 1).heldShapeOwners == (cohort & ~1u),
           "last duplicate key-up retained its cohort bit");
    // A forced allocator replacement exercises the same startNote hook that
    // a public voice steal uses, independent of the allocator's cost tuning.
    Access::replace(e, 3, 58);
    expect(Access::voice(e, 1).heldShapeOwners == (1u << 5),
           "a reassigned physical string remained in its old cohort");
    advance(e, 5000);
    expect(e.transitionNote(64, 65, 0.8f), "refret fixture was rejected");
    expect(noCohorts(e), "a connected refret retained the old key lifetime");

    auto target = player();
    chord(*target);
    target->engine().noteOn(45, 0.8f);
    const auto& a = Access::voice(target->engine(), 1);
    expect(a.keyDown && !a.mutedStroke && a.heldShapeOwners == 0u,
           "playing the muted string did not lift its touch");
}

void shapeAndBoundaries()
{
    for (int action = 0; action < 7; ++action)
    {
        auto p = player();
        chord(*p);
        auto& e = p->engine();
        advance(e, 34000);
        if (action == 0)
        {
            const int next[] {40, 45, 50, 55, 59, 64};
            e.planChord(next, 6);
        }
        else if (action == 1) e.allNotesOff();
        else if (action == 2) e.allSoundOff();
        else if (action == 3) e.reset();
        else if (action == 4) e.setLowerZoneMemberCount(5);
        else if (action == 5) e.setStringPerChannelMode(true);
        else e.setPerformanceRealism(e.performanceRealism());
        expect(noCohorts(e), "new shape or explicit boundary retained an old cohort");
    }

    auto p = player();
    chord(*p, {40, 57, 64}, 2);
    auto& e = p->engine();
    advance(e, 34000);
    const unsigned cohort = owners(e, 2);
    e.allSoundOff(1);
    expect(Access::voice(e, 1).heldShapeOwners == cohort,
           "channel-scoped panic lifted another channel's hand");
    e.setSustainPedal(true, 2);
    for (int note : {40, 57, 64}) e.noteOff(note, 2);
    expect(noCohorts(e), "pedal-only ownership invented an indefinite hand touch");
    advance(e, 1);
    expect(!Access::voice(e, 1).mutedStroke, "pedal kept the unused string damped");
    e.allSoundOff(2);
    std::array<float, 127> left {}, right {};
    e.process(left.data(), right.data(), 127);
    for (int i = 0; i < 127; ++i)
        expect(left[i] == 0.0f && right[i] == 0.0f, "panic did not produce exact silence");
}

void pendingAndRepeatedShapes()
{
    auto p = player();
    chord(*p);
    auto& e = p->engine();
    const unsigned cohort = owners(e);
    int queuedString = 1;
    for (int string : {2, 4})
        if (Access::voice(e,string).pluckDelay > Access::voice(e,queuedString).pluckDelay)
            queuedString = string;
    const auto firstOrder = Access::voice(e, queuedString).startOrder;
    const int pending = Access::voice(e, queuedString).pluckDelay;
    expect(pending > 1, "repeated-shape fixture has no pending dead stroke");
    chord(*p);
    expect(Access::voice(e, queuedString).heldShapeOwners == cohort,
           "repeated shape lost ownership of its pending dead stroke");
    expect(Access::voice(e, queuedString).startOrder == firstOrder
               && Access::voice(e, queuedString).pluckDelay == pending - 1,
           "repeated shape retriggered or retimed its pending dead stroke");
    // Both copies lift before the delayed picks arrive. Their firePluck
    // callbacks must never recreate the expired physical-key cohort.
    for (int repeat = 0; repeat < 2; ++repeat)
        for (int note : {40, 57, 64}) e.noteOff(note);
    expect(noCohorts(e), "pending key-up left physical hand ownership");
    advance(e, 34000);
    expect(noCohorts(e) && !Access::voice(e,1).mutedStroke,
           "a delayed dead stroke revived a released hand shape");

    auto replacement = player();
    chord(*replacement);
    chord(*replacement, {40, 59, 64});
    auto& changed = replacement->engine();
    unsigned next = 0u;
    for (int note : {40, 59, 64}) next |= 1u << changed.heldString(note);
    expect(Access::voice(changed,1).heldShapeOwners == next,
           "second public gapped chord did not replace the contact cohort");
    for (int note : {40, 57, 64}) changed.noteOff(note);
    advance(changed, 34000);
    expect(Access::voice(changed,1).heldShapeOwners == next
               && Access::voice(changed,1).mutedStroke,
           "old shape's releases lifted the replacement shape's hand");
    for (int note : {40, 59, 64}) changed.noteOff(note);
    advance(changed,1);
    expect(noCohorts(changed) && !Access::voice(changed,1).mutedStroke,
           "replacement shape did not release its own final contact");

    auto validation = player();
    auto& empty = validation->engine();
    empty.muteStroke(1, 0, 0.8f, 1, 63u);
    expect(noCohorts(empty), "requested but nonexistent owners were accepted");

    for (bool controller : {false, true})
    {
        auto unplanned = player();
        if (controller) unplanned->engine().setStringPerChannelMode(true);
        else unplanned->engine().setLowerZoneMemberCount(5);
        chord(*unplanned, {40,57,64}, 2);
        expect(noCohorts(unplanned->engine()),
               "an unplanned controller/MPE group acquired a shape contact");
    }
}

struct Event { int at, note, velocity; };
std::vector<float> render(bool enabled, const std::vector<Event>& score,
    int length = 48000, int block = 127,
    acustra::PickingTechnique picking = acustra::PickingTechnique::Pick,
    bool mutedStrings = true)
{
    auto p = player(enabled, 48000.0, picking, mutedStrings);
    std::vector<float> out(static_cast<std::size_t>(length * 2));
    std::array<float, 127> left {}, right {};
    std::size_t next = 0;
    const long allocationsBefore = allocations.load();
    for (int at = 0; at < length; at += block)
    {
        const int count = std::min(block, length - at);
        p->beginBlock(left.data(), right.data(), count);
        while (next < score.size() && score[next].at < at + count)
        {
            const auto& event = score[next++];
            if (event.velocity > 0) p->noteOn(event.at - at, 1, event.note, event.velocity);
            else p->noteOff(event.at - at, 1, event.note);
        }
        p->endBlock();
        for (int i = 0; i < count; ++i)
        {
            out[static_cast<std::size_t>(2 * (at + i))] = left[i];
            out[static_cast<std::size_t>(2 * (at + i) + 1)] = right[i];
        }
    }
    expect(allocations.load() == allocationsBefore,
           "public held-shape event/render path allocated");
    return out;
}

void noOpsAndPublicWitness()
{
    const std::vector<Event> single {{2400, 45, 100}, {12000, 45, 0}};
    const std::vector<Event> contiguous {{2400,40,100},{2400,45,100},{2400,50,100}};
    const std::vector<Event> sparse {{2400,40,100},{2400,57,100},{2400,64,100}};
    expect(render(false, single) == render(true, single), "unrelated single-note score changed");
    expect(render(false, contiguous) == render(true, contiguous), "contiguous chord changed");
    expect(render(false, sparse, 48000, 127, acustra::PickingTechnique::Finger)
        == render(true, sparse, 48000, 127, acustra::PickingTechnique::Finger),
        "independent finger pinch acquired a swept-string mute");
    expect(render(false, sparse, 48000, 127, acustra::PickingTechnique::Pick, false)
        == render(true, sparse, 48000, 127, acustra::PickingTechnique::Pick, false),
        "disabled muted strings changed audio");
    // No new source/force: the onset and measured decay are bit-identical
    // until the first formerly retired crossed string would be handed back.
    expect(render(false, sparse, 12000) == render(true, sparse, 12000),
           "held contact changed an attack before any old retirement deadline");
    auto standalone = player();
    standalone->engine().muteStroke(1, 0, 0.8f);
    advance(standalone->engine(), 30000);
    expect(!Access::voice(standalone->engine(),1).played && noCohorts(standalone->engine()),
           "standalone dead stroke without a cohort became persistent");
    auto score = sparse;
    for (int at : {38400,62400,86400})
    { score.push_back({at,57,0}); score.push_back({at+2400,57,100}); }
    const auto before = render(false, score, 144000);
    const auto after = render(true, score, 144000);
    expect(after == render(true, score, 144000, 64), "held contact depends on host block size");
    double reference = 0.0, difference = 0.0;
    for (std::size_t i = 67200; i < after.size(); ++i)
    {
        const double delta = static_cast<double>(after[i]) - before[i];
        difference += delta * delta;
        reference += static_cast<double>(before[i]) * before[i];
    }
    const double relativeDb = 10.0 * std::log10(difference / reference);
    std::cout << "Held E2/A3/E4 score, 0.7-3 s: difference " << relativeDb
              << " dB relative, " << 10.0 * std::log10(difference / (after.size()-67200))
              << " dBFS (common gain, no normalization)\n";
    expect(relativeDb > -25.0 && relativeDb < 0.0,
           "public score no longer exposes the unused open A ringing difference");
}
}

int main()
{
    lifetimeAndRates();
    duplicateOwnersAndReplacement();
    shapeAndBoundaries();
    pendingAndRepeatedShapes();
    noOpsAndPublicWitness();
    if (failures == 0) std::cout << "Held-shape mute contracts passed\n";
    return failures == 0 ? 0 : 1;
}
