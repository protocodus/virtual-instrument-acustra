// Offline attribution probe. Friend access observes the existing instrument;
// none of these diagnostic interventions is a product parameter.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static std::vector<int> lowBodyModes(const AcustraEngine& e)
    {
        std::vector<int> result;
        for (int i = 0; i < e.bodyBank_.count; ++i)
            if (poleHz(e, i) > 0.0 && poleHz(e, i) < 300.0)
                result.push_back(i);
        return result;
    }

    static double poleHz(const AcustraEngine& e, int i)
    {
        return std::atan2(e.bodyBank_.poleImaginary[i], e.bodyBank_.poleReal[i])
            * e.sampleRate_ / (2.0 * std::acos(-1.0));
    }

    static double poleT60(const AcustraEngine& e, int i)
    {
        const double radius = std::hypot(e.bodyBank_.poleReal[i],
                                         e.bodyBank_.poleImaginary[i]);
        return -std::log(1000.0) / (e.sampleRate_ * std::log(radius));
    }

    static std::vector<float> bodyCoefficients(const AcustraEngine& e)
    {
        std::vector<float> result;
        const auto& b = e.bodyBank_;
        for (int i = 0; i < b.count; ++i)
            for (float v : {b.poleReal[i], b.poleImaginary[i], b.leftReal[i],
                 b.leftImaginary[i], b.rightReal[i], b.rightImaginary[i],
                 b.leftMomentReal[i], b.leftMomentImaginary[i],
                 b.rightMomentReal[i], b.rightMomentImaginary[i]})
                result.push_back(v);
        return result;
    }

    static float dampLowE(AcustraEngine& e, float t60)
    {
        // Extra gain per round trip is a passive loss, applied to both
        // planes. Keep impedance, anchors and all six reciprocal ports.
        auto& v = e.voices_[0];
        if (v.played || !(t60 > 0.0f) || !std::isfinite(t60))
            throw std::runtime_error("invalid idle-string damping intervention");
        const float factor = e.handDamping(t60, e.loopFundamental(v));
        if (!(factor > 0.0f && factor <= 1.0f))
            throw std::runtime_error("diagnostic gain is not passive");
        for (auto& loop : v.loops)
            loop.loopGain *= factor;
        return factor;
    }

    static float bodyMono(const AcustraEngine& e, int i)
    {
        const auto& b = e.bodyBank_;
        const float left = 2.0f * (b.leftReal[i] * b.real[i]
            - b.leftImaginary[i] * b.imaginary[i]
            + b.leftMomentReal[i] * b.momentReal[i]
            - b.leftMomentImaginary[i] * b.momentImaginary[i]);
        const float right = 2.0f * (b.rightReal[i] * b.real[i]
            - b.rightImaginary[i] * b.imaginary[i]
            + b.rightMomentReal[i] * b.momentReal[i]
            - b.rightMomentImaginary[i] * b.momentImaginary[i]);
        return 0.5f * (left + right);
    }

    static std::vector<std::string> columns(const std::vector<int>& modes)
    {
        std::vector<std::string> result {"bridge_velocity", "bridge_body_force",
            "bridge_reaction_force", "bridge_power",
            "body_power", "tail_power", "junction_impedance"};
        for (int i = 0; i < 6; ++i)
            for (const char* field : {"normal_quarter", "parallel_quarter",
                                     "normal_midpoint", "outgoing_wave"})
                result.push_back("string_" + std::to_string(i) + "_" + field);
        for (int i : modes)
            result.push_back("body_" + std::to_string(i) + "_mono");
        result.push_back("body_above_300hz_mono");
        return result;
    }

    static void observe(const AcustraEngine& e, const std::vector<int>& modes,
                        std::vector<float>& result)
    {
        result.insert(result.end(), {e.lastBridgeVelocity_, e.lastBridgeBodyForce_,
            e.lastBridgeReactionForce_, e.lastBridgePower_,
            e.lastBridgeBodyPower_, e.lastBridgeTailPower_, e.lastImpedanceSum_});
        for (const auto& v : e.voices_)
        {
            result.push_back(v.loops[0].displacementAt(0.25f));
            result.push_back(v.loops[1].displacementAt(0.25f));
            result.push_back(v.loops[0].displacementAt(0.5f));
            // This is the last outgoing displacement-wave write, not a
            // second call to the stateful bridge velocity observer.
            const int index = (v.loops[0].writeIndex + e.maximumDelaySamples - 1)
                            % e.maximumDelaySamples;
            result.push_back(v.loops[0].delay[index]);
        }
        for (int i : modes)
            result.push_back(bodyMono(e, i));
        float remainder = 0.0f;
        for (int i = 0; i < e.bodyBank_.count; ++i)
            if (std::find(modes.begin(), modes.end(), i) == modes.end())
                remainder += bodyMono(e, i);
        result.push_back(remainder);
    }

    static void state(const AcustraEngine& e, std::ostream& o, int sample)
    {
        o << "{\"sample\":" << sample << ",\"voices\":[";
        for (int i = 0; i < 6; ++i)
        {
            if (i) o << ',';
            const auto& v = e.voices_[i];
            const auto& h = e.hand_[i];
            o << "{\"string\":" << i << ",\"open_midi\":" << v.openMidi
              << ",\"midi\":" << v.midiNote << ",\"fret\":" << v.fret
              << ",\"played\":" << (v.played ? "true" : "false")
              << ",\"key_down\":" << (v.keyDown ? "true" : "false")
              << ",\"pedal_held\":" << (v.pedalHeld ? "true" : "false")
              << ",\"impedance\":" << v.characteristicImpedance
              << ",\"anchor_stiffness\":" << v.bridgeTailStiffness
              << ",\"delay\":" << v.loops[0].currentDelay
              << ",\"loop_gain\":" << v.loops[0].loopGain
              << ",\"release_gain\":" << v.releaseDamping
              << ",\"applied_release_gain\":" << v.loops[0].appliedReleaseGain
              << ",\"hand_valid\":" << (h.valid ? "true" : "false")
              << ",\"hand_fret\":" << h.fret << ",\"hand_held_at\":" << h.heldAt
              << '}';
        }
        o << "]}";
    }

    static bool allPorts(const AcustraEngine& e)
    {
        return e.bridgeCouplingEnabled_ && e.sympatheticStringsEnabled_
            && std::all_of(e.voices_.begin(), e.voices_.end(), [] (const auto& v)
                { return v.characteristicImpedance > 0.0f; });
    }

    static float lowEGain(const AcustraEngine& e) { return e.voices_[0].loops[0].loopGain; }


};
} // namespace acustra

