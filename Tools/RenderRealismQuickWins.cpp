// Research-backed quick-win trials, 2026-10-16. The same renderer builds
// both frozen revisions. Only public Performer events and engine setup APIs.
#include "DSP/AcustraPerformer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
constexpr int blockSize=64;
struct Event { int frame, kind, channel; float a; int b; };
template<class Settings> void select(Settings& s, const std::string& mode) {
    if constexpr (requires(Settings x) { x.hammerWork; x.pullOffPluck; x.fingerPairs;
            x.woundRidges; x.longitudinalSqueak; x.directionalRoom; x.roomRateConversion; }) {
        s.hammerWork = mode=="all" || mode=="hammer";
        s.pullOffPluck = mode=="all" || mode=="pull";
        s.fingerPairs = mode=="all" || mode=="pairs";
        s.woundRidges = mode=="all" || mode=="ridges";
        s.longitudinalSqueak = mode=="axial";
        s.directionalRoom = mode=="all" || mode=="room";
        s.roomRateConversion = mode=="all" || mode=="rate";
    }
    if constexpr (requires(Settings x) { x.heldShapeMute; })
        s.heldShapeMute = mode=="all" || mode=="held-mute";
    // Baseline has none of these flags; its production behavior is retained.
}
acustra::EngineParameters controls() {
    acustra::EngineParameters p;
    p.outputGain=1; p.piezoMix=.2f; p.releaseNoise=.1f; p.room=.1f;
    return p;
}
struct Score {
    std::string name,title,description,mode="all";
    int rate=48000;
    bool strings=false;
    acustra::EngineParameters parameters=controls();
    struct Spotlight {double start=0,end=0,gainDb=0;} spotlight;
    std::vector<Event> events;
    void event(double t,int kind,int channel,float a,int b) {
        events.push_back({static_cast<int>(std::lround(t*rate)),kind,channel,a,b});
    }
    void note(double t,double duration,int pitch,int velocity,int channel=1) {
        event(t,0x90,channel,static_cast<float>(pitch),velocity);
        event(t+duration,0x80,channel,static_cast<float>(pitch),64);
    }
    void cc(double t,int channel,int number,int value) {
        event(t,0xb0,channel,static_cast<float>(number),value);
    }
    void connect(double t,int from,int to,int velocity,int channel=3) {
        cc(t,channel,84,from); event(t,0x90,channel,static_cast<float>(to),velocity);
        event(t+.01,0x80,channel,static_cast<float>(from),64);
    }
    void slide(double t,double duration,float from,float to,int channel=2) {
        int count=static_cast<int>(std::lround(duration/.002));
        for (int i=0;i<=count;++i) {
            double u=double(i)/count;
            event(t+duration*u,0xe0,channel,from+(to-from)*static_cast<float>(u*u*(3-2*u)),0);
        }
    }
};
std::vector<Score> scores() {
    std::vector<Score> a(16);
    auto& s=a[0]; s.name="01-finger-phrase"; s.title="Complete candidate: fingerstyle phrase";
    s.description="48 kHz; seven production changes enabled, experimental axial path off. Alternating bass, repeated two-finger pinches and a final four-note chord. Room 10%, Release Noise 10%, Piezo Mix 20%, output 0 dB.";
    for(int n=0;n<12;++n) {
        double t=.2+.55*n;
        s.note(t,.45,n%4<2?40:45,83);
        s.note(t+.20,.38,n%4<2?59:60,70);
        s.note(t+.20,.38,64,75);
    }
    for(int p:{40,55,59,64}) s.note(7.2,.9,p,91);
    auto& l=a[1]; l.name="02-connected-phrase"; l.title="Complete candidate: connected fretting hand";
    l.description="48 kHz; all candidates. One initial D-string pluck, alternating hammer-ons and pull-offs, then an open-string descent. Explicit CC65/84 gestures; no repeated picking attack.";
    l.strings=true; l.cc(0,3,65,127); l.note(.2,7.8,55,100,3);
    int previous=55;
    for(int n=0;n<18;++n) {int next=n%2?55:57; l.connect(.8+.32*n,previous,next,70+(n%3)*15);previous=next;}
    l.connect(6.8,55,50,90); l.event(8,0x80,3,50,64);
    auto& h=a[2]; h.name="03-hammer-work"; h.title="Hammer-on after the string decays"; h.mode="hammer"; h.strings=true;
    h.description="Only independent hammer work changes. Hear the initial D-string note decay, then hammer 55 to 57 at 6 seconds. A fresh short-hold comparison follows at 7.5 seconds. Finger velocity is unchanged.";
    h.cc(0,3,65,127); h.note(.2,6.4,55,102,3); h.connect(6,55,57,102); h.event(6.9,0x80,3,57,64);
    h.note(7.5,.8,55,102,3);h.connect(7.9,55,57,102);h.event(8.5,0x80,3,57,64);
    auto& p=a[3]; p.name="04-pull-off";p.title="Pull-off to an open string";p.mode="pull";p.strings=true;
    p.description="Only pull-off work, shape, location and polarization change. A held G3 descends to open D3 at 6 seconds, then repeats after a short hold. Explicit CC65/84, fixed finger velocity.";
    p.cc(0,3,65,127);p.note(.2,6.4,55,102,3);p.connect(6,55,50,102);p.event(6.9,0x80,3,50,64);
    p.note(7.5,.8,55,102,3);p.connect(7.9,55,50,102);p.event(8.5,0x80,3,50,64);
    auto& f=a[4];f.name="05-finger-pairs";f.title="Two-finger pinch timing";f.mode="pairs";
    f.description="Only pair timing changes. Simultaneous MIDI pairs gain a bounded separation. The distribution is informed by recordings, but those recordings cannot prove simultaneous player intent.";
    for(int n=0;n<14;++n) {double t=.2+.5*n;f.note(t,.35,n%2?60:59,80);f.note(t,.35,64,80);}
    const auto fillSlides=[](Score& x) {
        x.strings=true;
        for(int n=0;n<3;++n) {
            double t=.2+2.7*n;x.event(t-.01,0xe0,2,0,0);x.note(t,2.35,52,96,2);
            x.slide(t+.45,.5,0,4);x.slide(t+1.35,.65,4,-3);
        }
    };
    auto& r=a[5];r.name="06-winding-ridges";r.title="Wound-string ridge texture";r.mode="ridges";
    r.description="Only periodic winding crossings change. Three A-string slides at Release Noise 10%. Listen for texture attached to movement; source level and score are unchanged.";fillSlides(r);
    auto& x=a[6];x=r;x.name="07-axial-color";x.title="Wound-string longitudinal color";x.mode="axial";
    x.description="Experimental, disabled by default: only the axial-force observer changes, with winding pulses disabled. Reduced coupling with authored damping at Release Noise 10%; no proven audible benefit or complete axial bridge port.";
    const auto fillRoom=[](Score& x) {
        x.parameters.picking=acustra::PickingTechnique::Pick;
        for(int n=0;n<6;++n) for(int pitch:{40,47,55,59,64}) x.note(.2+n*1.2,.55,pitch,85+(n%2)*20);
    };
    auto& room=a[7];room.name="08-room-default";room.title="Early reflections at Room 10%";room.mode="room";
    room.description="Only receiver-specific early-reflection timing and directivity change. 48 kHz, Room 10%. The late reverberation network and dry microphones retain their design.";fillRoom(room);
    a[8]=room;a[8].name="09-room-diagnostic";a[8].title="Early reflections at Room 50%";a[8].parameters.room=.5f;
    a[8].description="The same isolated reflection change and score with Room 50%. This stronger room setting demonstrates the mechanism; it does not establish audibility at Room 10%.";
    auto& rate=a[9];rate.name="10-rate-default";rate.title="Room conversion at 96 kHz, Room 10%";rate.mode="rate";rate.rate=96000;
    rate.description="Only high-rate room conversion changes. Native 96 kHz renders are identically converted to 48 kHz for playback. Room 10%; this fixes numerical coloration rather than changing the intended room.";fillRoom(rate);
    a[10]=rate;a[10].name="11-rate-diagnostic";a[10].title="Room conversion at 96 kHz, Room 50%";a[10].parameters.room=.5f;
    a[10].description="The same isolated 96 kHz conversion correction with Room 50%. Native float files retain the original rate; both audition sides use the same offline downsampling filter.";
    auto& combined=a[11];combined.name="12-complete-slides-96k";combined.title="Complete candidate: slides at 96 kHz";combined.rate=96000;
    combined.description="Seven production changes at native 96 kHz; experimental axial observer off. Wound A-string slides with Room 10% and Release Noise 10%. Includes winding texture and both room changes.";fillSlides(combined);
    auto& ridgeTail=a[12];ridgeTail.name="13-ridges-quiet-tail";ridgeTail.title="Winding texture: amplified quiet-tail diagnostic";ridgeTail.mode="ridges";
    ridgeTail.description="Only winding crossings change. Diagnostic Release Noise 50%; a held A-string note decays for 5.8 seconds before two slides. Full tracks retain nominal gain. The optional spotlight crops the same 5.8–8.2 s window on both sides with one shared boost up to 30 dB; it does not represent stock loudness.";
    ridgeTail.strings=true;ridgeTail.parameters.releaseNoise=.5f;
    ridgeTail.note(.2,8.6,52,96,2);ridgeTail.slide(6,.5,0,4);ridgeTail.slide(7,.65,4,-3);
    ridgeTail.spotlight={5.8,8.2,30};
    a[13]=ridgeTail;a[13].name="14-axial-quiet-tail";a[13].title="Longitudinal color: amplified quiet-tail diagnostic";a[13].mode="axial";
    a[13].description="Experimental, disabled by default: only the axial-force observer changes; winding pulses are disabled. Diagnostic Release Noise 50%; the same held note and late slides. The 5.8–8.2 s spotlight uses identical cropping and a shared boost up to 30 dB. No difference-only signal or per-side normalization; this is not stock loudness.";
    auto& mute=a[14];mute.name="15-held-shape-mute";mute.title="Keep skipped strings muted under a held chord";mute.mode="held-mute";
    mute.parameters.picking=acustra::PickingTechnique::Pick;
    mute.description="Only sustained fretting-hand muting changes. Hold the outer E strings of a gapped chord while repeating A3. Listen for the unused open A string ringing sympathetically after the old mute timer expires.";
    for(int pitch:{40,57,64}) mute.note(.05,pitch==57?.75:3.0,pitch,100);
    for(double t:{.85,1.35,1.85}) mute.note(t,.45,57,100);
    for(int pitch:{40,57,64}) mute.note(4.05,pitch==57?.75:3.0,pitch,100);
    for(double t:{4.85,5.35,5.85}) mute.note(t,.45,57,100);
    a[15]=mute;a[15].name="16-complete-held-chords";a[15].title="Complete candidate: held chords and repeated inner notes";a[15].mode="all";
    a[15].description="The same gapped chords with all seven production changes enabled. Default Room 10%, Release Noise 10% and Piezo Mix 20%; experimental axial observer remains off.";
    return a;
}
void writeWave(const std::filesystem::path& path, const std::vector<float>& audio, int rate)
{
    std::ofstream stream(path, std::ios::binary);
    const auto u16 = [&] (std::uint16_t x)
    { for (int n = 0; n < 2; ++n) stream.put(static_cast<char>(x >> (8 * n))); };
    const auto u32 = [&] (std::uint32_t x)
    { for (int n = 0; n < 4; ++n) stream.put(static_cast<char>(x >> (8 * n))); };
    const auto bytes = static_cast<std::uint32_t>(audio.size() * sizeof(float));
    stream.write("RIFF", 4); u32(36 + bytes); stream.write("WAVEfmt ", 8);
    u32(16); u16(3); u16(2); u32(rate); u32(rate * 8); u16(8); u16(32);
    stream.write("data", 4); u32(bytes);
    for (const float value : audio)
    {
        static_assert(sizeof(float) == 4);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }
    if (!stream) throw std::runtime_error("cannot write WAV: " + path.string());
}


