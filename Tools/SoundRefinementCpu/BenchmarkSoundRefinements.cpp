// Paired sound-changing baseline/candidate CPU evidence. Each adapter is
// compiled against its own snapshot, with acustra namespace renamed. The
// benchmark intentionally checks finite, nonzero audio rather than equality.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__SSE2__)
#include <xmmintrin.h>
#endif

struct Config
{
    int rate = 48000, block = 64, technique = 0, scenario = 0;
    float room = 0, touch = .58f;
    bool direct = false;
};
struct Event { int frame, channel, note, velocity; bool on; };
struct Contacts
{
    std::array<std::uint64_t, 6> sample {};
    std::array<int, 6> note {};
    std::array<float, 6> velocity {}, touch {};
    int active = 0;
    std::uint32_t dropped = 0;
};
struct Player
{
    void* object = nullptr;
    void (*callback)(void*, float*, float*, float*, int, int, const Event*, std::size_t) = nullptr;
    Contacts (*inspect)(const void*) = nullptr;
    void (*destroy)(void*) = nullptr;
    ~Player() { if (object) destroy(object); }
    Player() = default;
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
};
#define JOIN_IMPL(a,b) a##b
#define JOIN(a,b) JOIN_IMPL(a,b)

#if defined(ACUSTRA_TIMING_ADAPTER)
#include "DSP/AcustraPerformer.h"
namespace acustra
{
struct AcustraEngineTestAccess
{
    static Contacts inspect(const AcustraEngine& engine)
    {
        Contacts r;
        r.active = engine.getActiveVoiceCount();
        for (std::size_t i = 0; i < 6; ++i)
        {
            const auto& v = engine.voices_[i];
            r.sample[i] = v.lastPluckSample;
            r.note[i] = v.lastPluckMidiNote;
            r.velocity[i] = v.velocity;
            r.touch[i] = engine.effectiveTouch(v.velocity);
        }
        return r;
    }
};
}
namespace
{
struct Adapter
{
    acustra::Performer performer;
    bool direct;
    explicit Adapter(const Config& c) : direct(c.direct)
    {
        acustra::EngineParameters p;
        p.picking = static_cast<acustra::PickingTechnique>(c.technique);
        p.room = c.room; p.touch = c.touch; p.releaseNoise = .5f;
        performer.setParameters(p);
        performer.prepare(c.rate, c.block);
        performer.setTempoBpm(120);
        performer.setGatherChords(false);
        performer.engine().setPortObserversEnabled(true);
        if (direct) performer.engine().setStringPerChannelMode(true);
    }
};
void callback(void* handle, float* left, float* right, float* piezo,
              int count, int start, const Event* events, std::size_t eventCount)
{
    auto& a = *static_cast<Adapter*>(handle);
    if (a.direct)
    {
        // Force-edge events are aligned to callback boundaries. Direct
        // string-per-channel contacts bypass performance velocity variation.
        for (std::size_t i = 0; i < eventCount; ++i)
        {
            const auto& e = events[i];
            if (e.on) a.performer.engine().noteOn(e.note, e.velocity / 127.0f, e.channel);
            else a.performer.engine().noteOff(e.note, e.channel);
        }
        a.performer.process(left, right, acustra::AcustraEngine::OutputBuses {piezo}, count);
    }
    else
    {
        a.performer.beginBlock(left, right, acustra::AcustraEngine::OutputBuses {piezo}, count);
        for (std::size_t i = 0; i < eventCount; ++i)
        {
            const auto& e = events[i];
            if (e.on) a.performer.noteOn(e.frame - start, e.channel, e.note, e.velocity);
            else a.performer.noteOff(e.frame - start, e.channel, e.note, 82);
        }
        a.performer.endBlock();
    }
}
Contacts inspect(const void* handle)
{
    const auto& a = *static_cast<const Adapter*>(handle);
    auto result = acustra::AcustraEngineTestAccess::inspect(a.performer.engine());
    result.dropped = a.performer.droppedEventCount();
    return result;
}
}
void JOIN(make_, ACUSTRA_TIMING_ADAPTER)(Player& player, const Config& config)
{
    player.object = new Adapter(config);
    player.callback = callback;
    player.inspect = inspect;
    player.destroy = [](void* p) { delete static_cast<Adapter*>(p); };
}
#else
void make_baseline(Player&, const Config&);
void make_candidate(Player&, const Config&);
namespace
{
using Clock = std::chrono::steady_clock;
struct Evidence
{
    std::array<std::uint64_t, 6> seenSample {};
    std::array<int, 6> seenNote {-1,-1,-1,-1,-1,-1};
    double energy = 0, peak = 0;
    float velocityMin = 2, velocityMax = -1, touchMin = 2, touchMax = -1;
    int contacts = 0, maximumActive = 0;
    std::uint64_t hash = 14695981039346656037ull;
    void observe(const Player& player, const std::array<std::array<float, 128>, 3>& audio, int count)
    {
        for (const auto& bus : audio)
            for (int i = 0; i < count; ++i)
            {
                const float v = bus[static_cast<std::size_t>(i)];
                if (!std::isfinite(v)) throw std::runtime_error("nonfinite audio");
                energy += double(v) * v; peak = std::max(peak, std::abs(double(v)));
                std::uint32_t bits; std::memcpy(&bits, &v, sizeof(bits));
                for (int byte = 0; byte < 4; ++byte)
                { hash ^= (bits >> (8 * byte)) & 255u; hash *= 1099511628211ull; }
            }
        const auto state = player.inspect(player.object);
        if (state.dropped != 0) throw std::runtime_error("dropped MIDI event");
        maximumActive = std::max(maximumActive, state.active);
        for (std::size_t i = 0; i < 6; ++i)
        {
            if (!std::isfinite(state.velocity[i]) || !std::isfinite(state.touch[i]))
                throw std::runtime_error("nonfinite contact observer");
            if (state.note[i] >= 0 && (state.sample[i] != seenSample[i] || state.note[i] != seenNote[i]))
            {
                ++contacts; seenSample[i] = state.sample[i]; seenNote[i] = state.note[i];
                velocityMin = std::min(velocityMin, state.velocity[i]);
                velocityMax = std::max(velocityMax, state.velocity[i]);
                touchMin = std::min(touchMin, state.touch[i]);
                touchMax = std::max(touchMax, state.touch[i]);
            }
        }
    }
};
struct Pair { int start; bool baselineFirst, attack; double a, b; };
struct Result { Config c; std::vector<Pair> pairs; Evidence a, b; };
std::vector<Event> score(const Config& c)
{
    std::vector<Event> events;
    constexpr std::array<int, 6> chord {40,47,52,56,59,64};
    const auto frame = [&](double t) {
        const int exact = static_cast<int>(std::llround(t * c.rate));
        return c.direct ? ((exact + c.block - 1) / c.block) * c.block : exact;
    };
    const auto add = [&](double t, int velocity, bool on) {
        for (int i = 0; i < 6; ++i)
        {
            // A four-finger attack instead of a brushed six-string chord.
            if (!c.direct && c.scenario == 1 && c.technique == 0 && (i == 1 || i == 3)) continue;
            events.push_back({frame(t), c.direct ? i + 1 : 1, chord[static_cast<std::size_t>(i)], velocity, on});
        }
    };
    if (c.scenario == 0) add(.01, 100, true);
    else
    {
        add(.01, c.direct ? 127 : 87, true);
        add(.17, 0, false);
        for (int k = 0; k < 8; ++k)
        {
            constexpr std::array<int, 4> velocities {50,80,105,127};
            const double t = .25 + k * .16;
            add(t, c.direct ? 127 : velocities[static_cast<std::size_t>(k % 4)], true);
            add(t + .12, 0, false);
        }
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.frame < b.frame; });
    return events;
}
Result run(const Config& c, int runIndex, int configIndex)
{
    Player a, b; make_baseline(a, c); make_candidate(b, c);
    const auto events = score(c);
    std::array<std::array<float, 128>, 3> outputA {}, outputB {};
    Result result; result.c = c;
    const int end = static_cast<int>(1.5 * c.rate);
    result.pairs.reserve(static_cast<std::size_t>(end / c.block + 1));
    std::size_t cursor = 0;
    for (int start = 0, block = 0; start < end; start += c.block, ++block)
    {
        const int count = std::min(c.block, end - start);
        const auto eventStart = cursor;
        while (cursor < events.size() && events[cursor].frame < start + count) ++cursor;
        const auto* currentEvents = events.data() + eventStart;
        const auto eventCount = cursor - eventStart;
        const bool timed = start >= static_cast<int>(.25 * c.rate) && count == c.block;
        const bool baselineFirst = (block + runIndex + configIndex) % 2 == 0;
        const auto callbackTime = [&](Player& p, auto& output) {
            if (!timed)
            {
                p.callback(p.object, output[0].data(), output[1].data(), output[2].data(), count, start, currentEvents, eventCount);
                return 0.0;
            }
            const auto before = Clock::now();
            p.callback(p.object, output[0].data(), output[1].data(), output[2].data(), count, start, currentEvents, eventCount);
            const auto after = Clock::now();
            return std::chrono::duration<double, std::micro>(after - before).count();
        };
        double ta, tb;
        if (baselineFirst) { ta = callbackTime(a, outputA); tb = callbackTime(b, outputB); }
        else { tb = callbackTime(b, outputB); ta = callbackTime(a, outputA); }
        // Inspection and hashes remain outside both timed callbacks.
        result.a.observe(a, outputA, count); result.b.observe(b, outputB, count);
        if (timed)
        {
            const double phase = std::fmod(std::max(0.0, start / double(c.rate) - .25), .16);
            result.pairs.push_back({start, baselineFirst, c.scenario == 1 && phase < .060, ta, tb});
        }
    }
    for (const auto* e : {&result.a, &result.b})
    {
        if (!(e->energy > 1.0e-12) || e->contacts < 1 || e->maximumActive < 4)
            throw std::runtime_error("scenario did not produce intended active sound");
        if (c.direct && (e->velocityMin != 1.0f || e->velocityMax != 1.0f))
            throw std::runtime_error("direct full-force edge not reached");
    }
    return result;
}
void stats(std::ostream& out, const std::vector<Pair>& pairs, bool candidate, int attackOnly)
{
    std::vector<double> values;
    for (const auto& p : pairs)
        if (attackOnly < 0 || p.attack == (attackOnly != 0)) values.push_back(candidate ? p.b : p.a);
    std::sort(values.begin(), values.end());
    if (values.empty()) { out << "null"; return; }
    const auto q = [&](double t) { return values[static_cast<std::size_t>(t * (values.size() - 1))]; };
    out << "{\"n\":" << values.size() << ",\"median_us\":" << q(.5)
        << ",\"p95_us\":" << q(.95) << ",\"p99_us\":" << q(.99) << ",\"max_us\":" << values.back() << '}';
}
void evidence(std::ostream& out, const Evidence& e)
{
    out << "{\"audio_fnv64\":\"" << std::hex << e.hash << std::dec << "\",\"sum_squares\":" << e.energy
        << ",\"peak\":" << e.peak << ",\"observed_contacts\":" << e.contacts << ",\"maximum_active\":" << e.maximumActive
        << ",\"contact_velocity_min\":" << e.velocityMin << ",\"contact_velocity_max\":" << e.velocityMax
        << ",\"effective_touch_min\":" << e.touchMin << ",\"effective_touch_max\":" << e.touchMax << '}';
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3 && argc != 4) throw std::runtime_error("Usage: benchmark OUTPUT_PREFIX RUN_INDEX [--smoke]");
        const bool smoke = argc == 4 && std::string(argv[3]) == "--smoke";
        const int runIndex = std::stoi(argv[2]);
        std::vector<Config> configs;
        for (int rate : {48000,96000}) for (int block : {64,128}) for (int technique : {0,1})
            for (int scenario : {0,1}) for (float room : {0.0f,.5f,1.0f})
                configs.push_back({rate,block,technique,scenario,room,.58f,false});
        for (int rate : {48000,96000}) for (int technique : {0,1}) for (float touch : {0.0f,1.0f})
            configs.push_back({rate,64,technique,1,.5f,touch,true});
        if (smoke) { const auto edge = configs.back(); configs = {configs.front(), configs[9], edge}; }
        std::vector<Result> results;
        for (std::size_t i = 0; i < configs.size(); ++i) results.push_back(run(configs[i], runIndex, static_cast<int>(i)));
        // All filesystem writes happen after every timed scenario.
        std::ofstream out(std::string(argv[1]) + ".json"), raw(std::string(argv[1]) + ".csv");
        if (!out || !raw) throw std::runtime_error("cannot open output files");
        out << std::setprecision(10); raw << std::setprecision(10);
        out << "{\"run_index\":" << runIndex << ",\"smoke_only\":" << (smoke ? "true" : "false")
            << ",\"warmup_seconds\":0.25,\"scenario_seconds\":1.5,\"checks\":\"finite nonzero audio, actual contact state, no dropped events; audio parity intentionally not required\"";
