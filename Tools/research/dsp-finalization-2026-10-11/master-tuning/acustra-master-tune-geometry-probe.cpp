#include "DSP/AcustraPerformer.h"
#include <array>
#include <iostream>
#include <memory>
namespace acustra {
struct AcustraEngineTestAccess {
 static void report(AcustraEngine& e, const char* stage) {
  const auto& v=e.voices_[0];
  std::cout<<stage<<" length="<<v.speakingLengthMetres<<" tension="<<v.tensionNewtons
   <<" fret="<<v.speakingFret<<" vibrato="<<e.vibratoSemitones(v,v.speakingLengthMetres)
   <<" slideRng="<<v.fretMotionNoiseState<<"\n";
 }
}; }
int main() {
 auto p=std::make_unique<acustra::Performer>();
 acustra::EngineParameters params;
 params.releaseNoise=0.5f;
 p->setParameters(params);
 p->prepare(48000,64);
 std::array<float,64> l{},r{};
 p->beginBlock(l.data(),r.data(),64);
 p->noteOn(0,1,40,100);
 p->endBlock();
 for(int n=0;n<500;++n) p->process(l.data(),r.data(),64);
 acustra::AcustraEngineTestAccess::report(p->engine(),"before");
 p->setMasterTuneCents(50);
 for(int n=0;n<100;++n) p->process(l.data(),r.data(),64);
 acustra::AcustraEngineTestAccess::report(p->engine(),"tune+50");
 p->engine().setVibrato(1.0f);
 for(int n=0;n<500;++n) p->process(l.data(),r.data(),64);
 acustra::AcustraEngineTestAccess::report(p->engine(),"tune+50+wheel");
}
