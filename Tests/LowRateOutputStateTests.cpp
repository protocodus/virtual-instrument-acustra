// Final finite samples are insufficient: a guard could hide a broken upstream
// filter by replacing NaN with silence. Inspect the warmed microphone/room
// states while pure Piezo Capture must remain identical to the direct pickup.
#include "DSP/AcustraEngine.h"
#include "DSP/AcustraPresets.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <type_traits>
namespace acustra {
struct AcustraEngineTestAccess {
 template<class T> static bool finiteValues(const T& value) {
  if constexpr(std::is_arithmetic_v<T>) return std::isfinite(value);
  else {for(const auto& item:value) if(!finiteValues(item))return false;return true;}
 }
 static bool stable(const AcustraEngine& e) {
  const auto& f=e.bodyBank_.captureFilter;
  for(const auto& c:f.coefficients)
   if(!finiteValues(c)||!(1+c[3]+c[4]>0&&1-c[3]+c[4]>0&&1-c[4]>0))return false;
  const auto& r=e.room_;
  for(std::size_t i=0;i<r.absorptionPole.size();++i)
   if(!(std::abs(r.absorptionPole[i])<1&&r.absorptionGain[i]>=0&&r.absorptionGain[i]/(1-r.absorptionPole[i])<1))return false;
  return finiteValues(f.stateLeft)&&finiteValues(f.stateRight)
   &&finiteValues(r.absorptionState)&&finiteValues(r.sideHighpassState)
   &&finiteValues(r.currentLeft)&&finiteValues(r.currentRight)
   &&finiteValues(r.earlyLeft)&&finiteValues(r.earlyRight)&&finiteValues(r.inputState)
   &&finiteValues(r.outputScale)&&r.outputScale>0;
 }
 static bool buffersFinite(const AcustraEngine&e){return finiteValues(e.room_.lines)&&finiteValues(e.room_.early)&&finiteValues(e.room_.diffusers)&&finiteValues(e.room_.converterInput)&&finiteValues(e.room_.converterStereo);}
}; }
int main(){
 for(int rate:{8000,16000,22050,32000,44100})for(int patch=0;patch<9;++patch){
  auto p=acustra::engineParameters(acustra::factoryPresets[patch%8]);
  if(patch==8)p.bodyMaterial=acustra::BodyMaterial::Maple;
  p.capture=acustra::CaptureType::Piezo;p.room=1;p.piezoMix=1;p.stereoWidth=1;
  p.outputGain=std::pow(10.f,6.f/20.f);p.picking=acustra::PickingTechnique::Pick;p.touch=1;p.pluckPosition=0;
  auto engine=std::make_unique<acustra::AcustraEngine>();engine->setParameters(p);engine->prepare(rate,1);
  double lateEnergy=0;
  for(int n=0;n<rate*8/5;++n){
   if(n%(rate/10)==0&&n<rate*13/10)for(int note:acustra::AcustraEngine::openNotes(p.tuning))engine->noteOn(note,1,1);
   float l,r,z;engine->process(&l,&r,{&z},1);
   if(!std::isfinite(l)||!std::isfinite(r)||!std::isfinite(z)||l!=z||r!=z||((n&63)==0&&!acustra::AcustraEngineTestAccess::stable(*engine))){std::cerr<<"FAIL upstream state or pure-pickup parity at "<<rate<<" preset "<<patch<<" frame "<<n<<'\n';return 1;}
   if(n>rate/2)lateEnergy+=double(z)*z;
  }
  if(!(lateEnergy>1e-8)||!acustra::AcustraEngineTestAccess::buffersFinite(*engine)){std::cerr<<"FAIL sustained pickup or finite delay buffers\n";return 1;}
 }
 std::cout<<"PASS45 low-rate full-room cases: stable upstream filters, finite internal buffers, sustained Main/direct pickup identity\n";
}
