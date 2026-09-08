// Ordinary keyboard release must never request a fretting-hand excitation.
// Render complete stereo waves, including the existing body/sympathetic tail,
// so a retuned open string, delayed pedal pull-off or boundary impulse fails.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
constexpr int blockSize = 127;
struct Audio { std::vector<float> left, right; int activeAtEnd; };
enum class Gesture { Ordinary, Pedal, Legato, LeaveLegatoUnderPedal,
                     EnterLegatoUnderPedal, ToggleLegatoUnderPedal, Held };
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

Audio render(acustra::EngineParameters parameters, double rate, int note,
             float velocity, float lift, Gesture gesture = Gesture::Ordinary)
{
    acustra::AcustraEngine engine;
    engine.setParameters(parameters);
    engine.prepare(rate, blockSize);
    engine.setParameters(parameters);
    const bool startsLegato = gesture == Gesture::Legato
                          || gesture == Gesture::LeaveLegatoUnderPedal
                          || gesture == Gesture::ToggleLegatoUnderPedal;
    const bool pedal = gesture == Gesture::Pedal
                    || gesture == Gesture::LeaveLegatoUnderPedal
                    || gesture == Gesture::EnterLegatoUnderPedal
                    || gesture == Gesture::ToggleLegatoUnderPedal;
    engine.setLegato(startsLegato);
    engine.setSustainPedal(pedal);
    const auto frames = static_cast<std::size_t>(1.8 * rate);
    Audio audio { std::vector<float>(frames), std::vector<float>(frames), 0 };
    int position = 0;
    const auto to = [&] (double seconds)
    {
        const int target = static_cast<int>(seconds * rate);
        while (position < target)
        {
            const int count = std::min(blockSize, target - position);
            engine.process(audio.left.data() + position,
                           audio.right.data() + position, count);
            position += count;
        }
    };
    to(0.2);
    engine.noteOn(note, velocity);
    to(1.0);
    if (gesture != Gesture::Held)
        engine.noteOff(note, 1, lift);
    to(1.1);
    if (gesture == Gesture::LeaveLegatoUnderPedal || gesture == Gesture::ToggleLegatoUnderPedal)
        engine.setLegato(false);
    else if (gesture == Gesture::EnterLegatoUnderPedal)
        engine.setLegato(true);
    if (gesture == Gesture::ToggleLegatoUnderPedal)
        engine.setLegato(true);
    if (pedal)
        engine.setSustainPedal(false);
    to(1.8);
    audio.activeAtEnd = engine.getActiveVoiceCount();
    return audio;
}

bool same(const Audio& a, const Audio& b)
{
    return a.left == b.left && a.right == b.right;
}

double peak(const Audio& audio, double rate, double begin, double end)
{
    double result = 0.0;
    for (int i = static_cast<int>(begin * rate); i < static_cast<int>(end * rate); ++i)
        result = std::max({ result,
            std::abs(static_cast<double>(audio.left[static_cast<std::size_t>(i)])),
            std::abs(static_cast<double>(audio.right[static_cast<std::size_t>(i)])) });
    return result;
}

double energy(const Audio& audio, double rate, double begin, double end)
{
    double result = 0.0;
    for (int i = static_cast<int>(begin * rate); i < static_cast<int>(end * rate); ++i)
    {
        const auto n = static_cast<std::size_t>(i);
        result += static_cast<double>(audio.left[n]) * audio.left[n]
                + static_cast<double>(audio.right[n]) * audio.right[n];
    }
    return result;
}

void writeWave(const std::filesystem::path& path, const Audio& audio, int rate)
{
    std::ofstream output(path, std::ios::binary);
    const auto write = [&] (std::uint32_t value, int bytes)
    {
        for (int i = 0; i < bytes; ++i)
            output.put(static_cast<char>((value >> (8 * i)) & 0xffu));
    };
    const auto size = static_cast<std::uint32_t>(audio.left.size() * 4u);
    output.write("RIFF", 4); write(36u + size, 4); output.write("WAVEfmt ", 8);
    write(16, 4); write(1, 2); write(2, 2); write(static_cast<std::uint32_t>(rate), 4);
    write(static_cast<std::uint32_t>(rate * 4), 4); write(4, 2); write(16, 2);
    output.write("data", 4); write(size, 4);
    for (std::size_t i = 0; i < audio.left.size(); ++i)
        for (const float value : { audio.left[i], audio.right[i] })
            write(static_cast<std::uint16_t>(static_cast<std::int16_t>(
                std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f))), 2);
    expect(static_cast<bool>(output), "could not write release comparison WAV");
}

