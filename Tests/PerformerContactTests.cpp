// External finger work, pull-off geometry and independently timed Finger pairs.
// The physical invariants are separate from the authored gesture magnitudes.
#include "DSP/AcustraPerformer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace acustra {
struct AcustraEngineTestAccess {
    static auto& voice(AcustraEngine& e, int s=0) { return e.voices_[s]; }
    static double slope(const AcustraEngine& e) {
        double sum=0;
        for (const auto& l:e.voices_[0].loops) {
            const int n=std::clamp(static_cast<int>(std::round(l.currentDelay)),8,AcustraEngine::maximumDelaySamples-3);
            const auto at=[&](int age){return l.delay[(l.writeIndex-age+AcustraEngine::maximumDelaySamples)%AcustraEngine::maximumDelaySamples];};
            float old=at(n);
            for(int age=n-1;age>=0;--age){const float x=at(age);const double d=double(x)-old;sum+=d*d;old=x;}
        }
        return sum;
    }
    static void quiet(AcustraEngine& e) {for(auto& l:e.voices_[0].loops) l.delay.fill(0.0f);}
    static double contactEnergy(AcustraEngine& e) {
        const auto& v=voice(e);double old=0,sum=0;
        for(int i=0;i<=v.legatoContactSamples;++i){double x=double(v.legatoContactAmplitude)*v.legatoContactPulse[i];sum+=(x-old)*(x-old);old=x;}
        return sum;
    }
    static std::vector<std::array<float,2>> arrivals(AcustraEngine& e) {
        const auto& v=voice(e);auto t=v.legatoContactTravel;int age=v.legatoContactAge,n=v.legatoContactSamples;
        std::vector<std::array<float,2>> out;
        while(t.active || n>0){float x=0;if(n>0){x=v.legatoContactAmplitude*v.legatoContactPulse[age];if(++age>n)n=0;}auto p=t.process(x);float local=0.7071067811865475f*(p[0]-p[1]);out.push_back({v.legatoNormalGain*local,v.legatoParallelGain*local});}
        return out;
    }
};
}
namespace {
using Engine=acustra::AcustraEngine;
using Access=acustra::AcustraEngineTestAccess;
int failures=0;
void expect(bool yes,const char* text){if(!yes){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
void advance(Engine& e,int n){std::array<float,127> l{},r{};while(n>0){int c=std::min(n,127);e.process(l.data(),r.data(),c);for(int i=0;i<c;++i)expect(std::isfinite(l[i])&&std::isfinite(r[i]),"nonfinite contact render");n-=c;}}
std::unique_ptr<Engine> engine(bool hammer,bool enabled,double rate=48000) {
    auto e=std::make_unique<Engine>();acustra::PerformanceRealism f;f.hammerWork=enabled;f.pullOffPluck=enabled;
    e->setPerformanceRealism(f);e->prepare(rate,127);e->setStringPerChannelMode(true);e->noteOn(hammer?43:45,.8f);advance(*e,int(.1*rate));return e;
}
void workAndRates(){
    for(bool hammer:{false,true}) {
        double reference=0;
        for(double rate:{8000.,44100.,48000.,96000.,384000.}) {
            auto e=engine(hammer,true,rate);Access::quiet(*e);
            expect(e->transitionNote(hammer?43:45,hammer?45:43,.8f),"quiet held source transition rejected");
            const auto& v=Access::voice(*e);double work=Access::contactEnergy(*e);
            expect(work>0 && work*rate<2,"independent finger work absent or unbounded");
            expect(v.legatoContactPulse.front()==0 && v.legatoContactPulse[v.legatoContactSamples]==0,"contact did not start/end at rest");
            expect(hammer?(v.legatoNormalGain==1&&v.legatoParallelGain==0):(v.legatoNormalGain==.5f&&v.legatoParallelGain>.86f),"wrong gesture polarization");
            expect(hammer?v.legatoContactAmplitude<0:v.legatoContactAmplitude>0,"contact work followed residual phase instead of finger direction");
            expect(std::abs(v.legatoNormalGain*v.legatoNormalGain+v.legatoParallelGain*v.legatoParallelGain-1)<1e-6,"polarization added energy");
            const double seconds=double(v.legatoContactSamples)/rate;
            expect(std::abs(seconds-(hammer?.002:.003))<=1/rate,"contact duration changed with rate");
            if(rate==8000.)reference=work*rate;else expect(std::abs(work*rate/reference-1)<.025,"continuous finger work changed with rate");
            auto softer=engine(hammer,true,rate);Access::quiet(*softer);expect(softer->transitionNote(hammer?43:45,hammer?45:43,.4f),"contact fixture transition rejected");
            expect(std::abs(Access::contactEnergy(*softer)/work-1./16)<1e-6,"independent force law is not continuous quadratic force");
            auto old=engine(hammer,false,rate);Access::quiet(*old);expect(old->transitionNote(hammer?43:45,hammer?45:43,.8f),"contact fixture transition rejected");
            expect(Access::contactEnergy(*old)==0,"legacy ablation injected external finger work");
            advance(*e,int(.3*rate));
        }
    }
}
void legacyBudget(){
    for(bool hammer:{false,true}){
        auto e=engine(hammer,false);double old=Access::slope(*e);
        expect(e->transitionNote(hammer?43:45,hammer?45:43,.8f),"contact fixture transition rejected");
        expect(std::abs(Access::contactEnergy(*e)/(old*.02*.8*.8)-1)<2e-6,"legacy budget changed with candidate disabled");
    }
}
void lateGesture(){
    for(bool hammer:{false,true}){
        auto after=engine(hammer,true),before=engine(hammer,false);
        advance(*after,8*48000);advance(*before,8*48000);
        expect(after->transitionNote(hammer?43:45,hammer?45:43,.8f),"contact fixture transition rejected");expect(before->transitionNote(hammer?43:45,hammer?45:43,.8f),"contact fixture transition rejected");
        expect(Access::contactEnergy(*after)>1000*Access::contactEnergy(*before),"late articulation still depends on decayed string energy");
        expect(after->heldString(hammer?45:43)==0,"late articulation lost source string");
    }
}
void carriedPackets(){
    for(bool hammer:{false,true})for(bool samePitch:{false,true})for(int elapsed:{0,1,60,180,400}){
        auto e=engine(hammer,true);int target=hammer?45:43;
        expect(e->transitionNote(hammer?43:45,target,.8f),"contact fixture transition rejected");advance(*e,elapsed);
        auto& v=Access::voice(*e);v.excitationEnvelope=0;v.contactTravel.active=false;
        const auto expected=Access::arrivals(*e);
        const float ng=v.legatoNormalGain,pg=v.legatoParallelGain;
        auto travel=v.legatoContactTravel;int age=v.legatoContactAge,samples=v.legatoContactSamples;
        e->noteOn(samePitch?target:target+1,.7f);
        if(samePitch){
            const auto& q=v.repluckArrivals;
            expect(!q.overflow && !v.tailActive,"same-note contact queue overflowed or manufactured tail");
            for(std::size_t i=0;i<expected.size();++i)for(int plane=0;plane<2;++plane)
                expect(std::abs(q.wave[plane][(q.readIndex+i)%q.capacity]-expected[i][plane])<1e-9,"repluck changed committed contact packet");
        }else{
            expect(v.tailLegatoNormalGain==ng&&v.tailLegatoParallelGain==pg,"tail lost contact direction");
            expect(v.tailLegatoContactTravel.history==travel.history&&v.tailLegatoContactAge==age&&v.tailLegatoContactSamples==samples,"tail lost contact history/source");
        }
        expect(!v.legatoContactTravel.active&&v.legatoContactSamples==0,"retained contact emitted twice");
        advance(*e,512);e->allSoundOff();
        expect(!v.tailLegatoContactTravel.active&&!v.legatoContactTravel.active&&v.repluckArrivals.remaining==0,"panic retained contact arrivals");
    }
}
void pulledLocation(){
    for(int target:{40,43,44}){
        auto e=engine(false,true);const auto& v=Access::voice(*e);const double oldPeriod=v.contactPeriodSamples;
        expect(e->transitionNote(45,target,.8f),"contact fixture transition rejected");
        // Compare first nonzero contact arrivals with the two geometrical paths,
        // using an isolated impulse in a copy of the internal transport state.
        auto t=v.legatoContactTravel;std::array<int,2> first{-1,-1};
        for(int i=0;i<2000;++i){auto x=t.process(i==0?1.f:0.f);for(int j=0;j<2;++j)if(first[j]<0&&std::abs(x[j])>1e-5f)first[j]=i;}
        const double ratio=std::exp2(-(45-target)/12.0);
        expect(first[0]>=0&&std::abs(first[0]-.5*oldPeriod)<3,"pull did not originate at departing fret");
        expect(first[1]>=0&&std::abs(first[1]-(oldPeriod/ratio-.5*oldPeriod))<3,"pull used source rather than target string for reflected path");
    }
}
void damping() {
    for(bool hammer:{false,true}) {
        auto e=engine(hammer,true);const int target=hammer?45:43;
        expect(e->transitionNote(hammer?43:45,target,.8f),"release contact rejected");
        auto& v=Access::voice(*e);const float amplitude=v.legatoContactAmplitude;
        e->noteOffWithVelocity(target,1,false,1.0f,true);
        expect(v.legatoContactAmplitude==amplitude && v.legatoContactSamples>0,"key-up cut an emitted contact");
        advance(*e,64);
        expect(v.loops[0].appliedReleaseGain<1 && v.loops[1].appliedReleaseGain<1,"release failed to damp both contact planes");
        advance(*e,512);expect(v.legatoContactSamples==0,"released source failed to finish");
    }
}
std::vector<float> connectedScore(bool enabled,bool hammer,int block,bool otherEnabled=true) {
    auto p=std::make_unique<acustra::Performer>();acustra::PerformanceRealism f;
    f.hammerWork=hammer?enabled:otherEnabled;f.pullOffPluck=hammer?otherEnabled:enabled;p->engine().setPerformanceRealism(f);p->prepare(48000,block);
    p->engine().setStringPerChannelMode(true);int source=hammer?43:45,target=hammer?45:43;
    std::vector<float> out(2*24000),l(block),r(block);
    for(int at=0;at<24000;at+=block) {
        const int n=std::min(block,24000-at);p->beginBlock(l.data(),r.data(),n);
        if(at==0)p->noteOn(0,1,source,100);
        if(at<=4800 && at+n>4800) {
            const int offset=4800-at;p->controlChange(offset,1,65,127);p->controlChange(offset,1,84,source);
            p->noteOn(offset,1,target,100);p->noteOff(offset,1,source);
        }
        p->endBlock();for(int i=0;i<n;++i){out[2*(at+i)]=l[i];out[2*(at+i)+1]=r[i];}
    }
    expect(p->engine().heldString(source)<0&&p->engine().heldString(target)==0,"public connected trigger lost ownership");
    return out;
}
void publicGestures() {
    for(bool hammer:{false,true}) {
        auto before=connectedScore(false,hammer,64),after=connectedScore(true,hammer,64);
        expect(std::equal(before.begin(),before.begin()+2*4800,after.begin()),"contact switch changed pregesture audio");
        expect(before!=after,"public MIDI did not reach changed finger gesture");
        expect(after==connectedScore(true,hammer,17),"connected contact changed with event partition");
        expect(before==connectedScore(false,hammer,64,false),"unrelated enabled contact flag changed the legacy gesture");
        expect(after==connectedScore(true,hammer,64,false),"unrelated contact flag changed the candidate gesture");
    }
}
void pairDistribution(){
    for(double rate:{44100.,48000.,96000.}){
        auto e=std::make_unique<Engine>();e->prepare(rate,64);std::vector<double> gaps;double pos=0,together=0;
        for(int i=0;i<40000;++i){double ms=1000.*e->fingerPairSeparationSamples()/rate;gaps.push_back(std::abs(ms));pos+=ms>0;together+=std::abs(ms)<2;expect(std::abs(ms)<=22.312+1000./rate,"pair exceeded bounded separation");}
        std::sort(gaps.begin(),gaps.end());
        expect(std::abs(gaps[20000]-11.156)<.2&&std::abs(gaps[10000]-5.578)<.2&&std::abs(gaps[30000]-16.734)<.2,"pair quantiles differ from authored measured-scale law");
        expect(std::abs(pos/40000-.5)<.01&&std::abs(together/40000-.0896)<.01,"pair direction/coincidence law drifted");
    }
}
std::vector<float> pairScore(bool enabled,int mode,int block){
    auto p=std::make_unique<acustra::Performer>();acustra::PerformanceRealism f;f.fingerPairs=enabled;p->engine().setPerformanceRealism(f);
    acustra::EngineParameters a;a.picking=mode==4?acustra::PickingTechnique::Pick:mode==8?acustra::PickingTechnique::Thumb:acustra::PickingTechnique::Finger;p->setParameters(a);p->prepare(48000,block);
    if(mode==2||mode==7)p->engine().setStringPerChannelMode(true);
    if(mode==6||mode==7)p->setGatherChords(true);
    const int secondAt=mode==1?2400:mode>=5&&mode<=7?480:0;
    const int firstChannel=mode==9?2:1;
    const int secondChannel=mode==2||mode==7||mode==9?2:1;
    std::vector<float> out(2*24000),l(block),r(block);
    for(int at=0;at<24000;at+=block){int n=std::min(block,24000-at);p->beginBlock(l.data(),r.data(),n);
        if(at==0){
            if(mode==9){
                p->controlChange(0,1,101,0);p->controlChange(0,1,100,6);
                p->controlChange(0,1,6,6);p->controlChange(0,1,101,127);p->controlChange(0,1,100,127);
            }
            if(mode==3)p->noteOn(0,1,24,100);p->noteOn(0,firstChannel,43,100);
        }
        if(at<=secondAt && at+n>secondAt)p->noteOn(secondAt-at,secondChannel,47,100);
        p->endBlock();for(int i=0;i<n;++i){out[2*(at+i)]=l[i];out[2*(at+i)+1]=r[i];}
    }return out;
}
void pairIntent(){
    expect(pairScore(true,0,64)!=pairScore(false,0,64),"automatic Finger pair stayed simultaneous");
    expect(pairScore(true,0,17)==pairScore(true,0,127),"Finger pair changed with host partition");
    for(int mode:{1,2,3,4,5,7,8,9})expect(pairScore(true,mode,64)==pairScore(false,mode,64),"explicit timing/string/MPE/stroke or non-Finger intent was changed by Finger timing");
    // Gather Chords explicitly replaces sub-window key timing with one
    // instrument gesture. That declared grouping owns a ten-millisecond pair.
    expect(pairScore(true,6,64)!=pairScore(false,6,64),"explicitly gathered pair bypassed Finger scheduling");
    expect(pairScore(true,6,17)==pairScore(true,6,127),"gathered pair changed with block partition");
}

void pairSchedule()
{
    for (int skipped = 0; skipped < 12; ++skipped)
    {
        auto performer = std::make_unique<acustra::Performer>();
        acustra::EngineParameters parameters;
        parameters.picking = acustra::PickingTechnique::Finger;
        performer->setParameters(parameters);
        performer->prepare(48000, 64);
        auto reference = std::make_unique<Engine>();
        reference->prepare(48000, 64);
        for (int i = 0; i < skipped; ++i)
        {
            static_cast<void>(performer->engine().fingerPairSeparationSamples());
            static_cast<void>(reference->fingerPairSeparationSamples());
        }
        const int separation = reference->fingerPairSeparationSamples();
        float left = 0, right = 0;
        performer->beginBlock(&left, &right, 1);
        performer->noteOn(0, 1, 43, 100);
        performer->noteOn(0, 1, 47, 100);
        performer->endBlock();
        const int a = performer->engine().heldString(43);
        const int b = performer->engine().heldString(47);
        expect(a >= 0 && b >= 0 && a != b, "pair did not retain two physical strings");
        if (a < 0 || b < 0 || a == b) continue;
        const auto& lower = Access::voice(performer->engine(), std::min(a, b));
        const auto& higher = Access::voice(performer->engine(), std::max(a, b));
        expect(lower.pluckDelay == std::max(0, separation)
                   && higher.pluckDelay == std::max(0, -separation),
               "public pair did not schedule its signed draw on physical strings");
        expect(lower.attackFired || higher.attackFired,
               "pair moved both notes later than their requested sample");
        expect(!lower.strumming && !higher.strumming,
               "independent fingers became a swept stroke");
    }
}

void pairRandomIndependence()
{
    auto candidate = std::make_unique<Engine>();
    auto reference = std::make_unique<Engine>();
    candidate->prepare(48000, 64);
    reference->prepare(48000, 64);
    std::array<int, 64> first {};
    for (int i = 0; i < 64; ++i)
    {
        first[static_cast<std::size_t>(i)] = candidate->fingerPairSeparationSamples();
        expect(first[static_cast<std::size_t>(i)] == reference->fingerPairSeparationSamples(),
               "fresh engines disagreed on pair timing");
        expect(candidate->fingerChordOffsetSamples(i % 4, 4)
                   == reference->fingerChordOffsetSamples(i % 4, 4),
               "pair timing consumed the existing chord stream");
    }
    candidate->reset();
    expect(candidate->fingerPairSeparationSamples() == reference->fingerPairSeparationSamples(),
           "panic restarted pair timing instead of preserving variation");
    candidate->prepare(48000, 64);
    for (int sample : first)
        expect(candidate->fingerPairSeparationSamples() == sample,
               "prepare did not restart the deterministic pair stream");
}
}
int main(){workAndRates();legacyBudget();lateGesture();carriedPackets();pulledLocation();damping();publicGestures();pairDistribution();pairIntent();pairSchedule();pairRandomIndependence();if(failures)return 1;std::cout<<"PASS independent finger work, pull release and pair timing\n";}
