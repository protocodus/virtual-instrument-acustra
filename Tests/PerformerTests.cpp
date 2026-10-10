// The player (DSP/AcustraPerformer) without JUCE: the performance battery
// through handleMidi, the MIDI helpers a front end without MIDI uses, the
// Gather Chords window, overflow accounting and real-time safety.
#include "DSP/AcustraPerformer.h"
#include "PerformanceBattery.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <vector>

// Counts every allocation, so a test can prove the player makes none while
// it plays.
namespace
{
std::atomic<long> allocationCount { 0 };
}

void* operator new(std::size_t size)
{
    ++allocationCount;
    if (void* pointer = std::malloc(size == 0 ? 1 : size))
        return pointer;
    throw std::bad_alloc {};
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace acustra
{
struct AcustraEngineTestAccess
{
    static int pluckDelay(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)].pluckDelay;
    }
    static int owners(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)].ownerCount;
    }
    static std::array<std::uint32_t, AcustraEngine::stringCount> attackStates(
        const AcustraEngine& engine)
    {
        std::array<std::uint32_t, AcustraEngine::stringCount> states {};
        for (std::size_t string = 0; string < states.size(); ++string)
            states[string] = engine.voices_[string].randomState;
        return states;
    }
};
} // namespace acustra

namespace
{
using acustra::Performer;
using namespace acustra::battery;

int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (! condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// How the events reach the player: as raw MIDI bytes, or spelt through the
// helpers a front end without MIDI calls.
enum class Feed { Raw, Helpers };

void feed(Performer& performer, int offset, const Event& event, Feed how)
{
    const auto kind = event.size > 0 ? event.bytes[0] & 0xf0u : 0u;
    const int channel = static_cast<int>(event.bytes[0] & 0x0fu) + 1;
    if (how == Feed::Helpers && event.size == 3 && kind == 0x90u)
        performer.noteOn(offset, channel, event.bytes[1], event.bytes[2]);
    else if (how == Feed::Helpers && event.size == 3 && kind == 0x80u)
        performer.noteOff(offset, channel, event.bytes[1], event.bytes[2]);
    else if (how == Feed::Helpers && event.size == 3 && kind == 0xb0u)
        performer.controlChange(offset, channel, event.bytes[1], event.bytes[2]);
    else if (how == Feed::Helpers && event.size == 2 && kind == 0xd0u)
        performer.channelPressure(offset, channel, event.bytes[1]);
    else if (how == Feed::Helpers && event.size == 3 && kind == 0xe0u)
    {
        const int raw = event.bytes[1] | (event.bytes[2] << 7);
        performer.pitchWheel(offset, channel,
                             raw < 8192 ? static_cast<float>(raw - 8192) / 8192.0f
                                        : static_cast<float>(raw - 8192) / 8191.0f);
    }
    else
        performer.handleMidi(offset, event.bytes.data(), event.size);
}

void applyControl(acustra::EngineParameters& parameters, bool& gather,
                  bool& panic, const Control& control)
{
    using Kind = Control::Kind;
    const int index = static_cast<int>(control.value);
    switch (control.kind)
    {
        case Kind::GatherChords: gather = control.value >= 0.5f; break;
        case Kind::Panic: panic = true; break;
        case Kind::CaptureMode:
            parameters.capture = index == 0 ? acustra::CaptureType::StereoMic
                : index == 1 ? acustra::CaptureType::MonoMic
                             : acustra::CaptureType::Piezo;
            break;
        case Kind::Picking:
            parameters.picking = static_cast<acustra::PickingTechnique>(index);
            break;
        case Kind::Tuning: parameters.tuning = static_cast<acustra::Tuning>(index); break;
        case Kind::BodyAmount: parameters.bodyAmount = 0.01f * control.value; break;
        case Kind::Output:
            parameters.outputGain = std::pow(10.0f, 0.05f * control.value);
            break;
        case Kind::Shape: parameters.shape = static_cast<acustra::BodyShape>(index); break;
        case Kind::Wood:
            parameters.bodyMaterial = static_cast<acustra::BodyMaterial>(index);
            break;
        case Kind::Width: parameters.stereoWidth = 0.01f * control.value; break;
        case Kind::Age: parameters.stringAge = 0.01f * control.value; break;
        case Kind::Pluck: parameters.pluckPosition = 0.01f * control.value; break;
        case Kind::Touch: parameters.touch = 0.01f * control.value; break;
        case Kind::PiezoMix: parameters.piezoMix = 0.01f * control.value; break;
    }
}

struct Render
{
    std::vector<float> left;
    std::vector<float> right;
    int latency { 0 };
    std::uint32_t dropped { 0 };
    long allocations { 0 };

    bool operator==(const Render& other) const
    {
        return left == other.left && right == other.right;
    }
    bool operator!=(const Render& other) const { return ! (*this == other); }
};

// Plays a scenario as the plug-in does: controls before the block that
// contains them, then the block's events in time order (a stable sort, as a
// MIDI buffer keeps them) at their offsets.
Render render(const Scenario& scenario, double sampleRate, int blockSize,
              bool gather, Feed how = Feed::Raw)
{
    auto performer = std::make_unique<Performer>();
    acustra::EngineParameters parameters;
    performer->setParameters(parameters);
    performer->prepare(sampleRate, blockSize);
    performer->engine().setPortObserversEnabled(false);

    const int length = sampleAt(scenario.seconds, sampleRate);
    Render result;
    result.left.assign(static_cast<std::size_t>(length + blockSize), 0.0f);
    result.right.assign(static_cast<std::size_t>(length + blockSize), 0.0f);
    std::vector<bool> applied(scenario.controls.size(), false);
    std::vector<std::pair<int, const Event*>> block;
    block.reserve(scenario.events.size());

    const long allocationsBefore = allocationCount.load();
    for (int start = 0; start < length; start += blockSize)
    {
        bool panic = false;
        for (std::size_t index = 0; index < scenario.controls.size(); ++index)
            if (! applied[index]
                && sampleAt(scenario.controls[index].seconds, sampleRate)
                       < start + blockSize)
            {
                applyControl(parameters, gather, panic, scenario.controls[index]);
                applied[index] = true;
            }
        block.clear();
        for (const auto& event : scenario.events)
        {
            const int at = sampleAt(event.seconds, sampleRate);
            if (at >= start && at < start + blockSize)
                block.emplace_back(at - start + event.skew, &event);
        }
        // A stable insertion sort: std::stable_sort would allocate inside
        // the loop the allocation count watches.
        for (std::size_t index = 1; index < block.size(); ++index)
            for (std::size_t at = index;
                 at > 0 && block[at].first < block[at - 1].first; --at)
                std::swap(block[at], block[at - 1]);

        performer->setParameters(parameters);
        if (panic)
            performer->reset();
        performer->setGatherChords(gather);
        result.latency = std::max(result.latency, performer->latencySamples());
        performer->beginBlock(result.left.data() + start,
                              result.right.data() + start, blockSize);
        for (const auto& [offset, event] : block)
            feed(*performer, offset, *event, how);
        performer->endBlock();
    }
    result.allocations = allocationCount.load() - allocationsBefore;
    result.left.resize(static_cast<std::size_t>(length));
    result.right.resize(static_cast<std::size_t>(length));
    result.dropped = performer->droppedEventCount();
    return result;
}

float peak(const Render& render)
{
    float result = 0.0f;
    for (const auto* channel : { &render.left, &render.right })
        for (const float value : *channel)
            result = std::max(result, std::abs(value));
    return result;
}

bool finite(const Render& render)
{
    for (const auto* channel : { &render.left, &render.right })
        for (const float value : *channel)
            if (! std::isfinite(value))
                return false;
    return true;
}

const Scenario& scenarioNamed(const std::vector<Scenario>& battery,
                              const std::string& name)
{
    for (const auto& scenario : battery)
        if (name == scenario.name)
            return scenario;
    std::cerr << "no scenario " << name << '\n';
    std::abort();
}

void testBatteryPlaysSoundsAndRepeats(const std::vector<Scenario>& battery)
{
    for (const auto& scenario : battery)
        for (const bool gather : { false, true })
        {
            const std::string label = std::string { scenario.name }
                + (gather ? " (gathering)" : "");
            const auto first = render(scenario, 48000.0, 64, gather);
            expect(finite(first), label + " rendered a nonfinite sample");
            expect(peak(first) > 1.0e-3f, label + " rendered silence");
            expect(first.allocations == 0,
                   label + " allocated while playing ("
                       + std::to_string(first.allocations) + ")");
            expect(render(scenario, 48000.0, 64, gather) == first,
                   label + " did not repeat to the bit");
            expect(render(scenario, 48000.0, 64, gather, Feed::Helpers) == first,
                   label + " sounded differently spelt through the helpers");
        }
}

// The player is sample-accurate: only front-end controls, which land
// between blocks, and events a host places outside their block hear the
// block size.
void testBlockSizeDoesNotChangeThePerformance(const std::vector<Scenario>& battery)
{
    for (const auto& scenario : battery)
    {
        if (! scenario.controls.empty()
            || std::any_of(scenario.events.begin(), scenario.events.end(),
                           [](const Event& event) { return event.skew != 0; }))
            continue;
        for (const bool gather : { false, true })
        {
            const auto reference = render(scenario, 44100.0, 64, gather);
            for (const int blockSize : { 1, 127, 512 })
                expect(render(scenario, 44100.0, blockSize, gather) == reference,
                       std::string { scenario.name } + (gather ? " (gathering)" : "")
                           + " changed with block size "
                           + std::to_string(blockSize));
        }
    }
}

Scenario custom(const char* name, double seconds, std::vector<Event> events)
{
    Scenario scenario;
    scenario.name = name;
    scenario.seconds = seconds;
    scenario.events = std::move(events);
    return scenario;
}

Event message(double seconds, std::uint8_t status, int data1, int data2,
              int size = 3)
{
    Event event;
    event.seconds = seconds;
    event.bytes[0] = status;
    event.bytes[1] = static_cast<std::uint8_t>(data1);
    event.bytes[2] = static_cast<std::uint8_t>(data2);
    event.size = size;
    return event;
}

void testLiveRollKeepsItsOriginalAttacks()
{
    using Access = acustra::AcustraEngineTestAccess;
    const std::vector<Event> events {
        message(0.010, 0x90, 60, 100), message(0.020, 0x90, 64, 100),
        message(0.030, 0x90, 67, 100) };
    const auto roll = custom("committed roll", 0.09, events);
    const auto lone = custom("first rolled note", 0.09, { events.front() });
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto reference = render(roll, rate, 1, false);
        const auto first = render(lone, rate, 1, false);
        const auto prefix = static_cast<std::size_t>(sampleAt(0.020, rate));
        expect(std::equal(first.left.begin(), first.left.begin() + prefix,
                          reference.left.begin())
                   && std::equal(first.right.begin(), first.right.begin() + prefix,
                                 reference.right.begin()),
               "a future rolled key changed the first attack or added look-ahead");
        expect(std::any_of(reference.left.begin(), reference.left.begin() + prefix,
                          [](float sample) { return std::abs(sample) > 1e-6f; }),
               "a live roll waited for later pitches before sounding its first key");

        // The per-string pluck generator is advanced by a release, never by
        // ongoing burst noise. An isolated open-string release gives the
        // state after one attack on that string, independently of chord
        // reshaping. It detects hidden second attacks even in a large block
        // containing all three onsets.
        auto fresh = std::make_unique<acustra::AcustraEngine>();
        fresh->prepare(rate, 64);
        const auto initial = Access::attackStates(*fresh);
        auto expected = initial;
        constexpr std::array<int, 6> open { 40, 45, 50, 55, 59, 64 };
        for (const int string : { 3, 4, 5 })
        {
            auto isolated = std::make_unique<acustra::AcustraEngine>();
            isolated->prepare(rate, 64);
            isolated->noteOn(open[static_cast<std::size_t>(string)], 100.0f / 127.0f);
            expected[static_cast<std::size_t>(string)]
                = Access::attackStates(*isolated)[static_cast<std::size_t>(string)];
        }
        for (const int frames : { 1, 64, 4096 })
        {
            const auto audio = render(roll, rate, frames, false);
            expect(audio == reference,
                   "a committed roll changed with block size " + std::to_string(frames));
            expect(audio.latency == acustra::AcustraEngine::outputLatencySamples(),
                   "committing a sounded string added player latency");
            auto performer = std::make_unique<Performer>();
            performer->prepare(rate, frames);
            std::vector<float> left(static_cast<std::size_t>(frames)), right(left.size());
            const int length = sampleAt(0.040, rate);
            for (int start = 0; start < length; start += frames)
            {
                const int count = std::min(frames, length - start);
                performer->beginBlock(left.data(), right.data(), count);
                for (const auto& event : events)
                {
                    const int at = sampleAt(event.seconds, rate);
                    if (at >= start && at < start + count)
                        performer->handleMidi(at - start, event.bytes.data(), event.size);
                }
                performer->endBlock();
            }
            expect(performer->engine().heldString(60) == 4
                       && performer->engine().heldString(64) == 5
                       && performer->engine().heldString(67) == 3,
                   "a later live key moved a committed physical string");
            expect(Access::attackStates(performer->engine()) == expected,
                   "three live keys created more than three releases");
            for (const int string : { 3, 4, 5 })
                expect(Access::owners(performer->engine(), string) == 1,
                       "a live chord lost a sounded key's ownership");
        }
    }
}

// The two-second rest that starts strumming over on a downstroke is
// measured between the strums' own samples, so two strums just inside or
// just outside it alternate, or not, at every block size (audit F23): the
// rest used to be read between the starts of the blocks they fell in.
void testStrumRestIsMeasuredBetweenStrums()
{
    for (const double gap : { 1.99, 2.01 })
        for (const bool gather : { false, true })
        {
            std::vector<Event> events;
            for (const double at : { 0.08, 0.08 + gap })
                for (const int note : { 40, 45, 50, 55, 59, 64 })
                    events.push_back(message(at, 0x90, note, 100));
            const auto strums = custom("strums", gap + 0.4, events);
            const auto reference = render(strums, 48000.0, 1, gather);
            for (const int blockSize : { 64, 4096 })
                expect(render(strums, 48000.0, blockSize, gather) == reference,
                       "strums " + std::to_string(gap) + " s apart"
                           + (gather ? " (gathering)" : "")
                           + " changed with block size " + std::to_string(blockSize));
        }
}

// One sample's notes are one canonical wrist event whatever order a host
// stored them in; a zero-length note does not stick; All Notes/Sound Off
// acts before its sample's notes, whether a host inserted it before or
// after them: it ends what was already sounding, and the notes that start
// on its sample, a loop's first beat after a host's reset, still sound
// (audit F41).
void testOneSampleIsOneCanonicalEvent()
{
    const auto chord = [](std::array<int, 4> order)
    {
        std::vector<Event> events;
        for (const int note : order)
            events.push_back(message(0.01, 0x90, note, 100));
        return custom("chord", 0.5, events);
    };
    const auto lowFirst = render(chord({ 40, 47, 52, 56 }), 48000.0, 64, false);
    expect(lowFirst == render(chord({ 56, 40, 52, 47 }), 48000.0, 64, false)
               && lowFirst == render(chord({ 52, 56, 47, 40 }), 48000.0, 64, false),
           "a same-sample chord depended on its insertion order");

    // Which of these keys are down after one sample's events.
    const auto keysDown = [](std::vector<Event> events)
    {
        auto performer = std::make_unique<Performer>();
        performer->prepare(48000.0, 256);
        std::vector<float> left(256), right(256);
        performer->beginBlock(left.data(), right.data(), 256);
        for (const auto& event : events)
            performer->handleMidi(10, event.bytes.data(), event.size);
        performer->endBlock();
        return std::pair { performer->engine().heldString(45) >= 0,
                           performer->engine().heldString(52) >= 0 };
    };
    expect(keysDown({ message(0.0, 0x80, 45, 64), message(0.0, 0x90, 45, 100) })
               == std::pair { false, false },
           "a zero-length note stuck when its Off came first");
    for (const int controller : { 120, 123 })
        expect(keysDown({ message(0.0, 0x90, 45, 100), message(0.0, 0xb0, controller, 0),
                          message(0.0, 0x90, 52, 100) })
                   == std::pair { true, true },
               "CC" + std::to_string(controller)
                   + " cancelled a Note On on its own sample");

    // Before or after the notes on its sample, the reset sounds the same:
    // the held note it ends, then the new one.
    for (const int controller : { 120, 123 })
    {
        const auto held = message(0.0, 0x90, 45, 100);
        const auto reset = message(0.5, 0xb0, controller, 0);
        const auto next = message(0.5, 0x90, 52, 100);
        const auto resetFirst = render(custom("reset", 1.0, { held, reset, next }),
                                       48000.0, 64, false);
        expect(resetFirst == render(custom("reset", 1.0, { held, next, reset }),
                                    48000.0, 64, false),
               "CC" + std::to_string(controller)
                   + " depended on its place among its sample's notes");
        expect(resetFirst != render(custom("reset", 1.0, { held, reset }),
                                    48000.0, 64, false),
               "the note on CC" + std::to_string(controller) + "'s sample was silent");
    }
}

// A strum is timed by the strings it reaches: a note the tuning cannot
// sound (C2 below Standard's low E, with no harmonic to reach it) takes no
// place in the stroke and no part in its speed, so the stroke starts on its
// first real string at once instead of a string spacing late (audit F24).
void testStrumsAreTimedByTheNotesThatSound()
{
    const auto firstSound = [](std::vector<int> notes, std::vector<int> velocities)
    {
        std::vector<Event> events;
        for (std::size_t index = 0; index < notes.size(); ++index)
            events.push_back(message(0.01, 0x90, notes[index], velocities[index]));
        const auto audio = render(custom("strum", 0.2, events), 48000.0, 64, false);
        std::size_t first = 0;
        while (first < audio.left.size() && std::abs(audio.left[first]) < 1.0e-6f
               && std::abs(audio.right[first]) < 1.0e-6f)
            ++first;
        return std::pair { first, audio };
    };
    const auto [withLowC, lowCAudio] = firstSound({ 36, 40, 43 }, { 40, 40, 40 });
    const auto [reachable, reachableAudio] = firstSound({ 40, 45, 50 }, { 40, 40, 40 });
    expect(withLowC == reachable,
           "a strum with an unreachable C2 started "
               + std::to_string(static_cast<long>(withLowC) - static_cast<long>(reachable))
               + " samples late");
    expect(firstSound({ 36, 40, 43 }, { 127, 40, 40 }).second == lowCAudio,
           "an unreachable note's velocity changed the strum's speed");
}

// A pick traverses physical strings, including the gaps between strings it
// does not strike. In a crossed C4-E4-G4 voicing E4 is on the high E while G4
// is on B: sorting the pitches used to reach E before B. An open E2-B3-E4
// stroke skips A/D/G, whose distance used to disappear from the timing.
void testStrumsFollowPhysicalStrings()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (const double sampleRate : { 44100.0, 48000.0, 96000.0 })
        for (const bool sparse : { false, true })
        {
            const std::array<int, 3> notes = sparse
                ? std::array<int, 3> { 40, 59, 64 }
                : std::array<int, 3> { 60, 67, 64 };
            const std::array<int, 3> expectedStrings = sparse
                ? std::array<int, 3> { 0, 4, 5 }
                : std::array<int, 3> { 3, 4, 5 };
            auto performer = std::make_unique<Performer>();
            performer->prepare(sampleRate, 64);
            std::array<float, 64> left {}, right {};
            const auto stroke = [&]
            {
                performer->beginBlock(left.data(), right.data(), 1);
                for (const int note : notes)
                    performer->noteOn(0, 1, note, 100);
                performer->endBlock();
                std::array<int, 3> delay {};
                for (std::size_t index = 0; index < notes.size(); ++index)
                {
                    const int string = performer->engine().heldString(notes[index]);
                    expect(string == expectedStrings[index],
                           "the physical-strum fixture changed string assignment");
                    // After the first rendered sample this is the number of
                    // samples left before that string's pick arrives.
                    delay[index] = string >= 0
                        ? Access::pluckDelay(performer->engine(), string) : -1;
                }
                return delay;
            };
            const auto down = stroke();
            expect(down[0] == 0 && down[1] > 0 && down[2] > down[1],
                   "a downstroke did not traverse low to high physical strings");
            if (sparse)
                expect(std::abs(5 * down[1] - 4 * down[2]) <= 8,
                       "a downstroke did not cross the three skipped strings");
            else
                expect(std::abs(2 * down[1] - down[2]) <= 3,
                       "adjacent strings did not retain one shared pick speed");

            for (int start = 0; start < sampleAt(0.2, sampleRate); start += 64)
                performer->process(left.data(), right.data(), 64);
            const auto up = stroke();
            expect(up[2] == 0 && up[1] > 0 && up[0] > up[1],
                   "the returning upstroke did not reverse physical string order");
            if (sparse)
                expect(std::abs(5 * up[1] - up[0]) <= 8,
                       "an upstroke did not cross the three skipped strings");
            else
                expect(std::abs(2 * up[1] - up[0]) <= 3,
                       "the upstroke lost its shared speed between adjacent strings");
        }

