// A re-pluck establishes a new physical stopping reference even when the
// sounding pitch and every running loss parameter already reached that slide.
#include "DSP/AcustraEngine.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
namespace acustra {
struct AcustraEngineTestAccess {
 static void settle(AcustraEngine& e,int string) {
  auto& v=e.voices_[static_cast<std::size_t>(string)];v.attackPitchCents=0;
  e.configureVoice(v,string,v.midiNote,false);
 }
 static void invalidate(AcustraEngine& e,int string) {
  e.voices_[static_cast<std::size_t>(string)].configurationKey.generation=0;
 }
 static bool contactDesign(AcustraEngine& e,int string) {
  const auto& v=e.voices_[static_cast<std::size_t>(string)];
  return v.dispersionDesignArguments.back()==(v.pitchGeometry.value.tuningFrequency>=350.f?1.:0.);
 }
};}
int main(){
 using E=acustra::AcustraEngine;using A=acustra::AcustraEngineTestAccess;
 int failures=0,cases=0;double worst=0;
 for(double rate:{44100.,48000.,96000.})for(int picking=0;picking<3;++picking)for(float slide:{-12.f,12.f}) {
  std::array<std::unique_ptr<E>,2> engines;
  std::array<std::array<float,64>,2> l{},r{};
  for(auto& e:engines){
   e=std::make_unique<E>();auto realism=e->performanceRealism();realism.frettingPitch=false;e->setPerformanceRealism(realism);
   acustra::EngineParameters p;p.picking=static_cast<acustra::PickingTechnique>(picking);p.room=0;e->setParameters(p);e->prepare(rate,64);e->setStringPerChannelMode(true);
   // Cross the upper-dispersion contact boundary in either direction.
   e->setPitchBend(slide<0?12.f:0.f,6);e->noteOn(64,.6f,6);
   for(int block=0;block<150;++block)e->process(l[0].data(),r[0].data(),64);
   e->setPitchBend(slide<0?0.f:12.f,6);
   for(int block=0;block<24;++block)e->process(l[0].data(),r[0].data(),64);
   A::settle(*e,5);
  }
  // This changes only memoization. Both instruments retain the same ringing
  // waves, RNG state and public note/controller sequence.
  A::invalidate(*engines[1],5);
  for(auto& e:engines)e->noteOn(64,.6f,6);
  for(int block=0;block<64;++block){
   for(int i=0;i<2;++i)engines[static_cast<std::size_t>(i)]->process(l[static_cast<std::size_t>(i)].data(),r[static_cast<std::size_t>(i)].data(),64);
   if(l[0]!=l[1]||r[0]!=r[1])++failures;
   for(int n=0;n<64;++n)worst=std::max(worst,std::abs(double(l[0][n]-l[1][n])));
  }
  if(!A::contactDesign(*engines[0],5))++failures;
  ++cases;
 }
 std::printf("Contact-reference cache: %d public re-pluck cases, maximum cached/fresh difference %.9g, failures %d\n",cases,worst,failures);
 return failures?1:0;
}
