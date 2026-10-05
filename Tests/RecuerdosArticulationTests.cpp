#include "DSP/AcustraEngine.h"
#include "../Tools/TremoloPerformance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

using namespace acustra::repertoire;
void testWrittenNotesAndContinuousMelody()
{
    const auto performed = joinTremoloLine(recuerdos);
    const auto repeated = joinTremoloLine(recuerdos);
    double end = 0.0;
    for (const auto& note : recuerdos)
        end = std::max(end, note.startBeats + note.heldBeats);
    for (std::size_t i = 0; i < recuerdos.size(); ++i)
    {
        const auto& source = recuerdos[i];
        const auto& note = performed[i];
        expect(note.note == source.note && note.startBeats == source.startBeats
                   && note.velocity == source.velocity,
               "articulation changed the composition, timing or authored velocity");
        expect(note.heldBeats == repeated[i].heldBeats,
               "articulation was not deterministic");
        if (source.heldBeats > 0.1875)
        {
            expect(note.heldBeats == source.heldBeats,
                   "tremolo articulation changed the accompaniment");
            continue;
        }
        auto next = i + 1;
        while (next < recuerdos.size() && recuerdos[next].heldBeats > 0.1875)
            ++next;
        const double nextAttack = next < recuerdos.size()
            ? recuerdos[next].startBeats : end;
        expect(std::abs(note.startBeats + note.heldBeats - nextAttack) < 1e-12,
               "melody releases before the next tremolo attack, including the thumb slot");
        expect(note.heldBeats > 0.0,
               "tremolo articulation created a nonpositive hold");
    }
}

struct Event { int sample, note; float velocity; bool starts; };
struct Render { std::array<std::vector<float>, 2> audio; bool heldInGap, released; };
Render render(int block)
{
    constexpr double rate = 44100.0;
    const double samplesPerBeat = rate * 60.0 / recuerdosBeatsPerMinute;
    const auto performed = joinTremoloLine(recuerdos);
    std::vector<Event> events;
    for (const auto& note : performed)
    {
        events.push_back({static_cast<int>(std::llround(note.startBeats * samplesPerBeat)),
                          note.note, note.velocity, true});
        events.push_back({static_cast<int>(std::llround(
                              (note.startBeats + note.heldBeats) * samplesPerBeat)),
                          note.note, 0.0f, false});
    }
    // Observe the first thumb slot, after the third melody finger has played.
    const int probe = static_cast<int>(std::llround(0.55 * samplesPerBeat));
    events.push_back({probe, -1, 0.0f, false});
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b)
    { return a.sample != b.sample ? a.sample < b.sample : a.starts < b.starts; });
    auto engine = std::make_unique<acustra::AcustraEngine>();
    engine->prepare(rate, block);
    engine->setTempoBpm(recuerdosBeatsPerMinute);
    const int length = events.back().sample;
    Render result {{std::vector<float>(length), std::vector<float>(length)}, false, false};
    int rendered = 0;
    for (const auto& event : events)
    {
        while (rendered < event.sample)
        {
            const int count = std::min(block, event.sample - rendered);
            engine->process(result.audio[0].data() + rendered,
                            result.audio[1].data() + rendered, count);
            rendered += count;
        }
        if (event.note < 0) result.heldInGap = engine->heldString(64) >= 0;
        else if (event.starts) engine->noteOn(event.note, event.velocity);
        else engine->noteOff(event.note);
    }
    const auto strings = engine->getStringActivity();
    result.released = std::none_of(strings.begin(), strings.end(),
                                  [](const auto& string) { return string.keyDown; });
    return result;
}
}

int main()
{
    testWrittenNotesAndContinuousMelody();
    const auto a = render(256), b = render(17);
    expect(a.heldInGap && b.heldInGap,
           "MIDI events release the fretting hand while the thumb plays");
    expect(a.released && b.released, "tremolo articulation left an unbalanced note owner");
    expect(a.audio == b.audio, "tremolo articulation depended on process block cuts");
    for (const auto& channel : a.audio)
        expect(std::all_of(channel.begin(), channel.end(), [](float x)
                          { return std::isfinite(x) && std::abs(x) < 0.9f; }),
               "Recuerdos articulation produced invalid or limited audio");
    return failures == 0 ? 0 : 1;
}