namespace
{
using Access = acustra::AcustraEngineTestAccess;
constexpr int rate = 48000;
constexpr int frames = 72000;
constexpr int velocity = 91;
static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

struct Condition { const char* name; int retune; float damping; };
constexpr Condition conditions[] {{"baseline", 0, 0.0f},
    {"drop-d", -2, 0.0f},
    {"low-e-damped", 0, 0.15f}};

std::unique_ptr<acustra::AcustraEngine> makeEngine(
    acustra::PickingTechnique picking, int midi, const Condition& condition,
    float& dampingFactor)
{
    auto e = std::make_unique<acustra::AcustraEngine>();
    acustra::EngineParameters p;
    p.picking = picking;
    p.room = 0.0f;
    p.tuning = condition.retune == -2 ? acustra::Tuning::DropD : acustra::Tuning::Standard;
    e->setParameters(p);
    e->prepare(rate, 1);
    if (condition.retune != 0)
    {
        auto standard = std::make_unique<acustra::AcustraEngine>();
        p.tuning = acustra::Tuning::Standard;
        standard->setParameters(p);
        standard->prepare(rate, 1);
        if (Access::bodyCoefficients(*standard) != Access::bodyCoefficients(*e))
            throw std::runtime_error("public tuning changed body coefficients");
    }
    e->noteOn(midi, static_cast<float>(velocity) / 127.0f);
    dampingFactor = condition.damping > 0.0f
        ? Access::dampLowE(*e, condition.damping) : 1.0f;
    if (!Access::allPorts(*e))
        throw std::runtime_error("probe must retain all six reciprocal ports");
    return e;
}

void writeF32(const std::filesystem::path& path, const std::vector<float>& values)
{
    if (!std::all_of(values.begin(), values.end(), [] (float v) { return std::isfinite(v); }))
        throw std::runtime_error("non-finite probe output");
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(values.data()),
               static_cast<std::streamsize>(values.size() * sizeof(float)));
    if (!file) throw std::runtime_error("failed writing " + path.string());
}

