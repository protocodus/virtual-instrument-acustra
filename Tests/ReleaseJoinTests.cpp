// A 1/32-note key-up window preserves one ringing physical string while
// releasing MIDI ownership immediately. It never delays the next attack.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static const auto& voice(const AcustraEngine& e) { return e.voices_[0]; }
    static std::uint64_t clock(const AcustraEngine& e) { return e.sampleClock_; }
    static double tempo(const AcustraEngine& e) { return e.tempoBpm_; }
};
}

namespace
{
using Access = acustra::AcustraEngineTestAccess;
using Stereo = std::array<std::vector<float>, 2>;
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

int window(double rate, double bpm)
{
    return static_cast<int>(std::ceil(0.125L * (60.0L * rate) / bpm));
}

auto fresh(double rate = 48000.0, double bpm = 120.0)
{
    auto e = std::make_unique<acustra::AcustraEngine>();
    e->prepare(rate, 511);
    e->setTempoBpm(bpm);
    e->setStringPerChannelMode(true);
    acustra::EngineParameters p;
    p.releaseNoise = 0.6f;
    e->setParameters(p);
    return e;
}

void advance(acustra::AcustraEngine& e, int frames, int block = 127)
{
    std::array<float, 511> left {}, right {};
    while (frames > 0)
    {
        const int count = std::min(frames, block);
        e.process(left.data(), right.data(), count);
        expect(std::all_of(left.begin(), left.begin() + count, [](float x)
            { return std::isfinite(x) && std::abs(x) <= 1.0f; })
            && std::all_of(right.begin(), right.begin() + count, [](float x)
            { return std::isfinite(x) && std::abs(x) <= 1.0f; }),
            "release-join audio became nonfinite or unbounded");
        frames -= count;
    }
}

void exactMusicalDeadline()
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double bpm : { 74.0, 120.0, 240.0 })
            for (int block : { 1, 17, 127, 511 })
            {
                auto e = fresh(rate, bpm);
                e->noteOn(43, 0.8f, 1);
                advance(*e, static_cast<int>(0.03 * rate) + 19, block);
                const auto offAt = Access::clock(*e);
                e->noteOffWithVelocity(43, 1, 0.9f);
                const std::string at = " at " + std::to_string(rate)
                    + " Hz / " + std::to_string(bpm) + " BPM / block "
                    + std::to_string(block);
                const auto& v = Access::voice(*e);
                const int endpoint = window(rate, bpm);
                expect(!v.keyDown && v.ownerCount == 0 && e->heldString(43, 1) < 0,
                       "key-up retained MIDI ownership" + at);
                expect(v.releaseJoinPending && v.releaseSeconds == 0.0f
                           && v.releaseNoiseSamples == 0 && v.returnSamples == 0,
                       "physical damping/noise began before the joining window" + at);
                expect(v.releaseJoinAnchorSample == offAt
                           && v.releaseJoinWindowSamples == static_cast<std::uint64_t>(endpoint),
                       "1/32-note deadline did not follow the supplied tempo" + at);
                advance(*e, endpoint + 1, block); // Includes the endpoint sample.
                expect(v.releaseJoinPending && v.releaseSeconds == 0.0f
                           && v.releaseNoiseSamples == 0,
                       "inclusive endpoint was damped early" + at);
                advance(*e, 1, block);
                expect(!v.releaseJoinPending && v.releaseSeconds > 0.0f
                           && v.returnSamples > 0 && v.releaseNoiseSamples > 0,
                       "physical release did not begin on the first later sample" + at);
            }
}

Stereo repeat(double rate, double bpm, int block, int gap, bool keyUp,
              bool* releaseStartedAtReattack = nullptr)
{
    auto e = fresh(rate, bpm);
    const int warm = static_cast<int>(0.03 * rate) + 19;
    const int onset = warm + gap;
    const int length = onset + static_cast<int>(0.04 * rate) + 37;
    Stereo audio { std::vector<float>(length), std::vector<float>(length) };
    const auto render = [&](int first, int last)
    {
        while (first < last)
        {
            const int count = std::min(block, last - first);
            e->process(audio[0].data() + first, audio[1].data() + first, count);
            first += count;
        }
    };
    e->noteOn(43, 0.65f, 1);
    render(0, warm);
    if (keyUp) e->noteOff(43, 1);
    render(warm, onset);
    const auto releaseNoiseStateBefore = Access::voice(*e).releaseNoiseState;
    e->noteOn(43, 0.65f, 1);
    const auto& v = Access::voice(*e);
    if (releaseStartedAtReattack != nullptr)
        *releaseStartedAtReattack = v.releaseNoiseState != releaseNoiseStateBefore;
    expect(v.attackFired && v.pluckDelay == 0
               && v.lastPluckSample == static_cast<std::uint64_t>(onset),
           "joining moved a repeat's physical attack away from its MIDI sample");
    expect(!v.releaseJoinPending && v.keyDown && e->heldString(43, 1) == 0,
           "a reattack retained an old release deadline or lost ownership");
    render(onset, length);
    expect(v.keyDown && !v.releaseJoinPending && v.releaseSeconds == 0.0f,
           "an old key-up later released the joined note");
    return audio;
}

