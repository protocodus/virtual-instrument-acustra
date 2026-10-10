#include "DSP/AcustraPerformer.h"

#include <array>
#include <fstream>
#include <memory>

// Built twice, once per storage ABI. Compare the actual float bytes, rather
// than a perceptual tolerance: extra capacity must not alter ordinary notes.
int main(int argc, char** argv)
{
    if (argc != 2)
        return 1;
    std::ofstream output(argv[1], std::ios::binary);
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0, 384000.0 })
    {
        auto performer = std::make_unique<acustra::Performer>();
        performer->prepare(rate, 64);
        acustra::EngineParameters parameters;
        parameters.room = 0.12f;
        parameters.piezoMix = 0.25f;
        performer->setParameters(parameters);
        performer->setTempoBpm(97.0);
        std::array<float, 64> left {}, right {}, piezo {};
        // Rack hosts stop at 192 kHz, where both builds use 8192 histories.
        // At 384 kHz the extended ring is deliberately larger: the legacy
        // explicit legato energy observer reads its unwritten ring slot,
        // which contains a different old sample. Keep the ordinary 384 kHz
        // fixture while exercising legato/wrapped tails at every host rate.
        const int blocks = rate <= 192000.0 ? 1200 : 350;
        for (int block = 0; block < blocks; ++block)
        {
            performer->beginBlock(left.data(), right.data(), { piezo.data() }, 64);
            if (block == 0)
            {
                performer->noteOn(3, 1, 40, 92);
                performer->noteOn(3, 1, 59, 105);
                performer->noteOn(3, 1, 64, 90);
            }
            if (block == 50)
            {
                performer->noteOff(10, 1, 64);
                performer->noteOn(12, 1, 64, 82);
            }
            if (block == 70) performer->controlChange(17, 1, 1, 30);
            if (block == 120) performer->pitchWheel(29, 1, 0.25f);
            if (block == 160) performer->noteOn(11, 1, 69, 96);
            if (block == 200) performer->controlChange(13, 1, 123, 0);
            // Repeated six-string strokes exercise active-prefix copies,
            // retained arrivals and the smaller ring after several wraps.
            if (block >= 400 && block < 1000 && block % 100 == 0)
                for (const int note : { 40, 47, 52, 56, 59, 64 })
                {
                    performer->noteOff(2, 1, note);
                    performer->noteOn(3, 1, note, 110);
                }
            performer->endBlock();
            if (block == 450 && !performer->engine().transitionNote(64, 65, 0.65f))
                return 2;
            if (block == 1050) performer->reset();
            for (const auto* bus : { &left, &right, &piezo })
                output.write(reinterpret_cast<const char*>(bus->data()),
                             static_cast<std::streamsize>(sizeof(float) * bus->size()));
        }
    }
    output.close();
    return output ? 0 : 1;
}