    // Canonical note insertion and block boundaries cannot change either
    // stroke. Test both the ordinary group and Gather Chords' delayed group.
    for (const auto notes : { std::array<int, 3> { 60, 64, 67 },
                              std::array<int, 3> { 40, 59, 64 } })
        for (const bool gather : { false, true })
        {
            const auto phrase = [&](bool reverse)
            {
                std::vector<Event> events;
                for (const double at : { 0.01, 0.15 })
                    for (std::size_t index = 0; index < notes.size(); ++index)
                        events.push_back(message(at, 0x90,
                            notes[reverse ? notes.size() - 1 - index : index], 100));
                return custom("physical strings", 0.3, events);
            };
            const auto reference = render(phrase(false), 48000.0, 1, gather);
            for (const int blockSize : { 64, 4096 })
            {
                const auto audio = render(phrase(true), 48000.0, blockSize, gather);
                expect(audio == reference,
                       "physical strum timing changed with insertion order/block size");
                expect(audio.latency == acustra::AcustraEngine::outputLatencySamples()
                           + (gather ? Performer::gatherWindowSamples(48000.0) : 0),
                       "physical strum timing added host latency");
            }
        }
}

// Planning filters unreachable notes and coalesces duplicated keys without
// losing their owners. Where no complete assignment exists (harmonics or an
// MPE member), preserve the previous note-by-note schedule and allocation.
void testPhysicalStrumPlanningKeepsSpecialNotes()
{
    using Access = acustra::AcustraEngineTestAccess;
    const auto delays = [](const std::vector<int>& notes, int channel = 1,
                           bool mpe = false)
    {
        auto performer = std::make_unique<Performer>();
        performer->prepare(48000.0, 64);
        std::array<float, 64> left {}, right {};
        performer->beginBlock(left.data(), right.data(), 1);
        if (mpe)
        {
            performer->controlChange(0, 1, 101, 0);
            performer->controlChange(0, 1, 100, 6);
            performer->controlChange(0, 1, 6, 1);
        }
        for (const int note : notes)
            performer->noteOn(0, channel, note, 100);
        performer->endBlock();
        std::vector<int> result;
        for (const int note : notes)
        {
            const int string = performer->engine().heldString(note, channel);
            result.push_back(string >= 0
                ? Access::pluckDelay(performer->engine(), string) : -1);
            if (note == 59 && std::count(notes.begin(), notes.end(), note) > 1)
                expect(string >= 0 && Access::owners(performer->engine(), string) == 2,
                       "a duplicated key lost an owner in its chord plan");
        }
        return result;
    };
    const auto ordinary = delays({ 40, 59, 64 });
    const auto unreachable = delays({ 36, 40, 59, 64 });
    expect(unreachable[0] == -1
               && std::equal(ordinary.begin(), ordinary.end(), unreachable.begin() + 1),
           "an unreachable key changed physical string timing");
    const auto duplicate = delays({ 40, 59, 64, 59 });
    expect(std::equal(ordinary.begin(), ordinary.end(), duplicate.begin())
               && duplicate[1] == duplicate[3],
           "a duplicated key acquired a second physical string spacing");
    const auto harmonic = delays({ 60, 64, 67, 88 });
    expect(harmonic[0] == 0 && harmonic[1] > 0
               && harmonic[2] > harmonic[1] && harmonic[3] > harmonic[2],
           "an unplanned harmonic chord changed its existing note-by-note stroke");
    const auto member = delays({ 60, 64, 67 }, 2, true);
    expect(member[0] == 0 && member[1] > 0 && member[2] > member[1],
           "an MPE member's unplanned chord changed its existing stroke");
}

