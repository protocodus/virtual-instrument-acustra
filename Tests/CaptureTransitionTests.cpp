// Exercise overlapping observation and construction changes, rather than
// each control in isolation. No observer is allowed to change string motion.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct Observation { bool finite { true }; double bound {}, modePeak {}; };

    static Observation inspect(const AcustraEngine& e)
    {
        Observation out;
        const auto value = [&] (double x)
        {
            out.finite = out.finite && std::isfinite(x);
            out.modePeak = std::max(out.modePeak, std::abs(x));
        };
        for (const auto* bank : { &e.bodyBank_, &e.fadingBodyBank_ })
            for (int i = 0; i < bank->count; ++i)
                for (double x : { bank->real[i], bank->imaginary[i],
                                  bank->momentReal[i], bank->momentImaginary[i] })
                    value(x);
        for (const auto* load : { &e.bridgeLoad_, &e.fadingBridgeLoad_ })
            for (int i = 0; i < load->activeModeCount; ++i)
            {
                const auto index = load->activeModes[i];
                for (const auto& mode : { load->heaveMode(index), load->rockMode(index) })
                    for (double x : { mode.input1, mode.output1, mode.output2 })
                        value(x);
            }
        double mic = 0.0;
        for (const auto* line : { &e.micDelayLeft_, &e.micDelayRight_, &e.micDelayMono_ })
            for (float x : *line)
            {
                out.finite = out.finite && std::isfinite(x);
                mic = std::max(mic, std::abs(double(x)));
            }
        // These are the actual warm piezo output and microphone delay
        // samples, before safetyLimit. The capture is a convex blend; its
        // additional Piezo Mix cannot exceed one more copy of the piezo.
        const double piezo = double(e.piezoTrim_) * e.piezoOutputScale_
            * e.piezoOutputVolts_ * e.outputGain_;
        // radiationReferenceGain is 18 in the production output unit.
        out.bound = mic + 2.0 * std::abs(18.0 * piezo);
        out.finite = out.finite && std::isfinite(out.bound);
        return out;
    }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Parameters = acustra::EngineParameters;
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        if (failures <= 12) std::cerr << "FAIL: " << message << '\n';
    }
}
struct Audio { std::vector<float> left, right, piezo; };
struct Evidence
{
    double rawBound {}, boundEnergy {}, modePeak {}, bridgePeak {}, observationDifference {};
};
struct Result { Audio audio; Evidence evidence; };

Parameters controls(int step)
{
    Parameters p;
    p.outputGain = 1.0f;
    p.room = 0.5f;
    p.releaseNoise = 0.7f;
    if (step < 0 || step >= 12)
        return p;
    p.shape = static_cast<acustra::BodyShape>((step * 3) % 4);
    p.bodyMaterial = static_cast<acustra::BodyMaterial>((step / 4 + step) % 3);
    constexpr std::array captures { acustra::CaptureType::StereoMic,
        acustra::CaptureType::MonoMic, acustra::CaptureType::Piezo };
    p.capture = captures[step % 3];
    p.room = ((step / 3) % 3) * 0.5f;
    p.piezoMix = (step % 2) ? 1.0f : 0.0f;
    p.outputGain = (step % 4) == 0 ? 0.0f : (step % 4) == 1
        ? 1.995262315f : (step % 4) == 2 ? 0.063095734f : 1.0f;
    p.bodyAmount = (step % 2) ? 1.0f : 0.0f;
    p.stereoWidth = (step % 2) ? 0.0f : 1.0f;
    p.picking = static_cast<acustra::PickingTechnique>(step % 3);
    p.stringAge = (step % 2) ? 1.0f : 0.0f;
    p.pluckPosition = (step % 2) ? 0.0f : 1.0f;
    p.touch = (step % 2) ? 1.0f : 0.0f;
    return p;
}

