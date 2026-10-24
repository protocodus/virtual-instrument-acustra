// Paired steady-state callback benchmark (CPU optimization, 2026-10-24).
// Build with Tools/BuildSteadyCallbackBenchmark.sh, which compiles a frozen
// baseline and candidate DSP tree into separate namespaces of one executable
// (each against its own headers, each with its own extra flags).
//
// Protocol: five authored performances (a re-struck held chord, alternating
// Pick strums with Room 10%, a picked scale run, a held chord under a
// continuous pitch-bend sweep, and a released chord ringing out with Room
// 10%) play through Performer with the Piezo bus connected, release noise
// 0.5 and otherwise default controls, for six seconds each, after 200 ms of
// untimed silence. Both versions render every callback; which one renders
// first alternates per callback and per repeat. Only beginBlock, handleMidi
// and endBlock are timed, as thread CPU time (CLOCK_THREAD_CPUTIME_ID), so
// preemption by other processes on a shared machine is not charged; cache
// and frequency effects of a loaded machine still are, so baseline and
// candidate must be compared only within one run. Every callback's Main L/R
// and Piezo samples of both versions are compared with memcmp; any mismatch
// makes the run exit 1. Setup, preparation and the CSV are outside the timed
// interval. Analyse with Tools/AnalyzeSteadyCallbackBenchmark.py.
// Usage: AcustraSteadyCallbackBenchmark OUT.csv REPEATS [RATE=48000]
//        [BLOCK=64] [WORKLOAD]
#include <algorithm>
#include <array>
#include <time.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#define J_(a,b) a##b
#define J(a,b) J_(a,b)

struct Ev { int sample; std::uint8_t b[3]; };
struct Workload { const char* name; int seconds; int picking; float room; std::vector<Ev> events; };

#if defined(ADAPTER)
#include "DSP/AcustraPerformer.h"
namespace {
struct State {
    acustra::Performer p;
    std::vector<float> l, r, pz;
    bool piezo {};
};
}
extern "C" void* J(ADAPTER,_create)(int rate, int block, int picking, float room, int piezo)
{
    auto* s = new State;
    acustra::EngineParameters ep;
    ep.picking = static_cast<acustra::PickingTechnique>(picking);
    ep.room = room;
    ep.releaseNoise = 0.5f;
    s->p.setParameters(ep);
    s->p.prepare(rate, block);
    s->l.assign(block, 0.f); s->r.assign(block, 0.f); s->pz.assign(block, 0.f);
    s->piezo = piezo != 0;
    return s;
}
extern "C" void J(ADAPTER,_destroy)(void* v) { delete static_cast<State*>(v); }
extern "C" void J(ADAPTER,_block)(void* v, int n, const Ev* ev, int count)
{
    auto* s = static_cast<State*>(v);
    acustra::AcustraEngine::OutputBuses buses;
    if (s->piezo) buses.piezo = s->pz.data();
    s->p.beginBlock(s->l.data(), s->r.data(), buses, n);
    for (int i = 0; i < count; ++i) s->p.handleMidi(ev[i].sample, ev[i].b, 3);
    s->p.endBlock();
}
extern "C" const float* J(ADAPTER,_out)(void* v, int which)
{
    auto* s = static_cast<State*>(v);
    return which == 0 ? s->l.data() : which == 1 ? s->r.data() : s->pz.data();
}
#else
#define DECL(n) extern "C" void* n##_create(int,int,int,float,int); extern "C" void n##_destroy(void*); \
  extern "C" void n##_block(void*,int,const Ev*,int); extern "C" const float* n##_out(void*,int);
DECL(baseline) DECL(current)
struct Api { void*(*create)(int,int,int,float,int); void(*destroy)(void*); void(*block)(void*,int,const Ev*,int); const float*(*out)(void*,int); };
static Api A[2] = {{baseline_create,baseline_destroy,baseline_block,baseline_out},{current_create,current_destroy,current_block,current_out}};

static void on(std::vector<Ev>& e, double t, int rate, int ch, int note, int vel){ e.push_back({int(t*rate+0.5), {std::uint8_t(0x90|(ch-1)), std::uint8_t(note), std::uint8_t(vel)}}); }
static void off(std::vector<Ev>& e, double t, int rate, int ch, int note){ e.push_back({int(t*rate+0.5), {std::uint8_t(0x80|(ch-1)), std::uint8_t(note), 64}}); }
static void bend(std::vector<Ev>& e, double t, int rate, int ch, int v){ e.push_back({int(t*rate+0.5), {std::uint8_t(0xe0|(ch-1)), std::uint8_t(v&127), std::uint8_t((v>>7)&127)}}); }

