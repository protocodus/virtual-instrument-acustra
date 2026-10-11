#define main probe_early_main
#include "early_tests.cpp"
#undef main
#include <iomanip>
int main(){
 std::cout<<std::setprecision(15);
 for(double rate:{44100.,48000.,96000.}) for(double highDb:{55.,80.}) for(double highAmp:{.003,.0001}) for(double glide:{0.,-10.}){
  int n=int(1.6*rate);Audio a{std::vector<float>(n),std::vector<float>(n)};
  for(int i=0;i<n;++i){double t=i/rate;
   float v=float(.1*std::pow(10.,-10.*t/20.)*std::cos(2.*std::numbers::pi*1046.5*t)
     +highAmp*std::pow(10.,-highDb*t/20.)*std::cos(2.*std::numbers::pi*(8430.*t+.5*glide*t*t)));
   a.left[i]=a.right[i]=v;}
  double early=spectralDecayRate(a,8430.,60.,rate);
  double f1=spectralPeakFrequency(a,8430.,60.,.2,.6,rate),f2=spectralPeakFrequency(a,8430.,60.,1.,1.4,rate);
  double late=20*std::log10(spectralAmplitudeAt(a,f1,.2,.6,rate)/spectralAmplitudeAt(a,f2,1.,1.4,rate))/.8;
  std::cout<<"{\"rate\":"<<rate<<",\"high_decay_db_per_second\":"<<highDb<<",\"high_amp\":"<<highAmp<<",\"glide\":"<<glide<<",\"early_measured\":"<<early<<",\"original_measured\":"<<late<<"}\n";
 }
}
