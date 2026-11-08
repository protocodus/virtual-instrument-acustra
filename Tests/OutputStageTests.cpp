#include "DSP/AcustraEngine.h"
#include "DSP/OutputStage.h"
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <cstdlib>
#include <limits>
void check(bool pass,const char* message){if(!pass){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
 for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.,768000.}){
  auto stage=std::make_unique<protocodus::OutputStage<2>>();stage->Prepare(rate);
  int delay=4*static_cast<int>(rate/2000.);check(stage->DelaySamples()==delay,"wrong output delay");
  for(int i=0;i<delay+40;++i){std::array<float,2> f{i==0?.001f:0.f,i==0?-.00025f:0.f};stage->Process(f);check(f[0]==(i==delay?.001f:0.f)&&f[1]==(i==delay?-.00025f:0.f),"below-knee impulse not delayed bit-exactly");}
  for(bool guard:{false,true}){
   stage->Prepare(rate,guard?protocodus::kVuGuard:protocodus::CompressorSettings{});
   // The shared limiter carries gain as 1-(1-hold). Four float roundings
   // (two subtractions, the window-mean cast and output multiply) each cost
   // at most epsilon times the corresponding magnitude. With |input|<=100,
   // this conservative input-scaled bound follows from arithmetic precision,
   // not a measured overshoot. The double running sum contributes <1e-9
   // output error over these40000 updates (40000*2^-52*100/window).
   const float ceilingRoundoff = 4 * std::numeric_limits<float>::epsilon()
       * (100.f + protocodus::OutputStage<2>::kCeiling) + 1e-9f;
   unsigned random=173;
   for(int i=0;i<40000;++i){random=1664525u*random+1013904223u;float x=(float(int(random>>8)-8388608)/8388608.f)*((i%1000)<20?100.f:3.f);std::array<float,2> f{x,-.25f*x};stage->Process(f);if(std::abs(f[0])>protocodus::OutputStage<2>::kCeiling+ceilingRoundoff||std::abs(f[1])>protocodus::OutputStage<2>::kCeiling+ceilingRoundoff){std::cerr.precision(12);std::cerr<<"rate "<<rate<<" guard "<<guard<<" frame "<<i<<" peak "<<f[0]<<" ceiling "<<protocodus::OutputStage<2>::kCeiling<<"\n";check(false,"sample ceiling exceeded");}check(f[1]==-.25f*f[0],"stereo image not linked");}
   stage->Reset();auto fresh=std::make_unique<protocodus::OutputStage<2>>();fresh->Prepare(rate,guard?protocodus::kVuGuard:protocodus::CompressorSettings{});
   for(int i=0;i<delay+100;++i){std::array<float,2> a{.01f*float(i%3),-.002f},b=a;stage->Process(a);fresh->Process(b);check(a==b,"reset not identical to fresh output stage");}
  }
 }
 // The low-rate ceiling reserve changes only the peak threshold. Reciprocal
 // gain and the matched compressor threshold preserve its VU transfer below
 // that ceiling, including signals that engage the compressor soft knee.
 for(double amplitude:{.005,.05,.2,.4}) {
  protocodus::OutputStage<1> ordinary, reserved;
  auto settings=protocodus::kVuGuard;settings.thresholdDb+=2.5;
  ordinary.Prepare(16000,protocodus::kVuGuard);reserved.Prepare(16000,settings);
  const float up=static_cast<float>(std::pow(10.,2.5/20.));
  const float down=static_cast<float>(std::pow(10.,-2.5/20.));
  for(int i=0;i<32000;++i){std::array<float,1>a{static_cast<float>(amplitude)},b{a[0]*up};ordinary.Process(a);reserved.Process(b);b[0]*=down;check(std::abs(a[0]-b[0])<2e-6f,"low-rate reserve changed quiet gain or VU transfer");}
 }
 for(int rate:{8000,16000,22050,32000,44100}){
  auto engine=std::make_unique<acustra::AcustraEngine>();acustra::EngineParameters p;
  p.outputGain=4;p.picking=acustra::PickingTechnique::Pick;p.touch=1;p.pluckPosition=0;p.room=1;p.piezoMix=1;p.stereoWidth=1;
  engine->setParameters(p);engine->prepare(rate,1);for(int note:{40,45,50,55,59,64})engine->noteOn(note,1,1);
  const float ceiling=static_cast<float>(std::pow(10.,(rate<44100?-4.:-1.5)/20.));
  for(int i=0;i<rate/2;++i){float l,r,z;engine->process(&l,&r,{&z},1);check(std::abs(l)<=ceiling+2e-6f&&std::abs(r)<=ceiling+2e-6f&&std::abs(z)<=ceiling+2e-6f,"rate-dependent instrument peak reserve failed");}
 }
 // The calibrated trim and output dynamics must never alter model mechanics.
 auto quiet=std::make_unique<acustra::AcustraEngine>(),loud=std::make_unique<acustra::AcustraEngine>();
 acustra::EngineParameters p;p.outputGain=.02f;p.picking=acustra::PickingTechnique::Pick;p.room=1;p.piezoMix=1;p.stereoWidth=1;p.touch=1;p.pluckPosition=0;quiet->setParameters(p);p.outputGain=4;loud->setParameters(p);quiet->prepare(48000,1);loud->prepare(48000,1);
 for(int n:{40,45,50,55,59,64}){quiet->noteOn(n,1,1);loud->noteOn(n,1,1);}
 for(int i=0;i<48000;++i){float l,r,z;quiet->process(&l,&r,1);loud->process(&l,&r,{&z},1);check(std::abs(l)<=protocodus::OutputStage<2>::kCeiling+1e-6f&&std::abs(r)<=protocodus::OutputStage<2>::kCeiling+1e-6f&&std::abs(z)<=protocodus::OutputStage<1>::kCeiling+1e-6f,"instrument output exceeded ceiling");check(quiet->getLastBridgeVelocity()==loud->getLastBridgeVelocity()&&quiet->getLastBridgeReactionForce()==loud->getLastBridgeReactionForce()&&quiet->getLastBridgeBodyForce()==loud->getLastBridgeBodyForce()&&quiet->getLastBridgePower()==loud->getLastBridgePower()&&quiet->getLastBridgeBodyPower()==loud->getLastBridgeBodyPower(),"output dynamics changed mechanical state");}
 std::cout<<"PASS output delay, transparency, ceiling, linked stereo, reset, and mechanical independence\n";
}