void testGatheredRollSoundsAsOneSampleChord()
{
    const auto rolled = custom("rolled", 0.6, {
        message(0.02, 0x90, 60, 100), message(0.03, 0x90, 64, 100),
        message(0.04, 0x90, 67, 100) });
    const auto together = custom("together", 0.6, {
        message(0.02, 0x90, 60, 100), message(0.02, 0x90, 64, 100),
        message(0.02, 0x90, 67, 100) });
    for (const double sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto gathered = render(rolled, sampleRate, 64, true);
        // The host is told the engine's own fixed output latency (the
        // microphones wait out the piezo's pipeline) plus, while gathering,
        // the 30 ms window.
        constexpr int engineLatency = acustra::AcustraEngine::outputLatencySamples();
        expect(gathered.latency
                       == Performer::gatherWindowSamples(sampleRate) + engineLatency
                   && gathered.latency
                          == static_cast<int>(std::lround(0.030 * sampleRate))
                                 + engineLatency,
               "Gather Chords did not report its 30 ms window as latency");
        expect(render(rolled, sampleRate, 64, false).latency == engineLatency,
               "the player reported more than the engine's latency without "
               "gathering");
        expect(gathered == render(together, sampleRate, 64, true),
               "a chord rolled inside the window did not sound as the same "
               "chord on one sample");
        expect(render(rolled, sampleRate, 64, false)
                   != render(together, sampleRate, 64, false),
               "the rolled chord no longer needs gathering to voice like a chord");
    }
}

