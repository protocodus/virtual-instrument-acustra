#include "DSP/AcustraEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <cstring>

namespace acustra {
struct AcustraEngineTestAccess {
    using Coefficients = std::array<float, 7>;
    static Coefficients applied(const AcustraEngine::StringLoop& l) {
        return {l.bendingLossGain,l.bendingLossA1,l.bendingLossA2,
            l.dispersionA1,l.dispersionA2,l.secondDispersionA1,l.secondDispersionA2};
    }
    static bool sameBits(const Coefficients& a, const Coefficients& b) {
        return std::memcmp(a.data(), b.data(), sizeof(float) * a.size()) == 0;
    }
    static bool stable(const Coefficients& c) {
        for (const int n : {1,3,5})
            if (!(std::abs(c[n+1]) < 1.0f && 1.0f+c[n]+c[n+1] > 0.0f
                  && 1.0f-c[n]+c[n+1] > 0.0f)) return false;
        return std::abs(c[0]-(1.0f+c[1]+c[2])) < 1.0e-6f;
    }
    static bool transitionContract() {
        auto a=std::make_unique<AcustraEngine::StringLoop>();
        const Coefficients initial {.2f,-1.3f,.5f,-.9f,.4f,-.3f,.1f};
        const Coefficients target {.35f,-1.1f,.45f,-.8f,.3f,-.2f,.1f};
        const Coefficients revised {.25f,-1.2f,.45f,-.85f,.35f,-.25f,.12f};
        a->currentDelay=a->targetDelay=101.25f;
        a->setIntrinsicCoefficients(initial,false);
        a->reset();
        a->setIntrinsicCoefficients(target,true);
        if (applied(*a)!=initial || a->intrinsicCoefficientSamples!=102) return false;
        const auto step=a->intrinsicCoefficientStep;
        // Reconfiguring may happen every control period or on a cache miss.
        // It must not recalculate a rounded step or postpone completion.
        a->setIntrinsicCoefficients(target,true);
        a->setIntrinsicCoefficients(target,false);
        if (applied(*a)!=initial || a->intrinsicCoefficientSamples!=102
            || !sameBits(a->intrinsicCoefficientStep,step)) return false;
        for(int n=0;n<32;++n) {
            a->advance(0.0f,1.0f);
            if (!stable(applied(*a))) return false;
        }
        const auto before=applied(*a);
        const int remaining=a->intrinsicCoefficientSamples;
        a->setIntrinsicCoefficients(revised,false);
        if (applied(*a)!=before || a->intrinsicCoefficientSamples!=remaining) return false;
        auto forced=std::make_unique<AcustraEngine::StringLoop>(*a);
        for(int n=0;n<remaining;++n) {
            forced->setIntrinsicCoefficients(revised,false);
            forced->advance(0.0f,1.0f); a->advance(0.0f,1.0f);
            const auto now=applied(*a);
            if (applied(*forced)!=now || !stable(now)
                || !sameBits(forced->intrinsicCoefficientStep,a->intrinsicCoefficientStep)
                || forced->intrinsicCoefficientSamples!=a->intrinsicCoefficientSamples) return false;
            for(std::size_t k=0;k<now.size();++k)
                if(now[k]<std::min(before[k],revised[k]) || now[k]>std::max(before[k],revised[k])) return false;
        }
        if(!sameBits(applied(*a),revised) || a->intrinsicCoefficientSamples!=0) return false;
        // The first configuration and ordinary continuous updates outside a
        // discrete transition retain the existing immediate coefficient path.
        a->setIntrinsicCoefficients(target,false);
        if (!sameBits(applied(*a),target) || a->intrinsicCoefficientSamples!=0) return false;
        a->setIntrinsicCoefficients(initial,true);
        for(int n=0;n<100;++n) a->advance(0.0f,1.0f);
        const auto reversing=applied(*a);
        a->setIntrinsicCoefficients(target,true);
        if(applied(*a)!=reversing || a->intrinsicCoefficientSamples!=102) return false;
        a->advance(0.0f,1.0f);
        a->reset();
        if(applied(*a)!=target || a->intrinsicCoefficientSamples!=0) return false;
        // A one-ULP request cannot move on each rounded intermediate step,
        // but its finite deadline must still arrive at the exact target.
        auto tiny=target;
        tiny[3]=std::nextafter(tiny[3],1.0f);
        a->setIntrinsicCoefficients(tiny,true);
        for(int n=0;n<102;++n) a->advance(0.0f,1.0f);
        if (!sameBits(applied(*a),tiny) || a->intrinsicCoefficientSamples!=0) return false;
        // Reset resolves an in-flight transition without retaining its ramp.
        a->setIntrinsicCoefficients(initial,true);
        a->advance(0.0f,1.0f);
        a->reset();
        return sameBits(applied(*a),initial) && a->intrinsicCoefficientSamples==0
            && sameBits(a->intrinsicCoefficientStep,Coefficients {});
    }
    static void invalidateCurrentConfiguration(AcustraEngine& e) {
        for(int s=0;s<AcustraEngine::stringCount;++s) {
            auto& v=e.voices_[static_cast<std::size_t>(s)];
            // Force the normal control-period call to miss its cache. Do not
            // execute configuration early: new MIDI bends are intentionally
            // consumed at the existing control boundary in both engines.
            v.configurationKey.generation=0;
        }
    }
    static int active(const AcustraEngine& e) {
        int n=0;for(const auto& v:e.voices_)for(const auto& l:v.loops)n+=l.intrinsicCoefficientSamples>0;
        return n;
    }
};
}
int main() {
    using Engine=acustra::AcustraEngine;
    using Access=acustra::AcustraEngineTestAccess;
    if(!Access::transitionContract()) {std::cerr<<"FAIL: intrinsic filter transition endpoint/deadline/stability contract\n";return 1;}
    for(double rate:{44100.0,48000.0,96000.0})
    for(bool enabled:{false,true}) {
        auto cached=std::make_unique<Engine>(), forced=std::make_unique<Engine>();
        acustra::EngineParameters parameters;
        acustra::PerformanceRealism realism;
        realism.retuneContinuity=enabled;
        for(auto* e:{cached.get(),forced.get()}) {
            e->setPerformanceRealism(realism);
            e->setParameters(parameters);
            e->prepare(rate,128); e->setStringPerChannelMode(true);
            e->noteOn(40,.8f,1);e->noteOn(52,.6f,3);e->noteOn(64,.4f,6);
        }
        std::array<float,128> a{},b{},c{},d{};
        for(int n=0;n<20;++n) {cached->process(a.data(),b.data(),128);forced->process(c.data(),d.data(),128);}
        parameters.tuning=acustra::Tuning::Dadgad;
        cached->setParameters(parameters);forced->setParameters(parameters);
        if((Access::active(*cached)>0)!=enabled) {
            std::cerr<<"FAIL: tuning transition did not follow its ablation switch\n";return 1;
        }
        for(int n=0;n<static_cast<int>(rate/20);++n) {
            // A new tuning reverses the move before the first completes.
            if(n==9) {
                parameters.tuning=acustra::Tuning::Standard;
                cached->setParameters(parameters);forced->setParameters(parameters);
            }
            // Bend and touch revise the target during the ramp. Neither may
            // cause a forced cache miss to restart the physical deadline.
            if (n==21) {
                cached->setPitchBend(0.35f); forced->setPitchBend(0.35f);
                cached->setPalmMutePressure(0.4f); forced->setPalmMutePressure(0.4f);
            }
            Access::invalidateCurrentConfiguration(*forced);
            cached->process(a.data(),b.data(),1);forced->process(c.data(),d.data(),1);
            if(a[0]!=c[0] || b[0]!=d[0] || !std::isfinite(a[0]) || !std::isfinite(b[0])) {
                std::cerr<<"FAIL: cached/forced retune audio diverged at "<<rate<<", sample "<<n<<"\n";return 1;
            }
        }
        if(Access::active(*cached)!=0) {std::cerr<<"FAIL: intrinsic filter transition never completed\n";return 1;}
    }
    std::cout<<"Intrinsic filter retune continuity, physical deadline and forced-cache parity passed\n";
}
