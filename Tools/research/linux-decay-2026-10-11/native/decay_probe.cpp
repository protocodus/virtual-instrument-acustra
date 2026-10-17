#define main acustra_original_test_main
#include "Tests/AcustraEngineTests.cpp"
#undef main
#include <iomanip>
#include <fstream>
int main() {
  std::cout << std::setprecision(15);
  const int midi=84;
  for(double rate:{44100.,48000.,96000.}) {
    auto engine=std::make_unique<acustra::AcustraEngine>();
    acustra::EngineParameters p; p.capture=acustra::CaptureType::SaddlePiezo;
    engine->setParameters(p); engine->prepare(rate,blockSize);
    engine->setBridgeCouplingEnabled(false); engine->setSympatheticStringsEnabled(false);
    engine->noteOn(midi,.8f);
    const int n=int(1.6*rate);
    Audio a{std::vector<float>(n),std::vector<float>(n)},raw=a;
    std::ofstream file("build-audit/linux-decay-ci-20261011/native/midi84-"+std::to_string(int(rate))+".f32",std::ios::binary);
    for(int i=0;i<n;++i) {
      engine->process(&a.left[i],&a.right[i],1);
      raw.left[i]=raw.right[i]=engine->getLastBridgeReactionForce();
      const float row[]={a.left[i],a.right[i],raw.left[i]};
      file.write(reinterpret_cast<const char*>(row),sizeof(row));
    }
    for(auto [kind,audio]: {std::pair{"piezo",&a},std::pair{"force",&raw}}) {
      double firstA=0;
      for(auto [b,e]:{std::pair{.05,.15},std::pair{.20,.30},std::pair{.35,.45},std::pair{.5,.6},std::pair{.65,.75},std::pair{.8,.9},std::pair{1.,1.1},std::pair{1.2,1.3},std::pair{.2,.6},std::pair{1.,1.4}}) {
        const double f=spectralPeakFrequency(*audio,8.*440.*std::exp2((midi-69.)/12.),60.,b,e,rate);
        const double amp=spectralAmplitudeAt(*audio,f,b,e,rate)*4./((e-b)*rate);
        if(!firstA)firstA=amp;
        std::cout << "{\"rate\":"<<rate<<",\"kind\":\""<<kind<<"\",\"begin\":"<<b<<",\"end\":"<<e<<",\"frequency\":"<<f<<",\"amplitude\":"<<amp<<",\"relative_db\":"<<20.*std::log10(amp/firstA)<<"}\n";
      }
    }
  }
}
