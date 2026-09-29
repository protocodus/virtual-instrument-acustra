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

double decibels(double ratio) { return 10.0 * std::log10(std::max(ratio, 1.0e-30)); }

// Notes played through the player as raw MIDI on channel 1, each event at
// the start of the block that contains its time. The idle strings are kept
// out of it, so what is heard after key-up is the released string itself
// and not, say, the open A answering an A3.
struct Timed { double seconds; std::array<std::uint8_t, 3> bytes; };

Audio renderEvents(acustra::EngineParameters parameters, double seconds,
                   const std::vector<Timed>& events)
{
    constexpr double rate = 48000.0;
    auto performer = std::make_unique<acustra::Performer>();
    performer->setParameters(parameters);
    performer->prepare(rate, blockSize);
    performer->engine().setSympatheticStringsEnabled(false);
    const auto frames = static_cast<std::size_t>(seconds * rate);
    Audio audio { std::vector<float>(frames), std::vector<float>(frames), 0 };
    std::size_t next = 0;
    for (std::size_t position = 0; position < frames; position += blockSize)
    {
        const int count = static_cast<int>(std::min<std::size_t>(blockSize, frames - position));
        performer->beginBlock(audio.left.data() + position, audio.right.data() + position, count);
        while (next < events.size()
               && static_cast<std::size_t>(events[next].seconds * rate)
                      < position + static_cast<std::size_t>(count))
        {
            performer->handleMidi(0, events[next].bytes.data(), 3);
            ++next;
        }
        performer->endBlock();
    }
    audio.activeAtEnd = performer->engine().getActiveVoiceCount();
    return audio;
}

// How far below the held note the released one is over [from, to).
double releaseDrop(const acustra::EngineParameters& parameters,
                   const std::vector<Timed>& held, const std::vector<Timed>& released,
                   double from, double to)
{
    const auto heldAudio = renderEvents(parameters, to + 0.05, held);
    const auto releasedAudio = renderEvents(parameters, to + 0.05, released);
    return decibels(energy(heldAudio, 48000.0, from, to)
                    / energy(releasedAudio, 48000.0, from, to));
}

const char* materialName(acustra::StringMaterial material)
{
    return material == acustra::StringMaterial::Steel ? "steel" : "nylon";
}

// A released natural harmonic is damped by the hand as its open string is:
// the loop runs at the open string's period, so the hand's loss per trip
// round it must be the open string's, not the sounding harmonic's n times
// smaller one (audit F1). Damped at 1/n of that loss it sounded almost as
// if it were held (2 to 5 dB below it, against the open string's 18).
void testReleasedHarmonicsAreDampedLikeTheirOpenString()
{
    for (const auto material : { acustra::StringMaterial::Steel, acustra::StringMaterial::Nylon })
    {
        acustra::EngineParameters parameters;
        parameters.stringMaterial = material;
        const auto drop = [&] (int note)
        {
            const Timed on { 0.1, { 0x90, static_cast<std::uint8_t>(note), 115 } };
            const Timed off { 0.5, { 0x80, static_cast<std::uint8_t>(note), 64 } };
            return releaseDrop(parameters, { on }, { on, off }, 0.8, 1.0);
        };
        const double open = drop(64);
        std::cout << "Acustra " << materialName(material)
                  << " released/held 0.3-0.5 s after key-up: open E4 -" << open << " dB";
        for (const int note : { 88, 91, 95 })
        {
            const double harmonic = drop(note);
            std::cout << ", harmonic " << note << " -" << harmonic << " dB";
            expect(harmonic > open - 4.0,
                   std::string(materialName(material)) + " released harmonic "
                       + std::to_string(note) + " was only " + std::to_string(harmonic)
                       + " dB below held, against the open string's "
                       + std::to_string(open));
        }
        std::cout << '\n';
    }
}

// A slid note is damped at the pitch it has slid to: the loop runs there,
// so the hand's loss per trip round it follows the slide, before key-up or
// after it (audit F1). Two octaves down redoubles the period; damped per
// trip as if unslid, it rang four times as long. Measured 50 to 100 ms
// after key-up, while the released string still dominates the sound.
void testSlidNotesAreDampedAtTheirSlidPitch()
{
    const std::vector<Timed> range24 {
        { 0.0, { 0xb0, 101, 0 } }, { 0.0, { 0xb0, 100, 0 } }, { 0.0, { 0xb0, 6, 24 } },
        { 0.0, { 0xb0, 101, 127 } }, { 0.0, { 0xb0, 100, 127 } } };
    const auto with = [&] (std::vector<Timed> events)
    {
        auto all = range24;
        all.insert(all.end(), events.begin(), events.end());
        return all;
    };
    for (const auto material : { acustra::StringMaterial::Steel, acustra::StringMaterial::Nylon })
    {
        acustra::EngineParameters parameters;
        parameters.stringMaterial = material;
        const Timed on { 0.1, { 0x90, 69, 100 } };
        const Timed off { 0.5, { 0x80, 69, 64 } };
        const Timed down { 0.3, { 0xe0, 0, 0 } };
        const Timed downAfter { 0.52, { 0xe0, 0, 0 } };
        const double plain = releaseDrop(parameters, with({ on }), with({ on, off }), 0.55, 0.6);
        const double slid = releaseDrop(parameters, with({ on, down }),
                                        with({ on, down, off }), 0.55, 0.6);
        const double slidAfter = releaseDrop(parameters, with({ on, downAfter }),
                                             with({ on, off, downAfter }), 0.55, 0.6);
        std::cout << "Acustra " << materialName(material)
                  << " A4 released/held 50-100 ms after key-up: unslid -" << plain
                  << " dB, slid two octaves down -" << slid << " dB, slid down after key-up -"
                  << slidAfter << " dB\n";
        // Damped per trip as if unslid, the slide drops a quarter to a half
        // as far as the unslid note in this window (steel 6.8 and 12.0 dB
        // against 20.0, nylon 6.9 and 11.8 against 25.4); damped at its slid
        // pitch, 0.7-1.1 of it. The rest is the body: at A2 the default
        // construction's low modes ring on under the damped string, and how
        // long depends on where Shape and Wood put them, so the gate is a
        // fraction of the unslid drop rather than a fixed number of dB.
        expect(slid > 0.65 * plain && slidAfter > 0.65 * plain,
               std::string(materialName(material))
                   + ": a slid note was not damped at its slid pitch");
    }
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
        testReleasedHarmonicsAreDampedLikeTheirOpenString();
        testSlidNotesAreDampedAtTheirSlidPitch();
    }
    return failures == 0 ? 0 : 1;
}
