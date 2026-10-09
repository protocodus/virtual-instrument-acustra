#include "DSP/AcustraEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace acustra {
struct AcustraEngineTestAccess {
    struct Field {
        std::array<std::vector<float>, 2> wave;
        double pole {}, reference {};
        std::array<double, 5> geometry {};
        float cachedEnergy {}, fittedShare {};
        std::uint32_t strokeRandom {};
    };
    static int configured(const AcustraEngine& e) { return static_cast<int>(e.configuredBridgeModel_); }
    static bool pending(const AcustraEngine& e) { return e.bridgeUpdatePending_; }
    static void copyReleaseGeometry(AcustraEngine& to, const AcustraEngine& from, int s) {
        // A source-only fixture: both engines retain their real configured model.
        // Hold the physical period/contact geometry and incoming stroke state fixed.
        // No mechanical reconfiguration or audio is performed on this copied fixture.
        to.voices_[s] = from.voices_[s];
    }
    static void initialiseSource(AcustraEngine& e, int s) {
        auto& v = e.voices_[s]; v.pluckDelay = 0;
        e.initialisePluck(v, s, v.velocity, false, false);
    }
    static Field field(const AcustraEngine& e, int s) {
        const auto& v = e.voices_[s]; Field out;
        out.pole = v.releaseSlipPole; out.reference = v.releaseReferencePole;
        out.geometry = {v.pluckPoint, v.speakingLengthMetres, v.contactPeriodSamples,
                        v.loops[0].currentDelay, v.loops[1].currentDelay};
        out.cachedEnergy = v.attackSlopeEnergy; out.strokeRandom = v.randomState;
        out.fittedShare = e.physicalCalibration_.pickReleaseVelocityShare;
        for (int plane = 0; plane < 2; ++plane) {
            const auto& l = v.loops[plane];
            const int n = std::clamp(static_cast<int>(std::round(l.currentDelay)), 8,
                                     e.activeDelaySamples() - 3);
            for (int i = 1; i <= n; ++i) {
                const int index = (l.writeIndex - i + static_cast<int>(l.delay.size()))
                                % static_cast<int>(l.delay.size());
                out.wave[plane].push_back(l.delay[static_cast<std::size_t>(index)]);
            }
        }
        return out;
    }
};
}