// Retired CC68 remains inert at every supplied release velocity. Explicit
// release gestures now change damping, tested independently by the release
// suites; toggling CC68 must still preserve every sample of this battery.
void testCc68ChangesNothing(const std::vector<Scenario>& battery)
{
    for (const auto& scenario : battery)
    {
        auto changed = scenario;
        std::vector<Event> pedal;
        for (const auto& event : scenario.events)
            if (event.size == 3 && (event.bytes[0] & 0xf0u) == 0x90u)
            {
                const auto channel = static_cast<std::uint8_t>(event.bytes[0] & 0x0fu);
                pedal.push_back(message(event.seconds,
                                        static_cast<std::uint8_t>(0xb0u | channel), 68, 127));
                pedal.push_back(message(event.seconds + 0.004,
                                        static_cast<std::uint8_t>(0xb0u | channel), 68, 0));
            }
        // render() orders each block's events by time, keeping the order
        // given within a sample, so these land after the notes they sit on.
        changed.events.insert(changed.events.end(), pedal.begin(), pedal.end());
        for (const bool gather : { false, true })
            expect(render(changed, 48000.0, 64, gather)
                       == render(scenario, 48000.0, 64, gather),
                   std::string { scenario.name } + (gather ? " (gathering)" : "")
                       + ": CC68 changed the performance");
    }
}

