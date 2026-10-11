#include "DSP/AcustraEngine.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
namespace acustra {
struct AcustraEngineTestAccess {
 static void run(double rate, int fret, int interval) {
  using Loop = AcustraEngine::StringLoop;
  const double fundamental=440*std::exp2((40.0+fret-69)/12);
  const double totalPeriod=rate/fundamental, oldRead=totalPeriod-2;
  auto nominal=std::make_unique<Loop>();
  nominal->reset(); nominal->currentDelay=nominal->targetDelay=oldRead;
  nominal->loopGain=nominal->targetLoopGain=1;
  nominal->broadLossMix=nominal->highLossMix=0;
  nominal->bendingLossActive=nominal->constantLossActive=false;
  const double omega=2*std::acos(-1.0)/totalPeriod;
  for(int n=0;n<6000;++n) {
   nominal->advance(0,1);
   nominal->write(static_cast<float>(std::sin(omega*n+.31)));
  }
  auto moved=std::make_unique<Loop>(*nominal);
  moved->targetDelay=static_cast<float>(totalPeriod/std::exp2(interval/12.0)-2);
  const float alpha=1-std::exp(-1.f/(.006f*static_cast<float>(rate)));
  int first=-1; double err2=0, ref2=0, maxdiff=0;
  for(int n=0;n<static_cast<int>(totalPeriod/2);++n) {
   const float a=nominal->advance(alpha,1), b=moved->advance(alpha,1);
   if(a!=b && first<0) first=n;
   const double d=b-a; err2+=d*d;ref2+=a*a;maxdiff=std::max(maxdiff,std::abs(d));
   // Identical outgoing bridge wave: isolate only the incoming read path.
   const float source=static_cast<float>(std::sin(omega*(6000+n)+.31));
   nominal->write(source); moved->write(source);
  }
  const double initialSpeedOverC=(std::exp2(-interval/12.0)-1)/(2*fundamental*.006);
  std::printf("{\"sample_rate\":%.0f,\"fret\":%d,\"slide_semitones\":%d,\"round_trip_samples\":%.12g,\"first_changed_sample\":%d,\"earliest_physical_return_samples\":%.12g,\"pre_return_relative_rms\":%.12g,\"pre_return_max_difference\":%.12g,\"initial_stop_speed_over_c\":%.12g}\n",rate,fret,interval,totalPeriod,first,totalPeriod/2,std::sqrt(err2/ref2),maxdiff,initialSpeedOverC);
 }
}; }
int main(){for(double rate:{44100.,48000.,96000.}) for(auto fixture: {std::array<int,2>{2,-2},std::array<int,2>{2,2},std::array<int,2>{5,12},std::array<int,2>{17,-12}}) acustra::AcustraEngineTestAccess::run(rate,fixture[0],fixture[1]);}