// Public ownership observation complements the internal contact-state tests.
// Alone it cannot distinguish a connected gesture from every possible
// replacement pluck on the same physical string.
struct RenderAudit { int connectedOwnershipChecks=0; };
RenderAudit render(Score& score,const std::filesystem::path& directory,const std::string& selection) {
    std::stable_sort(score.events.begin(),score.events.end(),[](const Event& a,const Event& b){return a.frame<b.frame;});
    auto player=std::make_unique<acustra::Performer>();
    auto switches=player->engine().performanceRealism();select(switches,selection=="off"?"off":score.mode);
    player->engine().setPerformanceRealism(switches);
    player->setParameters(score.parameters);player->setTempoBpm(120);player->setGatherChords(false);player->setSustainHold(false);
    player->prepare(score.rate,blockSize);player->engine().setStringPerChannelMode(score.strings);
    const int frames=10*score.rate;
    if(score.events.empty()||score.events.front().frame<0||score.events.back().frame>=frames)
        throw std::runtime_error("event outside score window: "+score.name);
    std::vector<float> audio(static_cast<std::size_t>(2*frames));std::array<float,blockSize> left{},right{};
    std::array<int,17> connectedSources{};connectedSources.fill(-1);
    std::array<bool,17> connectedEnabled{};
    struct Connection {int source,target,channel,string;};
    std::vector<Connection> connections;
    RenderAudit audit;
    std::size_t next=0;
    for(int frame=0;frame<frames;frame+=blockSize) {
        connections.clear();
        const int count=std::min(blockSize,frames-frame);player->beginBlock(left.data(),right.data(),count);
        while(next<score.events.size()&&score.events[next].frame<frame+count) {
            const auto& e=score.events[next++];const int offset=e.frame-frame;
            if(e.kind==0x90){
                const int source=connectedSources[static_cast<std::size_t>(e.channel)];
                connectedSources[static_cast<std::size_t>(e.channel)]=-1;
                if(connectedEnabled[static_cast<std::size_t>(e.channel)]&&source>=0){
                    const int string=player->engine().heldString(source,e.channel);
                    if(string<0)throw std::runtime_error("connected scene lost its source: "+score.name);
                    connections.push_back({source,static_cast<int>(e.a),e.channel,string});
                }
                player->noteOn(offset,e.channel,static_cast<int>(e.a),e.b);
            }
            else if(e.kind==0x80)player->noteOff(offset,e.channel,static_cast<int>(e.a),e.b);
            else if(e.kind==0xb0){
                player->controlChange(offset,e.channel,static_cast<int>(e.a),e.b);
                if(e.a==65)connectedEnabled[static_cast<std::size_t>(e.channel)]=e.b>=64;
                if(e.a==84)connectedSources[static_cast<std::size_t>(e.channel)]=e.b;
            }
            else if(e.kind==0xe0){player->setPitchBendRange(offset,e.channel,12);player->pitchWheel(offset,e.channel,e.a/12.f);}
        }
        player->endBlock();
        for(const auto& c:connections){
            if(player->engine().heldString(c.target,c.channel)!=c.string
                ||player->engine().heldString(c.source,c.channel)>=0)
                throw std::runtime_error("connected scene ownership mismatch: "+score.name);
            ++audit.connectedOwnershipChecks;
        }
        for(int n=0;n<count;++n) {
            if(!std::isfinite(left[n])||!std::isfinite(right[n]))throw std::runtime_error("nonfinite "+score.name);
            audio[static_cast<std::size_t>(2*(frame+n))]=left[n];audio[static_cast<std::size_t>(2*(frame+n)+1)]=right[n];
        }
    }
    if(next!=score.events.size())throw std::runtime_error("score extends beyond render");
    if(player->droppedEventCount()!=0)throw std::runtime_error("dropped public events: "+score.name);
    writeWave(directory/(score.name+".wav"),audio,score.rate);
    return audit;
}
void writeScore(std::ostream& stream, const Score& s)
{
    const auto& p = s.parameters;
    stream << "{\"name\":\"" << s.name << "\",\"title\":\"" << s.title
           << "\",\"description\":\"" << s.description << "\",\"sample_rate\":" << s.rate << ",\"string_per_channel\":" << (s.strings ? "true" : "false") << ",\"candidate_mode\":\"" << s.mode << "\",\"parameters\":{"
           << "\"shape\":" << static_cast<int>(p.shape)
           << ",\"bodyMaterial\":" << static_cast<int>(p.bodyMaterial)
           << ",\"capture\":" << static_cast<int>(p.capture)
           << ",\"tuning\":" << static_cast<int>(p.tuning)
           << ",\"picking\":" << static_cast<int>(p.picking)
           << ",\"stringAge\":" << p.stringAge << ",\"pluckPosition\":" << p.pluckPosition
           << ",\"touch\":" << p.touch << ",\"bodyAmount\":" << p.bodyAmount
           << ",\"stereoWidth\":" << p.stereoWidth << ",\"outputGain\":" << p.outputGain
           << ",\"piezoMix\":" << p.piezoMix << ",\"piezoPolarityInverted\":" << (p.piezoPolarityInverted?"true":"false")
           << ",\"releaseNoise\":" << p.releaseNoise << ",\"room\":" << p.room
           << ",\"detune\":" << p.detune << "},\"events\":[";
    bool comma = false;
    for (const auto& e : s.events)
    {
        if (comma) stream << ',';
        comma = true;
        stream << '[' << e.frame << ',' << e.kind << ',' << e.channel << ','
               << e.a << ',' << e.b << ']';
    }
    stream << ']';
    if(s.spotlight.end>s.spotlight.start)
        stream<<",\"spotlight\":{\"start_seconds\":"<<s.spotlight.start
              <<",\"end_seconds\":"<<s.spotlight.end<<",\"requested_common_gain_db\":"<<s.spotlight.gainDb
              <<",\"balance\":\"native\",\"diagnostic\":true}";
    stream << '}';
}
}
int main(int argc,char** argv) {
    try {
        if(argc<2||argc>3)throw std::runtime_error("usage: renderer NEW_DIRECTORY [off]");
        const std::string selection=argc==3?argv[2]:"candidate";
        if(selection!="candidate"&&selection!="off")throw std::runtime_error("unknown selection");
        std::filesystem::path output(argv[1]);if(std::filesystem::exists(output))throw std::runtime_error("output exists");
        std::filesystem::create_directories(output);
        auto scenes=scores();std::ofstream json(output/"scores.json"),audit(output/"render-audit.json");
        json<<std::setprecision(std::numeric_limits<float>::max_digits10);
        audit<<"{\"selection\":\""<<selection<<"\",\"scenes\":[";
        json<<"{\"duration_seconds\":10,\"block_size\":64,\"tempo_bpm\":120,\"gather_chords\":false,\"sustain\":false,\"events_format\":[\"frame\",\"status\",\"channel\",\"note_or_cc_or_bend_semitones\",\"velocity_or_value\"],\"scenes\":[";
        for(std::size_t n=0;n<scenes.size();++n){
            const auto result=render(scenes[n],output,selection);
            if(n){json<<',';audit<<',';}writeScore(json,scenes[n]);
            audit<<"{\"name\":\""<<scenes[n].name<<"\",\"connected_ownership_checks\":"<<result.connectedOwnershipChecks
                 <<",\"dropped_events\":0,\"finite_samples\":"<<20*scenes[n].rate<<'}';
        }
        json<<"]}\n";if(!json)throw std::runtime_error("cannot write scores");
        audit<<"]}\n";if(!audit)throw std::runtime_error("cannot write render audit");
        std::cout<<"Rendered "<<scenes.size()<<" scenes ("<<selection<<")\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