namespace {
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
using Model = acustra::GuitarModel;
using Style = acustra::PickingTechnique;
constexpr double pi = 3.141592653589793238462643383279502884;
int failures = 0, pickCases = 0, thumbCases = 0, fingerCases = 0, queueCases = 0, continuityCases = 0, boundCases = 0;
double maxPickError = 0, maxThumbError = 0, maxQueueError = 0, maxContinuityError = 0, maxQueueCacheError = 0, maxQueueGeometryError = 0, maxQueueDelayCents = 0;
void expect(bool pass, const char* message) {
    if (!pass) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
acustra::EngineParameters parameters(Model model, Style style, float touch = .58f) {
    acustra::EngineParameters p; p.guitarModel = model; p.picking = style;
    p.touch = touch; p.room = 0; return p;
}
std::unique_ptr<Engine> fresh(double rate, Model model, Style style, float touch = .58f,
                              float pickCalibrationScale = 1) {
    auto e = std::make_unique<Engine>(); auto cal = acustra::fittedPhysicalCalibration;
    cal.pickReleaseVelocityShare *= pickCalibrationScale;
    e->setPhysicalCalibration(cal); e->setParameters(parameters(model, style, touch));
    e->prepare(rate, 1); e->setStringPerChannelMode(true); return e;
}
double relativeError(const Access::Field& a, const Access::Field& b) {
    double error = 0, power = 0;
    for (int p = 0; p < 2; ++p) {
        if (a.wave[p].size() != b.wave[p].size()) return 1;
        for (std::size_t i = 0; i < a.wave[p].size(); ++i) {
            const double d = static_cast<double>(a.wave[p][i]) - b.wave[p][i];
            error += d*d; power += static_cast<double>(b.wave[p][i])*b.wave[p][i];
        }
    }
    return std::sqrt(error / std::max(power, 1e-30));
}
double spread(const std::vector<float>& x) {
    double mean = 0; for (float v : x) mean += v; mean /= x.size();
    double sum = 0; for (float v : x) sum += (v-mean)*(v-mean); return sum;
}
double storedSlopeEnergy(const Access::Field& field) {
    double sum = 0;
    for (const auto& x : field.wave) {
        double previous = x.back(), squared = 0;
        for (float sample : x) {
            const double difference = static_cast<double>(sample)-previous;
            squared += difference*difference; previous = sample;
        }
        sum += x.size()*squared;
    }
    return sum;
}
std::vector<float> referenceSlipByFourier(const std::vector<float>& x, double b) {
    // Independent cyclic frequency-domain one-pole oracle. It never calls the
    // production slip/normalisation helpers or reads a generated policy constant.
    const int n = static_cast<int>(x.size());
    std::vector<std::complex<double>> spectrum(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        const double w = 2*pi*k/n;
        const std::complex<double> step {std::cos(w), -std::sin(w)};
        std::complex<double> phase {1,0}, coefficient {};
        for (float v : x) { coefficient += static_cast<double>(v)*phase; phase *= step; }
        spectrum[k] = coefficient*(1-b)/(1.0-b*step);
    }
    std::vector<float> out(static_cast<std::size_t>(n)); double first = 0;
    for (int i = 0; i < n; ++i) {
        std::complex<double> value {};
        for (int k = 0; k < n; ++k) {
            const double w = 2*pi*k*i/n;
            value += spectrum[k]*std::complex<double>(std::cos(w), std::sin(w));
        }
        const double current = value.real()/n; if (i == 0) first = current;
        out[i] = static_cast<float>(current-first);
    }
    const double before = spread(x), after = spread(out);
    if (before > 0 && after > 0) {
        const float gain = static_cast<float>(std::sqrt(before/after));
        for (float& v : out) v *= gain;
    }
    return out;
}
void sourceOracles() {
    const auto open = Engine::openNotes(acustra::Tuning::Standard);
    for (double rate : {44100.,48000.,96000.}) for (int s : {0,3,5})
        for (float velocity : {.15f,100.f/127.f,1.f}) {
            auto pick = fresh(rate, Model::Bellido1978, Style::Pick);
            auto halfOriginal = fresh(rate, Model::Original, Style::Pick, .58f, .5f);
            const int note = open[s]+12;
            pick->noteOn(note, velocity, s+1, 1); halfOriginal->noteOn(note, velocity, s+1, 1);
            Access::copyReleaseGeometry(*halfOriginal,*pick,s);
            Access::initialiseSource(*pick,s); Access::initialiseSource(*halfOriginal,s);
            const auto a=Access::field(*pick,s), b=Access::field(*halfOriginal,s);
            const double error=relativeError(a,b); maxPickError=std::max(maxPickError,error);
            expect(error<2e-6,"Classic Pick differs from independent half-calibrated Original release field");
            expect(a.fittedShare==acustra::fittedPhysicalCalibration.pickReleaseVelocityShare,
                   "Classic Pick rewrote stored public calibration");
            expect(b.fittedShare==.5f*acustra::fittedPhysicalCalibration.pickReleaseVelocityShare,
                   "Original source altered its supplied half calibration");
            expect(Access::configured(*pick)==1 && Access::configured(*halfOriginal)==0,
                   "source oracle altered configured model eligibility");
            expect(a.strokeRandom==b.strokeRandom,"Pick policy changed stroke noise draws"); ++pickCases;

            auto thumb=fresh(rate,Model::Bellido1978,Style::Thumb);
            auto nominal=fresh(rate,Model::Original,Style::Thumb);
            thumb->noteOn(note,velocity,s+1,1); nominal->noteOn(note,velocity,s+1,1);
            Access::copyReleaseGeometry(*nominal,*thumb,s);
            Access::initialiseSource(*thumb,s); Access::initialiseSource(*nominal,s);
            const auto c=Access::field(*thumb,s); auto d=Access::field(*nominal,s);
            // Undo Original's nominal-slip inverse in frequency space. At exact
            // nominal equality Original skips both operations, so apply the actual
            // slip to its untouched release field instead.
            const double pole=d.reference>0?d.reference:c.pole;
            for(int p=0;p<2;++p) d.wave[p]=referenceSlipByFourier(d.wave[p],pole);
            const double thumbError=relativeError(c,d);maxThumbError=std::max(maxThumbError,thumbError);
            expect(thumbError<2e-5,"Classic Thumb differs from independent full-slip Fourier release field");
            expect(c.pole>0 && c.pole<1 && c.reference==0,"Classic Thumb retained inverse or lost full actual slip");
            expect(c.strokeRandom==d.strokeRandom,"Thumb policy changed stroke noise draws"); ++thumbCases;

            auto finger=fresh(rate,Model::Bellido1978,Style::Finger);
            auto originalFinger=fresh(rate,Model::Original,Style::Finger);
            finger->noteOn(note,velocity,s+1,1);originalFinger->noteOn(note,velocity,s+1,1);
            Access::copyReleaseGeometry(*originalFinger,*finger,s);
            Access::initialiseSource(*finger,s);Access::initialiseSource(*originalFinger,s);
            const auto f=Access::field(*finger,s),g=Access::field(*originalFinger,s);
            for(int p=0;p<2;++p)
                expect(f.wave[p].size()==g.wave[p].size()
                    && std::memcmp(f.wave[p].data(),g.wave[p].data(),f.wave[p].size()*sizeof(float))==0,
                    "model release policy changed calibrated Finger source bytes");
            expect(f.pole==g.pole&&f.reference==g.reference&&f.cachedEnergy==g.cachedEnergy
                &&f.strokeRandom==g.strokeRandom,"model release policy changed Finger source state");
            ++fingerCases;
        }
}
void advance(Engine& e,int n) { for(int i=0;i<n;++i){float l=0,r=0;e.process(&l,&r,1);} }
void queuedOwnership() {
    const auto open=Engine::openNotes(acustra::Tuning::Standard);
    for(double rate:{44100.,48000.,96000.}) for(auto style:{Style::Pick,Style::Thumb})
        for(auto initial:{Model::Original,Model::Bellido1978}) for(int s:{0,3,5}) {
            const auto active=initial==Model::Original?Model::Bellido1978:Model::Original;
            auto pending=fresh(rate,initial,style), control=fresh(rate,initial,style);
            const int note=open[s]+12;
            for(auto*e:{pending.get(),control.get()}) {
                e->noteOn(note,.7f,s+1);advance(*e,240);
                e->setParameters(parameters(active,style));advance(*e,240);
            }
            pending->setParameters(parameters(initial,style));
            expect(Access::configured(*pending)==static_cast<int>(active) && Access::pending(*pending),
                   "queued release fixture never entered pending model state");
            pending->noteOn(note+2,.7f,s+1);control->noteOn(note+2,.7f,s+1);
            const auto a=Access::field(*pending,s),b=Access::field(*control,s);
            const double error=relativeError(a,b);maxQueueError=std::max(maxQueueError,error);
            expect(error<2e-6,"pending requested model changed active Pick/Thumb release field");
            for (std::size_t i=0;i<a.geometry.size();++i) {
                const double geometryError=std::abs(a.geometry[i]-b.geometry[i])
                    /std::max(std::abs(b.geometry[i]),1e-30);
                if (i<3) {
                    maxQueueGeometryError=std::max(maxQueueGeometryError,geometryError);
                    expect(a.geometry[i]==b.geometry[i],
                           "pending model request changed active release point/length/period");
                } else {
                    // The extra public request can revise the retained loop's
                    // tuning interpolation, separately from the exact release
                    // contact period. Bound that observer difference in cents.
                    const double cents=std::abs(1200*std::log2(a.geometry[i]/b.geometry[i]));
                    maxQueueDelayCents=std::max(maxQueueDelayCents,cents);
                    expect(std::isfinite(cents)&&cents<.5,
                           "queued history changed retained loop delay by half a cent");
                }
            }
            expect(a.reference==b.reference && a.pole==b.pole,
                   "pending requested model changed active release poles");
            // An extra public model request invalidates voice configuration caches;
            // its recomputation can change stored samples by float rounding. Compare
            // the cache to that engine's actual field, rather than require the twin
            // histories to have identical energy bits.
            const double work=storedSlopeEnergy(a);
            const double cacheError=std::abs(a.cachedEnergy-work)/std::max(work,1e-30);
            maxQueueCacheError=std::max(maxQueueCacheError,cacheError);
            expect(cacheError<2e-6,"queued source energy cache differs from actual stored field");
            if(style==Style::Thumb && active==Model::Bellido1978)
                expect(a.reference==0 && a.pole>0,"queued Classic Thumb lost full-slip policy");
            expect(a.fittedShare==acustra::fittedPhysicalCalibration.pickReleaseVelocityShare,
                   "queued source changed stored public Pick calibration");++queueCases;
        }
}
void continuityAndBounds() {
    const auto open=Engine::openNotes(acustra::Tuning::Standard);
    for(double rate:{44100.,48000.,96000.}) for(int s=0;s<6;++s) {
        auto near=fresh(rate,Model::Bellido1978,Style::Thumb),full=fresh(rate,Model::Bellido1978,Style::Thumb);
        near->noteOn(open[s]+12,.9999f,s+1);full->noteOn(open[s]+12,1.f,s+1);
        const auto a=Access::field(*near,s),b=Access::field(*full,s);
        const double error=relativeError(a,b);maxContinuityError=std::max(maxContinuityError,error);
        expect(b.pole>0 && b.pole<1 && b.reference==0,"full velocity/default Touch bypassed Thumb slip");
        expect(error<.002,"Thumb release field jumps at exact full velocity");++continuityCases;
    }
    for(double rate:{44100.,48000.,96000.}) for(auto style:{Style::Pick,Style::Thumb})
        for(float touch:{0.f,.58f,1.f}) for(float velocity:{.001f,.15f,1.f}) for(int s:{0,3,5}) {
            auto e=fresh(rate,Model::Bellido1978,style,touch);e->noteOn(open[s]+12,velocity,s+1);
            const auto a=Access::field(*e,s);
            expect(std::isfinite(a.cachedEnergy)&&a.cachedEnergy>=0,"bounded release produced invalid cached work");
            for(const auto&wave:a.wave)for(float v:wave)expect(std::isfinite(v),"bounded release field is nonfinite");
            if(style==Style::Thumb)expect(a.pole>0&&a.pole<1&&a.reference==0,"bounded Thumb lost actual full-slip policy");
            expect(a.fittedShare==acustra::fittedPhysicalCalibration.pickReleaseVelocityShare,
                   "bounded release mutated fitted Pick calibration");
            for(int i=0;i<32;++i){float l=0,r=0;e->process(&l,&r,1);expect(std::isfinite(l)&&std::isfinite(r),"bounded native release output is nonfinite");}
            ++boundCases;
        }
}
}
int main() {
    sourceOracles();queuedOwnership();continuityAndBounds();
    std::cout<<"pick_oracle "<<pickCases<<" thumb_fourier_oracle "<<thumbCases<<" queued "<<queueCases
             <<" finger_byte_controls "<<fingerCases<<" continuity "<<continuityCases<<" bounds "<<boundCases<<" max_pick_error "<<maxPickError
             <<" max_thumb_error "<<maxThumbError<<" max_queue_error "<<maxQueueError
             <<" max_queue_geometry_error "<<maxQueueGeometryError
             <<" max_queue_delay_cents "<<maxQueueDelayCents
             <<" max_queue_cache_error "<<maxQueueCacheError<<" max_continuity_error "<<maxContinuityError<<'\n';
    return failures?1:0;
}
