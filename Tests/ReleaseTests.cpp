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

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct IntrinsicGainProbe
    {
        std::vector<float> gains;
        float beforeRestart { 0.0f };
        int restartSamples { 0 };
        int finalSamples { 0 };
        float resetGain { 0.0f };
    };
    static IntrinsicGainProbe intrinsicGains(float start, float target,
                                            int period, bool reverse,
                                            bool repeatTarget = true)
    {
        auto loop = std::make_unique<AcustraEngine::StringLoop>();
        loop->currentDelay = loop->targetDelay = static_cast<float>(period);
        loop->setLoopGain(start);
        loop->reset();
        loop->setLoopGain(target, true);
        IntrinsicGainProbe result;
        if (reverse)
        {
            for (int sample = 0; sample < period - 1; ++sample)
                loop->advance(0.0f, 1.0f);
            result.beforeRestart = loop->loopGain;
            loop->setLoopGain(start, true);
            result.restartSamples = loop->loopGainTransitionSamples;
        }
        for (int sample = 0; sample < period; ++sample)
        {
            // The two-pass tuning design may repeat an unchanged target,
            // and continuous control may update it without a fresh event.
            if (repeatTarget)
                loop->setLoopGain(reverse ? start : target, sample % 2 == 0);
            loop->advance(0.0f, 1.0f);
            result.gains.push_back(loop->loopGain);
        }
        result.finalSamples = loop->loopGainTransitionSamples;
        loop->setLoopGain(reverse ? target : start, true);
        loop->advance(0.0f, 1.0f);
        loop->reset();
        result.resetGain = loop->loopGain;
        return result;
    }

    struct TailHandState
    {
        bool active;
        float gain, highLoss, parallelGain, parallelHighLoss, period;
        float currentGain, currentHighLoss;
    };

    static TailHandState tailHandState(const AcustraEngine& engine, int string)
    {
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        return { voice.tailActive, voice.tailLoop.loopGain,
            voice.tailLoop.highLossMix, voice.tailParallelLoop.loopGain,
            voice.tailParallelLoop.highLossMix, voice.tailLoop.targetDelay,
            voice.loops[0].loopGain, voice.loops[0].highLossMix };
    }

    struct DrivenTail { double returnEnergy, suppliedEnergy; };
    // A fixed bridge-motion probe drives the old branch even after its
    // initial wave has gone, just as another held string backdrives it. The
    // returned wave, and the net work that must come from the bridge, are
    // observed independently of the new note or microphone response.
    static DrivenTail drivenTail(const AcustraEngine& engine, int string,
                                 double rate, double frequency, bool captured)
    {
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        auto loop = std::make_unique<AcustraEngine::StringLoop>(voice.tailLoop);
        if (captured)
        {
            loop->loopGain = voice.tailCapturedLoopGain[0];
            loop->highLossMix = voice.tailCapturedHighLoss[0];
        }
        loop->reset();
        DrivenTail result {};
        for (int sample = 0; sample < static_cast<int>(0.25 * rate); ++sample)
        {
            const double bridge = 0.001 * std::sin(
                6.2831853071795864769 * frequency * sample / rate);
            const float incident = loop->advance(engine.delaySmoothing_, voice.tailDamping);
            const float reflected = incident - static_cast<float>(bridge);
            loop->write(reflected);
            if (sample > static_cast<int>(0.15 * rate))
            {
                result.returnEnergy += static_cast<double>(incident) * incident;
                result.suppliedEnergy += static_cast<double>(reflected) * reflected
                                     - static_cast<double>(incident) * incident;
            }
        }
        return result;
    }
};
}

