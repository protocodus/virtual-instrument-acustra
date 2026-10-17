#include "DSP/AcustraEngine.h"
#include <array>
#include <fstream>
#include <memory>
#include <cstdlib>
#include <cmath>
#include <iostream>
namespace acustra {
struct AcustraEngineTestAccess {
 static std::array<float,2> inputs(const AcustraEngine& e) {
  const auto& d=e.bridgeBodyMomentDerivative_;
  auto h=[&](unsigned age){return d.history[(static_cast<unsigned>(d.index)-age)&d.historyMask];};
  const auto& f=e.bridgeBodyForceDerivative_;
  const float force=f.history[(static_cast<unsigned>(f.index)-1)&f.historyMask]-f.history[(static_cast<unsigned>(f.index)-2)&f.historyMask];
  if(force!=e.lastBridgeBodyForce_) std::abort();
  return {e.lastBridgeBodyForce_,h(1)-h(2)};
 }
}; }
int main(int argc,char**argv) {
 if(argc!=5)return 2;
 auto e=std::make_unique<acustra::AcustraEngine>();
 acustra::EngineParameters p;
 p.shape=acustra::BodyShape::Parlor;
 p.bodyMaterial=static_cast<acustra::BodyMaterial>(std::atoi(argv[1]));
 p.picking=static_cast<acustra::PickingTechnique>(std::atoi(argv[2]));
 p.room=0;p.piezoMix=0;p.outputGain=.42f;p.bodyAmount=.82f;p.stereoWidth=.62f;p.touch=.58f;p.pluckPosition=.28f;
 e->setParameters(p);e->prepare(48000,64);e->setStringPerChannelMode(true);e->setMasterTuneCents(std::atof(argv[3]));
 e->noteOn(40,p.picking==acustra::PickingTechnique::Pick?120.f/127:96.f/127,1);
 std::ofstream out(argv[4],std::ios::binary);
 for(int n=0;n<6*48000;++n){float l,r;e->process(&l,&r,1);auto v=acustra::AcustraEngineTestAccess::inputs(*e);float frame[]={v[0],v[1],l,r};out.write(reinterpret_cast<char*>(frame),sizeof(frame));}
 return 0;
}
