// Final finite samples are insufficient: a guard could hide a broken upstream
// filter by replacing NaN with silence. Inspect the warmed microphone/room
// states while pure Piezo Capture must remain identical to the direct pickup.
#include "DSP/AcustraPerformer.h"
#include "DSP/AcustraPresets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
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
 static bool upstreamFinite(const AcustraEngine& e) {
  const auto body=[](const auto& b){return finiteValues(b.real)&&finiteValues(b.imaginary)
   &&finiteValues(b.momentReal)&&finiteValues(b.momentImaginary)
   &&finiteValues(b.captureFilter.stateLeft)&&finiteValues(b.captureFilter.stateRight);};
  if(!body(e.bodyBank_)||!body(e.fadingBodyBank_))return false;
  for(const auto& voice:e.voices_){const auto& a=voice.stringAxialForce;
   if(!finiteValues(a.ring)||!finiteValues(a.bandState)||!finiteValues(a.force)
    ||!finiteValues(a.lossY1)||!finiteValues(a.thiranY1)||!finiteValues(a.increment))return false;}
  const auto& f=e.lastBodyForceProbe_;
  return finiteValues(f.transverseNormal)&&finiteValues(f.transverseMoment)
   &&finiteValues(f.axialNormal)&&finiteValues(f.axialMoment)
   &&finiteValues(f.totalNormal)&&finiteValues(f.totalMoment);
 }
 static bool buffersFinite(const AcustraEngine&e){return finiteValues(e.room_.lines)&&finiteValues(e.room_.early)&&finiteValues(e.room_.diffusers)&&finiteValues(e.room_.converterInput)&&finiteValues(e.room_.converterStereo);}
}; }

// Exact low-rate stress that originally overflowed at8.10s, after its1.45s
// final note-offs. A short finite-output test missed the growing axial DC
// mode because final output protection turned nonfinite samples into silence.
bool releasedOpenGStaysFiniteAndDecays(){
 constexpr int rate=11025,block=64,total=rate*10;
 auto p=acustra::engineParameters(acustra::factoryPresets[6]);
 p.capture=acustra::CaptureType::StereoMic;p.room=p.piezoMix=p.stereoWidth=1;
 p.outputGain=std::pow(10.f,6.f/20.f);p.picking=acustra::PickingTechnique::Pick;
 p.touch=1;p.pluckPosition=0;
 auto player=std::make_unique<acustra::Performer>();player->setParameters(p);player->prepare(rate,block);
 std::array<float,block> l{},r{},z{};
 const acustra::AcustraEngine::OutputBuses buses{z.data()};
 for(int i=0;i<(rate/2+63)/64;++i){player->beginBlock(l.data(),r.data(),buses,block);
  if(i==0)player->setPitchBendRange(0,1,12);
  player->endBlock();}
 struct Event{int frame,note;bool on;};std::vector<Event> events;
 const auto opens=acustra::AcustraEngine::openNotes(p.tuning);
 for(int k=0;k<13;++k){const int frame=int((.02+.1*k)*rate+.5);
  if(k)for(int note:opens)events.push_back({frame-1,note,false});
  for(int note:opens)events.push_back({frame,note,true});}
 for(int note:opens)events.push_back({int((1.6-.15)*rate),note,false});
 std::stable_sort(events.begin(),events.end(),[](const Event&a,const Event&b){return a.frame<b.frame;});
 std::size_t next=0;double earlyForce=0,lateForce=0,earlyAudio=0,lateAudio=0;
 for(int pos=0;pos<total;pos+=block){const int count=std::min(block,total-pos);
  player->beginBlock(l.data(),r.data(),buses,count);
  while(next<events.size()&&events[next].frame<pos+count){const auto event=events[next++];
   if(event.on)player->noteOn(event.frame-pos,1,event.note,127);
   else player->noteOff(event.frame-pos,1,event.note,64);}
  player->endBlock();const auto& engine=player->engine();
  if(!acustra::AcustraEngineTestAccess::stable(engine)
   ||!acustra::AcustraEngineTestAccess::upstreamFinite(engine)){
   std::cerr<<"FAIL growing/nonfinite upstream state after OpenG stress at frame "<<pos+count-1<<'\n';return false;}
  const auto force=engine.getLastBodyForceProbe();
  const double power=double(force.totalNormal)*force.totalNormal+double(force.totalMoment)*force.totalMoment;
  if(pos<17640)earlyForce+=power;
  if(pos>=rate*9)lateForce+=power;
  for(int i=0;i<count;++i){
   if(!std::isfinite(l[i])||!std::isfinite(r[i])||!std::isfinite(z[i]))return false;
   const double energy=double(l[i])*l[i]+double(r[i])*r[i]+double(z[i])*z[i];
   if(pos+i<17640)earlyAudio+=energy;
   if(pos+i>=rate*9)lateAudio+=energy;
  }
 }
 const bool okay=player->droppedEventCount()==0&&next==events.size()
  &&acustra::AcustraEngineTestAccess::buffersFinite(player->engine())
  &&earlyForce>1e-8&&lateForce<earlyForce*1e-10&&earlyAudio>1e-8&&lateAudio<earlyAudio*1e-8;
 std::cout<<"11025Hz ten-second OpenG: tail/early force power "<<lateForce/earlyForce
  <<", output energy "<<lateAudio/earlyAudio<<'\n';
 if(!okay)std::cerr<<"FAIL released OpenG force/output did not decay, or delayed state became nonfinite\n";
 return okay;
}

int main(){
 if(!releasedOpenGStaysFiniteAndDecays())return 1;
 for(int rate:{8000,11025,16000,22050,32000,44100})for(int patch=0;patch<9;++patch){
  auto p=acustra::engineParameters(acustra::factoryPresets[patch%8]);
  if(patch==8)p.bodyMaterial=acustra::BodyMaterial::Maple;
  p.capture=acustra::CaptureType::Piezo;p.room=1;p.piezoMix=1;p.stereoWidth=1;
  p.outputGain=std::pow(10.f,6.f/20.f);p.picking=acustra::PickingTechnique::Pick;p.touch=1;p.pluckPosition=0;
  auto engine=std::make_unique<acustra::AcustraEngine>();engine->setParameters(p);engine->prepare(rate,1);
  double lateEnergy=0;
  for(int n=0;n<rate*8/5;++n){
   if(n%(rate/10)==0&&n<rate*13/10)for(int note:acustra::AcustraEngine::openNotes(p.tuning))engine->noteOn(note,1,1);
   float l,r,z;engine->process(&l,&r,{&z},1);
   if(!std::isfinite(l)||!std::isfinite(r)||!std::isfinite(z)||l!=z||r!=z||((n&63)==0&&(!acustra::AcustraEngineTestAccess::stable(*engine)||!acustra::AcustraEngineTestAccess::upstreamFinite(*engine)))){std::cerr<<"FAIL upstream state or pure-pickup parity at "<<rate<<" preset "<<patch<<" frame "<<n<<'\n';return 1;}
   if(n>rate/2)lateEnergy+=double(z)*z;
  }
  if(!(lateEnergy>1e-8)||!acustra::AcustraEngineTestAccess::buffersFinite(*engine)){std::cerr<<"FAIL sustained pickup or finite delay buffers\n";return 1;}
 }
 std::cout<<"PASS54 low-rate full-room cases plus ten-second OpenG decay: stable upstream filters, finite internal buffers, sustained Main/direct pickup identity\n";
}
