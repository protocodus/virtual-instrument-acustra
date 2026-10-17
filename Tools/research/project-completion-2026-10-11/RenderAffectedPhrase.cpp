#include "DSP/AcustraPerformer.h"
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
struct Event {int sample,type,note,velocity;};
void u16(std::ostream&out,std::uint16_t value){for(int i=0;i<2;++i)out.put(static_cast<char>(value>>(8*i)));}
void u32(std::ostream&out,std::uint32_t value){for(int i=0;i<4;++i)out.put(static_cast<char>(value>>(8*i)));}
int main(int argc,char**argv){try{
    if(argc!=4)throw std::runtime_error("usage: renderer piezo|room events.tsv OUTPUT.wav");
    const bool piezo=std::string(argv[1])=="piezo";
    if(!piezo&&std::string(argv[1])!="room")throw std::runtime_error("unknown scenario");
    const int rate=piezo?48000:44100,total=6*rate;
    if(std::filesystem::exists(argv[3]))throw std::runtime_error("output exists");
    std::ifstream score(argv[2]);if(!score)throw std::runtime_error("cannot read score");
    std::vector<Event> events;int time,type,note,velocity;
    while(score>>time>>type>>note>>velocity)events.push_back({time*rate/100,type,note,velocity});
    if(!score.eof())throw std::runtime_error("invalid score");
    auto player=std::make_unique<acustra::Performer>();
    acustra::EngineParameters p;
    p.shape=acustra::BodyShape::Dreadnought;p.bodyMaterial=acustra::BodyMaterial::Spruce;
    p.capture=piezo?acustra::CaptureType::Piezo:acustra::CaptureType::StereoMic;
    p.tuning=acustra::Tuning::Standard;p.picking=acustra::PickingTechnique::Finger;
    p.stringAge=.15f;p.pluckPosition=.28f;p.touch=.58f;p.bodyAmount=.82f;
    p.stereoWidth=.62f;p.outputGain=.42f;p.piezoMix=0.0f;p.releaseNoise=0.0f;p.room=piezo?0.0f:1.0f;
    player->setParameters(p);player->setTempoBpm(120.0);player->setGatherChords(false);player->prepare(rate,64);
    std::vector<float> audio(static_cast<std::size_t>(total)*2);
    std::array<float,64> left{},right{};std::size_t next=0;
    for(int frame=0;frame<total;frame+=64){
        const int count=std::min(64,total-frame);player->beginBlock(left.data(),right.data(),count);
        while(next<events.size()&&events[next].sample<frame+count){
            const auto&e=events[next++];if(e.sample<frame)throw std::runtime_error("unsorted score");
            if(e.type)player->noteOn(e.sample-frame,1,e.note,e.velocity);
            else player->noteOff(e.sample-frame,1,e.note,e.velocity);
        }
        player->endBlock();
        for(int i=0;i<count;++i){audio[2*(frame+i)]=left[i];audio[2*(frame+i)+1]=right[i];}
    }
    if(next!=events.size()||player->droppedEventCount()!=0)throw std::runtime_error("score lost an event");
    std::ofstream out(argv[3],std::ios::binary);if(!out)throw std::runtime_error("cannot write output");
    static_assert(std::endian::native==std::endian::little);
    const std::uint32_t bytes=static_cast<std::uint32_t>(audio.size()*sizeof(float));
    out.write("RIFF",4);u32(out,48+bytes);out.write("WAVEfmt ",8);u32(out,16);u16(out,3);u16(out,2);
    u32(out,rate);u32(out,rate*8);u16(out,8);u16(out,32);out.write("fact",4);u32(out,4);u32(out,total);
    out.write("data",4);u32(out,bytes);out.write(reinterpret_cast<const char*>(audio.data()),bytes);
    if(!out)throw std::runtime_error("wave write failed");
    std::cout<<argv[3]<<" rate="<<rate<<" frames="<<total<<" events="<<events.size()<<" latency="<<player->latencySamples()<<'\n';
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