Result render(int rate, int block, bool independentReference)
{
    auto engine = std::make_unique<Engine>();
    engine->setParameters(controls(-1));
    engine->prepare(rate, block);
    std::unique_ptr<Engine> reference;
    if (independentReference)
    {
        reference = std::make_unique<Engine>();
        reference->setParameters(controls(-1));
        reference->prepare(rate, block);
    }
    const auto sample = [rate] (double t) { return int(std::lround(t * rate)); };
    constexpr std::array notes { 40, 45, 50, 55, 59, 64 };
    struct Event { int at, kind, value; };
    std::vector<Event> events { {0, 1, 127} };
    for (int step = 0; step < 12; ++step)
        events.push_back({ sample(.019 + .017 * step), 0, step });
    for (double time : { .087, .163, .249 })
    {
        events.push_back({sample(time), 2, 0});
        events.push_back({sample(time + .002), 1, 127});
    }
    events.push_back({sample(.320), 0, 12});
    events.push_back({sample(.490), 2, 0});
    events.push_back({sample(.523), 1, 32});
    events.push_back({sample(.630), 3, 0});
    std::stable_sort(events.begin(), events.end(), [] (const auto& a, const auto& b)
        { return a.at < b.at; });
    const int length = sample(.650);
    Result result;
    for (auto* channel : { &result.audio.left, &result.audio.right, &result.audio.piezo })
        channel->resize(length);
    std::vector<float> l(block), r(block), z(block);
    std::size_t event = 0;
    for (int done = 0; done < length;)
    {
        while (event < events.size() && events[event].at == done)
        {
            const auto e = events[event++];
            if (e.kind == 0)
            {
                auto p = controls(e.value);
                engine->setParameters(p);
                if (reference)
                {
                    p.capture = acustra::CaptureType::StereoMic;
                    p.room = 0.0f; p.piezoMix = 0.0f;
                    p.bodyAmount = .82f; p.stereoWidth = .62f;
                    reference->setParameters(p);
                }
            }
            else
                for (Engine* target : { engine.get(), reference.get() })
                    if (target)
                    {
                        if (e.kind == 1)
                            for (int note : notes) target->noteOn(note, e.value / 127.0f);
                        else if (e.kind == 2)
                            for (int note : notes) target->noteOff(note);
                        else target->allSoundOff();
                    }
        }
        int count = std::min(block, length - done);
        if (event < events.size()) count = std::min(count, events[event].at - done);
        auto& audio = result.audio;
        engine->process(audio.left.data() + done, audio.right.data() + done,
            {audio.piezo.data() + done}, count);
        if (reference)
        {
            reference->process(l.data(), r.data(), {z.data()}, count);
            expect(std::memcmp(z.data(), audio.piezo.data() + done, count * sizeof(float)) == 0,
                   "observation changes reached the independent piezo bus");
            expect(engine->getLastBridgeReactionForce() == reference->getLastBridgeReactionForce()
                && engine->getLastBridgeBodyForce() == reference->getLastBridgeBodyForce(),
                "observation changes altered physical bridge motion");
            for (int i = 0; i < count; ++i)
            {
                const double dl = double(l[i]) - audio.left[done + i];
                const double dr = double(r[i]) - audio.right[done + i];
                result.evidence.observationDifference += (dl * dl + dr * dr) / rate;
            }
        }
        const auto observed = acustra::AcustraEngineTestAccess::inspect(*engine);
        expect(observed.finite, "a pre-limiter sensor or body/bridge state became nonfinite");
        auto& evidence = result.evidence;
        evidence.rawBound = std::max(evidence.rawBound, observed.bound);
        evidence.modePeak = std::max(evidence.modePeak, observed.modePeak);
        evidence.boundEnergy += observed.bound * observed.bound * count / rate;
        evidence.bridgePeak = std::max(evidence.bridgePeak,
            std::abs(double(engine->getLastBridgeReactionForce())));
        done += count;
    }
    for (const auto* channel : { &result.audio.left, &result.audio.right, &result.audio.piezo })
    {
        expect(std::all_of(channel->begin(), channel->end(), [] (float x)
            { return std::isfinite(x) && std::abs(x) <= 1.0f; }),
            "a capture output became nonfinite or unbounded");
        expect(std::all_of(channel->begin() + sample(.630), channel->end(),
            [] (float x) { return x == 0.0f; }), "panic left a capture or room tail");
    }
    // An operational runaway fence, not a loudness target or a claim that
    // this signal is naturally below the output limiter. The raw bound is
    // reported, including during hard plucks at +6 dB and full Piezo Mix.
    expect(result.evidence.rawBound < 16.0 && std::isfinite(result.evidence.boundEnergy),
           "the final safety limiter concealed excessive sensor energy");
    if (independentReference)
        expect(result.evidence.observationDifference > 1.0e-6,
               "the comparison was silent or observation controls had no audible effect");
    return result;
}

bool same(const Audio& a, const Audio& b)
{
    return a.left.size() == b.left.size()
        && std::memcmp(a.left.data(), b.left.data(), a.left.size() * sizeof(float)) == 0
        && std::memcmp(a.right.data(), b.right.data(), a.right.size() * sizeof(float)) == 0
        && std::memcmp(a.piezo.data(), b.piezo.data(), a.piezo.size() * sizeof(float)) == 0;
}
}

int main()
{
    for (int rate : { 32000, 48000, 96000, 192000, 384000 })
    {
        const auto baseline = render(rate, 1, true);
        for (int block : { 37, 256 })
            expect(same(baseline.audio, render(rate, block, false).audio),
                   "combined capture/construction transitions changed with block partition");
        const auto& e = baseline.evidence;
        std::cout << "rate=" << rate << " raw_sensor_bound=" << e.rawBound
            << " bound_energy=" << e.boundEnergy << " mode_peak=" << e.modePeak
            << " bridge_peak=" << e.bridgePeak
            << " observation_difference=" << e.observationDifference << '\n';
    }
    std::cout << "Capture transition failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