void cancellationAndBlockInvariance()
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double bpm : { 74.0, 120.0, 240.0 })
        {
            const int endpoint = window(rate, bpm);
            for (int gap : { 0, endpoint - 1, endpoint })
            {
                bool releaseStartedAtReattack = false;
                expect(repeat(rate, bpm, 127, gap, true, &releaseStartedAtReattack)
                           == repeat(rate, bpm, 127, gap, false),
                       "an eligible MIDI gap changed the ringing string or its new attack");
                expect(!releaseStartedAtReattack,
                       "an eligible reattack started a key-up contact before cancelling it");
            }
            // At the first outside callback no damped frame has elapsed yet.
            // The new pick stops the key-up drive, so PCM may still equal a
            // held repeat. The release contact's independent random stream
            // records that the overdue key-up was settled before the attack.
            bool releaseStartedAtReattack = false;
            repeat(rate, bpm, 127, endpoint + 1, true, &releaseStartedAtReattack);
            expect(releaseStartedAtReattack,
                   "the first outside-window reattack cancelled an already-due physical release");
            expect(repeat(rate, bpm, 127, endpoint + 2, true)
                       != repeat(rate, bpm, 127, endpoint + 2, false),
                   "an outside-window reattack erased a rendered damping contact");
        }
    const int endpoint = window(48000.0, 74.0);
    const auto sampleWise = repeat(48000.0, 74.0, 1, endpoint, true);
    for (int block : { 17, 127, 511 })
        expect(repeat(48000.0, 74.0, block, endpoint, true) == sampleWise,
               "joining depended on the host block partition");
    expect(acustra::AcustraEngine::outputLatencySamples() == 7,
           "the release window changed reported attack latency");
}

void tempoChangesAndFallback()
{
    auto e = fresh(48000.0, 74.0);
    e->noteOn(43, 0.8f, 1); advance(*e, 1500);
    e->noteOff(43, 1); advance(*e, 1731, 17);
    double remaining = static_cast<double>(0.125L
        - 1731.0L * (74.0L / 60.0L) / 48000.0L);
    e->setTempoBpm(240.0);
    const auto& v = Access::voice(*e);
    expect(v.releaseJoinWindowSamples == static_cast<std::uint64_t>(
               std::ceil(static_cast<long double>(remaining) * (60.0L * 48000.0L) / 240.0L)),
           "tempo change restarted the full grace instead of retiming its remaining beats");
    advance(*e, 241, 17);
    remaining = static_cast<double>(static_cast<long double>(remaining)
        - 241.0L * (240.0L / 60.0L) / 48000.0L);
    e->setTempoBpm(120.0);
    const int endpoint = static_cast<int>(std::ceil(
        static_cast<long double>(remaining) * (60.0L * 48000.0L) / 120.0L));
    expect(v.releaseJoinWindowSamples == static_cast<std::uint64_t>(endpoint),
           "a second tempo change used the previous segment's tempo");
    advance(*e, endpoint + 1, 511);
    expect(v.releaseJoinPending, "retimed inclusive endpoint expired early");
    advance(*e, 1);
    expect(!v.releaseJoinPending && v.releaseSeconds > 0.0f,
           "retimed release failed to start at its new deadline");
    e->setTempoBpm(30.0);
    expect(!v.releaseJoinPending, "slowing tempo revived an expired release");

    // This rational retiming lands exactly on an integer sample: rounding
    // the stored beat remainder must not add another whole sample.
    auto exact = fresh(44100.0, 60.0);
    exact->noteOn(43, 0.8f, 1); exact->noteOff(43, 1);
    advance(*exact, 55); exact->setTempoBpm(74.0);
    expect(Access::voice(*exact).releaseJoinWindowSamples == 4425,
           "tempo retiming rounded floating-point residue into an extra sample");

    for (double invalid : { 0.0, -1.0, std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN() })
    {
        auto fallback = fresh(48000.0, 74.0);
        fallback->setTempoBpm(invalid);
        fallback->noteOn(43, 0.8f, 1); fallback->noteOff(43, 1);
        expect(Access::voice(*fallback).releaseJoinWindowSamples == 3000,
               "invalid tempo did not use the documented 120 BPM fallback");
    }
    auto context = fresh(48000.0, 240.0);
    context->noteOn(43, 0.8f, 1); context->noteOff(43, 1);
    context->reset();
    expect(!Access::voice(*context).releaseJoinPending, "reset retained a release deadline");
    context->noteOn(43, 0.8f, 1); context->noteOff(43, 1);
    expect(Access::voice(*context).releaseJoinWindowSamples == 1500,
           "reset discarded the current host tempo");
    context->prepare(96000.0, 511);
    expect(!Access::voice(*context).releaseJoinPending, "prepare retained a release deadline");
    context->noteOn(43, 0.8f, 1); context->noteOff(43, 1);
    expect(Access::voice(*context).releaseJoinWindowSamples == 3000,
           "prepare discarded the host tempo or retained the old sample-rate deadline");
}

