#include "DSP/AcustraEngine.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
int main() {
 for(double rate:{44100.0,48000.0,96000.0}) for(int pick=0;pick<3;++pick) {
  auto e=std::make_unique<acustra::AcustraEngine>();
  acustra::EngineParameters p;p.picking=static_cast<acustra::PickingTechnique>(pick);p.room=.2f;
  e->setParameters(p);e->prepare(rate,64);e->setStringPerChannelMode(true);
  std::array<float,64> l{},r{};std::uint64_t hash=1469598103934665603ULL;
  for(int block=0;block<600;++block) {
   if(block==0) {e->noteOn(43,.8f,1);e->noteOn(64,.5f,6);}
   if(block==20) e->setPitchBend(.7f,1);
   if(block==60) e->noteOn(43,.8f,1);
   if(block==110)e->noteOn(45,.7f,1);
   if(block==170)e->noteOff(45,1);
   if(block==200)e->setVibrato(.5f);
   if(block==240)e->setPalmMutePressure(.3f);
   e->process(l.data(),r.data(),64);
   for(int i=0;i<64;++i)for(float x:{l[i],r[i]}) {std::uint32_t b;std::memcpy(&b,&x,4);hash^=b;hash*=1099511628211ULL;}
  }
  std::cout<<rate<<" "<<pick<<" "<<hash<<"\n";
 }
}
