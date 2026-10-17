#include "DSP/AcustraEngine.h"
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
namespace acustra {struct AcustraEngineTestAccess {
static void snapshot(const AcustraEngine& e){const auto& v=e.voices_[1]; std::cout<<"idle clock="<<e.sampleClock_<<" L="<<v.loops[0].currentDelay<<" target="<<v.loops[0].targetDelay<<" appliedZ="<<v.appliedBendImpedanceScale<<" targetZ="<<v.bendImpedanceScale<<"\n";}
};}
int main(){for(float cents:{0.0f,50.0f}){
auto a=std::make_unique<acustra::AcustraEngine>(),b=std::make_unique<acustra::AcustraEngine>();
a->prepare(48000,64);b->prepare(48000,64);std::array<float,64> l{},r{},bl{},br{};
b->process(bl.data(),br.data(),64);a->setMasterTuneCents(cents);b->setMasterTuneCents(cents);
acustra::AcustraEngineTestAccess::snapshot(*a);acustra::AcustraEngineTestAccess::snapshot(*b);
a->noteOn(40,.7f);b->noteOn(40,.7f);double maxDiff=0,energy=0,diff=0;
for(int block=0;block<100;++block){a->process(l.data(),r.data(),64);b->process(bl.data(),br.data(),64);for(int i=0;i<64;++i){maxDiff=std::max(maxDiff,double(std::abs(l[i]-bl[i])));diff+=std::pow(l[i]-bl[i],2);energy+=l[i]*l[i];}}
std::cout<<"tune="<<cents<<" maxDiff="<<maxDiff<<" relativeRms="<<std::sqrt(diff/energy)<<"\n";
}}