namespace
{
constexpr int blockSize = 127;
// Default host tempo: the ordinary key-up remains a held wave through the
// inclusive 1/32-note endpoint; physical damping starts on the next sample.
int releaseDelaySamples(double rate)
{
    return static_cast<int>(std::ceil(0.125 * 60.0 * rate / 120.0)) + 1;
}

double releaseDelaySeconds(double rate)
{
    return static_cast<double>(releaseDelaySamples(rate)) / rate;
}

double dampingStart(double keyUp, double rate, bool blockQuantised = false)
{
    int sample = static_cast<int>(keyUp * rate);
    if (blockQuantised)
        sample = sample / blockSize * blockSize;
    return static_cast<double>(sample + releaseDelaySamples(rate)) / rate;
}

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

void testIntrinsicLossRetunesStayContinuousAndBounded()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (const int period : { 128, 512, 4093, 8189, 8190 })
        for (const auto endpoints : {
                std::array<float, 2> { 0.70f, 0.999995f },
                std::array<float, 2> { 0.999995f, 0.70f },
                std::array<float, 2> { 0.999815f, 0.999995f } })
        {
            const auto probe = Access::intrinsicGains(
                endpoints[0], endpoints[1], period, false);
            const auto uninterrupted = Access::intrinsicGains(
                endpoints[0], endpoints[1], period, false, false);
            expect(probe.gains == uninterrupted.gains,
                   "unchanged configuration altered an intrinsic-loss transition");
            expect(std::all_of(probe.gains.begin(), probe.gains.end(),
                       [&] (float gain) { return std::isfinite(gain)
                           && gain >= std::min(endpoints[0], endpoints[1])
                           && gain <= std::max(endpoints[0], endpoints[1]); }),
                   "an intrinsic-loss retune left its passive endpoint interval");
            expect(probe.gains.back() == endpoints[1] && probe.finalSamples == 0,
                   "an intrinsic-loss retune did not reach its exact finite target");
            expect(probe.resetGain == endpoints[0],
                   "reset did not settle an in-flight intrinsic-loss target");
        }
    const auto reversed = Access::intrinsicGains(0.70f, 0.99f, 512, true);
    expect(reversed.restartSamples == 512 && reversed.gains.front() > 0.98f,
           "a second discrete retune near the old deadline stepped to its target");
    expect(std::all_of(reversed.gains.begin(), reversed.gains.end(),
               [&] (float gain) { return gain >= 0.70f
                   && gain <= reversed.beforeRestart; })
               && reversed.gains.back() == 0.70f && reversed.finalSamples == 0,
           "an interrupted intrinsic-loss retune did not stay bounded and finish");
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
    // Retain the original 0.8 s of post-damping observation at every rate.
    const double endSeconds = 1.8 + releaseDelaySeconds(rate);
    const auto frames = static_cast<std::size_t>(endSeconds * rate);
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
    to(endSeconds);
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
    const acustra::EngineParameters parameters;
    const auto normal = render(parameters, rate, 43, 127, 64);
    const auto fast = render(parameters, rate, 43, 127, 127);
    const double release = dampingStart(1.0, rate);
    writeWave(directory / "ordinary-release.wav", normal, 48000);
    writeWave(directory / "fast-release.wav", fast, 48000);
    std::cout << "fast/ordinary first-50ms after damping peak="
              << peak(fast, rate, release, release + 0.05)
                 / peak(normal, rate, release, release + 0.05)
              << " tail-energy-ratio=" << energy(fast, rate, release + 0.3, release + 0.8)
                 / energy(normal, rate, release + 0.3, release + 0.8)
              << " bit-identical=" << same(normal, fast) << '\n';
}

void testExplicitLiftSpeedChangesOnlyDamping()
{
    int cases = 0;
    for (const auto shape : { acustra::BodyShape::Parlor, acustra::BodyShape::Auditorium,
                             acustra::BodyShape::Dreadnought, acustra::BodyShape::Jumbo })
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
            for (const int velocity : { 121, 127 })
                for (const int note : { 43, 60, 76 })
                {
                    acustra::EngineParameters parameters;
                    parameters.shape = shape;
                    const auto ordinary = render(parameters, rate, note, velocity, 64);
                    const auto fast = render(parameters, rate, note, velocity, 127);
                    const auto releaseSample = static_cast<int>(rate) + releaseDelaySamples(rate);
                    const std::string label = "case " + std::to_string(++cases)
                        + " note " + std::to_string(note) + " at " + std::to_string(rate);
                    // The coupled body and sympathetic strings can exchange
                    // energy through cancellation; tail RMS is not a direct
                    // measure of the hand's passive loss. ReleaseRealismTests
                    // checks the damping law and string's wave energy itself.
                    expect(!same(ordinary, fast),
                           label + ": explicit release velocity did not change damping");
                    expect(std::equal(ordinary.left.begin(),
                                      ordinary.left.begin() + releaseSample,
                                      fast.left.begin())
                           && std::equal(ordinary.right.begin(),
                                         ordinary.right.begin() + releaseSample,
                                         fast.right.begin()),
                           label + ": release velocity changed audio before damping");
                    expect(fast.activeAtEnd == 0, label + ": a fretted key-up retained note ownership");
                }
    std::cout << "Acustra explicit release damping: " << cases << " cases\n";
}

