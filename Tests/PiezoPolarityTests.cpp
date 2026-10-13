// Public output contract: polarity belongs only to the pickup blended into
// microphone Capture, and automation fades its sign without touching voices.
#include "DSP/AcustraEngine.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace {
using acustra::AcustraEngine;
using acustra::CaptureType;
int failures = 0;
void expect(bool ok, const char* message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
struct Audio { std::vector<float> l, r, p; };
bool exact(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()*sizeof(float)) == 0;
}
Audio render(int rate, CaptureType capture, float mix, bool inverted, bool automate = false) {
    acustra::EngineParameters parameters;
    parameters.capture = capture;
    parameters.piezoMix = mix;
    parameters.piezoPolarityInverted = inverted;
    parameters.room = 0.1f;
    auto engine = std::make_unique<AcustraEngine>();
    engine->setParameters(parameters);
    engine->prepare(rate, 64);
    engine->noteOn(66, 0.65f, 1);
    const int length = rate * 4 / 5;
    Audio out { std::vector<float>(length), std::vector<float>(length), std::vector<float>(length) };
    // Per-sample calls put switches at exact times independently of block size.
    for (int i = 0; i < length; ++i) {
        if (automate && (i == rate/10 || i == rate*13/100 || i == rate/5)) {
            parameters.piezoPolarityInverted = !parameters.piezoPolarityInverted;
            engine->setParameters(parameters);
        }
        engine->process(&out.l[i], &out.r[i], { &out.p[i] }, 1);
    }
    return out;
}
void verify(int rate, CaptureType capture) {
    const auto dry = render(rate, capture, 0.0f, false);
    const auto normal = render(rate, capture, 0.35f, false);
    const auto inverted = render(rate, capture, 0.35f, true);
    const auto moved = render(rate, capture, 0.35f, false, true);
    expect(exact(dry.p, normal.p) && exact(normal.p, inverted.p) && exact(normal.p, moved.p),
           "polarity changed the dedicated Piezo output");
    double worst = 0.0, smoothWorst = 0.0, effect = 0.0;
    // Independent physical-time reference for the 20 ms one-pole. Float
    // endpoint settlement differs only below the engine's tiny snap bound.
    const double decay = std::exp(-1.0 / (0.020 * rate));
    double polarity = 1.0, target = 1.0;
    for (std::size_t i = 0; i < normal.l.size(); ++i) {
        if (i == static_cast<std::size_t>(rate/10) || i == static_cast<std::size_t>(rate*13/100)
            || i == static_cast<std::size_t>(rate/5)) target = -target;
        polarity = target + decay * (polarity - target);
        const double blend = capture == CaptureType::Piezo ? 0.0 : 0.35 * normal.p[i];
        for (const auto side : {0, 1}) {
            const float d = side == 0 ? dry.l[i] : dry.r[i];
            const float n = side == 0 ? normal.l[i] : normal.r[i];
            const float v = side == 0 ? inverted.l[i] : inverted.r[i];
            const float m = side == 0 ? moved.l[i] : moved.r[i];
            worst = std::max({worst, std::abs(n-d-blend), std::abs(v-d+blend)});
            smoothWorst = std::max(smoothWorst, std::abs(m-d-polarity*blend));
            effect = std::max(effect, std::abs(static_cast<double>(n)-v));
            expect(std::isfinite(m) && std::abs(m) < 0.5f, "invalid/limited polarity witness");
            if (i > static_cast<std::size_t>(rate*7/10))
                expect(m == v, "polarity did not settle exactly after retargeting");
        }
    }
    expect(worst < 1.0e-7, "normal/inverted blend is not mic +/- pickup");
    expect(smoothWorst < 1.0e-5, "switching did not follow the smooth sign transition");
    if (capture == CaptureType::Piezo) {
        expect(exact(normal.l, inverted.l) && exact(normal.r, inverted.r)
            && exact(normal.l, moved.l), "polarity changed Piezo-only Capture");
    } else {
        expect(effect > 1.0e-4, "polarity switch had no effect on the mixed pickup");
        const auto zeroMoved = render(rate, capture, 0.0f, false, true);
        expect(exact(dry.l, zeroMoved.l) && exact(dry.r, zeroMoved.r),
               "polarity affected microphones with Piezo Mix at zero");
    }
    std::cout << rate << " capture " << static_cast<int>(capture)
              << ": sum error " << worst << ", smooth error " << smoothWorst << '\n';
}
} // namespace
int main() {
    expect(!acustra::EngineParameters{}.piezoPolarityInverted, "polarity default is inverted");
    for (const int rate : {48000, 96000})
        for (const auto capture : {CaptureType::StereoMic, CaptureType::MonoMic, CaptureType::Piezo})
            verify(rate, capture);
    return failures == 0 ? 0 : 1;
}