#if defined(__SSE2__)
        out << ",\"mxcsr\":" << _mm_getcsr();
#endif
        out << ",\"configurations\":[\n";
        raw << "configuration,frame,baseline_first,attack_window,baseline_us,candidate_us\n";
        for (std::size_t i = 0; i < results.size(); ++i)
        {
            const auto& r = results[i]; const auto& c = r.c;
            if (i) out << ",\n";
            out << "{\"index\":" << i << ",\"rate\":" << c.rate << ",\"block\":" << c.block
                << ",\"technique\":\"" << (c.technique ? "Pick" : "Finger") << "\",\"scenario\":\""
                << (c.direct ? "direct_full_force_edges" : c.scenario ? "repeated_attacks" : "held_chord") << "\",\"room\":" << c.room << ",\"touch\":" << c.touch;
            out << ",\"baseline\":"; stats(out,r.pairs,false,-1);
            out << ",\"candidate\":"; stats(out,r.pairs,true,-1);
            out << ",\"attack_baseline\":"; stats(out,r.pairs,false,1);
            out << ",\"attack_candidate\":"; stats(out,r.pairs,true,1);
            out << ",\"between_attacks_baseline\":"; stats(out,r.pairs,false,0);
            out << ",\"between_attacks_candidate\":"; stats(out,r.pairs,true,0);
            out << ",\"baseline_evidence\":"; evidence(out,r.a);
            out << ",\"candidate_evidence\":"; evidence(out,r.b);
            out << '}';
            for (const auto& p : r.pairs) raw << i << ',' << p.start << ',' << p.baselineFirst << ',' << p.attack << ',' << p.a << ',' << p.b << '\n';
        }
        out << "\n]}\n";
        std::cout << results.size() << " paired configurations; outputs " << argv[1] << ".{json,csv}\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
#endif
