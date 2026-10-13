#include "DSP/AcustraEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

namespace acustra {
struct AcustraEngineTestAccess {
    using Travel = AcustraEngine::ContactTravel;
    using Voice = AcustraEngine::Voice;
    static Voice& voice(AcustraEngine& e) { return e.voices_[0]; }
    static void emit(AcustraEngine& e, float source) {
        auto& v = voice(e);
        e.queueReleaseNoisePaths(v, v.releaseNoiseTravel.processFractional(source));
    }
    static int change(AcustraEngine& e, float first, float second) {
        auto& v = voice(e);
        const int drained = e.drainReleaseNoiseTravel(v);
        v.releaseNoiseTravel.reset(first, second);
        return drained;
    }
    static int drain(AcustraEngine& e) { return e.drainReleaseNoiseTravel(voice(e)); }
    static bool publicGeometry() {
        auto e = std::make_unique<AcustraEngine>();
        EngineParameters p; p.releaseNoise = .1f; p.room = .1f;
        p.piezoMix = .2f; p.bodyAmount = .82f;
        e->setParameters(p); e->prepare(96000,128); e->setStringPerChannelMode(true);
        float l,r;
        for (int n=0;n<38000;++n) {
            if(n==0)e->noteOn(40,.7f,1);
            if(n==14400)e->noteOff(40,1);
            if(n==23520)e->noteOn(52,.7f,1);
            if(n==31680)e->noteOff(52,1);
            e->process(&l,&r,1);
        }
        auto actual = std::make_unique<Travel>(voice(*e).releaseNoiseTravel);
        // Calculate the same current contact from the same RNG/physical
        // state, with no prior fractional section. Queue contents do not
        // enter geometry; this witnesses the original stale-active defect.
        // Rewind the release-only generator to just before this contact by
        // replaying an identical cold witness until its release boundary.
        auto witness = std::make_unique<AcustraEngine>();
        witness->setParameters(p); witness->prepare(96000,128);
        witness->setStringPerChannelMode(true);
        for(int n=0;n<37681;++n) {
            if(n==0)witness->noteOn(40,.7f,1);
            if(n==14400)witness->noteOff(40,1);
            if(n==23520)witness->noteOn(52,.7f,1);
            if(n==31680)witness->noteOff(52,1);
            witness->process(&l,&r,1);
        }
        voice(*witness).releaseNoiseTravel.active=false;
        witness->process(&l,&r,1);
        const auto& expected = voice(*witness).releaseNoiseTravel;
        for(std::size_t i=0;i<2;++i)
            if(actual->taps[i].whole!=expected.taps[i].whole
                || actual->taps[i].a1!=expected.taps[i].a1
                || actual->taps[i].a2!=expected.taps[i].a2) return false;
        return actual->taps[0].whole>200 && actual->taps[1].whole<400;
    }
    static bool transferAndOverlap() {
        for (const std::array<float,2> first : {
            std::array<float,2>{.010001f,.99f}, {1.1f,2.1f},
            {138.22f,1024.75f}, {8190.1f,7.31f}}) {
            auto e=std::make_unique<AcustraEngine>();
            auto& v=voice(*e); v.releaseNoiseNormal=.8660254f;v.releaseNoiseParallel=.5f;
            v.releaseNoiseTravel.reset(first[0],first[1]);
            auto old=std::make_unique<Travel>();old->reset(first[0],first[1]);
            auto next=std::make_unique<Travel>();next->reset(273.25f,307.49f);
            auto third=std::make_unique<Travel>();third->reset(31.12f,240.75f);
            double maxError=0,maxComponent=0;
            for(int n=0;n<16000;++n) {
                if(n==73) { if(change(*e,273.25f,307.49f)>8192)return false; }
                if(n==91) { if(change(*e,31.12f,240.75f)>8192)return false; }
                const float signal = n<256 ? float(.3*std::sin(.23*n)+.1*std::cos(.71*n)) : 0.f;
                emit(*e,signal);
                const auto a=old->process(n<73?signal:0.f);
                const auto b=n>=73?next->process(n<91?signal:0.f):std::array<float,2>{};
                const auto c=n>=91?third->process(signal):std::array<float,2>{};
                const auto queued=v.repluckArrivals.process();
                const double local=double(a[0])-a[1]+double(b[0])-b[1]+double(c[0])-c[1];
                const double component=std::abs(double(a[0]))+std::abs(double(a[1]))
                    +std::abs(double(b[0]))+std::abs(double(b[1]))
                    +std::abs(double(c[0]))+std::abs(double(c[1]));
                maxComponent=std::max(maxComponent,component);
                maxError=std::max(maxError,std::abs(double(queued[0])-.8660254f*local));
                maxError=std::max(maxError,std::abs(double(queued[1])-.5*local));
            }
            // Only float product/sum order differs from combining the
            // original delayed paths. Bound absolute roundoff by their
            // component scale, including cancellation, not output RMS.
            if(v.repluckArrivals.overflow || maxError>16*std::numeric_limits<float>::epsilon()*maxComponent)
                return false;
            std::cout<<"Thiran queued overlap error "<<maxError<<" component "<<maxComponent<<'\n';
        }
        return true;
    }
    static bool drainBound() {
        int maximum=0;
        for(float path:{.010001f,.041667f,.1f,.99f,1.1f,1.5f,2.09999f,31.11f,8190.1f}) {
            auto e=std::make_unique<AcustraEngine>();auto&v=voice(*e);
            v.releaseNoiseNormal=.8660254f;v.releaseNoiseParallel=.5f;
            v.releaseNoiseTravel.reset(path,path);
            for(int n=0;n<8;++n)emit(*e,(n&1?-1.f:1.f)*std::numeric_limits<float>::max()/256.f);
            const int count=drain(*e);maximum=std::max(maximum,count);
            if(count>8192||v.releaseNoiseTravel.active||v.repluckArrivals.overflow
                ||!v.releaseNoiseTravel.fractionalQuiet(1e-30))return false;
        }
        std::cout<<"Worst synthetic finite-range drain "<<maximum<<" samples\n";
        return true;
    }
    static bool copyResetTail() {
        auto e=std::make_unique<AcustraEngine>();auto&v=voice(*e);
        v.releaseNoiseNormal=.8660254f;v.releaseNoiseParallel=.5f;v.level=.01f;
        v.releaseNoiseTravel.reset(138.22f,1024.75f);
        for(int n=0;n<73;++n){emit(*e,float(std::sin(.23*n)));v.repluckArrivals.process();}
        auto copied=std::make_unique<AcustraEngine>(*e);
        for(int n=0;n<100;++n) {
            emit(*e,.1f);emit(*copied,.1f);
            if(v.repluckArrivals.process()!=voice(*copied).repluckArrivals.process())return false;
        }
        drain(*e);auto expected=std::make_unique<AcustraEngine>(*e);
        e->captureTail(v);
        for(int n=0;n<3000;++n)
            if(v.tailRepluckArrivals.process()!=voice(*expected).repluckArrivals.process())return false;
        e->reset();
        return !voice(*e).releaseNoiseTravel.active && voice(*e).repluckArrivals.remaining==0
            && voice(*e).tailRepluckArrivals.remaining==0;
    }
    static std::vector<float> renderPartition(double rate, int block) {
        auto e=std::make_unique<AcustraEngine>();
        EngineParameters p;p.releaseNoise=1.f;p.room=.1f;p.piezoMix=.2f;
        e->setParameters(p);e->prepare(rate,128);e->setStringPerChannelMode(true);
        const std::array<double,8> times {0.,.15,.245,.330,.395,.410,.485,.500};
        const std::array<int,8> notes {40,40,52,52,43,43,40,40};
        const int total=static_cast<int>(.85*rate);std::vector<float> result;
        result.reserve(static_cast<std::size_t>(2*total));
        std::array<float,128> l{},r{};int cursor=0;std::size_t event=0;
        while(cursor<total) {
            if(event<times.size() && cursor==static_cast<int>(times[event]*rate)) {
                if((event&1)==0)e->noteOn(notes[event],.7f,1);else e->noteOff(notes[event],1);
                ++event;
            }
            const int next=event<times.size()?static_cast<int>(times[event]*rate):total;
            const int count=std::min({block,total-cursor,next-cursor});
            if(count<=0) return {};
            e->process(l.data(),r.data(),count);
            for(int n=0;n<count;++n){result.push_back(l[n]);result.push_back(r[n]);}
            cursor+=count;
        }
        return result;
    }
    static bool blockPartition() {
        for(double rate:{44100.,48000.,96000.}) {
            const auto reference=renderPartition(rate,1);
            if(reference.empty())return false;
            for(int block:{31,128})if(reference!=renderPartition(rate,block))return false;
        }
        return true;
    }
};}
int main(){using A=acustra::AcustraEngineTestAccess;
 if(!A::transferAndOverlap()){std::cerr<<"FAIL: cold Thiran response/overlapping packet preservation\n";return 1;}
 if(!A::drainBound()){std::cerr<<"FAIL: bounded finite-state tail drain/queue capacity\n";return 1;}
 if(!A::copyResetTail()){std::cerr<<"FAIL: packet copy/reset/tail capture\n";return 1;}
 if(!A::publicGeometry()){std::cerr<<"FAIL: public key-up retained old contact geometry\n";return 1;}
 if(!A::blockPartition()){std::cerr<<"FAIL: overlapping release block partition invariance\n";return 1;}
 std::cout<<"Release contact geometry and immutable packet contracts passed\n";
}
