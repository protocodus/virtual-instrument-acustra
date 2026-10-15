#include "DSP/AcustraEngine.h"
#include "DSP/AcustraPerformer.h"
#include "DSP/MicrophoneBalanceData.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
namespace acustra {
struct AcustraEngineTestAccess {
static std::array<double,5> peak(double f,double db,double q) {
 constexpr double pi=3.141592653589793; const double A=std::pow(10.,db/40.), w=2*pi*f/48000., c=std::cos(w),a=std::sin(w)/(2*q),d=1+a/A;
 return {(1+a*A)/d,-2*c/d,(1-a*A)/d,-2*c/d,(1-a/A)/d};
}
static void configure(AcustraEngine& e,int v) {
 auto& b=e.bodyBank_; auto& c=b.captureFilter.coefficients;
 if(v==1) { c[4]=peak(1000,1.78,1.2);c[5]=peak(1400,4.0,1.2); }
 if(v==5) { c[4]=peak(1000,1.78,1.2);c[5]=peak(1800,6.0,1.2); }
 if(v==6) { c[4]=peak(1000,1.78,1.2);c[5]=peak(1800,4.0,1.2); }
 if(v==2) { const double w=2*3.141592653589793*35/48000.,co=std::cos(w),a=std::sin(w)/std::sqrt(2.),d=1+a; c[1]={(1+co)/(2*d),-(1+co)/d,(1+co)/(2*d),-2*co/d,(1-a)/d}; }
 if(v==3) { const double A=std::pow(10.,-1.5/40.), w=2*3.141592653589793*4000/48000.,co=std::cos(w),a=std::sin(w)/(2*.7071067811865476),s=2*std::sqrt(A)*a,sum=A+1,diff=A-1,d=sum-diff*co+s;
 c[1]={A*(sum+diff*co+s)/d,-2*A*(diff+sum*co)/d,A*(sum+diff*co-s)/d,2*(diff-sum*co)/d,(sum-diff*co-s)/d}; }
 if(v==4) for(int i=0;i<b.count;i++) { if(b.poleImaginary[i]==0) continue;float f=std::atan2(b.poleImaginary[i],b.poleReal[i])*48000.f/(2*3.141592653589793f), g=detail::microphoneTiltGain(f);
 for(float* x:{&b.leftReal[i],&b.leftImaginary[i],&b.rightReal[i],&b.rightImaginary[i],&b.leftMomentReal[i],&b.leftMomentImaginary[i],&b.rightMomentReal[i],&b.rightMomentImaginary[i]}) *x/=g; }
}
}; }
int main(int argc,char**argv) {
 const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
 constexpr int rate=48000,total=201600,block=127;
 for(int note:{40,46,52,58,64,71,77,83})for(int v:{0,1,5,6}){
 auto engine=std::make_unique<acustra::AcustraEngine>();acustra::EngineParameters p;engine->setParameters(p);engine->setPhysicalCalibration(acustra::fittedPhysicalCalibration);engine->prepare(rate,block);acustra::AcustraEngineTestAccess::configure(*engine,v);engine->noteOn(note,91.f/127.f);
 std::vector<float> output(2*total);std::array<float,block>left{},right{};
 for(int at=0;at<total;at+=block){int n=std::min(block,total-at);engine->process(left.data(),right.data(),n);for(int i=0;i<n;i++){output[2*(at+i)]=left[i];output[2*(at+i)+1]=right[i];}}
 std::ofstream f(out/("m"+std::to_string(note)+"-v"+std::to_string(v)+".f32"),std::ios::binary);f.write(reinterpret_cast<const char*>(output.data()),output.size()*4);
 }
}
