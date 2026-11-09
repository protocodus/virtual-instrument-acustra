// Plays a list of MIDI events through acustra::Performer with one factory
// preset, at 48 kHz in 256-frame blocks, into a 32-bit float stereo WAV:
//
//   AcustraRenderEventList <preset 0-7> <events.txt> <out.wav> <seconds>
//                          [--picking 0|1|2] [--room 0-1] [--release-noise 0-1]
//                          [--axial-force 0|1] [--axial-waves 0|1]
//                          [--plain-bending-loss eta] [--wound-bending-loss eta]
//                          [--lookahead 0|1] [--pick-release-direction 0|1]
//
// --lookahead 1 turns Finger Lookahead on and drops its 50 ms from the start
// of the file, so a render with it lines up with one without it.
// --axial-force, --axial-waves and --pick-release-direction set offline
// comparison switches (PerformanceRealism), and the two bending-loss options
// the strings' bending loss (PhysicalCalibration), for A-Z candidates.
// Each line of events.txt is "seconds status data1 data2". The preset loads
// as a fresh plug-in instance does. Used for listening tests.

#include "DSP/AcustraPerformer.h"
#include "DSP/AcustraPresets.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{
struct Event
{
    long sample;
    std::array<std::uint8_t, 3> bytes;
};

void put(std::ofstream& out, std::uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        out.put(static_cast<char>((value >> (8 * i)) & 0xff));
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::puts("usage: AcustraRenderEventList <preset> <events.txt> <out.wav> <seconds>"
                  " [--picking 0|1|2] [--room 0-1] [--release-noise 0-1]"
                  " [--axial-force 0|1] [--axial-waves 0|1]"
                  " [--plain-bending-loss eta] [--wound-bending-loss eta]"
                  " [--lookahead 0|1] [--pick-release-direction 0|1]");
        return 1;
    }
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    const auto preset = static_cast<std::size_t>(std::atoi(argv[1]));
    if (preset >= acustra::factoryPresets.size())
        return 1;
    auto parameters = acustra::engineParameters(acustra::factoryPresets[preset]);
    acustra::PerformanceRealism realism {};
    acustra::PhysicalCalibration calibration = acustra::fittedPhysicalCalibration;
    bool lookahead = false;
    for (int i = 5; i + 1 < argc; i += 2)
    {
        const std::string option = argv[i];
        if (option == "--axial-force")
            realism.stringAxialForce = std::atoi(argv[i + 1]) != 0;
        else if (option == "--axial-waves")
            realism.axialWaves = std::atoi(argv[i + 1]) != 0;
        else if (option == "--plain-bending-loss")
            calibration.steelPlainBendingLoss = static_cast<float>(std::atof(argv[i + 1]));
        else if (option == "--wound-bending-loss")
            calibration.steelWoundBendingLoss = static_cast<float>(std::atof(argv[i + 1]));
        else if (option == "--picking")
            parameters.picking = static_cast<acustra::PickingTechnique>(std::atoi(argv[i + 1]));
        else if (option == "--room")
            parameters.room = static_cast<float>(std::atof(argv[i + 1]));
        else if (option == "--pick-release-direction")
            realism.pickReleaseDirection = std::atoi(argv[i + 1]) != 0;
        else if (option == "--lookahead")
            lookahead = std::atoi(argv[i + 1]) != 0;
        else if (option == "--release-noise")
            parameters.releaseNoise = static_cast<float>(std::atof(argv[i + 1]));
    }
    std::vector<Event> events;
    std::ifstream list(argv[2]);
    double seconds = 0.0;
    int status = 0, data1 = 0, data2 = 0;
    while (list >> seconds >> status >> data1 >> data2)
        events.push_back({ static_cast<long>(seconds * rate),
                           { static_cast<std::uint8_t>(status), static_cast<std::uint8_t>(data1),
                             static_cast<std::uint8_t>(data2) } });
    std::stable_sort(events.begin(), events.end(),
                     [](const Event& a, const Event& b) { return a.sample < b.sample; });

    auto performer = std::make_unique<acustra::Performer>();
    performer->engine().setPerformanceRealism(realism);
    performer->engine().setPhysicalCalibration(calibration);
    performer->setParameters(parameters);
    performer->prepare(rate, block);
    performer->setParameters(parameters);
    performer->setFingerLookahead(lookahead);
    const long skip = lookahead ? acustra::Performer::fingerLookaheadSamples(rate) : 0;
    const long total = static_cast<long>(std::atof(argv[4]) * rate);
    std::vector<float> interleaved(2 * static_cast<std::size_t>(total));
    std::array<float, block> left {}, right {};
    std::size_t next = 0;
    for (long start = 0; start < total + skip; start += block)
    {
        const int frames = static_cast<int>(std::min<long>(block, total + skip - start));
        performer->beginBlock(left.data(), right.data(), frames);
        for (; next < events.size() && events[next].sample < start + frames; ++next)
            performer->handleMidi(static_cast<int>(std::max(0L, events[next].sample - start)),
                                  events[next].bytes.data(), 3);
        performer->endBlock();
        for (int i = 0; i < frames; ++i)
        {
            const long at = start + i - skip;
            if (at < 0)
                continue;
            interleaved[2 * static_cast<std::size_t>(at)] = left[static_cast<std::size_t>(i)];
            interleaved[2 * static_cast<std::size_t>(at) + 1] = right[static_cast<std::size_t>(i)];
        }
    }
    const auto bytes = static_cast<std::uint32_t>(interleaved.size() * sizeof(float));
    std::ofstream wav(argv[3], std::ios::binary);
    wav.write("RIFF", 4);
    put(wav, 36 + bytes, 4);
    wav.write("WAVEfmt ", 8);
    put(wav, 16, 4);
    put(wav, 3, 2);
    put(wav, 2, 2);
    put(wav, 48000, 4);
    put(wav, 48000 * 8, 4);
    put(wav, 8, 2);
    put(wav, 32, 2);
    wav.write("data", 4);
    put(wav, bytes, 4);
    wav.write(reinterpret_cast<const char*>(interleaved.data()), bytes);
    return 0;
}