void ownershipControllersAndScheduling()
{
    auto e = fresh();
    e->noteOn(43, 0.8f, 1); e->noteOn(43, 0.8f, 1);
    e->noteOff(43, 1);
    expect(Access::voice(*e).keyDown && !Access::voice(*e).releaseJoinPending,
           "one owner's key-up scheduled release while another owner held the note");
    e->noteOff(43, 1);
    const auto anchor = Access::voice(*e).releaseJoinAnchorSample;
    advance(*e, 1000); e->noteOff(43, 1);
    expect(Access::voice(*e).releaseJoinAnchorSample == anchor,
           "an unmatched repeated key-up extended the deadline");
    e->noteOn(44, 0.8f, 1); advance(*e, 4000);
    expect(e->heldString(44, 1) == 0 && Access::voice(*e).keyDown
               && !Access::voice(*e).releaseJoinPending,
           "an old deadline released a different pitch assigned to that string");

    e = fresh(); e->setStringPerChannelMode(false);
    e->noteOn(40, 0.8f, 1); advance(*e, 1500); e->noteOff(40, 1);
    e->noteOn(40, 0.8f, 2); advance(*e, 4000);
    expect(e->heldString(40, 1) < 0 && e->heldString(40, 2) == 0
               && !Access::voice(*e).releaseJoinPending,
           "a release deadline crossed into a different MIDI channel's owner");

    e = fresh(); e->setSustainPedal(true, 1);
    e->noteOn(43, 0.8f, 1); advance(*e, 1500); e->noteOff(43, 1);
    expect(Access::voice(*e).pedalHeld && !Access::voice(*e).releaseJoinPending,
           "sustain-held key-up scheduled a damping grace");
    e->setSustainPedal(false, 1);
    expect(!Access::voice(*e).releaseJoinPending && Access::voice(*e).releaseSeconds > 0.0f,
           "pedal-up added a second joining window");
    e = fresh(); e->noteOn(43, 0.8f, 1); advance(*e, 1500); e->noteOff(43, 1);
    advance(*e, 1500); e->setSustainPedal(true, 1); advance(*e, 1502);
    expect(!Access::voice(*e).pedalHeld && !Access::voice(*e).releaseJoinPending
               && Access::voice(*e).releaseSeconds > 0.0f,
           "a late pedal press recaught the already released MIDI owner");

    e = fresh(); e->noteOn(43, 0.8f, 1); advance(*e, 1500); e->noteOff(43, 1);
    e->allNotesOff(1);
    expect(!Access::voice(*e).releaseJoinPending && Access::voice(*e).releaseSeconds > 0.0f,
           "All Notes Off did not flush the pending physical release");
    e = fresh(); e->noteOn(43, 0.8f, 1); advance(*e, 1500); e->noteOff(43, 1);
    e->setSustainPedal(true, 1); e->allNotesOff(1);
    expect(!Access::voice(*e).pedalHeld && !Access::voice(*e).releaseJoinPending
               && Access::voice(*e).releaseSeconds > 0.0f,
           "All Notes Off recaught a pending ownerless note after a late pedal press");
    auto withoutLatePedal = fresh();
    withoutLatePedal->noteOn(43, 0.8f, 1); advance(*withoutLatePedal, 1500);
    withoutLatePedal->noteOff(43, 1); withoutLatePedal->allNotesOff(1);
    std::array<float, 127> lateLeft {}, lateRight {}, plainLeft {}, plainRight {};
    for (int at = 0; at < 2000; at += 127)
    {
        const int count = std::min(127, 2000 - at);
        e->process(lateLeft.data(), lateRight.data(), count);
        withoutLatePedal->process(plainLeft.data(), plainRight.data(), count);
        expect(std::equal(lateLeft.begin(), lateLeft.begin() + count, plainLeft.begin())
                   && std::equal(lateRight.begin(), lateRight.begin() + count, plainRight.begin()),
               "late pedal plus All Notes Off changed an already-ownerless note's damping audio");
    }
    e = fresh(); e->setSustainPedal(true, 1); e->noteOn(43, 0.8f, 1);
    e->allNotesOff(1);
    expect(Access::voice(*e).pedalHeld && !Access::voice(*e).releaseJoinPending,
           "All Notes Off discarded the pedal of an ordinarily held note");
    e = fresh(); e->noteOn(43, 0.8f, 1); advance(*e, 1500); e->noteOff(43, 1);
    e->allSoundOff();
    expect(!Access::voice(*e).releaseJoinPending, "panic retained a release deadline");
    std::array<float, 127> left {}, right {};
    e->process(left.data(), right.data(), 127);
    expect(std::all_of(left.begin(), left.end(), [](float x) { return x == 0.0f; })
               && std::all_of(right.begin(), right.end(), [](float x) { return x == 0.0f; }),
           "panic allowed a deferred key-up to make sound");

    e = fresh(); e->noteOn(43, 0.8f, 1, 1000, false); e->noteOff(43, 1);
    advance(*e, 5000);
    expect(!Access::voice(*e).attackFired && Access::voice(*e).pluckDelay == 0,
           "grace revived an explicitly scheduled pluck cancelled before its attack");
    e = fresh(); e->beginStrum();
    e->noteOn(43, 0.8f, 1, 1000, true);
    // beginStrum draws one physical speed shared by the stroke; joining
    // must retain that scheduled sample rather than the unscaled request.
    const auto scheduledAt = Access::voice(*e).pluckDelay - 1;
    e->noteOff(43, 1);
    expect(Access::voice(*e).ownerCount == 0 && Access::voice(*e).releaseAfterPluck,
           "grace changed early strum key-up ownership");
    advance(*e, scheduledAt);
    expect(!Access::voice(*e).attackFired, "strummed attack fired before its scheduled sample");
    advance(*e, 1);
    expect(Access::voice(*e).attackFired
               && Access::voice(*e).lastPluckSample == static_cast<std::uint64_t>(scheduledAt)
               && Access::voice(*e).releaseJoinPending && !Access::voice(*e).keyDown
               && Access::voice(*e).releaseJoinAnchorSample == static_cast<std::uint64_t>(scheduledAt),
           "grace delayed or cancelled the scheduled strum's attack/key-up");
}

