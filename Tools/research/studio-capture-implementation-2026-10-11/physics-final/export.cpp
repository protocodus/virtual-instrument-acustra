#include "DSP/AcustraEngine.h"
#include <iomanip>
#include <iostream>
#include <memory>
namespace acustra {
struct AcustraEngineTestAccess {
 static void write(const AcustraEngine& e, int shape, int wood) {
  const auto& b=e.bodyBank_;
  std::cout << "G," << shape << ',' << wood << ',' << b.captureFilter.gain << '\n';
  for (int k=0;k<b.captureFilter.sections;++k) {
   std::cout<<"F,"<<shape<<','<<wood<<','<<k;
   for(auto x:b.captureFilter.coefficients[k])std::cout<<','<<x;
   std::cout<<'\n';
  }
  for(int k=0;k<b.count;++k) {
   std::cout<<"M,"<<shape<<','<<wood<<','<<k;
   for(auto x:{b.poleReal[k],b.poleImaginary[k],b.leftReal[k],b.leftImaginary[k],b.rightReal[k],b.rightImaginary[k],b.leftMomentReal[k],b.leftMomentImaginary[k],b.rightMomentReal[k],b.rightMomentImaginary[k]})std::cout<<','<<x;
   std::cout<<'\n';
  }
 }
}; }
int main(){
 std::cout << std::setprecision(17);
 for(int shape=0;shape<4;++shape)for(int wood=0;wood<3;++wood){
  acustra::EngineParameters p;p.shape=static_cast<acustra::BodyShape>(shape);p.bodyMaterial=static_cast<acustra::BodyMaterial>(wood);p.room=0;
  auto e=std::make_unique<acustra::AcustraEngine>();e->setParameters(p);e->prepare(48000,64);
  acustra::AcustraEngineTestAccess::write(*e,shape,wood);
 }
}