void renderComparisons(const std::filesystem::path& directory)
{
    std::filesystem::create_directories(directory);
    constexpr double rate = 48000.0;
    for (const auto material : { acustra::StringMaterial::Steel, acustra::StringMaterial::Nylon })
    {
        acustra::EngineParameters parameters;
        parameters.stringMaterial = material;
        const std::string name = material == acustra::StringMaterial::Steel ? "steel" : "nylon";
        const auto normal = render(parameters, rate, 43, 1.0f, 0.0f);
        const auto fast = render(parameters, rate, 43, 1.0f, 1.0f);
        const auto intentional = render(parameters, rate, 43, 1.0f, 1.0f, Gesture::Legato);
        writeWave(directory / (name + "-ordinary-release.wav"), normal, 48000);
        writeWave(directory / (name + "-fast-release.wav"), fast, 48000);
        writeWave(directory / (name + "-cc68-pull-off.wav"), intentional, 48000);
        std::cout << name << " fast/ordinary first-50ms peak="
                  << peak(fast, rate, 1.0, 1.05) / peak(normal, rate, 1.0, 1.05)
                  << " tail-energy-ratio=" << energy(fast, rate, 1.3, 1.8)
                     / energy(normal, rate, 1.3, 1.8)
                  << " bit-identical=" << same(normal, fast) << '\n';
    }
}

void testOrdinaryReleaseIsIndependentOfLiftSpeed()
{
    int cases = 0;
    for (const auto material : { acustra::StringMaterial::Steel, acustra::StringMaterial::Nylon })
        for (const auto shape : { acustra::BodyShape::Parlor, acustra::BodyShape::Auditorium,
                                 acustra::BodyShape::Dreadnought, acustra::BodyShape::Jumbo })
            for (const double rate : { 44100.0, 48000.0, 96000.0 })
                for (const float velocity : { 0.95f, 1.0f })
                    for (const int note : { 43, 60, 76 })
                    {
                        acustra::EngineParameters parameters;
                        parameters.stringMaterial = material;
                        parameters.shape = shape;
                        const auto ordinary = render(parameters, rate, note, velocity, 0.0f);
                        const auto fast = render(parameters, rate, note, velocity, 1.0f);
                        const std::string label = "case " + std::to_string(++cases)
                            + " note " + std::to_string(note) + " at " + std::to_string(rate);
                        expect(same(ordinary, fast), label + ": fast key-up changed the stereo wave");
                        expect(fast.activeAtEnd == 0, label + ": a fretted key-up retained note ownership");
                    }
    std::cout << "Acustra ordinary release invariance: " << cases << " cases\n";
}

void testPedalCannotTurnOrdinaryReleaseIntoAnExcitation()
{
    for (const auto material : { acustra::StringMaterial::Steel, acustra::StringMaterial::Nylon })
        for (const auto gesture : { Gesture::Pedal, Gesture::LeaveLegatoUnderPedal,
                                   Gesture::EnterLegatoUnderPedal, Gesture::ToggleLegatoUnderPedal })
        {
            acustra::EngineParameters parameters;
            parameters.stringMaterial = material;
            const auto ordinary = render(parameters, 48000.0, 43, 1.0f, 0.0f, gesture);
            const auto fast = render(parameters, 48000.0, 43, 1.0f, 1.0f, gesture);
            expect(same(ordinary, fast), "pedal-up generated an unrequested pull-off");
            expect(fast.activeAtEnd == 0, "pedal-up did not retire its fretted note");
        }
}

void testHardPluckReleaseDoesNotCreateAnAttack()
{
    double worst = 0.0;
    for (const auto material : { acustra::StringMaterial::Steel, acustra::StringMaterial::Nylon })
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
            for (const int note : { 43, 60, 76 })
            {
                acustra::EngineParameters parameters;
                parameters.stringMaterial = material;
                const auto held = render(parameters, rate, note, 1.0f, 0.0f, Gesture::Held);
                const auto released = render(parameters, rate, note, 1.0f, 1.0f);
                const double reference = peak(held, rate, 0.95, 1.005);
                const double ratio = peak(released, rate, 1.0, 1.005) / reference;
                worst = std::max(worst, ratio);
                expect(std::isfinite(ratio) && ratio <= 1.05,
                       "maximum-velocity release creates a new onset: ratio " + std::to_string(ratio));
            }
    std::cout << "Acustra maximum-velocity release/held 5-ms peak: " << worst << '\n';
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--render")
        renderComparisons(argv[2]);
    else
    {
        testOrdinaryReleaseIsIndependentOfLiftSpeed();
        testPedalCannotTurnOrdinaryReleaseIntoAnExcitation();
        testHardPluckReleaseDoesNotCreateAnAttack();
    }
    return failures == 0 ? 0 : 1;
}
