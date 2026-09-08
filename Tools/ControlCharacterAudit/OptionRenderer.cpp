#include "AcustraEngine.h"
#include "FittedPhysicalData.h"
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
int main(int argc,char**argv){
 if(argc!=10){std::cerr<<"OUTPUT material shape style note velocity timbre capture touch\n";return 2;}
 acustra::EngineParameters p;
 p.stringMaterial=std::string(argv[2])=="steel"?acustra::StringMaterial::Steel:acustra::StringMaterial::Nylon;
 const std::array<std::string,4> shapes{"parlor","auditorium","dreadnought","jumbo"};
 const std::array<std::string,3> styles{"finger","pick","thumb"};
 for(int i=0;i<4;++i)if(shapes[i]==argv[3])p.shape=static_cast<acustra::BodyShape>(i);
 for(int i=0;i<3;++i)if(styles[i]==argv[4])p.picking=static_cast<acustra::PickingTechnique>(i);
 const int note=std::stoi(argv[5]),velocity=std::stoi(argv[6]);const float timbre=std::stof(argv[7]);
 p.capture=std::string(argv[8])=="piezo"?acustra::CaptureType::Piezo:acustra::CaptureType::StereoMic;
 p.touch=std::stof(argv[9]);
 acustra::AcustraEngine engine;engine.setPhysicalCalibration(acustra::fittedPhysicalCalibration);engine.setParameters(p);engine.prepare(48000,127);
 const std::array<int,6> opens{40,45,50,55,59,64};int string=0;for(int i=0;i<6;++i)if(note>=opens[i])string=i;
 int channel=string+1;
 if(timbre<0){engine.setStringPerChannelMode(true);}else{channel=2;engine.setLowerZoneMemberCount(2);engine.setMpeTimbre(timbre,channel);}
 engine.setPitchBend(0,channel);engine.noteOn(note,velocity/127.0f,channel);
 std::ofstream out(argv[1],std::ios::binary);if(!out)return 2;
 std::array<float,127>l{},r{};std::vector<float> interleaved;interleaved.reserve(192000);
 for(int n=0;n<96000;){int count=std::min(127,96000-n);engine.process(l.data(),r.data(),count);for(int i=0;i<count;++i){if(!std::isfinite(l[i])||!std::isfinite(r[i]))return 1;interleaved.push_back(l[i]);interleaved.push_back(r[i]);}n+=count;}
 out.write(reinterpret_cast<const char*>(interleaved.data()),interleaved.size()*sizeof(float));return out?0:2;
}