void capture(std::ostream& manifest, const std::filesystem::path& dir,
             acustra::PickingTechnique picking, int midi,
             const Condition& condition, bool selfTest)
{
    float dampingFactor, parityDamping;
    auto engine = makeEngine(picking, midi, condition, dampingFactor);
    auto parity = makeEngine(picking, midi, condition, parityDamping);
    // Read-only observation must not change even one float of later audio.
    const auto modes = Access::lowBodyModes(*engine);
    const auto bodyCoefficients = Access::bodyCoefficients(*engine);
    const float lowEGain = Access::lowEGain(*engine);
    const auto names = Access::columns(modes);
    const std::string technique = picking == acustra::PickingTechnique::Finger ? "finger" : "pick";
    const std::string key = technique + "-" + std::to_string(midi) + "-" + condition.name;
    std::vector<float> audio, trace;
    audio.reserve(2 * frames);
    trace.reserve(names.size() * frames);
    std::ostringstream states;
    states << std::setprecision(10) << '[';
    Access::state(*engine, states, 0);
    std::array<double, 3> work {}, minimum {}, maximum {}, positive {};
    float maximumAudio = 0.0f;
    const int length = selfTest ? 12000 : frames;
    for (int sample = 0; sample < length; ++sample)
    {
        float left, right, parityLeft, parityRight;
        engine->process(&left, &right, 1);
        parity->process(&parityLeft, &parityRight, 1);
        if (!std::isfinite(left) || !std::isfinite(right)
            || !std::isfinite(parityLeft) || !std::isfinite(parityRight))
            throw std::runtime_error("non-finite audio: " + key);
        if (std::bit_cast<std::uint32_t>(left) != std::bit_cast<std::uint32_t>(parityLeft)
            || std::bit_cast<std::uint32_t>(right) != std::bit_cast<std::uint32_t>(parityRight))
            throw std::runtime_error("observation changed the audio: " + key);
        maximumAudio = std::max({maximumAudio, std::abs(left), std::abs(right)});
        audio.insert(audio.end(), {left, right});
        Access::observe(*engine, modes, trace);
        const std::array<float, 3> powers {engine->getLastBridgePower(),
            engine->getLastBridgeBodyPower(), engine->getLastBridgeTailPower()};
        for (int i = 0; i < 3; ++i)
        {
            work[i] += powers[i] / rate;
            positive[i] += std::max(powers[i], 0.0f) / rate;
            minimum[i] = std::min(minimum[i], work[i]);
            maximum[i] = std::max(maximum[i], work[i]);
        }
        if ((sample + 1) % 4800 == 0)
        {
            states << ',';
            Access::state(*engine, states, sample + 1);
        }
    }
    states << ']';
    if (!Access::allPorts(*engine) || Access::bodyCoefficients(*engine) != bodyCoefficients
        || Access::lowEGain(*engine) != lowEGain)
        throw std::runtime_error("ports, body bank or diagnostic loss changed during capture");
    if (!(maximumAudio > 0.0f) || !std::isfinite(maximumAudio))
        throw std::runtime_error("silent or non-finite probe");
    for (int i = 0; i < 3; ++i)
        // Same roundoff bounds as AcustraEngineTests' stationary ledger:
        // the conservative spring's tiny return is less well conditioned.
        if (!std::isfinite(work[i]) || minimum[i]
            < (i == 2 ? -1.0e-4 * maximum[i] - 1.0e-15 : -1.0e-14))
        {
            std::ostringstream error;
            error << "negative cumulative bridge supplied work: " << key
                  << " branch=" << i << " minimum=" << minimum[i]
                  << " positive=" << positive[i];
            throw std::runtime_error(error.str());
        }
    if (!std::all_of(trace.begin(), trace.end(), [] (float v) { return std::isfinite(v); }))
        throw std::runtime_error("non-finite trace");
    if (selfTest) return;
    writeF32(dir / (key + ".audio.f32"), audio);
    writeF32(dir / (key + ".trace.f32"), trace);
    manifest << "{\"key\":\"" << key << "\",\"picking\":\"" << technique
             << "\",\"midi\":" << midi << ",\"velocity\":" << velocity
             << ",\"condition\":\"" << condition.name << "\",\"low_e_retune\":"
             << condition.retune << ",\"extra_low_e_t60\":" << condition.damping
             << ",\"extra_roundtrip_gain\":" << dampingFactor
             << ",\"sample_rate\":" << rate << ",\"frames\":" << length
             << ",\"audio\":\"" << key << ".audio.f32\",\"trace\":\""
             << key << ".trace.f32\",\"observer_audio_bit_identical\":true"
             << ",\"reciprocal_ports\":6,\"columns\":[";
    for (std::size_t i = 0; i < names.size(); ++i)
        manifest << (i ? "," : "") << '"' << names[i] << '"';
    manifest << "],\"body_modes\":[";
    for (std::size_t i = 0; i < modes.size(); ++i)
    {
        const int mode = modes[i];
        manifest << (i ? "," : "") << "{\"index\":" << mode
                 << ",\"frequency\":" << Access::poleHz(*engine, mode)
                 << ",\"intrinsic_t60\":" << Access::poleT60(*engine, mode) << '}';
    }
    manifest << "],\"cumulative_work\":[" << work[0] << ',' << work[1] << ',' << work[2]
             << "],\"minimum_work\":[" << minimum[0] << ',' << minimum[1] << ',' << minimum[2]
             << "],\"positive_work\":[" << positive[0] << ',' << positive[1] << ',' << positive[2]
             << "],\"states\":" << states.str() << '}';
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const bool selfTest = argc == 2 && std::string(argv[1]) == "--self-test";
        if (!selfTest && !(argc == 3 && std::string(argv[1]) == "--out"))
            throw std::runtime_error("usage: AcustraTraceRinging --out DIR | --self-test");
        const auto dir = selfTest ? std::filesystem::path {} : std::filesystem::path(argv[2]);
        if (!selfTest)
        {
            if (std::filesystem::exists(dir) && !std::filesystem::is_empty(dir))
                throw std::runtime_error("output directory must be new or empty");
            std::filesystem::create_directories(dir);
        }
        std::ostringstream manifest;
        const acustra::EngineParameters p;
        manifest << std::setprecision(10) << "{\"schema\":1,\"controls\":{"
                 << "\"capture\":\"StereoMic\",\"shape\":\"Dreadnought\","
                 << "\"body_material\":\"Spruce\","
                 << "\"room\":0,\"string_age\":" << p.stringAge
                 << ",\"pluck_position\":" << p.pluckPosition << ",\"touch\":" << p.touch
                 << ",\"body_amount\":" << p.bodyAmount << ",\"stereo_width\":" << p.stereoWidth
                 << ",\"output_gain\":" << p.outputGain
                 << "},"
                 << "\"retune_protocol\":\"Public Standard versus DropD tuning, selected before prepare; no custom tuner or control interception. Only the low string moves by two semitones. Its second partial overlaps the unchanged D3 string, so per-string traces are required.\","
                 << "\"work_guard\":\"Stationary bridge supplied-work ledger uses the existing engine-test roundoff bounds; this is not a complete string/hand/body energy proof.\",\"captures\":[";
        int count = 0;
        for (auto picking : {acustra::PickingTechnique::Finger, acustra::PickingTechnique::Pick})
            for (int midi : {64, 67, 72})
                for (const auto& condition : conditions)
                {
                    if (count++) manifest << ',';
                    capture(manifest, dir, picking, midi, condition, selfTest);
                }
        manifest << "]}\n";
        if (!selfTest)
        {
            std::ofstream out(dir / "manifest.json");
            out << manifest.str();
            if (!out) throw std::runtime_error("failed writing manifest");
        }
        std::cout << count << " captures: observer bit parity, finite samples and bridge supplied-work checks pass; "
                  << "public Standard/DropD tuning\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