enum class PedalOffRoute { Ordinary, DeferredSameSample };

Stereo queuedPedalStroke(double rate, int releaseVelocity, bool pedalUpBeforePick,
                         PedalOffRoute route)
{
    auto e = fresh(rate);
    e->setSustainPedal(true, 1);
    e->beginStrum();
    e->noteOn(43, 0.8f, 1, static_cast<int>(rate / 48.0), true);
    const int pickAt = Access::voice(*e).pluckDelay - 1;
    const int pedalUpAt = pickAt + (pedalUpBeforePick ? -31 : 131);
    const int length = pickAt + static_cast<int>(0.04 * rate) + 137;
    Stereo audio { std::vector<float>(length), std::vector<float>(length) };
    int rendered = 0;
    const auto renderTo = [&](int last)
    {
        while (rendered < last)
        {
            const int count = std::min(127, last - rendered);
            e->process(audio[0].data() + rendered, audio[1].data() + rendered, count);
            rendered += count;
        }
    };
    const std::string at = " at " + std::to_string(rate) + " Hz / velocity "
        + std::to_string(releaseVelocity)
        + (pedalUpBeforePick ? " / pedal-up before pick" : " / pedal-up after pick")
        + (route == PedalOffRoute::Ordinary ? " / ordinary off" : " / deferred off");
    renderTo(100);
    if (route == PedalOffRoute::Ordinary)
    {
        if (releaseVelocity < 0)
            e->noteOff(43, 1);
        else
            e->noteOffWithVelocity(43, 1, static_cast<float>(releaseVelocity) / 127.0f);
        const auto& v = Access::voice(*e);
        expect(!v.keyDown && v.ownerCount == 0 && v.releaseAfterPluck
                   && v.pedalHeld && v.pedalHeldAtKeyUp && v.releaseVelocity == -1.0f
                   && !v.releaseJoinPending && v.releaseSeconds == 0.0f,
               "queued pedal-held key-up retained its earlier release velocity" + at);
    }
    if (!pedalUpBeforePick)
    {
        renderTo(pickAt + 1);
        const auto& v = Access::voice(*e);
        expect(v.attackFired && v.lastPluckSample == static_cast<std::uint64_t>(pickAt)
                   && !v.releaseJoinPending && v.releaseSeconds == 0.0f
                   && v.releaseNoiseSamples == 0,
               "a pedal-held queued stroke released at its pick" + at);
    }
    renderTo(pedalUpAt);
    e->setSustainPedal(false, 1);
    if (route == PedalOffRoute::DeferredSameSample)
    {
        // Performer defers the key-up until the sample's Note Ons are
        // resolved. The pedal-up has already reached Engine, and this
        // snapshot records that the original key-up was pedal-held.
        e->noteOffWithVelocity(43, 1, false,
            releaseVelocity < 0 ? -1.0f : static_cast<float>(releaseVelocity) / 127.0f,
            true);
    }
    if (pedalUpBeforePick)
    {
        const auto& v = Access::voice(*e);
        expect(!v.attackFired && !v.keyDown && v.ownerCount == 0
                   && v.releaseAfterPluck && v.pedalReleasedBeforePluck
                   && !v.pedalHeld && !v.pedalHeldAtKeyUp && v.releaseVelocity == -1.0f
                   && !v.releaseJoinPending && v.releaseSeconds == 0.0f,
               "pedal-up lost the nominal contact queued for the actual pick" + at);
        renderTo(pickAt);
        expect(!Access::voice(*e).attackFired,
               "queued pedal release moved the pick earlier" + at);
        renderTo(pickAt + 1);
    }
    const auto& v = Access::voice(*e);
    expect(v.attackFired && v.lastPluckSample == static_cast<std::uint64_t>(pickAt)
               && !v.keyDown && v.ownerCount == 0 && !v.pedalHeld
               && !v.releaseAfterPluck && !v.pedalReleasedBeforePluck
               && !v.releaseJoinPending && v.releaseSeconds == 0.16f
               && v.releaseVelocity == -1.0f && v.returnSamples > 0
               && v.releaseNoiseSamples > 0,
           "queued pedal-up did not start the nominal immediate damping contact" + at);
    renderTo(length);
    expect(std::any_of(audio[0].begin(), audio[0].end(), [](float x) { return x != 0.0f; }),
           "queued pedal-release audio comparison contained no physical attack" + at);
    return audio;
}

