// What the parameter matrix's silence after release measures on Acustra
// (Tests/MatrixRelease.h): the matrix's own chord, played through the
// Performer as the plug-in plays it, on every factory preset with every
// picking technique. The played strings must be silent at the matrix's
// bound; the open strings' sympathetic ring must stay under the floor the
// matrix uses, before Output, so a preset trim cannot carry it over; and a
// lost note-off must stay above that floor, so the matrix still catches a
// stuck note.
#include "DSP/AcustraPerformer.h"
#include "DSP/AcustraPresets.h"
#include "MatrixRelease.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>

namespace
{
int failures = 0;

void expect(bool condition, const char* preset, const char* technique, const char* what, double decibels)
{
    if (condition)
        return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s, %s: %s (%.2f dBFS)\n", preset, technique, what, decibels);
}

double decibels(double linear)
{
    return 20.0 * std::log10(std::max(linear, 1.0e-12));
}

enum class Variant { Shipping, SympatheticOff, NoteOffsLost };

// The peak of the 250 ms the matrix reads after its release bound, before
// the Output gain. Rate and block are the matrix's middle pair.
double tailBeforeOutput(const acustra::EngineParameters& parameters, Variant variant)
{
    constexpr double rate = 48000.0, bpm = 120.0, noteOff = 0.8;
    constexpr int block = 64;
    auto performer = std::make_unique<acustra::Performer>();
    performer->setParameters(parameters);
    performer->prepare(rate, block);
    performer->setParameters(parameters);
    performer->setTempoBpm(bpm);
    if (variant == Variant::SympatheticOff)
        performer->engine().setSympatheticStringsEnabled(false);

    const auto frame = [](double seconds) { return static_cast<long long>(std::llround(seconds * rate)); };
    const double release = acustra::matrixrelease::releaseSeconds(rate, bpm, performer->latencySamples());
    const long long tailStart = frame(noteOff + release), total = frame(noteOff + release + 0.25);
    struct Timed { long long at; std::array<std::uint8_t, 3> bytes; };
    const std::array<Timed, 6> notes {{
        { frame(0.0), { 0x90, 60, 100 } }, { frame(0.1), { 0x90, 64, 80 } }, { frame(0.1), { 0x90, 67, 127 } },
        { frame(noteOff), { 0x80, 60, 0 } }, { frame(noteOff), { 0x80, 64, 0 } }, { frame(noteOff), { 0x80, 67, 0 } },
    }};
    std::array<float, block> left {}, right {};
    std::size_t next = 0;
    double tail = 0.0;
    for (long long done = 0; done < total; done += block)
    {
        const int frames = static_cast<int>(std::min<long long>(block, total - done));
        performer->beginBlock(left.data(), right.data(), frames);
        for (; next < notes.size() && notes[next].at < done + frames; ++next)
            if (variant != Variant::NoteOffsLost || notes[next].bytes[0] != 0x80)
                performer->handleMidi(static_cast<int>(notes[next].at - done), notes[next].bytes.data(), 3);
        performer->endBlock();
        if (done + frames > tailStart)
            for (int i = static_cast<int>(std::max<long long>(0, tailStart - done)); i < frames; ++i)
                tail = std::max<double>(tail, std::max(std::fabs(left[static_cast<std::size_t>(i)]),
                                                       std::fabs(right[static_cast<std::size_t>(i)])));
    }
    return tail / parameters.outputGain;
}
}

int main()
{
    using acustra::PickingTechnique;
    constexpr double floor = acustra::matrixrelease::silenceFloor;
    // The played strings alone are below -143 dBFS at the bound on every
    // preset and technique; 120 dB under full scale leaves that its margin
    // and is still 66 dB under the floor.
    constexpr double playedStringsSilent = 1.0e-6;
    const std::array<std::pair<PickingTechnique, const char*>, 3> techniques {{
        { PickingTechnique::Finger, "Finger" }, { PickingTechnique::Pick, "Pick" }, { PickingTechnique::Thumb, "Thumb" },
    }};
    double loudestRing = 0.0, quietestStuck = 1.0;
    for (const auto& preset : acustra::factoryPresets)
        for (const auto& [technique, name] : techniques)
        {
            auto parameters = acustra::engineParameters(preset);
            parameters.picking = technique;
            const double played = tailBeforeOutput(parameters, Variant::SympatheticOff);
            expect(played < playedStringsSilent, preset.name, name,
                   "the played strings still sound at the matrix's release bound", decibels(played));
            const double ring = tailBeforeOutput(parameters, Variant::Shipping);
            expect(ring < floor, preset.name, name,
                   "the open strings' ring reaches the matrix's silence floor before Output", decibels(ring));
            loudestRing = std::max(loudestRing, ring);
        }
    // One technique a preset: a note that never ends does not depend on it.
    for (const auto& preset : acustra::factoryPresets)
    {
        const auto parameters = acustra::engineParameters(preset);
        const double stuck = tailBeforeOutput(parameters, Variant::NoteOffsLost);
        expect(stuck >= 2.0 * floor, preset.name, "its own technique",
               "a lost note-off is within 6 dB of the matrix's silence floor", decibels(stuck));
        quietestStuck = std::min(quietestStuck, stuck);
    }
    std::printf("Matrix release: floor %.2f dBFS, loudest open-string ring %.2f dBFS, quietest stuck chord %.2f dBFS, "
                "before Output\n", decibels(floor), decibels(loudestRing), decibels(quietestStuck));
    if (failures != 0)
        std::fprintf(stderr, "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
