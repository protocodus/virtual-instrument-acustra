// Compile this same read-only diagnostic against each version's matching
// headers and static library. No observation table is loaded or injected.
#include "StudioObserverHarness.h"
#include <memory>

int main(int argc, char** argv)
{
    try
    {
        using Engine = acustra::AcustraEngine;
        using Access = acustra::AcustraEngineTestAccess;
        if (argc != 5 && argc != 6) throw std::runtime_error("usage: PREFIX RATE SHAPE WOOD [FRAMES48]");
        if (!Access::environment("ACUSTRA_STUDIO_OBSERVER_FILE").empty())
            throw std::runtime_error("production trace forbids an observation injector");
        const std::string prefix = argv[1];
        const int rate = std::stoi(argv[2]), shape = std::stoi(argv[3]), wood = std::stoi(argv[4]);
        const int referenceFrames = argc == 6 ? std::stoi(argv[5]) : 12000;
        if (rate < 8000 || rate > 384000 || shape < 0 || shape > 3 || wood < 0 || wood > 2)
            throw std::runtime_error("invalid test configuration");
        auto engine = std::make_unique<Engine>();
        acustra::EngineParameters p;
        p.shape = static_cast<acustra::BodyShape>(shape);
        p.bodyMaterial = static_cast<acustra::BodyMaterial>(wood);
        p.room = 0.0f;
        engine->setPhysicalCalibration(acustra::fittedPhysicalCalibration);
        engine->setParameters(p);
        engine->prepare(rate, 1);
        engine->setStringPerChannelMode(true);
        std::ofstream output, piezo, poles;
        Access::openNew(output, prefix + ".mic.f32", std::ios::out | std::ios::binary);
        Access::openNew(piezo, prefix + ".piezo.f32", std::ios::out | std::ios::binary);
        Access::openNew(poles, prefix + ".poles.u32", std::ios::out | std::ios::binary);
        for (const auto value : Access::poleBits(*engine))
            for (unsigned shift = 0; shift < 32; shift += 8)
                poles.put(static_cast<char>(value >> shift));
        poles.close();
        Access::Trace trace;
        trace.prepare(*engine);
        const auto at = [rate] (int frame48) { return int(std::lround(frame48 * (rate / 48000.))); };
        constexpr std::array notes {40,47,52,55,59,64};
        constexpr std::array velocities {90,84,88,81,78,76};
        const auto sample = [] (std::ofstream& file, float value)
        {
            if (!std::isfinite(value)) throw std::runtime_error("nonfinite production output");
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned shift = 0; shift < 32; shift += 8)
                file.put(static_cast<char>(bits >> shift));
        };
        for (int frame = 0; frame < at(referenceFrames); ++frame)
        {
            if (frame == 0) engine->noteOn(40,96/127.f,1);
            if (referenceFrames == 512)
            {
                if (frame == at(33)) engine->noteOn(45,80/127.f,2);
                if (frame == at(310)) engine->noteOff(40,1);
                if (frame == at(400)) engine->noteOn(47,103/127.f,1);
            }
            if (frame == at(1500)) engine->noteOff(40,1);
            if (frame == at(2200)) engine->noteOn(47,103/127.f,1);
            if (frame == at(3600)) engine->noteOff(47,1);
            if (frame == at(4800))
                for (int i=0;i<6;++i) engine->noteOn(notes[i],velocities[i]/127.f,i+1);
            if (frame == at(8500))
                for (int i=0;i<6;++i) engine->noteOff(notes[i],i+1);
            float l {}, r {}, z {};
            engine->process(&l,&r,Engine::OutputBuses{&z},1);
            sample(output,l); sample(output,r); sample(piezo,z);
            trace.sample(*engine,frame);
        }
        trace.finish(); output.close(); piezo.close();
        if (output.fail() || piezo.fail() || poles.fail())
            throw std::runtime_error("production trace output failed");
        return 0;
    }
    catch (const std::exception& error)
    { std::cerr << error.what() << '\n'; return 1; }
}
