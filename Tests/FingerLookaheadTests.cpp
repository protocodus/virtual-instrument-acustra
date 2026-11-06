// Finger Lookahead (Performer::setFingerLookahead): off is an exact no-op;
// on, it delays everything by the window and, where no finger has to move,
// changes nothing else; a same-string shift lifts the old note's finger and
// slides it before the new note, sounding the slide before the pluck; and the
// result does not depend on the host's block size.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr double rate = 48000.0;
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct Event
{
    double seconds;
    std::uint8_t status, data1, data2;
};

struct Render
{
    std::vector<float> left;
    // heldString of each probe note, sampled at each probe's time.
    std::vector<int> probes;
};

struct Probe
{
    double seconds;
    int note;
};

Render render(const std::vector<Event>& events, double seconds, bool lookahead,
              float releaseNoise, int block, const std::vector<Probe>& probes = {})
{
    auto performer = std::make_unique<acustra::Performer>();
    acustra::EngineParameters parameters;
    parameters.releaseNoise = releaseNoise;
    performer->setParameters(parameters);
    performer->prepare(rate, block);
    performer->setFingerLookahead(lookahead);
    const long total = static_cast<long>(seconds * rate);
    Render result;
    result.left.resize(static_cast<std::size_t>(total));
    std::vector<float> left(static_cast<std::size_t>(block)), right(left.size());
    std::size_t next = 0, probe = 0;
    for (long start = 0; start < total; start += block)
    {
        const int frames = static_cast<int>(std::min<long>(block, total - start));
        performer->beginBlock(left.data(), right.data(), frames);
        for (; next < events.size()
               && std::lround(events[next].seconds * rate) < start + frames; ++next)
        {
            const std::uint8_t bytes[3] { events[next].status, events[next].data1,
                                          events[next].data2 };
            performer->handleMidi(static_cast<int>((
                std::lround(events[next].seconds * rate)) - start), bytes, 3);
        }
        performer->endBlock();
        std::copy_n(left.begin(), frames, result.left.begin() + start);
        for (; probe < probes.size()
               && static_cast<long>(probes[probe].seconds * rate) < start + frames; ++probe)
            result.probes.push_back(performer->engine().heldString(probes[probe].note, 1));
    }
    return result;
}

double rms(const std::vector<float>& signal, long begin, long end)
{
    double sum = 0.0;
    for (long index = begin; index < end; ++index)
        sum += static_cast<double>(signal[static_cast<std::size_t>(index)])
            * signal[static_cast<std::size_t>(index)];
    return std::sqrt(sum / static_cast<double>(std::max(1L, end - begin)));
}

std::vector<Event> legato(const std::vector<int>& notes, double start, double step)
{
    std::vector<Event> events;
    double time = start;
    for (int note : notes)
    {
        events.push_back({ time, 0x90, static_cast<std::uint8_t>(note), 96 });
        events.push_back({ time + step, 0x80, static_cast<std::uint8_t>(note), 64 });
        time += step;
    }
    return events;
}
} // namespace

int main()
{
    const long window = acustra::Performer::fingerLookaheadSamples(rate);
    expect(window == 2400, "the lookahead is 50 ms at 48 kHz");
    {
        acustra::Performer performer;
        performer.prepare(rate, 256);
        const int plain = performer.latencySamples();
        performer.setFingerLookahead(true);
        expect(performer.latencySamples() == plain + window,
               "the lookahead adds its window to the reported latency");
        performer.setGatherChords(true);
        expect(performer.latencySamples() == plain + window
                   + acustra::Performer::gatherWindowSamples(rate),
               "with Gather Chords the two holds add");
    }

    // Open strings and a chord: no finger leaves a fret for another on a
    // string, so the lookahead only delays.
    {
        std::vector<Event> events {
            { 0.10, 0x90, 40, 100 }, { 0.60, 0x80, 40, 64 },
            { 0.60, 0x90, 45, 90 },  { 1.10, 0x80, 45, 64 },
            { 1.20, 0x90, 48, 90 },  { 1.20, 0x90, 52, 90 }, { 1.20, 0x90, 55, 90 },
            { 2.00, 0x80, 48, 64 },  { 2.00, 0x80, 52, 64 }, { 2.00, 0x80, 55, 64 },
        };
        // Against the plain player given every event 50 ms later, so both
        // engines meet each event on the same sample of the same block.
        auto delayed = events;
        for (auto& event : delayed)
            event.seconds += static_cast<double>(window) / rate;
        const auto off = render(delayed, 2.6, false, 0.5f, 256);
        const auto on = render(events, 2.6, true, 0.5f, 256);
        expect(on.left == off.left,
               "without a shift the lookahead output is the plain output, 50 ms later");
    }

    // F2 G2 F#2 G#2 on the low E string, end to end: each change moves the
    // finger along the winding.
    const std::vector<int> notes { 41, 43, 42, 44 };
    const double start = 0.3, step = 0.45;
    const auto events = legato(notes, start, step);
    std::vector<Probe> probes;
    for (std::size_t index = 0; index + 1 < notes.size(); ++index)
    {
        // 10 ms before the next note's (delayed) onset, and 60 ms before.
        const double onset = start + step * static_cast<double>(index + 1) + 0.05;
        probes.push_back({ onset - 0.060, notes[index] });
        probes.push_back({ onset - 0.010, notes[index] });
    }
    const auto noisy = render(events, 2.4, true, 0.5f, 256, probes);
    const auto quiet = render(events, 2.4, true, 0.0f, 256);
    const auto plainNoisy = render(events, 2.4, false, 0.5f, 256);
    const auto plainQuiet = render(events, 2.4, false, 0.0f, 256);
    for (std::size_t index = 0; index + 1 < notes.size(); ++index)
    {
        expect(noisy.probes[2 * index + 1] < 0,
               "the shifting finger has let go of note " + std::to_string(index)
                   + " before the next note");
        expect(noisy.probes[2 * index] == 0,
               "note " + std::to_string(index) + " is still held on the low E 60 ms before");
        const long onset = static_cast<long>((start + step * static_cast<double>(index + 1)) * rate);
        // The slide's own sound: Release Noise on against off, in the
        // 45 ms before the onset (aligned to the plain render's time).
        std::vector<float> slide(noisy.left.size()), plainSlide(noisy.left.size());
        for (std::size_t sample = 0; sample < slide.size(); ++sample)
        {
            slide[sample] = noisy.left[sample] - quiet.left[sample];
            plainSlide[sample] = plainNoisy.left[sample] - plainQuiet.left[sample];
        }
        const long lead = static_cast<long>(0.045 * rate);
        const double before = rms(slide, onset + window - lead, onset + window);
        const double plainBefore = rms(plainSlide, onset - lead, onset);
        const double note = rms(noisy.left, onset + window, onset + window + lead);
        expect(before > 4.0 * plainBefore,
               "the slide sounds before shift " + std::to_string(index)
                   + " (" + std::to_string(20.0 * std::log10(before / plainBefore)) + " dB)");
        expect(before < 0.1 * note,
               "the slide stays far under the note it leads to, shift " + std::to_string(index)
                   + " (" + std::to_string(20.0 * std::log10(before / note)) + " dB)");
    }

    // The host's block size does not move a finger.
    {
        const auto small = render(events, 2.4, true, 0.5f, 64);
        const auto odd = render(events, 2.4, true, 0.5f, 333);
        expect(small.left == noisy.left && odd.left == noisy.left,
               "the lookahead renders the same at 64, 256 and 333-sample blocks");
    }

    if (failures == 0)
        std::cout << "Finger lookahead checks passed\n";
    return failures == 0 ? 0 : 1;
}
