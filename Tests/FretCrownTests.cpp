// A folded travelling-wave potential has an arbitrary common reference.
// A fret/body junction must not manufacture a string displacement from it.
#include "DSP/AcustraEngine.h"

#include <iostream>
#include <memory>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static bool silentCommonPotential()
    {
        int cases = 0;
        for (double rate : { 8000.0, 48000.0, 192000.0 })
        {
            auto engine = std::make_unique<AcustraEngine>();
            engine->prepare(rate, 64);
            engine->setStringPerChannelMode(true);
            for (int fret : { 0, 5, 20 })
            {
                engine->noteOn(40 + fret, 0.8f, 1);
                auto& voice = engine->voices_[0];
                // A retained string is an independent folded wave too.
                voice.tailActive = true;
                voice.tailLoop.gestureContact = {};
                voice.tailParallelLoop.gestureContact = {};
                for (float common : { -4.0f, -0.5f, -0.1f, 0.0f, 0.5f, 4.0f })
                {
                    for (auto* loop : { &voice.loops[0], &voice.loops[1],
                                        &voice.tailLoop, &voice.tailParallelLoop })
                    {
                        loop->delay.fill(common);
                        loop->gestureContact = {};
                    }
                    voice.contactTravelEnabled = true;
                    // With fixed bridge/nut and no excitation, the physical
                    // field y(x)=s(D-x/c)-s(x/c) is identically zero. Its
                    // arbitrary potential must remain unchanged. Shipping
                    // 1.9.34 wrote a +0.378 kink at common=-0.5 on frets.
                    engine->finishVoice(voice, 0, common, common, 0.0f,
                                        common, common, 0.0f, 0.0f, 0.0f);
                    for (auto* loop : { &voice.loops[0], &voice.loops[1],
                                        &voice.tailLoop, &voice.tailParallelLoop })
                        for (float value : loop->delay)
                            if (value != common)
                            {
                                std::cerr << "Common-potential invariance failed: rate="
                                          << rate << " fret=" << fret << " common="
                                          << common << " value=" << value << '\n';
                                return false;
                            }
                    ++cases;
                }
            }
        }
        std::cout << "Silent common-potential invariance: " << cases
                  << " rate/fret/reference cases, live and retained planes\n";
        return true;
    }
};
} // namespace acustra

int main()
{
    return acustra::AcustraEngineTestAccess::silentCommonPotential() ? 0 : 1;
}
