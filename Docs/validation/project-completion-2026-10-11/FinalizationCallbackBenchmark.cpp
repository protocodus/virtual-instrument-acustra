// Current-model finalization callback harness. Adds held master tuning,
// simultaneous construction/capture controls, and verified queued contacts.
// Native, paired callback diagnostic. No timing threshold is a CI assertion.
// Compile this file three times: once normally for main, once per frozen DSP
// tree with -DACUSTRA_CALLBACK_ADAPTER=baseline/current and respectively
// -Dacustra=acustra_baseline/acustra_current. Compile each AcustraEngine.cpp
// with the same namespace definition and include its own Source directory.
// Link the seven objects (including both Performer objects) into one executable. No LTO or fast-math is required.
// All cases use the current steel-string model. The paired reference is the
// finalized model before exact-performance optimizations, never the older audit model.
// Usage: FinalCallbacks OUTPUT.json [pairs=32] [warmup=4]
//        FinalCallbacks --parity-only OUTPUT.json
// Output contains every timed observation; construction, preparation, scenario
// pre-roll and result checking are outside the interval. Each pair alternates order.

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#define ACUSTRA_JOIN_INNER(a, b) a##b
#define ACUSTRA_JOIN(a, b) ACUSTRA_JOIN_INNER(a, b)
#define ACUSTRA_SYMBOL_INNER(a, b) ACUSTRA_JOIN(ACUSTRA_JOIN(callback_, a), b)

#if defined(ACUSTRA_CALLBACK_ADAPTER)

#include "DSP/AcustraPerformer.h"
namespace acustra { struct AcustraEngineTestAccess {
    static bool noQueuedContacts(const AcustraEngine& e) {
        for (const auto& v : e.voices_) if (v.pluckDelay > 0) return false;
        return true;
    }
}; }
#define ACUSTRA_SYMBOL(b) ACUSTRA_SYMBOL_INNER(ACUSTRA_CALLBACK_ADAPTER, b)

namespace
{
struct CallbackState
{
    acustra::Performer player;
    acustra::AcustraEngine& engine { player.engine() };
    std::array<float, 128> left {}, right {};
    int rate {}, frames {};
    float room {};
    static constexpr std::array chord { 40, 47, 52, 56, 59, 64 };

    CallbackState(int sampleRate, int blockSize, float roomAmount)
        : rate(sampleRate), frames(blockSize), room(roomAmount)
    {
        acustra::EngineParameters parameters;
        parameters.capture = acustra::CaptureType::StereoMic;
        parameters.picking = acustra::PickingTechnique::Finger;
        parameters.shape = acustra::BodyShape::Dreadnought;
        parameters.bodyMaterial = acustra::BodyMaterial::Spruce;
        parameters.touch = 0.58f;
        parameters.pluckPosition = 0.28f;
        parameters.room = room;
        player.setParameters(parameters);
        player.prepare(rate, frames);
    }

    void playChord()
    {
        for (int string = 0; string < 6; ++string)
            engine.noteOn(chord[static_cast<std::size_t>(string)], 1.0f, string + 1);
    }

    void setup(int scenario)
    {
        engine.reset();
        engine.setStringPerChannelMode(scenario != 2 && scenario != 3);
        const auto preroll = [&](int samples)
        {
            for (int remaining = samples; remaining > 0;)
            {
                const int count = std::min(remaining, frames);
                engine.process(left.data(), right.data(), count);
                remaining -= count;
            }
        };
        if (scenario == 1 || scenario == 4 || scenario == 5)
        {
            playChord();
            preroll(rate / 20);
        }
        else if (scenario == 6)
        {
            engine.beginStrum();
            for (int string = 0; string < 6; ++string)
                engine.noteOn(chord[static_cast<std::size_t>(string)], 1.0f,
                    string + 1, (string + 1) * std::max(1, frames / 16), true);
        }
        else if (scenario == 3)
        {
            engine.noteOn(60, 1.0f);
            preroll(rate / 100);
            engine.noteOn(64, 1.0f);
            preroll(rate / 100);
        }
    }

    void callback(int scenario)
    {
        if (scenario == 4)
        {
            player.setMasterTuneCents(50.0f);
            player.process(left.data(), right.data(), frames);
            return;
        }
        if (scenario == 5)
        {
            acustra::EngineParameters p;
            p.touch = 0.58f;
            p.pluckPosition = 0.28f;
            p.shape = acustra::BodyShape::Parlor;
            p.bodyMaterial = acustra::BodyMaterial::Mahogany;
            p.capture = acustra::CaptureType::MonoMic;
            p.room = room;
            p.piezoMix = 1.0f;
            p.outputGain = 2.0f;
            player.setParameters(p);
            player.process(left.data(), right.data(), frames);
            return;
        }
        if (scenario == 6)
        {
            player.process(left.data(), right.data(), frames);
            return;
        }
        if (scenario == 2)
            engine.noteOn(100, 1.0f); // high E's eighth natural harmonic
        else if (scenario == 3)
            engine.noteOn(67, 1.0f); // joins two already sounded keyboard notes
        else
            playChord();
        engine.process(left.data(), right.data(), frames);
    }