static std::vector<Workload> workloads(int rate)
{
    std::vector<Workload> w;
    const int chord[6] = {40,45,50,55,59,64};
    { Workload x{"held-chord",6,0,0.0f,{}}; for (double t=0.0;t<6.0;t+=2.0){ for(int i=0;i<6;++i) on(x.events,t+0.012*i,rate,1,chord[i],90);} w.push_back(x);}
    { Workload x{"pick-strums-room",6,1,0.1f,{}}; int k=0; for (double t=0.0;t<5.5;t+=0.25,++k){ for(int i=0;i<6;++i){int s=(k&1)?5-i:i; on(x.events,t+0.008*i,rate,1,chord[s],70+(k%3)*15);} } w.push_back(x);}
    { Workload x{"picked-run",6,1,0.0f,{}}; const int sc[8]={52,54,56,57,59,61,63,64}; int k=0; for(double t=0;t<5.5;t+=0.125,++k){ int n=sc[k%8]; on(x.events,t,rate,1,n,95); off(x.events,t+0.11,rate,1,n);} w.push_back(x);}
    { Workload x{"bends",6,0,0.0f,{}}; for(int i=0;i<6;++i) on(x.events,0.01*i,rate,1,chord[i]+5,90); for(double t=0.1;t<5.8;t+=0.005){ int v=8192+int(3000*std::sin(t*6.0)); bend(x.events,t,rate,1,v);} w.push_back(x);}
    { Workload x{"released-tail",6,0,0.1f,{}}; for(int i=0;i<6;++i) on(x.events,0.01*i,rate,1,chord[i],100); for(int i=0;i<6;++i) off(x.events,1.0,rate,1,chord[i]); w.push_back(x);}
    for (auto& x : w) std::stable_sort(x.events.begin(), x.events.end(), [](const Ev& a, const Ev& b){return a.sample<b.sample;});
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: AcustraSteadyCallbackBenchmark OUT.csv REPEATS [RATE] [BLOCK] [WORKLOAD]\n"); return 2; }
    FILE* out = std::fopen(argv[1], "w");
    const int repeats = std::atoi(argv[2]);
    const int rate = argc > 3 ? std::atoi(argv[3]) : 48000;
    const int block = argc > 4 ? std::atoi(argv[4]) : 64;
    std::fprintf(out, "rep,workload,callback,baseline_ns,current_ns\n");
    auto ws = workloads(rate);
    unsigned long long mismatches = 0, compared = 0;
    for (int rep = 0; rep < repeats; ++rep)
        for (auto& w : ws)
        {
            if (argc > 5 && std::strcmp(argv[5], w.name) != 0) continue;
            void* s[2] = { A[0].create(rate, block, w.picking, w.room, 1), A[1].create(rate, block, w.picking, w.room, 1) };
            const int total = w.seconds * rate;
            std::size_t ei = 0; int cb = 0;
            // untimed warmup: 200 ms of silence on both
            std::vector<Ev> none;
            for (int i = 0; i < rate / 5; i += block) { A[0].block(s[0], block, none.data(), 0); A[1].block(s[1], block, none.data(), 0); }
            for (int start = 0; start < total; start += block, ++cb)
            {
                const int n = std::min(block, total - start);
                std::vector<Ev> local;
                while (ei < w.events.size() && w.events[ei].sample < start + n) { Ev e = w.events[ei++]; e.sample -= start; local.push_back(e); }
                long long ns[2];
                const int first = (cb + rep) & 1;
                for (int k = 0; k < 2; ++k)
                {
                    const int v = k == 0 ? first : 1 - first;
                    timespec t0, t1;
                    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t0);
                    A[v].block(s[v], n, local.data(), int(local.size()));
                    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t1);
                    ns[v] = (t1.tv_sec - t0.tv_sec) * 1000000000LL + (t1.tv_nsec - t0.tv_nsec);
                }
                for (int c = 0; c < 3; ++c) { compared += n; if (std::memcmp(A[0].out(s[0], c), A[1].out(s[1], c), sizeof(float) * n) != 0) ++mismatches; }
                std::fprintf(out, "%d,%s,%d,%lld,%lld\n", rep, w.name, cb, ns[0], ns[1]);
            }
            A[0].destroy(s[0]); A[1].destroy(s[1]);
        }
    std::fclose(out);
    std::printf("compared %llu samples, mismatching blocks %llu\n", compared, mismatches);
    return mismatches ? 1 : 0;
}
#endif
