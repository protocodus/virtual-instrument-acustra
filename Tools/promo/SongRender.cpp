// SongRender <patch> <in.mid> <out.wav> <trace.csv> [--oversampling 1|2|4] [--tempo bpm]
// One Acustra playing a performed part as the Rack device plays it, set as
// its factory patch <patch> sets it, at a 48 kHz host rate: the Performer at
// 48 kHz times the oversampling factor, the device's decimator (its
// OutputRate.h, from the Rack repository's Examples/Acustra, on the include
// path) and its host-rate ceiling stage. Notes reach the Performer as
// Reason's note states do (noteOn, and noteOff at the nominal firmness 64),
// on the 64-frame batches the device renders. The Rack patches mirror
// AcustraPresets.h value for value; the two settings a patch holds outside
// it are sent as the device sends them: Palm Muted Steel Pick's Bridge Hand
// as CC2, Open G Slide-ready's 12-semitone bend range. --tempo is Reason's
// song tempo in quarter notes a minute (the key-up join and the stroke
// alternation follow it).
//
// The trace, at 60 Hz, reads the engine's read-only getStringActivity() at
// the output time it describes (the device's latency trimmed off): per string
// "level:state:fret:note", level the smoothed string reaction force, state 0
// quiet or ringing in sympathy only, 1 played (plucked and sounding), 2 its
// key down; fret and note what the string is stopped at. The first row lists
// each string's open note.
#include "../../Source/DSP/AcustraPerformer.h"
#include "../../Source/DSP/AcustraPresets.h"
#include "../../Source/DSP/OutputStage.h"
#include "OutputRate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{

struct Event { double seconds; unsigned char status, data1, data2; };

unsigned readVariable(const std::vector<unsigned char>& b, std::size_t& i)
{
    unsigned v = 0;
    for (int k = 0; k < 4 && i < b.size(); ++k) { const unsigned char c = b[i++]; v = (v << 7) | (c & 0x7f); if (!(c & 0x80)) break; }
    return v;
}

// Format 0 or 1 with PPQ division; tempo changes in any track apply to all.
std::vector<Event> readMidi(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    const std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), {});
    if (b.size() < 14 || std::memcmp(b.data(), "MThd", 4) != 0) { std::fprintf(stderr, "not a MIDI file: %s\n", path); std::exit(1); }
    const int tracks = b[10] << 8 | b[11], ppq = b[12] << 8 | b[13];
    struct Raw { unsigned long tick; int order; unsigned char s, d1, d2; unsigned tempo; };
    std::vector<Raw> raw;
    std::size_t at = 14;
    for (int t = 0; t < tracks && at + 8 <= b.size(); ++t)
    {
        const std::size_t length = static_cast<std::size_t>(b[at + 4]) << 24 | b[at + 5] << 16 | b[at + 6] << 8 | b[at + 7];
        std::size_t i = at + 8;
        const std::size_t end = i + length;
        unsigned long tick = 0;
        unsigned char running = 0;
        while (i < end)
        {
            tick += readVariable(b, i);
            unsigned char s = b[i];
            if (s == 0xff) { const unsigned char type = b[i + 1]; i += 2; const unsigned n = readVariable(b, i);
                if (type == 0x51 && n == 3) raw.push_back({ tick, 0, 0, 0, 0, static_cast<unsigned>(b[i] << 16 | b[i + 1] << 8 | b[i + 2]) });
                i += n; continue; }
            if (s == 0xf0 || s == 0xf7) { ++i; i += readVariable(b, i); continue; }
            if (s & 0x80) { running = s; ++i; } else s = running;
            const int kind = s & 0xf0;
            const unsigned char d1 = b[i], d2 = (kind == 0xc0 || kind == 0xd0) ? 0 : b[i + 1];
            i += (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
            raw.push_back({ tick, static_cast<int>(raw.size()) + 1, s, d1, d2, 0 });
        }
        at = end;
    }
    std::stable_sort(raw.begin(), raw.end(), [] (const Raw& a, const Raw& c) { return a.tick < c.tick; });
    std::vector<Event> events;
    double seconds = 0.0, tempo = 500000.0;
    unsigned long last = 0;
    for (const Raw& r : raw)
    {
        seconds += (r.tick - last) * tempo / 1e6 / ppq;
        last = r.tick;
        if (r.tempo) tempo = r.tempo; else events.push_back({ seconds, r.s, r.d1, r.d2 });
    }
    return events;
}

void writeWav24(const char* path, const std::vector<float>& l, const std::vector<float>& r, int rate)
{
    std::ofstream f(path, std::ios::binary);
    const auto put = [&] (unsigned v, int n) { for (int k = 0; k < n; ++k) f.put(static_cast<char>((v >> (8 * k)) & 0xff)); };
    const unsigned bytes = static_cast<unsigned>(l.size()) * 6u;
    f.write("RIFF", 4); put(36 + bytes, 4); f.write("WAVEfmt ", 8); put(16, 4); put(1, 2); put(2, 2);
    put(static_cast<unsigned>(rate), 4); put(static_cast<unsigned>(rate) * 6, 4); put(6, 2); put(24, 2); f.write("data", 4); put(bytes, 4);
    for (std::size_t i = 0; i < l.size(); ++i)
        for (const float s : { l[i], r[i] })
            put(static_cast<unsigned>(static_cast<int>(std::lround(std::clamp(static_cast<double>(s), -1.0, 1.0) * 8388607.0))), 3);
}

} // namespace

