#include "DSP/AcustraEngine.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
namespace acustra { struct AcustraEngineTestAccess {
static void describe(const AcustraEngine& e, int i) {
const double r=e.bodyBank_.poleReal[std::size_t(i)], im=e.bodyBank_.poleImaginary[std::size_t(i)];
const double decay=-e.sampleRate_*std::log(std::hypot(r,im));
const double f=std::atan2(im,r)*e.sampleRate_/(2*3.14159265358979323846);
std::cout << "pole "<<i<<" f="<<f<<" Q="<<3.14159265358979323846*f/decay<<" t60_ms="<<1000*std::log(1000.0)/decay<<'\n';
}
}; }
int main(){
std::cout<<std::setprecision(10);
for(int shape=0;shape<4;++shape){
acustra::EngineParameters p; p.shape=static_cast<acustra::BodyShape>(shape);p.outputGain=.2f;
auto e=std::make_unique<acustra::AcustraEngine>(); e->setParameters(p); e->prepare(48000,64);
if(shape==2){acustra::AcustraEngineTestAccess::describe(*e,4);acustra::AcustraEngineTestAccess::describe(*e,137);}
for(int note:{40,47,52,55,59,64})e->noteOn(note,.8f);
double sum=0;float peak=0;
for(int i=0;i<48000;++i){float l,r;e->process(&l,&r,1);sum+=double(l)*l+double(r)*r;peak=std::max(peak,std::max(std::abs(l),std::abs(r)));}
std::cout<<"shape "<<shape<<" rms="<<std::sqrt(sum/96000)<<" peak="<<peak<<'\n';
}
}