    std::uint64_t checksum() const
    {
        if (!acustra::AcustraEngineTestAccess::noQueuedContacts(engine))
            throw std::runtime_error("queued contacts did not reach their deadlines");
        std::uint64_t hash = 14695981039346656037ULL;
        double energy = 0.0;
        for (int index = 0; index < frames; ++index)
            for (const float value : { left[static_cast<std::size_t>(index)],
                                       right[static_cast<std::size_t>(index)] })
            {
                if (!std::isfinite(value))
                    throw std::runtime_error("non-finite callback output");
                energy += static_cast<double>(value) * value;
                hash ^= std::bit_cast<std::uint32_t>(value);
                hash *= 1099511628211ULL;
            }
        // A measured microphone delay may legitimately exceed a short first
        // block. Active-note checks still verify that the event was accepted.
        if (!std::isfinite(energy))
            throw std::runtime_error("non-finite callback energy");
        return hash;
    }
};
}

extern "C" void* ACUSTRA_SYMBOL(_create)(int rate, int frames, float room)
{
    return new CallbackState(rate, frames, room);
}
extern "C" void ACUSTRA_SYMBOL(_destroy)(void* state)
{
    delete static_cast<CallbackState*>(state);
}
extern "C" void ACUSTRA_SYMBOL(_setup)(void* state, int scenario)
{
    static_cast<CallbackState*>(state)->setup(scenario);
}
extern "C" void ACUSTRA_SYMBOL(_run)(void* state, int scenario)
{
    static_cast<CallbackState*>(state)->callback(scenario);
}
extern "C" std::uint64_t ACUSTRA_SYMBOL(_checksum)(void* state)
{
    return static_cast<CallbackState*>(state)->checksum();
}
extern "C" int ACUSTRA_SYMBOL(_active)(void* state)
{
    return static_cast<CallbackState*>(state)->engine.getActiveVoiceCount();
}

extern "C" std::uint64_t ACUSTRA_SYMBOL(_continuation)(void* state, int samples)
{
    auto& s = *static_cast<CallbackState*>(state);
    std::uint64_t hash = 14695981039346656037ULL;
    for (int left = samples; left > 0;)
    {
        const int count = std::min(left, s.frames);
        s.player.process(s.left.data(), s.right.data(), count);
        for (int i = 0; i < count; ++i)
            for (float value : {s.left[static_cast<std::size_t>(i)], s.right[static_cast<std::size_t>(i)]})
            {
                if (!std::isfinite(value)) throw std::runtime_error("nonfinite continuation");
                hash ^= std::bit_cast<std::uint32_t>(value);
                hash *= 1099511628211ULL;
            }
        left -= count;
    }
    return hash;
}
#else
#include <time.h>
#define ACUSTRA_DECLARE(name) \
    extern "C" void* ACUSTRA_SYMBOL_INNER(name, _create)(int, int, float); \
    extern "C" void ACUSTRA_SYMBOL_INNER(name, _destroy)(void*); \
    extern "C" void ACUSTRA_SYMBOL_INNER(name, _setup)(void*, int); \
    extern "C" void ACUSTRA_SYMBOL_INNER(name, _run)(void*, int); \
    extern "C" std::uint64_t ACUSTRA_SYMBOL_INNER(name, _checksum)(void*); \
    extern "C" std::uint64_t ACUSTRA_SYMBOL_INNER(name, _continuation)(void*, int); \
    extern "C" int ACUSTRA_SYMBOL_INNER(name, _active)(void*);
ACUSTRA_DECLARE(baseline)
ACUSTRA_DECLARE(current)
namespace {
using Clock = std::chrono::steady_clock;
static_assert(Clock::is_steady);
std::uint64_t threadNanoseconds()
{
    timespec now {};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0)
        throw std::runtime_error("thread CPU clock unavailable");
    return static_cast<std::uint64_t>(now.tv_sec) * 1000000000ULL + now.tv_nsec;
}
struct Observation { double wall {}, cpu {}; };
struct EngineApi {
    void* (*create)(int,int,float);
    void (*destroy)(void*);
    void (*setup)(void*,int);
    void (*run)(void*,int);
    std::uint64_t (*checksum)(void*);
    std::uint64_t (*continuation)(void*,int);
    int (*active)(void*);
};
#define ACUSTRA_API(name) EngineApi {callback_##name##_create, callback_##name##_destroy, \
    callback_##name##_setup, callback_##name##_run, callback_##name##_checksum, \
    callback_##name##_continuation, callback_##name##_active}