void testHelpersSpellMidi()
{
    // Every 14-bit wheel position survives the float helper: a note bent to
    // each probe sounds the same spelt either way.
    for (const int raw : { 0, 1, 4000, 8191, 8192, 8193, 12000, 16382, 16383 })
    {
        const auto bent = custom("bent", 0.15, {
            message(0.0, 0xe0, raw & 0x7f, raw >> 7), message(0.001, 0x90, 45, 100) });
        expect(render(bent, 48000.0, 64, false, Feed::Raw)
                   == render(bent, 48000.0, 64, false, Feed::Helpers),
               "pitchWheel did not spell 14-bit bend " + std::to_string(raw));
    }

    // setPitchBendRange is RPN 0 then the null RPN.
    const auto viaRpn = custom("rpn", 0.3, {
        message(0.0, 0xb0, 101, 0), message(0.0, 0xb0, 100, 0),
        message(0.0, 0xb0, 6, 7), message(0.0, 0xb0, 38, 25),
        message(0.0, 0xe0, 0x7f, 0x7f), message(0.001, 0x90, 45, 100) });
    const auto raw = render(viaRpn, 48000.0, 64, false);
    auto performer = std::make_unique<Performer>();
    performer->setParameters(acustra::EngineParameters {});
    performer->prepare(48000.0, 64);
    std::vector<float> left(raw.left.size()), right(raw.right.size());
    for (std::size_t start = 0; start < left.size(); start += 64)
    {
        performer->setParameters(acustra::EngineParameters {});
        performer->beginBlock(left.data() + start, right.data() + start, 64);
        if (start == 0)
        {
            performer->setPitchBendRange(0, 1, 7, 25);
            performer->pitchWheel(0, 1, 1.0f);
            performer->noteOn(48, 1, 45, 100);
        }
        performer->endBlock();
    }
    expect(left == raw.left && right == raw.right,
           "setPitchBendRange did not set RPN 0");
}

// Master tune rides on every channel's bend, survives Reset All
// Controllers, and zero changes nothing.
void testMasterTune()
{
    const auto play = [](float cents, std::vector<Event> events)
    {
        auto performer = std::make_unique<Performer>();
        performer->prepare(48000.0, 64);
        performer->setMasterTuneCents(cents);
        std::vector<float> left(64 * 150), right(64 * 150);
        for (std::size_t start = 0; start < left.size(); start += 64)
        {
            performer->beginBlock(left.data() + start, right.data() + start, 64);
            if (start == 0)
                for (const auto& event : events)
                    performer->handleMidi(sampleAt(event.seconds, 48000.0),
                                          event.bytes.data(), event.size);
            performer->endBlock();
        }
        left.insert(left.end(), right.begin(), right.end());
        return left;
    };
    const std::vector<Event> note { message(0.0, 0x90, 45, 100) };
    const std::vector<Event> resetThenNote {
        message(0.0, 0xb0, 121, 0), message(0.0002, 0x90, 45, 100) };
    const std::vector<Event> semitoneUp {
        message(0.0, 0xb0, 101, 0), message(0.0, 0xb0, 100, 0),
        message(0.0, 0xb0, 6, 1), message(0.0, 0xe0, 0x7f, 0x7f),
        message(0.0002, 0x90, 45, 100) };
    expect(play(0.0f, note) == play(0.0f, note) && play(0.0f, note) != play(100.0f, note),
           "master tune did not reach the strings");
    const auto tunedAfterPause = play(100.0f, { message(0.0002, 0x90, 45, 100) });
    expect(tunedAfterPause == play(0.0f, semitoneUp),
           "100 cents of master tune did not sound as a semitone of bend");
    expect(play(100.0f, resetThenNote) == tunedAfterPause,
           "Reset All Controllers dropped the master tune");

    // An MPE member note already hears the manager's bend, so the tune
    // rides on the manager alone: 100 cents is a member note under a
    // semitone of manager bend, not two (audit F20). Reset All Controllers
    // on the member keeps that.
    const std::vector<Event> zone { message(0.0, 0xb0, 101, 0), message(0.0, 0xb0, 100, 6),
                                    message(0.0, 0xb0, 6, 15) };
    auto memberNote = zone;
    memberNote.push_back(message(0.0002, 0x91, 45, 100));
    auto memberUnderManagerSemitone = zone;
    for (const auto& event : { message(0.0, 0xb0, 101, 0), message(0.0, 0xb0, 100, 0),
                               message(0.0, 0xb0, 6, 1), message(0.0, 0xe0, 0x7f, 0x7f),
                               message(0.0002, 0x91, 45, 100) })
        memberUnderManagerSemitone.push_back(event);
    auto memberResetThenNote = zone;
    memberResetThenNote.push_back(message(0.0, 0xb1, 121, 0));
    memberResetThenNote.push_back(message(0.0002, 0x91, 45, 100));
    const auto tunedMember = play(100.0f, memberNote);
    expect(tunedMember == play(0.0f, memberUnderManagerSemitone),
           "master tune reached an MPE member note twice");
    expect(play(100.0f, memberResetThenNote) == tunedMember,
           "Reset All Controllers on a member put the master tune on it");
}

// Reset All Controllers (CC121) resets what RP-015 lists, in the reset's
// own scope: the wheel's vibrato when the channel that set it is in scope
// (it is one gesture across the instrument, so a reset elsewhere leaves
// it), and channel pressure (audit F21).
void testResetAllControllersResetsVibratoAndPressure()
{
    const auto play = [](std::vector<Event> events)
    {
        auto performer = std::make_unique<Performer>();
        performer->prepare(48000.0, 64);
        std::vector<float> left(64 * 750), right(64 * 750);
        for (std::size_t start = 0; start < left.size(); start += 64)
        {
            performer->beginBlock(left.data() + start, right.data() + start, 64);
            if (start == 0)
                for (const auto& event : events)
                    performer->handleMidi(sampleAt(event.seconds, 48000.0),
                                          event.bytes.data(), event.size);
            performer->endBlock();
        }
        left.insert(left.end(), right.begin(), right.end());
        return left;
    };
    const auto wheel = message(0.0, 0xb0, 1, 127);
    const auto note = message(0.001, 0x90, 57, 100);
    const auto plain = play({ note });
    const auto vibrato = play({ wheel, note });
    expect(vibrato != plain, "the wheel's vibrato did not reach the note");
    expect(play({ wheel, message(0.0, 0xb0, 121, 0), note }) == plain,
           "Reset All Controllers left the wheel's vibrato");
    expect(play({ wheel, message(0.0, 0xb2, 121, 0), note }) == vibrato,
           "Reset All Controllers on another channel took the wheel's vibrato");

    // MPE: a member's reset clears its pressure but not the manager's
    // wheel; the manager's clears both.
    const std::vector<Event> zone { message(0.0, 0xb0, 101, 0), message(0.0, 0xb0, 100, 6),
                                    message(0.0, 0xb0, 6, 15) };
    const auto with = [&](std::vector<Event> events)
    {
        auto all = zone;
        all.insert(all.end(), events.begin(), events.end());
        return all;
    };
    const auto memberNote = message(0.001, 0x91, 57, 100);
    const auto grip = message(0.0, 0xd1, 10, 0, 2);
    const auto memberWheel = play(with({ wheel, memberNote }));
    expect(play(with({ wheel, grip, memberNote })) != memberWheel,
           "a member's pressure did not reach its note");
    expect(play(with({ wheel, grip, message(0.0, 0xb1, 121, 0), memberNote }))
               == memberWheel,
           "a member's Reset All Controllers left its pressure or took the "
           "manager's wheel");
    expect(play(with({ wheel, grip, message(0.0, 0xb0, 121, 0), memberNote }))
               == play(with({ memberNote })),
           "the manager's Reset All Controllers left the wheel or a member's "
           "pressure");
}