void testPedalCannotTurnOrdinaryReleaseIntoAnExcitation()
{
    const acustra::EngineParameters parameters;
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

// CC68 was MIDI's Legato Footswitch here until the user asked for legato to
// be removed everywhere (Docs/decisions.md, 2026-09-28). It must now be an
// exact no-op, held through a note and its release at any release speed.
void testCc68ChangesNothing()
{
    for (const int note : { 43, 60 })
    {
        const acustra::EngineParameters parameters;
        for (const int release : { 0, 64, 127 })
            expect(same(render(parameters, 48000.0, note, 100, release),
                        render(parameters, 48000.0, note, 100, release,
                               Gesture::Cc68)),
                   "CC68 changed a note at release velocity "
                       + std::to_string(release));
    }
}

void testHardPluckReleaseDoesNotCreateAnAttack()
{
    double worst = 0.0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int note : { 43, 60, 76 })
        {
            const acustra::EngineParameters parameters;
            const auto held = render(parameters, rate, note, 127, 64, Gesture::Held);
            const auto released = render(parameters, rate, note, 127, 127);
            const double release = dampingStart(1.0, rate);
            const auto releaseSample = static_cast<int>(rate) + releaseDelaySamples(rate);
            expect(std::equal(held.left.begin(), held.left.begin() + releaseSample,
                              released.left.begin())
                   && std::equal(held.right.begin(), held.right.begin() + releaseSample,
                                 released.right.begin()),
                   "maximum-velocity release changed the held audio during grace");
            const double reference = peak(held, rate, release - 0.05, release + 0.005);
            const double ratio = peak(released, rate, release, release + 0.005) / reference;
            worst = std::max(worst, ratio);
            expect(std::isfinite(ratio) && ratio <= 1.05,
                   "maximum-velocity release creates a new onset: ratio " + std::to_string(ratio));
        }
    std::cout << "Acustra maximum-velocity release/held first-5-ms damping peak: " << worst << '\n';
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

// A released natural harmonic is damped by the hand as its open string is:
// the loop runs at the open string's period, so the hand's loss per trip
// round it must be the open string's, not the sounding harmonic's n times
// smaller one (audit F1). Damped at 1/n of that loss it sounded almost as
// if it were held (2 to 5 dB below it, against the open string's 18).
void testReleasedHarmonicsAreDampedLikeTheirOpenString()
{
    const acustra::EngineParameters parameters;
    const auto drop = [&] (int note)
    {
        const Timed on { 0.1, { 0x90, static_cast<std::uint8_t>(note), 115 } };
        const Timed off { 0.5, { 0x80, static_cast<std::uint8_t>(note), 64 } };
        const double release = dampingStart(off.seconds, 48000.0, true);
        return releaseDrop(parameters, { on }, { on, off }, release + 0.3, release + 0.5);
    };
    const double open = drop(64);
    std::cout << "Acustra released/held 0.3-0.5 s after damping: open E4 -" << open << " dB";
    for (const int note : { 88, 91, 95 })
    {
        const double harmonic = drop(note);
        std::cout << ", harmonic " << note << " -" << harmonic << " dB";
        expect(harmonic > open - 4.0,
               "released harmonic " + std::to_string(note) + " was only "
                   + std::to_string(harmonic) + " dB below held, against the open string's "
                   + std::to_string(open));
    }
    std::cout << '\n';
}

// A slid note is damped at the pitch it has slid to: the loop runs there,
// so the hand's loss per trip round it follows the slide, before key-up or
// after it (audit F1). Two octaves down redoubles the period; damped per
// trip as if unslid, it rang four times as long. Measured 50 to 100 ms
// after damping starts, while the released string still dominates the sound.
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
    {
        const acustra::EngineParameters parameters;
        const Timed on { 0.1, { 0x90, 69, 100 } };
        const Timed off { 0.5, { 0x80, 69, 64 } };
        const Timed down { 0.3, { 0xe0, 0, 0 } };
        const double release = dampingStart(off.seconds, 48000.0, true);
        const Timed downAfter { release + 0.02, { 0xe0, 0, 0 } };
        const double plain = releaseDrop(parameters, with({ on }), with({ on, off }),
                                         release + 0.05, release + 0.1);
        const double slid = releaseDrop(parameters, with({ on, down }),
                                        with({ on, down, off }), release + 0.05, release + 0.1);
        const double slidAfter = releaseDrop(parameters, with({ on, downAfter }),
                                             with({ on, off, downAfter }),
                                             release + 0.05, release + 0.1);
        std::cout << "Acustra A4 released/held 50-100 ms after damping: unslid -" << plain
                  << " dB, slid two octaves down -" << slid << " dB, slid down after damping -"
                  << slidAfter << " dB\n";
        // Damped per trip as if unslid, the slide drops a quarter to a half
        // as far as the unslid note in this window (6.8 and 12.0 dB against
        // 20.0); damped at its slid
        // pitch, 0.7-1.1 of it. The rest is the body: at A2 the default
        // construction's low modes ring on under the damped string, and how
        // long depends on where Shape and Wood put them, so the gate is a
        // fraction of the unslid drop rather than a fixed number of dB.
        expect(slid > 0.65 * plain && slidAfter > 0.65 * plain,
               "a slid note was not damped at its slid pitch");
    }
}
} // namespace

