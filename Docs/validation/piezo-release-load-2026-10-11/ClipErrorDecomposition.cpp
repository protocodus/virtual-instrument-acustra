#define main acceptanceMain
#include "PiezoReleaseAcceptance.cpp"
#undef main
#include <fstream>
int main()
{
    constexpr int rate = 44100;
    constexpr float touch = .58f;
    const auto linear = hardStrum(rate, 1.0f, touch, acustra::PickingTechnique::Pick, 1.0, 0, 0.0f, .5f);
    double railUse = 0.0, inputUse = 0.0;
    for (std::size_t i=0;i<linear.drive.size();++i) {
        railUse=std::max(railUse,linear.freeDrive[i]/(linear.freeDrive[i]>=0?Design::railHigh:Design::railLow));
        inputUse=std::max(inputUse,std::abs(double(linear.input[i]))/Design::commonModeLimit);
    }
    const double firstClip=.5/railUse, firstInput=.5/inputUse;
    for(int index=2;index<5;++index) {
        const float overdrive=static_cast<float>(firstClip+(firstInput-firstClip)*(index+.5)/5.0);
        const auto strum=hardStrum(rate,1.0f,touch,acustra::PickingTechnique::Pick,1.0,0,0.0f,overdrive);
        std::ofstream file("clip-"+std::to_string(index)+".f64",std::ios::binary);
        for(std::size_t i=0;i+7<strum.stage.size();++i) {
            const double values[]{strum.freeDrive[i],double(strum.stage[i+7])};
            file.write(reinterpret_cast<const char*>(values),sizeof values);
        }
        std::cout<<index<<" drive "<<overdrive<<" U1A-limited "<<strum.inputClampedSamples<<" rails "<<Design::railLow<<" "<<Design::railHigh<<'\n';
    }
}