// A sample's Note Offs wait for its Note Ons (testOneSampleIsOneCanonicalEvent),
// but the sustain pedal still meets them in the order the host sent them: a
// pedal pressed after a key-up on the same sample does not catch that note,
// and one lifted and pressed again after it lets the note go (audit F22).
void testSustainMeetsSameSampleKeyUpsInOrder()
{
    const auto on = message(0.0, 0x90, 60, 100);
    const auto off = message(0.5, 0x80, 60, 64);
    const auto pedal = [](double seconds, bool down)
    {
        return message(seconds, 0xb0, 64, down ? 127 : 0);
    };
    const auto play = [](std::vector<Event> events)
    {
        return render(custom("pedal", 1.5, std::move(events)), 48000.0, 64, false);
    };
    const auto released = play({ on, off });
    expect(play({ on, off, pedal(0.5, true) }) == released,
           "a pedal pressed after a key-up on its sample caught the note");
    expect(play({ on, off, pedal(0.5 + 0.5 * 7.5 / 120.0, true) }) == released,
           "a pedal pressed inside an ordinary release's grace window caught the note");
    const auto pedalled = play({ on, pedal(0.5, true), off });
    expect(pedalled != released, "a pedal pressed before a key-up did not hold it");
    const auto lifted = play({ on, pedal(0.2, true), off, pedal(0.5, false) });
    expect(play({ on, pedal(0.2, true), off, pedal(0.5, false), pedal(0.5, true) })
               == lifted,
           "a pedal lifted and pressed again after a key-up held the note");
    expect(lifted != play({ on, pedal(0.2, true), off }),
           "a pedal lifted after a key-up on its sample did not let it go");
    // The same order under Gather Chords, which plays the sample later.
    expect(render(custom("gathered", 1.5, { on, off, pedal(0.5, true) }), 48000.0, 64, true)
               == render(custom("gathered", 1.5, { on, off }), 48000.0, 64, true),
           "a gathered pedal pressed after a key-up caught the note");

    // An MPE member's key-up is held by its own pedal or the manager's, so
    // one pedal lifted after it on its sample lets it go only if the other
    // is up too. Lifting the member's pedal used to let it go under the
    // manager's, and the manager's under the member's.
    const std::vector<Event> zone { message(0.0, 0xb0, 101, 0), message(0.0, 0xb0, 100, 6),
                                    message(0.0, 0xb0, 6, 15) };
    const auto mpe = [&](std::vector<Event> events)
    {
        auto all = zone;
        all.insert(all.end(), events.begin(), events.end());
        return play(std::move(all));
    };
    const auto memberOn = message(0.0, 0x91, 60, 100);
    const auto memberOff = message(0.5, 0x81, 60, 64);
    const auto managerPedal = [](double seconds, bool down)
    {
        return message(seconds, 0xb0, 64, down ? 127 : 0);
    };
    const auto memberPedal = [](double seconds, bool down)
    {
        return message(seconds, 0xb1, 64, down ? 127 : 0);
    };
    const auto memberReleased = mpe({ memberOn, memberOff });
    const auto underManager = mpe({ memberOn, managerPedal(0.2, true), memberOff });
    expect(underManager != memberReleased, "the manager's pedal did not hold a member");
    expect(mpe({ memberOn, managerPedal(0.2, true), memberPedal(0.2, true), memberOff,
                 memberPedal(0.5, false) }) == underManager,
           "a member's pedal lifted after its key-up let it go under the manager's");
    expect(mpe({ memberOn, managerPedal(0.2, true), memberPedal(0.2, true), memberOff,
                 managerPedal(0.5, false) })
               == mpe({ memberOn, memberPedal(0.2, true), memberOff }),
           "the manager's pedal lifted after a member's key-up let it go under "
           "the member's own");
    expect(mpe({ memberOn, managerPedal(0.2, true), memberOff, managerPedal(0.5, false) })
               == mpe({ memberOn, message(0.5, 0xb1, 123, 0) }),
           "the manager's pedal lifted after a member's key-up did not let it go");
    // The deferred same-sample key-up keeps the pedal state at key-up.
    // A subsequent pedal-up (including Reset All Controllers) starts the
    // nominal immediate contact; the earlier key release velocity cannot
    // turn it into an ordinary grace or a different damping gesture.
    const auto pedalContact = mpe({ memberOn, message(0.5, 0xb1, 123, 0) });
    for (const int status : { 0xb0, 0xb1 })
        for (const int controller : { 64, 121 })
            for (const int velocity : { 0, 64, 127 })
                expect(mpe({ memberOn, message(0.2, status, 64, 127),
                             message(0.5, 0x81, 60, velocity),
                             message(0.5, status, controller, 0) }) == pedalContact,
                       "a deferred pedal-held key-up lost nominal immediate contact at velocity "
                           + std::to_string(velocity) + " under controller "
                           + std::to_string(controller));
}

void testOverflowIsCountedNotAllocated(const std::vector<Scenario>& battery)
{
    // 130 Note Ons on one sample: the group holds 128.
    const auto& edges = scenarioNamed(battery, "host-edge-cases");
    expect(render(edges, 48000.0, 64, false).dropped == 2,
           "a same-sample group's overflow was not counted");

    // 1100 controllers inside one gathered block: the queue holds 1024.
    std::vector<Event> flood;
    for (int index = 0; index < 1100; ++index)
        flood.push_back(message(0.01, 0xb0, 1, index % 128));
    flood.push_back(message(0.011, 0x90, 45, 100));
    const auto flooded = render(custom("flood", 0.2, flood), 48000.0, 1024, true);
    expect(flooded.dropped == 1100 + 1 - 1024,
           "the gathering queue's overflow was not counted ("
               + std::to_string(flooded.dropped) + ")");
    expect(flooded.allocations == 0, "a full gathering queue allocated");
}

// The level at one frequency over [from, to) seconds, from both channels.
double bandLevel(const Render& render, double sampleRate, double frequency,
                 double from, double to)
{
    // M_PI is POSIX, not standard C++: MSVC does not define it.
    constexpr double pi = 3.14159265358979323846;
    double real = 0.0, imaginary = 0.0;
    const auto begin = static_cast<std::size_t>(from * sampleRate);
    const auto end = std::min(static_cast<std::size_t>(to * sampleRate),
                              render.left.size());
    for (std::size_t n = begin; n < end; ++n)
    {
        const double window = 0.5 - 0.5 * std::cos(2.0 * pi
            * static_cast<double>(n - begin) / static_cast<double>(end - begin));
        const double phase = 2.0 * pi * frequency * static_cast<double>(n) / sampleRate;
        const double value = window * (render.left[n] + render.right[n]);
        real += value * std::cos(phase);
        imaginary += value * std::sin(phase);
    }
    return 10.0 * std::log10(real * real + imaginary * imaginary + 1.0e-30);
}