// The key-up's own sound (EngineParameters::releaseNoise). At zero, the
// default, every render above is bit-identical to an engine without it. On,
// a key-up adds a soft touch the note's attack towers over, never a new
// onset, and a faster key-up lands firmer; every key-up differs.
void testReleaseNoise()
{
    double quietest = 0.0, loudest = -1000.0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int note : { 43, 60, 76 })
        {
            acustra::EngineParameters off;
            acustra::EngineParameters on;
            on.releaseNoise = 0.5f;
            const auto plain = render(off, rate, note, 100, 64);
            const auto noisy = render(on, rate, note, 100, 64);
            const auto firm = render(on, rate, note, 100, 127);
            const double release = dampingStart(1.0, rate);
            Audio difference { {}, {}, 0 };
            difference.left.resize(plain.left.size());
            difference.right.resize(plain.left.size());
            for (std::size_t i = 0; i < plain.left.size(); ++i)
            {
                difference.left[i] = noisy.left[i] - plain.left[i];
                difference.right[i] = noisy.right[i] - plain.right[i];
            }
            const std::string label = "note " + std::to_string(note) + " at "
                + std::to_string(static_cast<int>(rate));
            expect(peak(difference, rate, 0.0, release) == 0.0,
                   label + ": release noise sounded before damping");
            const double attack = energy(plain, rate, 0.2, 0.25) / 0.05;
            const double touch = energy(difference, rate, release, release + 0.03) / 0.03;
            const double level = decibels(touch / attack);
            quietest = std::min(quietest, level);
            loudest = std::max(loudest, level);
            expect(level < -28.0 && level > -60.0,
                   label + ": release noise at " + std::to_string(level)
                       + " dB re attack");
            const double reference = peak(plain, rate, release - 0.05, release + 0.005);
            expect(peak(firm, rate, release, release + 0.005) / reference <= 1.05,
                   label + ": a firm key-up with release noise made an onset");
            expect(energy(firm, rate, release, release + 0.1)
                       > energy(noisy, rate, release, release + 0.1)
                       || !same(firm, noisy),
                   label + ": release velocity did not reach the release noise");
            expect(peak(noisy, rate, release, release + 0.8) <= peak(plain, rate, 0.2, 1.0),
                   label + ": release noise escaped the note's headroom");
        }
    std::cout << "Acustra release noise 30 ms re attack: " << loudest
              << " to " << quietest << " dB\n";
}