int main(int argc, char** argv)
{
    const acustra::FactoryPreset* preset = nullptr;
    if (argc >= 2)
        for (const auto& p : acustra::factoryPresets)
            if (std::string(p.name) == argv[1]) preset = &p;
    if (argc < 5 || !preset) { std::fprintf(stderr, "usage: SongRender <patch> <in.mid> <out.wav> <trace.csv> [--oversampling 1|2|4] [--tempo bpm]\n"); return 2; }
    int factor = 1;
    double tempo = 120.0;
    for (int i = 5; i + 1 < argc; i += 2)
    {
        if (std::string(argv[i]) == "--oversampling") factor = std::atoi(argv[i + 1]);
        if (std::string(argv[i]) == "--tempo") tempo = std::atof(argv[i + 1]);
    }
    if (factor != 1 && factor != 2 && factor != 4) { std::fprintf(stderr, "oversampling is 1, 2 or 4\n"); return 2; }
    constexpr int rate = 48000, batch = 64, step = 800;   // the device's batch; the trace's 60 Hz
    constexpr int channel = 1;
    auto performer = std::make_unique<acustra::Performer>();
    performer->setParameters(acustra::engineParameters(*preset));
    performer->prepare(rate * factor, batch * factor);
    performer->engine().setPortObserversEnabled(false);
    performer->setTempoBpm(tempo);
    const std::string name = preset->name;
    // The Rack patch's Bridge Hand (0.55 on Palm Muted Steel Pick) as the device spells it, and its bend range.
    if (name == "Palm Muted Steel Pick") performer->controlChange(0, channel, 2, static_cast<int>(0.55f * 127.0f + 0.5f));
    performer->setPitchBendRange(0, channel, name == "Open G Slide-ready" ? 12 : 2);
    auto rateStage = std::make_unique<acustra_rack::OutputRate>();
    rateStage->reset(factor);
    protocodus::OutputStage<2> ceiling;
    ceiling.Prepare(rate);
    const int latency = acustra_rack::OutputRate::latencySamples + 2 * protocodus::OutputStage<2>::DelayFor(rate);

    const auto events = readMidi(argv[2]);
    std::size_t total = static_cast<std::size_t>(std::ceil((events.back().seconds + 4.0) * rate)) + latency;
    total = (total + batch - 1) / batch * batch;
    std::vector<float> left(total), right(total), piezo(batch);
    std::vector<float> inL(batch * factor), inR(batch * factor), inP(batch * factor);
    std::ofstream csv(argv[4]);
    csv << "open";
    for (const auto& s : performer->engine().getStringActivity()) csv << ',' << s.openMidi;
    csv << '\n';
    std::size_t next = 0;
    for (std::size_t done = 0; done < total; done += batch)
    {
        acustra::AcustraEngine::OutputBuses buses { inP.data() };
        performer->beginBlock(inL.data(), inR.data(), buses, batch * factor);
        while (next < events.size() && static_cast<std::size_t>(std::llround(events[next].seconds * rate)) < done + batch)
        {
            const Event& e = events[next++];
            const int offset = static_cast<int>(std::max<long long>(0, std::llround(e.seconds * rate) - static_cast<long long>(done))) * factor;
            const int kind = e.status & 0xf0;
            if (kind == 0x90 && e.data2 > 0) performer->noteOn(offset, channel, e.data1, e.data2);
            else if (kind == 0x80 || kind == 0x90) performer->noteOff(offset, channel, e.data1);
            else if (kind == 0xb0) performer->controlChange(offset, channel, e.data1, e.data2);
        }
        performer->endBlock();
        rateStage->process(inL.data(), inR.data(), inP.data(), &left[done], &right[done], piezo.data(), batch);
        for (std::size_t i = done; i < done + batch; ++i)
        {
            std::array<float, 2> frame { left[i], right[i] };
            ceiling.Process(frame);
            left[i] = frame[0]; right[i] = frame[1];
        }
        // the observer describes what leaves the device `latency` samples later
        const long long out0 = static_cast<long long>(done) - latency, out1 = out0 + batch;
        const long long mark = (out1 / step) * step;
        if (mark > out0 && mark > 0)
        {
            char cell[64];
            std::snprintf(cell, sizeof cell, "%.4f", static_cast<double>(mark) / rate);
            csv << cell;
            for (const auto& s : performer->engine().getStringActivity())
            {
                std::snprintf(cell, sizeof cell, ",%.3g:%d:%d:%d", s.level, s.keyDown ? 2 : s.played ? 1 : 0, s.fret, s.midiNote);
                csv << cell;
            }
            csv << '\n';
        }
    }
    left.erase(left.begin(), left.begin() + latency);
    right.erase(right.begin(), right.begin() + latency);
    float peak = 0.0f;
    for (std::size_t i = 0; i < left.size(); ++i) peak = std::max({ peak, std::fabs(left[i]), std::fabs(right[i]) });
    writeWav24(argv[3], left, right, rate);
    std::printf("%s: %s, %.1f s, %dx, peak %.2f dBFS, %u events dropped\n", argv[3], argv[1], left.size() / double(rate), factor,
                20 * std::log10(peak + 1e-12), performer->droppedEventCount());
}