// A strum's key-ups that come before the pick reaches every string do not
// stop the pick: a short, sequenced stab still sounds all six strings, each
// released as it is plucked, as the same six notes sounded one at a time
// would (audit F2). At velocity 20 the pick takes 66 ms to cross them.
void testShortStrumsSoundEveryString()
{
    const int notes[] { 40, 45, 50, 55, 59, 64 };
    const auto stab = [&] (double seconds, int velocity, bool pedal)
    {
        std::vector<Event> events;
        if (pedal)
            events.push_back(message(0.0, 0xb0, 64, 127));
        for (const int note : notes)
            events.push_back(message(0.01, 0x90, note, velocity));
        for (const int note : notes)
            events.push_back(message(0.01 + seconds, 0x80, note, 64));
        return render(custom("stab", 0.4, events), 48000.0, 64, false);
    };
    for (const int velocity : { 20, 100 })
    {
        const auto held = stab(0.2, velocity, false);
        for (const double seconds : { 0.015, 0.040 })
            for (const bool pedal : { false, true })
            {
                const auto shortStab = stab(seconds, velocity, pedal);
                for (const int note : notes)
                {
                    const double frequency = 440.0 * std::exp2((note - 69) / 12.0);
                    // Preserve the same 100 ms post-damping observation
                    // window with the default-tempo 1/32-note grace.
                    constexpr double grace = 7.5 / 120.0;
                    const double missing = bandLevel(held, 48000.0, frequency,
                                                    0.08 + grace, 0.18 + grace)
                        - bandLevel(shortStab, 48000.0, frequency,
                                    0.08 + grace, 0.18 + grace);
                    expect(missing < 8.0,
                           "a " + std::to_string(static_cast<int>(seconds * 1000.0))
                               + " ms strum at velocity " + std::to_string(velocity)
                               + (pedal ? " under the pedal" : "") + " lost MIDI "
                               + std::to_string(note) + " (" + std::to_string(missing)
                               + " dB below held)");
                }
            }
    }
    // The pedal can lift on a later sample while a slow strum's remaining
    // picks are still approaching. Its held key-up velocity is ignored on
    // every physical string, including those not yet plucked at pedal-up.
    const auto liftedBeforePicks = [&] (int releaseVelocity)
    {
        std::vector<Event> events { message(0.0, 0xb0, 64, 127) };
        for (const int note : notes)
            events.push_back(message(0.01, 0x90, note, 20));
        for (const int note : notes)
            events.push_back(message(0.015, 0x80, note, releaseVelocity));
        events.push_back(message(0.020, 0xb0, 64, 0));
        return render(custom("pedal lifted before queued picks", 0.4, std::move(events)),
                      48000.0, 64, false);
    };
    const auto nominalPedalRelease = liftedBeforePicks(64);
    for (const int velocity : { 0, 127 })
        expect(liftedBeforePicks(velocity) == nominalPedalRelease,
               "a pedal-held key-up velocity changed a queued strum's later pedal contact");
    // Under the pedal the stab rings on; without it every string is let go.
    auto performer = std::make_unique<Performer>();
    performer->prepare(48000.0, 64);
    std::vector<float> left(64), right(64);
    for (int block = 0; block < 300; ++block)
    {
        performer->beginBlock(left.data(), right.data(), 64);
        if (block == 1)
            for (const int note : notes)
                performer->noteOn(0, 1, note, 20);
        if (block == 20)
            for (const int note : notes)
                performer->noteOff(0, 1, note);
        performer->endBlock();
    }
    for (const int note : notes)
        expect(performer->engine().heldString(note) < 0,
               "a short strum's key-up left MIDI " + std::to_string(note) + " held");

    // The pedal the key-up was under decides, as it stands when the pick
    // arrives: a string let go under the pedal is held while it stays down;
    // one let go without it, or whose pedal came up before the pick, is not
    // caught by a pedal pressed after its key-up.
    enum class Pedal { Throughout, UpBeforePicks, DownAfterKeyUp };
    for (const auto pedal : { Pedal::Throughout, Pedal::UpBeforePicks,
                              Pedal::DownAfterKeyUp })
    {
        performer = std::make_unique<Performer>();
        performer->prepare(48000.0, 64);
        for (int block = 0; block < 1400; ++block)
        {
            performer->beginBlock(left.data(), right.data(), 64);
            if (block == 1)
            {
                if (pedal != Pedal::DownAfterKeyUp)
                    performer->controlChange(0, 1, 64, 127);
                for (const int note : notes)
                    performer->noteOn(0, 1, note, 20);
            }
            if (block == 4)
                for (const int note : notes)
                    performer->noteOff(0, 1, note);
            if (block == 6)
                performer->controlChange(0, 1, 64,
                                         pedal == Pedal::UpBeforePicks ? 0 : 127);
            performer->endBlock();
        }
        // Released open strings are handed back 1.33 s after damping starts.
        expect(performer->engine().getActiveVoiceCount()
                   == (pedal == Pedal::Throughout ? 6 : 0),
               pedal == Pedal::Throughout
                   ? "a strum let go under the pedal was not held by it"
                   : "a pedal that did not hold a strum's key-up held its strings");
    }
}

// The player keeps the engine's time: at a rate the engine clamps or
// replaces (AcustraEngine::prepare), the gathering window and the strum
// rest follow the rate the engine models, and a NaN rate cannot reach a
// cast (audit F31).
void testPlayerKeepsTheEnginesSampleRate()
{
    for (const auto& [host, modelled] : { std::pair { 4000.0, 8000.0 },
                                          std::pair { 0.0, 48000.0 },
                                          std::pair { std::nan(""), 48000.0 },
                                          std::pair { 44100.0, 44100.0 } })
    {
        auto performer = std::make_unique<Performer>();
        performer->setGatherChords(true);
        performer->prepare(host, 256);
        expect(performer->engine().sampleRate() == modelled
                   && performer->latencySamples()
                          == Performer::gatherWindowSamples(modelled)
                                 + acustra::AcustraEngine::outputLatencySamples(),
               "the gathering window did not follow the engine's rate at "
                   + std::to_string(host) + " Hz");
    }
}

void testResetSilences()
{
    auto performer = std::make_unique<Performer>();
    performer->prepare(48000.0, 256);
    std::vector<float> left(256), right(256);
    performer->beginBlock(left.data(), right.data(), 256);
    performer->noteOn(0, 1, 40, 120);
    performer->endBlock();
    performer->reset();
    performer->process(left.data(), right.data(), 256);
    expect(std::all_of(left.begin(), left.end(), [](float v) { return v == 0.0f; }),
           "reset did not silence the engine");
    expect(performer->engine().getActiveVoiceCount() == 0,
           "reset left a string playing");
}
// All Notes Off (CC123) lets go of keys that are down; a string whose key was
// already up and is damping is not the pedal's to catch again. It used to be:
// a note released before the pedal went down rang on under it, ~24 dB up a
// second later.
void testAllNotesOffLeavesReleasedStringsAlone()
{
    const auto play = [](std::vector<Event> events)
    {
        return render(custom("all notes off", 1.5, std::move(events)), 48000.0, 64, false);
    };
    for (const int note : { 45, 52 })
    {
        const std::vector<Event> released { message(0.01, 0x90, note, 100),
                                            message(0.30, 0x80, note, 64),
                                            message(0.40, 0xb0, 64, 127) };
        auto allOff = released;
        allOff.push_back(message(0.45, 0xb0, 123, 0));
        expect(play(allOff) == play(released),
               "All Notes Off under the pedal caught released note " + std::to_string(note));
    }
}