void queuedPedalReleaseIgnoresEarlierKeyVelocity()
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (bool pedalUpBeforePick : { true, false })
        {
            const auto nominal = queuedPedalStroke(rate, -1, pedalUpBeforePick,
                                                   PedalOffRoute::Ordinary);
            for (const auto route : { PedalOffRoute::Ordinary, PedalOffRoute::DeferredSameSample })
                for (int velocity : { 0, 64, 127 })
                    expect(queuedPedalStroke(rate, velocity, pedalUpBeforePick, route) == nominal,
                           "an earlier key-up velocity changed the queued pedal-release audio");
        }
}

Stereo performance(int block, bool keyUp)
{
    auto p = std::make_unique<acustra::Performer>();
    p->prepare(48000.0, 511); p->setTempoBpm(74.0);
    p->engine().setStringPerChannelMode(true);
    const int offAt = 4813, onAt = offAt + window(48000.0, 74.0);
    const int length = onAt + 2000;
    Stereo audio { std::vector<float>(length), std::vector<float>(length) };
    for (int first = 0; first < length; first += block)
    {
        const int count = std::min(block, length - first);
        p->beginBlock(audio[0].data() + first, audio[1].data() + first, count);
        if (first <= 17 && 17 < first + count) p->noteOn(17 - first, 1, 43, 83);
        if (keyUp && first <= offAt && offAt < first + count)
            p->noteOff(offAt - first, 1, 43, 64);
        if (first <= onAt && onAt < first + count) p->noteOn(onAt - first, 1, 43, 83);
        p->endBlock();
    }
    expect(Access::voice(p->engine()).lastPluckSample == static_cast<std::uint64_t>(onAt)
               && !Access::voice(p->engine()).releaseJoinPending,
           "Performer did not forward tempo or preserve the joined MIDI attack sample");
    expect(p->latencySamples() == acustra::AcustraEngine::outputLatencySamples()
               && p->droppedEventCount() == 0,
           "joining added player latency or lost MIDI events");
    return audio;
}