// A released string is handed back to the open string once its hand has
// damped it (returnToOpenString, 0.16 s + 80 ms after fretted damping starts,
// 1.25 s + 80 ms after open damping starts). A string refretted while it rings keeps
// its old wave as a tail, which is a port on the bridge; the hand-back cut
// that port out in one sample, and the impedance step moved the bridge under
// every other string sounding: a faint tick, a few samples wide and 30 dB
// over the local high-frequency floor. Here the low E moves from its
// fifth to seventh fret, is released, and is handed back under held open B and G. The
// output is first band-limited to 20 kHz (a fourth-order Butterworth), so
// at 96 kHz an ultrasonic transient cannot pass for a tick; then the fifth
// difference is a high pass steep enough to leave the strings' own partials
// under a step's. Its peak across the hand-back is measured against its
// median over the 70 ms before, where a peak of noise alone sits 11 to 16 dB
// up and the cut port reached 30 dB at 44.1 and 48 kHz and 40 dB at 96 kHz
// when this was written; cut in one sample on today's bridge it reads 69 dB
// at 44.1 and 48 kHz and 76 dB at 96 kHz.
// The fretted-to-open reconfiguration itself leaves a step of its own: the
// second dispersion section switched out and the delay's 6 ms slew under the
// damped wave, steps a fret change under a sounding wave is allowed to make
// (switchSecondDispersion). Until 2026-10-10 it read 15-21 dB here, but only
// because this low E's former bending factor (0.035, FittedPhysicalData.h)
// took 10-30 dB a round trip off the top band, where both steps sit: in the
// same performance on a6f1ad8 the D string read 25-27 dB and the high E
// 26-29 dB at 44.1 and 48 kHz. With the winding friction's loss angle the low
// E passes them as the plain strings did, 31-39 dB (the D 28-37, the high E
// 24-27), so the bound sits between that and the cut port.
// (Docs/string-hf-loss-2026-10-10.md.)
void lowpass20k(std::vector<float>& x, double rate)
{
    for (const double q : { 0.5411961001461970, 1.3065629648763764 })
    {
        const double w = 2.0 * 3.14159265358979323846 * 20000.0 / rate;
        const double c = std::cos(w), a = std::sin(w) / (2.0 * q);
        const double b0 = 0.5 * (1.0 - c) / (1.0 + a), b1 = 2.0 * b0;
        const double a1 = -2.0 * c / (1.0 + a), a2 = (1.0 - a) / (1.0 + a);
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
        for (auto& sample : x)
        {
            const double y = b0 * sample + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = sample; y2 = y1; y1 = y;
            sample = static_cast<float>(y);
        }
    }
}