struct EngineInstance {
    EngineApi api;
    void* state {};
    int rate {}, frames {};
    float room {};
    std::uint64_t expectedHash {};
    bool hasHash {};
    EngineInstance(EngineApi a,int r,int f,float wet=0.0f):api(a),rate(r),frames(f),room(wet){}
    ~EngineInstance(){api.destroy(state);}
    EngineInstance(const EngineInstance&)=delete;
    void prepare(int scenario) {
        api.destroy(state); state=api.create(rate,frames,room); api.setup(state,scenario);
    }
    void check(int scenario) {
        const auto hash=api.checksum(state);
        if(hasHash && hash!=expectedHash) throw std::runtime_error("own-build trial output changed");
        expectedHash=hash; hasHash=true;
        if(api.active(state)!=(scenario==2?1:scenario==3?3:6))
            throw std::runtime_error("unexpected active-string count");
    }
    Observation measure(int scenario) {
        prepare(scenario);
        const auto wallStart=Clock::now();
        const auto cpuStart=threadNanoseconds();
        api.run(state,scenario);
        const auto cpuStop=threadNanoseconds();
        const auto wallStop=Clock::now();
        check(scenario);
        return {std::chrono::duration<double,std::micro>(wallStop-wallStart).count(),
                static_cast<double>(cpuStop-cpuStart)/1000.0};
    }
    void runUntimed(int scenario) {prepare(scenario);api.run(state,scenario);check(scenario);}
};
double percentile(std::vector<double> values,double q) {
    std::sort(values.begin(),values.end());
    return values[std::min(static_cast<std::size_t>(std::ceil(q*values.size()))-1,values.size()-1)];
}
void samples(std::ostream& out,const std::vector<double>& values) {
    out<<'[';for(std::size_t i=0;i<values.size();++i)out<<(i?",":"")<<values[i];out<<']';
}
void stats(std::ostream& out,const std::vector<double>& values,double deadline) {
    const auto over=std::count_if(values.begin(),values.end(),[=](double v){return v>deadline;});
    out<<"{\"p50_us\":"<<percentile(values,.5)<<",\"p95_us\":"<<percentile(values,.95)
       <<",\"max_us\":"<<*std::max_element(values.begin(),values.end())
       <<",\"over_deadline_count\":"<<over<<",\"samples_us\":";
    samples(out,values);out<<'}';
}
void observations(std::ostream& out,const std::vector<Observation>& values,double deadline,std::uint64_t hash) {
    std::vector<double> wall,cpu;int wallOnly=0;
    for(const auto& v:values){wall.push_back(v.wall);cpu.push_back(v.cpu);wallOnly+=v.wall>deadline&&v.cpu<=deadline;}
    out<<"{\"wall\":";stats(out,wall,deadline);out<<",\"thread_cpu\":";stats(out,cpu,deadline);
    out<<",\"wall_only_overrun_count\":"<<wallOnly<<",\"output_fnv64\":\""<<hash<<"\"}";
}
bool continuation(std::ostream& out,EngineInstance& before,EngineInstance& after,int samples) {
    const auto b=before.api.continuation(before.state,samples),c=after.api.continuation(after.state,samples);
    const bool first=before.expectedHash==after.expectedHash,next=b==c;
    out<<"{\"samples\":"<<samples<<",\"callback_equal\":"<<(first?"true":"false")
       <<",\"continuation_equal\":"<<(next?"true":"false")
       <<",\"baseline_callback_fnv64\":\""<<before.expectedHash<<"\",\"current_callback_fnv64\":\""<<after.expectedHash
       <<"\",\"baseline_continuation_fnv64\":\""<<b<<"\",\"current_continuation_fnv64\":\""<<c<<"\"}";
    return first&&next;
}
int positive(const char* raw){std::size_t n=0;std::string s(raw);int v=std::stoi(s,&n);if(n!=s.size()||v<1||v>100000)throw std::runtime_error("invalid count");return v;}
}
int main(int argc,char**argv) {
    try {
        const bool parityOnly=argc>1&&std::string(argv[1])=="--parity-only";
        if((parityOnly&&argc!=3)||(!parityOnly&&(argc<2||argc>4)))
            throw std::runtime_error("usage: FinalCallbacks OUTPUT.json [pairs=32] [warmup=4] OR --parity-only OUTPUT.json");
        const char* destination=argv[parityOnly?2:1];
        const int repeats=parityOnly?0:argc>2?positive(argv[2]):32;
        const int warmups=parityOnly?0:argc>3?positive(argv[3]):4;
        if(std::filesystem::exists(destination))throw std::runtime_error("output exists");
        std::ofstream out(destination);if(!out)throw std::runtime_error("cannot create output");
        out<<std::setprecision(12)<<"{\n\"protocol\":\"current_model_final_callbacks_v2\","
           <<"\n\"mode\":\""<<(parityOnly?"optimization_exact_parity":"final_combined_timing")<<"\","
           <<"\n\"scope\":\"native DSP plus Performer; excludes JUCE and host overhead\","
           <<"\n\"clock_scope\":\"steady wall enclosing two CLOCK_THREAD_CPUTIME_ID reads; no overhead subtraction\","
           <<"\n\"overrun_interpretation\":\"thread CPU overrun establishes CPU work beyond deadline; wall-only overrun retains elapsed-time miss without attributing its cause\","
           <<"\n\"setup\":\"fresh prepared engines per trial; creation, preparation, preroll, parity checks excluded\","
           <<"\n\"ordering\":\"baseline first on even trials, candidate first on odd; nearest-rank quantiles\","
           <<"\n\"velocity\":1.0,\n\"touch\":0.58,\n\"pluck_position\":0.28,"
           <<"\n\"picking\":\"finger\",\n\"capture\":\"stereo_mic\","
           <<"\n\"initial_shape\":\"dreadnought\",\n\"initial_wood\":\"spruce\","
           <<"\n\"construction_change\":\"parlor/mahogany, mono mic, piezo mix1, output gain2\","
           <<"\n\"chord_midi\":[40,47,52,56,59,64],\n\"repick_preroll_seconds\":0.05,"
           <<"\n\"harmonic_midi\":100,\n\"rolled_chord_midi\":[60,64,67],\n\"rolled_spacing_seconds\":0.01,"
           <<"\n\"room\":0,\n\"wet_reference_probe_rate\":48000,\n\"measured_pairs\":"<<repeats
           <<",\n\"warmup_pairs\":"<<warmups<<",\n\"results\":[\n";
        constexpr std::array names{"six_string_initial","six_string_repick","eighth_harmonic",
            "live_rolled_chord_join","held_master_tune","held_construction_capture_transition","queued_chord_contacts"};
        bool first=true,allEqual=true;
        for(int rate:{48000,96000})for(int frames:{32,64,128})for(int scenario=0;scenario<7;++scenario) {
            EngineInstance b(ACUSTRA_API(baseline),rate,frames),c(ACUSTRA_API(current),rate,frames);
            std::vector<Observation> before,after;
            if(parityOnly){b.runUntimed(scenario);c.runUntimed(scenario);}
            else for(int trial=-warmups;trial<repeats;++trial) {
                Observation bv,cv;
                if((trial&1)==0){bv=b.measure(scenario);cv=c.measure(scenario);}
                else{cv=c.measure(scenario);bv=b.measure(scenario);}
                if(trial>=0){before.push_back(bv);after.push_back(cv);}
            }
            const double deadline=1.0e6*frames/rate;
            out<<(first?"":",\n")<<"{\"rate\":"<<rate<<",\"frames\":"<<frames
               <<",\"scenario\":\""<<names[scenario]<<"\",\"deadline_us\":"<<deadline;
            if(!parityOnly){out<<",\"baseline\":";observations(out,before,deadline,b.expectedHash);
                out<<",\"current\":";observations(out,after,deadline,c.expectedHash);}
            out<<",\"untimed_audio_observation\":";allEqual=continuation(out,b,c,rate/4)&&allEqual;out<<'}';out.flush();first=false;
            std::cout<<rate<<" / "<<frames<<" / "<<names[scenario]<<" complete\n"<<std::flush;
        }
        EngineInstance b(ACUSTRA_API(baseline),48000,64,1.0f),c(ACUSTRA_API(current),48000,64,1.0f);
        b.runUntimed(5);c.runUntimed(5);
        out<<"\n],\n\"untimed_48k_room_one_reference\":";
        allEqual=continuation(out,b,c,96000)&&allEqual;
        out<<",\n\"all_audio_equal\":"<<(allEqual?"true":"false")<<",\n\"complete\":true\n}\n";out.close();
        if(!out)throw std::runtime_error("output write failed");
        if(parityOnly&&!allEqual)throw std::runtime_error("optimization-only exact audio parity failed");
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    return 0;
}
#endif
