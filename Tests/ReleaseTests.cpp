// Keyboard release must never request a fretting-hand excitation, however
// fast the key is lifted, and CC68 (once the legato footswitch, removed at
// the user's request) must change nothing. The notes go through the player,
// as a host's MIDI does, and complete stereo waves are compared, including
// the existing body/sympathetic tail, so a retuned open string, a delayed
// pedal pull-off or a boundary impulse fails.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr int blockSize = 127;
struct Audio { std::vector<float> left, right; int activeAtEnd; };
// Cc68 holds MIDI's old legato footswitch down through the note;
// Cc68UpUnderPedal, Cc68DownUnderPedal and ToggleCc68UnderPedal move it while
// the sustain pedal is holding the released note.
enum class Gesture { Ordinary, Pedal, Cc68, Cc68UpUnderPedal,
                     Cc68DownUnderPedal, ToggleCc68UnderPedal, Held };
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

Audio render(acustra::EngineParameters parameters, double rate, int note,
             int velocity, int releaseVelocity,
             Gesture gesture = Gesture::Ordinary)
{
    auto performer = std::make_unique<acustra::Performer>();
    performer->setParameters(parameters);
    performer->prepare(rate, blockSize);
    performer->setParameters(parameters);
    const bool startsDown = gesture == Gesture::Cc68
                         || gesture == Gesture::Cc68UpUnderPedal
                         || gesture == Gesture::ToggleCc68UnderPedal;
    const bool pedal = gesture == Gesture::Pedal
                    || gesture == Gesture::Cc68UpUnderPedal
                    || gesture == Gesture::Cc68DownUnderPedal
                    || gesture == Gesture::ToggleCc68UnderPedal;
    const auto frames = static_cast<std::size_t>(1.8 * rate);
    Audio audio { std::vector<float>(frames), std::vector<float>(frames), 0 };
    int position = 0;
    // Events land at the start of the next block the player renders.
    std::vector<std::array<int, 3>> pending;
    const auto send = [&] (int kind, int data1, int data2)
    {
        pending.push_back({ kind, data1, data2 });
    };
    const auto to = [&] (double seconds)
    {
        const int target = static_cast<int>(seconds * rate);
        while (position < target)
        {
            const int count = std::min(blockSize, target - position);
            performer->beginBlock(audio.left.data() + position,
                                  audio.right.data() + position, count);
            for (const auto& event : pending)
            {
                if (event[0] == 0x90)
                    performer->noteOn(0, 1, event[1], event[2]);
                else if (event[0] == 0x80)
                    performer->noteOff(0, 1, event[1], event[2]);
                else
                    performer->controlChange(0, 1, event[1], event[2]);
            }
            pending.clear();
            performer->endBlock();
            position += count;
        }
    };
    send(0xb0, 68, startsDown ? 127 : 0);
    send(0xb0, 64, pedal ? 127 : 0);
    to(0.2);
    send(0x90, note, velocity);
    to(1.0);
    if (gesture != Gesture::Held)
        send(0x80, note, releaseVelocity);
    to(1.1);
    if (gesture == Gesture::Cc68UpUnderPedal
        || gesture == Gesture::ToggleCc68UnderPedal)
        send(0xb0, 68, 0);
    else if (gesture == Gesture::Cc68DownUnderPedal)
        send(0xb0, 68, 127);
    if (gesture == Gesture::ToggleCc68UnderPedal)
        send(0xb0, 68, 127);
    if (pedal)
        send(0xb0, 64, 0);
    to(1.8);
    audio.activeAtEnd = performer->engine().getActiveVoiceCount();
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
        const auto normal = render(parameters, rate, 43, 127, 64);
        const auto fast = render(parameters, rate, 43, 127, 127);
        writeWave(directory / (name + "-ordinary-release.wav"), normal, 48000);
        writeWave(directory / (name + "-fast-release.wav"), fast, 48000);
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
                for (const int velocity : { 121, 127 })
                    for (const int note : { 43, 60, 76 })
                    {
                        acustra::EngineParameters parameters;
                        parameters.stringMaterial = material;
                        parameters.shape = shape;
                        const auto ordinary = render(parameters, rate, note, velocity, 64);
                        const auto fast = render(parameters, rate, note, velocity, 127);
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
    {
        acustra::EngineParameters parameters;
        parameters.stringMaterial = material;
        const auto plain = render(parameters, 48000.0, 43, 127, 64, Gesture::Pedal);
        for (const auto gesture : { Gesture::Pedal, Gesture::Cc68UpUnderPedal,
                                   Gesture::Cc68DownUnderPedal, Gesture::ToggleCc68UnderPedal })
        {
            const auto ordinary = render(parameters, 48000.0, 43, 127, 64, gesture);
            const auto fast = render(parameters, 48000.0, 43, 127, 127, gesture);
            expect(same(ordinary, fast), "pedal-up generated an unrequested pull-off");
            expect(same(plain, fast), "CC68 under the pedal changed the wave");
            expect(fast.activeAtEnd == 0, "pedal-up did not retire its fretted note");
        }
    }
}

// CC68 was MIDI's Legato Footswitch here until the user asked for legato to
// be removed everywhere (Docs/decisions.md, 2026-09-28). It must now be an
// exact no-op, held through a note and its release at any release speed.
void testCc68ChangesNothing()
{
    for (const auto material : { acustra::StringMaterial::Steel, acustra::StringMaterial::Nylon })
        for (const int note : { 43, 60 })
        {
            acustra::EngineParameters parameters;
            parameters.stringMaterial = material;
            const auto plain = render(parameters, 48000.0, note, 100, 64);
            for (const int release : { 0, 64, 127 })
                expect(same(plain, render(parameters, 48000.0, note, 100, release,
                                          Gesture::Cc68)),
                       "CC68 changed a note at release velocity "
                           + std::to_string(release));
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
                const auto held = render(parameters, rate, note, 127, 64, Gesture::Held);
                const auto released = render(parameters, rate, note, 127, 127);
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
        testCc68ChangesNothing();
        testHardPluckReleaseDoesNotCreateAnAttack();
    }
    return failures == 0 ? 0 : 1;
}