void testHandBackIsSilent()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto performer = std::make_unique<acustra::Performer>();
        acustra::EngineParameters parameters;
        parameters.releaseNoise = 0.0f;
        performer->setParameters(parameters);
        performer->prepare(rate, blockSize);
        performer->setParameters(parameters);
        const auto frames = static_cast<std::size_t>(1.4 * rate);
        std::vector<float> left(frames), right(frames);
        struct Event { double at; int kind, channel, note, value; };
        // String per channel: channel 1 is the low E, 4 the G, 5 the B.
        const std::vector<Event> events {
            { 0.0, 0xb0, 1, 126, 6 },
            { 0.1, 0x90, 5, 59, 127 }, { 0.1, 0x90, 4, 55, 127 },
            { 0.2, 0x90, 1, 45, 127 }, { 0.45, 0x80, 1, 45, 64 },
            { 0.5, 0x90, 1, 47, 127 }, { 0.8, 0x80, 1, 47, 64 } };
        std::size_t next = 0;
        int position = 0;
        while (position < static_cast<int>(frames))
        {
            const int count = std::min(blockSize,
                                       static_cast<int>(frames) - position);
            performer->beginBlock(left.data() + position,
                                  right.data() + position, count);
            while (next < events.size()
                   && static_cast<int>(events[next].at * rate) < position + count)
            {
                const auto& event = events[next++];
                const int offset = std::max(0,
                    static_cast<int>(event.at * rate) - position);
                if (event.kind == 0x90)
                    performer->noteOn(offset, event.channel, event.note, event.value);
                else if (event.kind == 0x80)
                    performer->noteOff(offset, event.channel, event.note, event.value);
                else
                    performer->controlChange(offset, event.channel, event.note,
                                             event.value);
            }
            performer->endBlock();
            position += count;
        }
        lowpass20k(left, rate);
        lowpass20k(right, rate);
        double worst = -1000.0;
        for (const auto* channel : { &left, &right })
        {
            const auto& x = *channel;
            const auto fifth = [&] (std::size_t i)
            {
                return std::abs(static_cast<double>(x[i + 5]) - 5.0 * x[i + 4]
                                + 10.0 * x[i + 3] - 10.0 * x[i + 2]
                                + 5.0 * x[i + 1] - x[i]);
            };
            const auto at = [rate] (double seconds)
            {
                return static_cast<std::size_t>(seconds * rate);
            };
            // Preserve the original scan and 70-ms floor relative to the
            // hand-back, now delayed by the ordinary release grace.
            const double delay = releaseDelaySeconds(rate);
            std::vector<double> floor;
            for (std::size_t i = at(0.95 + delay); i < at(1.02 + delay); ++i)
                floor.push_back(fifth(i));
            std::nth_element(floor.begin(), floor.begin()
                + static_cast<std::ptrdiff_t>(floor.size() / 2), floor.end());
            const double median = floor[floor.size() / 2];
            double across = 0.0;
            for (std::size_t i = at(1.02 + delay); i < at(1.10 + delay); ++i)
                across = std::max(across, fifth(i));
            worst = std::max(worst, 20.0 * std::log10(
                across / std::max(median, 1.0e-30)));
        }
        std::cout << "Acustra hand-back step at " << rate << " Hz: " << worst
                  << " dB over the median of the preceding 70 ms\n";
        expect(worst < 50.0, "the hand-back to the open string stepped "
                   + std::to_string(worst) + " dB over the floor at "
                   + std::to_string(static_cast<int>(rate)) + " Hz");
    }
}

