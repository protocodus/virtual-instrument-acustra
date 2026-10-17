#include "DSP/AcustraEngine.h"
#include "DSP/ConstructionLoudnessData.h"
#include <array>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>
namespace acustra {
struct AcustraEngineTestAccess {
 static std::array<std::complex<double>,2> body(const AcustraEngine& e,double frequency) {
  const auto z=std::polar(1.0,-2.0*std::acos(-1.0)*frequency/e.sampleRate_);
  std::array<std::complex<double>,2> sum{};
  const auto& b=e.bodyBank_;
  for(int i=0;i<b.count;++i) {
   const std::complex<double> pole(b.poleReal[i],b.poleImaginary[i]);
   for(int side=0;side<2;++side) {
    const std::complex<double> r(side?b.rightReal[i]:b.leftReal[i],side?b.rightImaginary[i]:b.leftImaginary[i]);
    sum[side]+=r/(1.0-pole*z)+std::conj(r)/(1.0-std::conj(pole)*z);
   }
  }
  return sum;
 }
 static std::complex<float> bridge(const AcustraEngine& e,float f) { return e.bridgePortMobility(f,0).normal; }
 static void poles(const AcustraEngine& e,std::ostream& out,int shape,int wood) {
  for(int i=0;i<e.bodyBank_.count;++i) {
   const double re=e.bodyBank_.poleReal[i],im=e.bodyBank_.poleImaginary[i];
   if(im==0.0)continue;
   const double f=std::atan2(im,re)*e.sampleRate_/(2.0*std::acos(-1.0));
   const double t60=-std::log(1000.0)/(e.sampleRate_*std::log(std::hypot(re,im)));
   out<<shape<<','<<wood<<','<<i<<','<<f<<','<<t60<<'\n';
  }
 }
 static float contour(float frequency) { return AcustraEngine::captureVoicingGain(frequency); }
};
}
int main(int argc,char**argv) {
 if(argc!=2)return 2;
 const std::filesystem::path output(argv[1]);
 std::ofstream transfer(output/"transfer.csv"),poles(output/"poles.csv");
 transfer<<"shape,wood,frequency,body_left_real,body_left_imag,body_right_real,body_right_imag,bridge_real,bridge_imag,contour,micgain,monotrim,piezotrim\n"<<std::setprecision(12);
 poles<<"shape,wood,mode,frequency,t60\n"<<std::setprecision(12);
 constexpr int samples=72000,block=127;
 for(int shape=0;shape<4;++shape)for(int wood=0;wood<3;++wood) {
  acustra::EngineParameters p;p.shape=static_cast<acustra::BodyShape>(shape);p.bodyMaterial=static_cast<acustra::BodyMaterial>(wood);
  auto e=std::make_unique<acustra::AcustraEngine>();e->setParameters(p);e->prepare(48000,block);
  acustra::AcustraEngineTestAccess::poles(*e,poles,shape,wood);
  const auto cell=(shape*3+wood)*3;
  for(int bin=0;bin<1024;++bin) {
   const double f=50.0*std::pow(160.0,bin/1023.0);const auto h=acustra::AcustraEngineTestAccess::body(*e,f);const auto y=acustra::AcustraEngineTestAccess::bridge(*e,f);
   transfer<<shape<<','<<wood<<','<<f<<','<<h[0].real()<<','<<h[0].imag()<<','<<h[1].real()<<','<<h[1].imag()<<','<<y.real()<<','<<y.imag()<<','<<acustra::AcustraEngineTestAccess::contour(f)<<','<<acustra::detail::constructionMicReference[cell]<<','<<acustra::detail::constructionMonoTrim[cell]<<','<<acustra::detail::constructionPiezoTrim[cell]<<'\n';
  }
  for(auto pair:{std::array<int,2>{40,1},std::array<int,2>{55,4},std::array<int,2>{64,6}}) {
   e->reset();e->setStringPerChannelMode(true);e->noteOn(pair[0],.72f,pair[1]);
   std::vector<float> interleaved;interleaved.reserve(samples*3);
   std::array<float,block> l{},r{},z{};
   for(int sample=0;sample<samples;sample+=block) {
    const int count=std::min(block,samples-sample);e->process(l.data(),r.data(),{z.data()},count);
    for(int i=0;i<count;++i){interleaved.push_back(l[i]);interleaved.push_back(r[i]);interleaved.push_back(z[i]);}
   }
   std::ofstream audio(output/(std::to_string(shape)+"-"+std::to_string(wood)+"-"+std::to_string(pair[0])+".f32"),std::ios::binary);
   audio.write(reinterpret_cast<const char*>(interleaved.data()),interleaved.size()*sizeof(float));
  }
 }
}
