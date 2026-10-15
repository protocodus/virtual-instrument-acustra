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
struct Event { int frame,note,velocity; };
int main(int argc,char**argv) {
 const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
 constexpr int rate=48000,total=rate*12,block=128;
 for(int shape=0;shape<4;shape++) for(int tech=0;tech<3;tech++) for(int v=argc>2?std::atoi(argv[2]):0;v<(argc>3?std::atoi(argv[3]):5);v++) {
 auto p=std::make_unique<acustra::Performer>(); acustra::EngineParameters controls; controls.shape=static_cast<acustra::BodyShape>(shape); controls.picking=static_cast<acustra::PickingTechnique>(tech);controls.room=.5f; controls.piezoMix=.2f;controls.releaseNoise=.1f;
 p->setParameters(controls);p->prepare(rate,block);acustra::AcustraEngineTestAccess::configure(p->engine(),v);
 std::vector<Event> events; auto note=[&](double at,int n,int vel,double length){events.push_back({int(at*rate),n,vel});events.push_back({int((at+length)*rate),n,0});};
 const int line[]{40,45,50,55,59,64,68,74,79,84}; for(int i=0;i<10;i++) note(.05+.29*i,line[i],i%3==0?112:82,.24);
 const int chords[4][6]{{40,47,52,55,59,64},{43,47,50,55,59,67},{45,48,52,57,60,64},{41,48,53,57,60,65}};
 for(int c=0;c<4;c++) for(int k=0;k<6;k++)note(3.4+c*.48,chords[c][k],96,.39);
 for(int i=0;i<12;i++)note(5.8+i*.19,chords[i/3][i%6],i%3==0?105:76,.47);
 note(8.7,40,110,1.5);note(8.7,52,110,1.5);note(8.7,64,110,1.5);
 std::stable_sort(events.begin(),events.end(),[](const auto&a,const auto&b){return a.frame<b.frame || (a.frame==b.frame && a.velocity<b.velocity);});
 std::vector<float> output(static_cast<size_t>(total)*2);std::array<float,block>left{},right{};size_t next=0;
 for(int at=0;at<total;at+=block){int n=std::min(block,total-at);p->beginBlock(left.data(),right.data(),n);while(next<events.size()&&events[next].frame<at+n){auto e=events[next++];uint8_t midi[]{static_cast<uint8_t>(e.velocity?0x90:0x80),static_cast<uint8_t>(e.note),static_cast<uint8_t>(e.velocity)};p->handleMidi(e.frame-at,midi,3);}p->endBlock();for(int i=0;i<n;i++){output[2*(at+i)]=left[i];output[2*(at+i)+1]=right[i];}}
 auto path=out/("shape"+std::to_string(shape)+"-tech"+std::to_string(tech)+"-v"+std::to_string(v)+".f32");std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(output.data()),output.size()*4);std::cout<<path.filename()<<'\n';
 }
}