// CC2 is the same physical bridge hand on the newly plucked string and on
// its retained wave. In the former implementation captureTail froze the
// copied loop coefficients: changing pressure darkened the new note while
// its old branch kept the pressure that was present at capture.
void testBridgeHandFollowsRetainedTails()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto performer = std::make_unique<acustra::Performer>();
        acustra::EngineParameters parameters;
        parameters.room = 0.0f;
        performer->setParameters(parameters);
        performer->prepare(rate, 64);
        auto& engine = performer->engine();
        engine.setStringPerChannelMode(true);
        std::array<float, 64> left {}, right {};
        const auto run = [&] (double seconds, int controller = -1, int value = 0)
        {
            const int frames = static_cast<int>(seconds * rate);
            for (int at = 0; at < frames; at += 64)
            {
                const int count = std::min(64, frames - at);
                performer->beginBlock(left.data(), right.data(), count);
                if (at == 0 && controller >= 0)
                    performer->controlChange(0, 1, controller, value);
                performer->endBlock();
            }
        };
        engine.noteOn(64, 0.8f, 6);
        run(0.3);
        engine.noteOn(67, 0.8f, 6);
        const auto captured = acustra::AcustraEngineTestAccess::tailHandState(engine, 5);
        run(0.03, 2, 127);
        const auto muted = acustra::AcustraEngineTestAccess::tailHandState(engine, 5);
        expect(captured.active && muted.active, "bridge-hand test retained no old wave");
        expect(muted.currentGain < captured.currentGain
                   && muted.currentHighLoss > captured.currentHighLoss,
               "CC2 test did not reach the current string");
        expect(muted.gain < captured.gain && muted.parallelGain < captured.parallelGain,
               "CC2 left the retained wave's fundamental loss frozen");
        expect(muted.highLoss > captured.highLoss
                   && muted.parallelHighLoss > captured.parallelHighLoss,
               "CC2 left the retained wave's darkening frozen");
        expect(muted.period == captured.period,
               "CC2 retuned a retained wave to the new note's period");
        for (const double frequency : { 330.0, 2000.0, 6000.0 })
        {
            const auto before = acustra::AcustraEngineTestAccess::drivenTail(
                engine, 5, rate, frequency, true);
            const auto after = acustra::AcustraEngineTestAccess::drivenTail(
                engine, 5, rate, frequency, false);
            expect(after.returnEnergy < before.returnEnergy * 0.85,
                   "CC2 did not damp the bridge-driven old wave at "
                       + std::to_string(frequency) + " Hz");
            expect(before.suppliedEnergy >= 0.0 && after.suppliedEnergy >= 0.0,
                   "a bridge-driven retained branch produced net energy");
            std::cout << "Acustra driven tail " << frequency << " Hz at " << rate
                      << ": CC2 return " << 10.0 * std::log10(
                          after.returnEnergy / before.returnEnergy) << " dB\n";
        }
        run(0.06, 2, 0);
        // A fresh bass note keeps moving the bridge while the hand lifts;
        // without a driver the already damped old branch can retire first.
        engine.noteOn(40, 0.9f, 1);
        run(0.19);
        const auto lifted = acustra::AcustraEngineTestAccess::tailHandState(engine, 5);
        expect(lifted.active && lifted.gain == captured.gain
                   && lifted.highLoss == captured.highLoss
                   && lifted.parallelGain == captured.parallelGain
                   && lifted.parallelHighLoss == captured.parallelHighLoss,
               "lifting CC2 did not restore the retained wave's original loss: active="
                   + std::to_string(lifted.active) + ", gain="
                   + std::to_string(lifted.gain) + ", shelf="
                   + std::to_string(lifted.highLoss));
        std::cout << "Acustra retained CC2 at " << rate << " Hz: gain "
                  << captured.gain << " -> " << muted.gain << ", shelf "
                  << captured.highLoss << " -> " << muted.highLoss << '\n';

        performer->prepare(rate, 64);
        engine.setStringPerChannelMode(true);
        run(0.25, 2, 127);
        engine.noteOn(64, 0.8f, 6);
        run(0.008);
        engine.noteOn(67, 0.8f, 6);
        const auto caughtMuted = acustra::AcustraEngineTestAccess::tailHandState(engine, 5);
        run(0.04, 2, 0);
        const auto liftingMuted = acustra::AcustraEngineTestAccess::tailHandState(engine, 5);
        expect(caughtMuted.active && liftingMuted.active
                   && liftingMuted.gain > caughtMuted.gain
                   && liftingMuted.parallelGain > caughtMuted.parallelGain
                   && liftingMuted.highLoss < caughtMuted.highLoss
                   && liftingMuted.parallelHighLoss < caughtMuted.parallelHighLoss,
               "a tail captured under CC2 pressure kept that pressure after the hand lifted");
        expect(liftingMuted.period == caughtMuted.period,
               "lifting a captured mute changed the retained wave's period");
    }

    const acustra::EngineParameters parameters;
    const std::vector<Timed> replucks {
        { 0.1, { 0x90, 40, 110 } }, { 0.4, { 0x90, 40, 110 } } };
    auto zeroPressure = replucks;
    zeroPressure.push_back({ 0.45, { 0xb0, 2, 0 } });
    expect(same(renderEvents(parameters, 0.8, replucks),
                renderEvents(parameters, 0.8, zeroPressure)),
           "zero CC2 changed a complete repluck wave");
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--render")
        renderComparisons(argv[2]);
    else
    {
        testIntrinsicLossRetunesStayContinuousAndBounded();
        testExplicitLiftSpeedChangesOnlyDamping();
        testPedalCannotTurnOrdinaryReleaseIntoAnExcitation();
        testCc68ChangesNothing();
        testHardPluckReleaseDoesNotCreateAnAttack();
        testReleasedHarmonicsAreDampedLikeTheirOpenString();
        testSlidNotesAreDampedAtTheirSlidPitch();
        testReleaseNoise();
        testHandBackIsSilent();
        testBridgeHandFollowsRetainedTails();
    }
    return failures == 0 ? 0 : 1;
}
