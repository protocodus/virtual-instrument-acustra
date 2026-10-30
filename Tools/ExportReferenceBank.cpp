// Writes the embedded reference bank (Source/DSP/SampleBank/) as 16-bit WAVs
// and an index, so recorded takes can be measured beside renders:
//
//   AcustraExportReferenceBank <out-dir>
//
// index.tsv holds one zone per line: file, bank (0 SteelPicked, 1
// SteelPlucked), name, root MIDI, low and high velocity, round robin.
// The files stay outside the checkout.

#include "DSP/SampleBank/EmbeddedGuitarBank.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
void put(std::ofstream& out, std::uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        out.put(static_cast<char>((value >> (8 * i)) & 0xff));
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::puts("usage: AcustraExportReferenceBank <out-dir>");
        return 1;
    }
    acustra::dense::Library library;
    std::string error;
    if (!library.prepare(&error))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    const std::filesystem::path directory = argv[1];
    std::filesystem::create_directories(directory);
    std::ofstream index(directory / "index.tsv");
    for (std::size_t i = 0; i < library.zoneCount(); ++i)
    {
        const auto& zone = *library.zone(i);
        const std::string file = std::to_string(i) + ".wav";
        const auto bytes = static_cast<std::uint32_t>(zone.frames * zone.channels * 2);
        std::ofstream wav(directory / file, std::ios::binary);
        wav.write("RIFF", 4);
        put(wav, 36 + bytes, 4);
        wav.write("WAVEfmt ", 8);
        put(wav, 16, 4);
        put(wav, 1, 2);
        put(wav, zone.channels, 2);
        put(wav, zone.sampleRate, 4);
        put(wav, zone.sampleRate * zone.channels * 2, 4);
        put(wav, zone.channels * 2u, 2);
        put(wav, 16, 2);
        wav.write("data", 4);
        put(wav, bytes, 4);
        wav.write(reinterpret_cast<const char*>(zone.samples), bytes);
        index << file << '\t' << static_cast<int>(zone.bank) << '\t' << zone.name << '\t'
              << int(zone.rootMidi) << '\t' << int(zone.lowVelocity) << '\t'
              << int(zone.highVelocity) << '\t' << int(zone.roundRobin) << '\n';
    }
    return 0;
}
