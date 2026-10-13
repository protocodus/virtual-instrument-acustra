#include "DSP/AcustraEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <cstring>

namespace acustra {
struct AcustraEngineTestAccess {
    // The bending section's g, a1, a2, the two dispersion sections' a1, a2,
    // then the constant-loss section's g, a1, a2, n1, n2 (StringLoop).
    using Coefficients = AcustraEngine::StringLoop::IntrinsicCoefficients;
    static Coefficients applied(const AcustraEngine::StringLoop& l) {
        return {l.bendingLossGain,l.bendingLossA1,l.bendingLossA2,
            l.dispersionA1,l.dispersionA2,l.secondDispersionA1,l.secondDispersionA2,
            l.constantLossGain,l.constantLossA1,l.constantLossA2,
            l.constantLossN1,l.constantLossN2};
    }
    static bool sameBits(const Coefficients& a, const Coefficients& b) {
        return std::memcmp(a.data(), b.data(), sizeof(float) * a.size()) == 0;
    }
    static bool delayProgressContract() {
        for (const double rate : {44100.0, 48000.0, 96000.0})
        for (const bool rising : {false, true}) {
            auto loop = std::make_unique<AcustraEngine::StringLoop>();
            const float target = 461.204224f;
            float initial = target;
            for (int n = 0; n < 92; ++n)
                initial = std::nextafter(initial, rising ? 0.0f : 1000.0f);
            const float smoothing = static_cast<float>(1.0 - std::exp(-1.0 / (.006 * rate)));
            loop->currentDelay = initial;
            loop->targetDelay = target;
            loop->reset();
            const double remaining = double(initial) - target;
            const float ulp = std::nextafter(target, 1000.0f) - target;
            // This is a normal 6ms move whose initial step is already below
            // half a float ULP. It must follow the exponential, not snap all
            // 92 ULPs immediately merely because a float += step would stall.
            for (int sample = 1; sample <= static_cast<int>(.06 * rate); ++sample) {
                loop->advance(smoothing, 1.0f);
                const double residual = remaining * std::pow(1.0 - double(smoothing), sample);
                const float expected = std::abs(residual) < 1.0e-4 ? target
                    : static_cast<float>(double(target) + residual);
                if (std::abs(loop->currentDelay - expected) > ulp
                    || loop->currentDelay < std::min(initial, target)
                    || loop->currentDelay > std::max(initial, target)) return false;
            }
            if (loop->currentDelay != target) return false;
            // Target reversal retains the unrounded progress. A copied
            // ringing string must take the same trajectory and line reads.
            loop->targetDelay = initial;
            for (int n = 0; n < 31; ++n) loop->advance(smoothing, 1.0f);
            loop->targetDelay = target;
            auto copied = std::make_unique<AcustraEngine::StringLoop>(*loop);
            for (int n = 0; n < 127; ++n) {
                if (loop->advance(smoothing, 1.0f) != copied->advance(smoothing, 1.0f)
                    || std::memcmp(loop.get(), copied.get(), sizeof(*loop)) != 0) return false;
            }
            // An explicit prescription starts from the prescribed value,
            // including the two-sample geometry handover of dispersion.
            loop->currentDelay = 121.5f;
            loop->targetDelay = 120.75f;
            loop->advance(smoothing, 1.0f);
            const float prescribed = static_cast<float>(121.5
                + double(smoothing) * (120.75 - 121.5));
            if (loop->currentDelay != prescribed) return false;
            loop->currentDelay = initial;
            loop->targetDelay = target;
            loop->reset();
            loop->advance(smoothing, 1.0f);
            const float resetExpected = static_cast<float>(double(initial)
                + double(smoothing) * (double(target) - initial));
            if (loop->currentDelay != resetExpected) return false;
            loop->switchSecondDispersion(true);
            loop->targetDelay -= 2.0f;
            const float switched = loop->currentDelay;
            loop->advance(smoothing, 1.0f);
            const float switchExpected = static_cast<float>(double(switched)
                + double(smoothing) * (double(loop->targetDelay) - switched));
            if (loop->currentDelay != switchExpected) return false;
            loop->switchSecondDispersion(false);
            loop->targetDelay += 2.0f;
            const float switchedBack = loop->currentDelay;
            loop->advance(smoothing, 1.0f);
            const float returnExpected = static_cast<float>(double(switchedBack)
                + double(smoothing) * (double(loop->targetDelay) - switchedBack));
            if (loop->currentDelay != returnExpected) return false;
        }
        // At long supported delays the float ULP exceeds the final 1e-4
        // threshold. A hard prescription may therefore write the SAME bits
        // while the hidden exponential still has legitimate work left.
        auto longDelay = std::make_unique<AcustraEngine::StringLoop>();
        longDelay->currentDelay = longDelay->targetDelay = 4096.0f;
        longDelay->reset();
        longDelay->delayProgress = 4096.0002;
        const auto publishedBits = longDelay->delayProgressBits;
        longDelay->setCurrentDelay(longDelay->targetDelay);
        if (longDelay->currentDelay != 4096.0f || longDelay->delayProgress != 4096.0
            || longDelay->delayProgressBits != publishedBits) return false;
        longDelay->targetDelay = 4095.0f;
        constexpr float smoothing = .003f;
        longDelay->advance(smoothing, 1.0f);
        const double prescribedProgress = 4096.0 + double(smoothing) * (4095.0 - 4096.0);
        if (longDelay->delayProgress != prescribedProgress
            || longDelay->currentDelay != static_cast<float>(prescribedProgress)) return false;
        return true;
    }
    static bool gainProgressContract() {
        for (const bool rising : {false, true}) {
            auto loop = std::make_unique<AcustraEngine::StringLoop>();
            loop->currentDelay = loop->targetDelay = 461.25f;
            const float initial = 0.992213070393f;
            float target = initial;
            for (int ulp = 0; ulp < 48; ++ulp)
                target = std::nextafter(target, rising ? 1.0f : 0.0f);
            loop->setLoopGain(initial, false);
            loop->reset();
            loop->setLoopGain(target, true);
            if (loop->loopGainTransitionSamples != 462) return false;
            const float step = loop->loopGainStep;
            loop->setLoopGain(target, true);
            loop->setLoopGain(target, false);
            if (loop->loopGain != initial || loop->loopGainTransitionSamples != 462
                || std::memcmp(&step, &loop->loopGainStep, sizeof(float)) != 0) return false;
            // Each requested sample step is below half an ULP. Rounded +=step
            // must not stall the gain until one whole endpoint jump.
            for (int sample = 0; sample < 231; ++sample) loop->advance(0.0f, 1.0f);
            const float midpoint = static_cast<float>(0.5 * (double(initial) + double(target)));
            const float ulp = std::nextafter(midpoint, 1.0f) - midpoint;
            if (std::abs(loop->loopGain - midpoint) > ulp
                || loop->loopGain < std::min(initial, target)
                || loop->loopGain > std::max(initial, target)) return false;
            const float before = loop->loopGain;
            float reversed = initial;
            for (int count = 0; count < 24; ++count)
                reversed = std::nextafter(reversed, rising ? 0.0f : 1.0f);
            const int remaining = loop->loopGainTransitionSamples;
            loop->setLoopGain(reversed, false);
            if (loop->loopGain != before || loop->loopGainTransitionSamples != remaining) return false;
            auto forced = std::make_unique<AcustraEngine::StringLoop>(*loop);
            for (int sample = 0; sample < remaining; ++sample) {
                forced->setLoopGain(reversed, false);
                loop->advance(0.0f, 1.0f); forced->advance(0.0f, 1.0f);
                if (std::memcmp(loop.get(), forced.get(), sizeof(*loop)) != 0
                    || loop->loopGain < std::min(before, reversed)
                    || loop->loopGain > std::max(before, reversed)) return false;
            }
            if (loop->loopGain != reversed || loop->loopGainTransitionSamples != 0) return false;
            loop->setLoopGain(initial, true);
            loop->advance(0.0f, 1.0f);
            loop->reset();
            if (loop->loopGain != initial || loop->targetLoopGain != initial
                || loop->loopGainTransitionSamples != 0 || loop->loopGainStep != 0.0f) return false;
        }
        return true;
    }
    // Every section inside its stability triangle, and both loss sections'
    // unit gain at DC, g = 1 + a1 + a2 for the bending section and
    // g + n1 + n2 = 1 + a1 + a2 for the constant-loss one, which a linear
    // ramp between two sections that have it keeps.
    static bool stable(const Coefficients& c) {
        for (const int n : {1,3,5,8})
            if (!(std::abs(c[n+1]) < 1.0f && 1.0f+c[n]+c[n+1] > 0.0f
                  && 1.0f-c[n]+c[n+1] > 0.0f)) return false;
        return std::abs(c[0]-(1.0f+c[1]+c[2])) < 1.0e-6f
            && std::abs((c[7]+c[10]+c[11])-(1.0f+c[8]+c[9])) < 1.0e-6f;
    }
    static bool transitionContract() {
        auto a=std::make_unique<AcustraEngine::StringLoop>();
        // The bending section all-pole and the constant-loss section with
        // zeros as well as poles, each with unit gain at DC.
        const Coefficients initial {.2f,-1.3f,.5f,-.9f,.4f,-.3f,.1f,
            .35f,-1.3f,.5f,-.25f,.1f};
        const Coefficients target {.35f,-1.1f,.45f,-.8f,.3f,-.2f,.1f,
            .65f,-1.1f,.45f,-.5f,.2f};
        const Coefficients revised {.25f,-1.2f,.45f,-.85f,.35f,-.25f,.12f,
            .5f,-1.2f,.45f,-.4f,.15f};
        if (!stable(initial) || !stable(target) || !stable(revised)) return false;
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
        // The same for a numerator tap alone.
        auto tinyZero=tiny;
        tinyZero[11]=std::nextafter(tinyZero[11],1.0f);
        a->setIntrinsicCoefficients(tinyZero,true);
        if (a->intrinsicCoefficientSamples!=102) return false;
        for(int n=0;n<102;++n) a->advance(0.0f,1.0f);
        if (!sameBits(applied(*a),tinyZero) || a->intrinsicCoefficientSamples!=0) return false;
        a->setIntrinsicCoefficients(tiny,false);
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
    if(!Access::delayProgressContract()) {std::cerr<<"FAIL: sub-ULP delay progress/copy/reset/prescription contract\n";return 1;}
    if(!Access::gainProgressContract()) {std::cerr<<"FAIL: sub-ULP feedback gain progress/copy/reset/deadline contract\n";return 1;}
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