// Reset All Controllers lifts the pedal as CC64 0 does, so after a key-up on
// its sample a pedal pressed again does not catch the note.
void testResetAllControllersLiftsThePedalInOrder()
{
    const auto on = message(0.0, 0x90, 60, 100);
    const auto off = message(0.5, 0x80, 60, 64);
    const auto play = [](std::vector<Event> events)
    {
        return render(custom("reset pedal", 1.5, std::move(events)), 48000.0, 64, false);
    };
    const auto held = message(0.2, 0xb0, 64, 127);
    const auto down = message(0.5, 0xb0, 64, 127);
    expect(play({ on, held, off, message(0.5, 0xb0, 121, 0), down })
               == play({ on, held, off, message(0.5, 0xb0, 64, 0), down }),
           "Reset All Controllers after a key-up did not let it go as a lifted pedal does");
}

// Only notes a string can sound make a stroke: two unreachable notes under
// one that sounds leave it a single note, not a strum's random stroke.
void testUnsoundableNotesMakeNoStrum()
{
    const auto play = [](std::vector<int> notes)
    {
        std::vector<Event> events;
        for (const int note : notes)
            events.push_back(message(0.01, 0x90, note, 100));
        return render(custom("strum count", 0.6, events), 48000.0, 64, false);
    };
    expect(play({ 30, 33, 52 }) == play({ 52 }),
           "two unreachable notes turned a single note into a strum");
    expect(play({ 36, 48, 52 }) == play({ 48, 52 }),
           "an unreachable note turned two notes into a strum");
}

// The master tune rides on every channel's bend, which prepare and reset
// zero in the engine: it must be put back, as the same tune sent again is
// ignored.
void testMasterTuneSurvivesPrepareAndReset()
{
    enum class Then { Nothing, Prepare, Reset };
    const auto play = [](float cents, Then then)
    {
        auto performer = std::make_unique<Performer>();
        performer->prepare(48000.0, 64);
        performer->setMasterTuneCents(cents);
        if (then == Then::Prepare)
            performer->prepare(48000.0, 64);
        if (then == Then::Reset)
            performer->reset();
        std::vector<float> left(64 * 100), right(64 * 100);
        for (std::size_t start = 0; start < left.size(); start += 64)
        {
            performer->beginBlock(left.data() + start, right.data() + start, 64);
            if (start == 0)
                performer->noteOn(0, 1, 45, 100);
            performer->endBlock();
        }
        return left;
    };
    const auto tuned = play(100.0f, Then::Nothing);
    expect(tuned != play(0.0f, Then::Nothing), "master tune did not reach the strings");
    expect(play(100.0f, Then::Prepare) == tuned, "prepare dropped the master tune");
    // A reset instrument is not bit for bit a fresh one (its smoothed levels
    // glide back), so the tune is heard against an untuned reset.
    expect(play(100.0f, Then::Reset) != play(0.0f, Then::Reset),
           "reset dropped the master tune");
}
// A chord still forming is one channel's: another channel's notes every
// 20 ms must not keep channel 1's window open, so a note it has held for a
// second is not refretted and plucked again when channel 1 plays the next.
void testChordWindowIsPerChannel()
{
    const auto heldAfter = [](int busyChannel)
    {
        auto performer = std::make_unique<Performer>();
        performer->prepare(48000.0, 64);
        std::vector<float> left(64), right(64);
        int before = -1;
        for (int start = 0; start < 48000 + 1920; start += 64)
        {
            performer->beginBlock(left.data(), right.data(), 64);
            for (int offset = 0; offset < 64; ++offset)
            {
                const int at = start + offset;
                if (at == 480)
                    performer->noteOn(offset, 1, 52, 90);
                if (busyChannel > 0 && at > 960 && at < 48000 && (at - 961) % 960 == 0)
                {
                    const int note = (at - 961) / 960 % 2 != 0 ? 45 : 40;
                    performer->noteOn(offset, busyChannel, note, 60);
                    performer->noteOff(offset, busyChannel, note);
                }
                if (at == 48480)
                    performer->noteOn(offset, 1, 72, 90);
            }
            performer->endBlock();
            if (start == 48000 - 64)
                before = performer->engine().heldString(52, 1);
        }
        return std::pair { before, performer->engine().heldString(52, 1) };
    };
    const auto quiet = heldAfter(0);
    const auto busy = heldAfter(2);
    expect(quiet.first >= 0 && quiet.second == quiet.first,
           "a note held a second was refretted by the next note on its channel");
    expect(busy.first == quiet.first && busy.second == quiet.second,
           "another channel's notes kept a chord forming and refretted a held note");
}
// A same-sample chord with a note only a natural harmonic reaches leaves
// that harmonic its string, as the same chord rolled does: the shape used
// to take it, and the harmonic then found no string and never sounded.
void testChordLeavesAHarmonicItsString()
{
    const auto strings = [](const std::vector<int>& notes, int spacing)
    {
        auto performer = std::make_unique<Performer>();
        performer->prepare(48000.0, 64);
        std::vector<float> left(64), right(64);
        performer->beginBlock(left.data(), right.data(), 64);
        for (std::size_t index = 0; index < notes.size(); ++index)
            performer->noteOn(static_cast<int>(index) * spacing, 1, notes[index], 100);
        performer->endBlock();
        for (int block = 0; block < 20; ++block)
        {
            performer->beginBlock(left.data(), right.data(), 64);
            performer->endBlock();
        }
        std::vector<int> held;
        for (const int note : notes)
            held.push_back(performer->engine().heldString(note, 1));
        return held;
    };
    for (const auto& chord : { std::vector<int> { 88, 67, 64, 60 },
                               std::vector<int> { 91, 64, 59, 55 } })
    {
        const auto together = strings(chord, 0);
        expect(std::all_of(together.begin(), together.end(), [](int s) { return s >= 0; }),
               "a same-sample chord dropped its harmonic " + std::to_string(chord[0]));
        expect(together == strings(chord, 5),
               "a same-sample chord was placed unlike the same chord rolled");
    }
}
} // namespace

int main()
{
    const auto battery = makeBattery();
    testBatteryPlaysSoundsAndRepeats(battery);
    testBlockSizeDoesNotChangeThePerformance(battery);
    testOneSampleIsOneCanonicalEvent();
    testLiveRollKeepsItsOriginalAttacks();
    testStrumRestIsMeasuredBetweenStrums();
    testStrumsAreTimedByTheNotesThatSound();
    testStrumsFollowPhysicalStrings();
    testPhysicalStrumPlanningKeepsSpecialNotes();
    testGatheredRollSoundsAsOneSampleChord();
    testCc68ChangesNothing(battery);
    testHelpersSpellMidi();
    testMasterTune();
    testResetAllControllersResetsVibratoAndPressure();
    testSustainMeetsSameSampleKeyUpsInOrder();
    testOverflowIsCountedNotAllocated(battery);
    testShortStrumsSoundEveryString();
    testPlayerKeepsTheEnginesSampleRate();
    testResetSilences();
    testAllNotesOffLeavesReleasedStringsAlone();
    testResetAllControllersLiftsThePedalInOrder();
    testUnsoundableNotesMakeNoStrum();
    testMasterTuneSurvivesPrepareAndReset();
    testChordWindowIsPerChannel();
    testChordLeavesAHarmonicItsString();

    if (failures != 0)
    {
        std::cerr << failures << " Acustra performer test(s) failed\n";
        return 1;
    }
    std::cout << "All Acustra performer tests passed\n";
    return 0;
}