void performerTempoAndEvents()
{
    const auto baseline = performance(1, true);
    expect(baseline == performance(1, false),
           "Performer released a same-string repeat within the tempo-derived window");
    for (int block : { 17, 127, 511 })
        expect(performance(block, true) == baseline,
               "Performer joining changed for MIDI events inside host blocks");
}

Stereo splitPerformance(bool gather, bool split)
{
    auto p = std::make_unique<acustra::Performer>();
    p->prepare(48000.0, 128); p->setGatherChords(gather);
    p->engine().setStringPerChannelMode(true);
    constexpr int cut = 31, block = 128;
    constexpr float untouched = 0.123456f;
    const int gatherDelay = gather ? acustra::Performer::gatherWindowSamples(48000.0) : 0;
    const int length = 4224;
    Stereo audio { std::vector<float>(length, untouched), std::vector<float>(length, untouched) };
    p->beginBlock(audio[0].data(), audio[1].data(), block);
    p->setTempoBpmAt(5, 74.0);
    p->noteOn(7, 1, 43, 83);
    p->noteOff(17, 1, 43, 64);
    p->setTempoBpmAt(23, 240.0);
    if (split)
    {
        p->endBlockAt(cut);
        expect(Access::clock(p->engine()) == cut && Access::tempo(p->engine()) == 240.0,
               "ending a block prefix advanced the suffix clock or delayed host tempo");
        expect(std::all_of(audio[0].begin() + cut, audio[0].begin() + block,
                          [](float x) { return x == untouched; })
                   && std::all_of(audio[1].begin() + cut, audio[1].begin() + block,
                                  [](float x) { return x == untouched; }),
               "ending a block prefix wrote audio into the unrendered suffix");
        p->beginBlock(audio[0].data() + cut, audio[1].data() + cut, block - cut);
    }
    p->noteOn(47 - (split ? cut : 0), 1, 43, 83);
    p->noteOff(83 - (split ? cut : 0), 1, 43, 64);
    p->endBlock();
    expect(Access::clock(p->engine()) == block,
           "prefix/suffix processing rendered one portion of the host block twice");
    int first = block;
    const int inspectAt = block + gatherDelay;
    while (first < inspectAt)
    {
        const int count = std::min(block, inspectAt - first);
        p->process(audio[0].data() + first, audio[1].data() + first, count);
        first += count;
    }
    const auto& v = Access::voice(p->engine());
    expect(v.releaseJoinPending && v.releaseJoinAnchorSample == static_cast<std::uint64_t>(83 + gatherDelay)
               && v.releaseJoinWindowSamples == 1500
               && v.lastPluckSample == static_cast<std::uint64_t>(47 + gatherDelay),
           "splitting a block changed pending MIDI arrival time or its tempo-derived release deadline");
    while (first < length)
    {
        const int count = std::min(block, length - first);
        p->process(audio[0].data() + first, audio[1].data() + first, count);
        first += count;
    }
    expect(Access::clock(p->engine()) == static_cast<std::uint64_t>(length)
               && p->droppedEventCount() == 0,
           "split processing lost pending MIDI/tempo events or advanced the wrong clock");
    return audio;
}

void splitPrefixPreservesTempoAndFutureMidi()
{
    for (bool gather : { false, true })
        expect(splitPerformance(gather, true) == splitPerformance(gather, false),
               "block prefix/suffix processing changed audio with pending MIDI and tempo events");
}
}

int main()
{
    exactMusicalDeadline(); cancellationAndBlockInvariance();
    tempoChangesAndFallback(); ownershipControllersAndScheduling();
    queuedPedalReleaseIgnoresEarlierKeyVelocity();
    performerTempoAndEvents(); splitPrefixPreservesTempoAndFutureMidi();
    std::cout << "Release join failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
