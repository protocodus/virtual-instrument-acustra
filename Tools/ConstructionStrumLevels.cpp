// The existing ConstructionMatrixTests open-strum protocol, measured natively.
// JSON goes to stdout; no scores, table writes, normalization, or DSP overrides.
#include "DSP/AcustraEngine.h"
#include "DSP/ConstructionLoudnessData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

namespace {
using acustra::AcustraEngine;
using acustra::EngineParameters;
using acustra::CaptureType;
constexpr double rate = 48000.0;
constexpr int block = 128;
constexpr int frames = 16800;
constexpr std::array<CaptureType, 3> captures {
    CaptureType::StereoMic, CaptureType::MonoMic, CaptureType::Piezo };
constexpr std::array<const char*, 3> captureNames { "stereo_mic", "mono_mic", "piezo" };
constexpr std::array<const char*, 2> modelNames { "original", "bellido1978" };
constexpr std::array<const char*, 4> shapeNames { "parlor", "auditorium", "dreadnought", "jumbo" };
constexpr std::array<const char*, 3> woodNames { "spruce", "mahogany", "maple" };
constexpr std::array<const char*, 5> tuningNames { "standard", "drop_d", "dadgad", "open_g", "half_step_down" };

// Direct form I and the BS.1770 48 kHz coefficients used by the guard.
struct Biquad {
    double b0, b1, b2, a1, a2;
    double x1 { 0.0 }, x2 { 0.0 }, y1 { 0.0 }, y2 { 0.0 };
    double process(double x) noexcept {
        const double y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1; x1=x; y2=y1; y1=y; return y;
    }
};
struct KWeighting {
    Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285,
                   -1.69065929318241, 0.73248077421585 };
    Biquad highPass { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };
    double process(double x) noexcept { return highPass.process(shelf.process(x)); }
};
struct Level { double weightedDb, peak, piezoPeak; bool finite; };
Level play(const EngineParameters& parameters) {
    auto engine=std::make_unique<AcustraEngine>();
    engine->setParameters(parameters); engine->prepare(rate,block);
    const auto open=AcustraEngine::openNotes(parameters.tuning);
    engine->beginStrum();
    for(int string=0;string<AcustraEngine::stringCount;++string)
        engine->noteOn(open[static_cast<std::size_t>(string)],0.8f,1,
                       static_cast<int>(string*0.012*rate),true);
    std::vector<float> left(block),right(block),piezo(block);
    AcustraEngine::OutputBuses buses; buses.piezo=piezo.data();
    Level result { -400.0,0.0,0.0,true };
    KWeighting weightLeft,weightRight; double weighted=0.0;
    for(int start=0;start<frames;start+=block) {
        const int count=std::min(block,frames-start);
        engine->process(left.data(),right.data(),buses,count);
        for(int index=0;index<count;++index) {
            const float l=left[static_cast<std::size_t>(index)];
            const float r=right[static_cast<std::size_t>(index)];
            const float p=piezo[static_cast<std::size_t>(index)];
            result.finite=result.finite&&std::isfinite(l)&&std::isfinite(r)&&std::isfinite(p);
            result.peak=std::max({ result.peak,double(std::abs(l)),double(std::abs(r)) });
            result.piezoPeak=std::max(result.piezoPeak,double(std::abs(p)));
            const double kl=weightLeft.process(l),kr=weightRight.process(r);
            weighted+=kl*kl+kr*kr;
        }
    }
    // Matches the guard: sum channel powers, mean over frames, with no gate.
    result.weightedDb=10.0*std::log10(std::max(weighted/frames,1.0e-40));
    return result;
}
template<std::size_t N> void printArray(const std::array<float,N>& values) {
    std::cout << '[';
    for(std::size_t index=0;index<N;++index) {
        if(index!=0)std::cout<<',';
        std::cout<<values[index];
    }
    std::cout<<']';
}
} // namespace

int main(int argc,char**) {
    if(argc!=1) { std::cerr<<"Usage: AcustraConstructionStrumLevels (JSON on stdout)\n"; return 2; }
    static_assert(acustra::detail::constructionMicReference.size()==72);
    static_assert(acustra::detail::constructionMonoTrim.size()==72);
    static_assert(acustra::detail::constructionPiezoTrim.size()==72);
    std::array<std::array<double,5>,3> reference {};
    for(std::size_t capture=0;capture<captures.size();++capture)
        for(std::size_t tuning=0;tuning<tuningNames.size();++tuning) {
            EngineParameters parameters; parameters.capture=captures[capture];
            parameters.tuning=static_cast<acustra::Tuning>(tuning);
            const auto level=play(parameters);
            if(!level.finite)return 1;
            reference[capture][tuning]=level.weightedDb;
        }
    std::cout<<std::setprecision(std::numeric_limits<double>::max_digits10)<<std::boolalpha;
    std::cout<<"{\"schema\":\"AcustraConstructionStrumLevelsV1\",\"rate\":48000,\"frames\":16800,\"picking\":\"finger\",\"built_gains\":{\"mic\":";
    printArray(acustra::detail::constructionMicReference);
    std::cout<<",\"mono\":"; printArray(acustra::detail::constructionMonoTrim);
    std::cout<<",\"piezo\":"; printArray(acustra::detail::constructionPiezoTrim);
    std::cout<<"},\"rows\":["; bool first=true; bool finite=true;
    for(std::size_t model=0;model<modelNames.size();++model)
        for(std::size_t shape=0;shape<shapeNames.size();++shape)
            for(std::size_t wood=0;wood<woodNames.size();++wood)
                for(std::size_t capture=0;capture<captures.size();++capture)
                    for(std::size_t tuning=0;tuning<tuningNames.size();++tuning) {
                        EngineParameters parameters;
                        parameters.guitarModel=static_cast<acustra::GuitarModel>(model);
                        parameters.shape=static_cast<acustra::BodyShape>(shape);
                        parameters.bodyMaterial=static_cast<acustra::BodyMaterial>(wood);
                        parameters.capture=captures[capture];
                        parameters.tuning=static_cast<acustra::Tuning>(tuning);
                        const auto level=play(parameters); finite=finite&&level.finite;
                        if(!first)std::cout<<','; first=false;
                        std::cout<<"{\"construction\":[\""<<modelNames[model]<<"\",\""<<shapeNames[shape]<<"\",\""<<woodNames[wood]<<"\"],\"capture\":\""<<captureNames[capture]<<"\",\"tuning\":\""<<tuningNames[tuning]<<"\",\"weighted_db\":"<<level.weightedDb<<",\"reference_db\":"<<reference[capture][tuning]<<",\"relative_db\":"<<level.weightedDb-reference[capture][tuning]<<",\"peak\":"<<level.peak<<",\"piezo_peak\":"<<level.piezoPeak<<",\"finite\":"<<level.finite<<'}';
                    }
    std::cout<<"]}\n"; return finite?0:1;
}
