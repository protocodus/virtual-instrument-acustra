#pragma once

// The portfolio's output stage: an optional guard compressor, then a
// stereo-linked look-ahead peak limiter that keeps every output sample at or
// under -1.5 dBFS. While neither needs to act, the stage passes its delayed
// input bit for bit.
//
// The master is Tools/portfolio/Source/DSP/OutputStage.h in
// reason-rack-extensions. A product that uses it carries it at
// Source/DSP/OutputStage.h unchanged (check_repo_standard.py, shared-copies),
// so every instrument limits alike. A product calibrates typical playing
// below the stage, so that only its hardest playing reaches it.
//
// The limiter: each frame's needed gain (ceiling over its peak, or 1) is held
// at the lowest within the window, released exponentially toward 1, then
// averaged over the same window, and the input is delayed by one sample less
// than the window. The gain therefore ramps down across the window before a
// peak and has reached that peak's gain, or less, as the peak leaves the
// delay (Signalsmith Audio, "Designing a straightforward limiter", 2022),
// with no gain step anywhere: no distortion beyond the slow gain change.
//
// The compressor keeps the hardest playing out of the red of a VU meter,
// which a peak limiter alone cannot do for an instrument whose loud chords
// are dense: it acts on power over tens of milliseconds, not on peaks. Its
// soft knee is Giannoulis, Massberg and Reiss, "Digital Dynamic Range
// Compressor Design", JAES 60(6), 2012.
//
// JUCE-free C++17 with no allocation. Process() calls the maths library only
// once every kControlPeriod frames, and only while the compressor is over its
// knee, so it suits the Rack toolchain's -fno-builtin; Prepare() calls it.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace protocodus {

// The compressor in front of the limiter; the default is none. It reads the
// louder channel's power, as Reason's channel meter does, smoothed over
// averageSeconds, then follows it with attackSeconds and releaseSeconds (0
// is unsmoothed or instant). The knee, kneeDb wide, is centred at
// thresholdDb of mean square (dBFS; 0 VU is -12 dBFS on Reason's default
// meter). A ratio of 1 is no compressor.
struct CompressorSettings {
    double thresholdDb = 0.0;
    double kneeDb = 0.0;
    double ratio = 1.0;
    double averageSeconds = 0.0;
    double attackSeconds = 0.0;
    double releaseSeconds = 0.0;
};

// The guard for a plucked instrument calibrated to -9 VU typical: power read
// over the meter's own 300 ms, and a knee 16 dB wide, from -11 to +5 VU on
// Reason's meter, at 10:1. Typical playing loses about 0.1 dB, every step in
// velocity still plays louder, and on Aculele and Aculyre the hardest chords
// stay at or under -1.4 VU, out of the red. A detector of 100 ms or less reads
// each pluck's attack louder than the meter does: with one, Aculele's
// velocity-127 chords read at most 0.5 dB over its 110 ones.
inline constexpr CompressorSettings kVuGuard { -15.0, 16.0, 10.0, 0.3, 0.01, 0.3 };

template <std::size_t Channels>
class OutputStage {
public:
    static constexpr float kCeiling = 0.84139514f; // -1.5 dBFS
    static constexpr double kReleaseSeconds = 0.08;
    // The longest delay: 2 ms at 768 kHz, the 4x rate of a 192 kHz host.
    static constexpr int kMaximumDelay = 1536;
    // Frames between compressor gain updates; the gain ramps linearly
    // between them.
    static constexpr int kControlPeriod = 16;

    // The look-ahead at a rate: 2 ms, rounded down to a multiple of 4
    // samples. Rounded down, it is the host rate's look-ahead times the
    // factor at 2x and 4x of every standard rate (each 44.1 kHz multiple's
    // 2 ms is under a quarter sample past a whole one), so the latency a
    // wrapper reports, DelayFor(rate * factor) / factor, is the same at every
    // factor.
    [[nodiscard]] static int DelayFor(double sampleRate) noexcept {
        return std::clamp(4 * static_cast<int>(sampleRate / 2000.0), 4, kMaximumDelay);
    }

    void Prepare(double sampleRate, const CompressorSettings& compressor = {}) noexcept {
        delay_ = DelayFor(sampleRate);
        window_ = delay_ + 1;
        decay_ = static_cast<float>(std::exp(-1.0 / (kReleaseSeconds * sampleRate)));
        compressing_ = compressor.ratio > 1.0;
        slope_ = 1.0 - 1.0 / std::max(compressor.ratio, 1.0);
        thresholdDb_ = compressor.thresholdDb;
        kneeDb_ = std::max(compressor.kneeDb, 0.0);
        kneeStart_ = static_cast<float>(std::pow(10.0, (thresholdDb_ - 0.5 * kneeDb_) / 10.0));
        const auto follow = [sampleRate](double seconds) {
            return seconds > 0.0 ? static_cast<float>(1.0 - std::exp(-1.0 / (seconds * sampleRate))) : 1.0f;
        };
        average_ = follow(compressor.averageSeconds);
        attack_ = follow(compressor.attackSeconds);
        release_ = follow(compressor.releaseSeconds);
        Reset();
    }

    void Reset() noexcept {
        averaged_ = envelope_ = 0.0f;
        from_ = to_ = 1.0f;
        phase_ = 0;
        for (auto& channel : delayed_)
            channel.fill(0.0f);
        gains_.fill(1.0f);
        heldFirst_ = heldCount_ = 0;
        reduction_ = 0.0f;
        sum_ = static_cast<double>(window_);
        reduced_ = 0;
        frame_ = 0;
        delayIndex_ = windowIndex_ = 0;
    }

    [[nodiscard]] int DelaySamples() const noexcept { return delay_; }

    // Processes one frame in place, returning it DelaySamples() late.
    void Process(std::array<float, Channels>& frame) noexcept {
        if (compressing_)
            Compress(frame);
        Limit(frame);
    }

private:
    void Compress(std::array<float, Channels>& frame) noexcept {
        float power = 0.0f;
        for (const float sample : frame)
            power = std::max(power, sample * sample);
        averaged_ += (power - averaged_) * average_;
        envelope_ += (averaged_ - envelope_) * (averaged_ > envelope_ ? attack_ : release_);
        // After the input stops both decay toward 0 through subnormals,
        // which are slow without flush-to-zero; 1e-30 is 300 dB down.
        if (averaged_ < 1.0e-30f)
            averaged_ = 0.0f;
        if (envelope_ < 1.0e-30f)
            envelope_ = 0.0f;
        if (phase_ == 0) {
            from_ = to_;
            to_ = envelope_ > kneeStart_ ? Gain(envelope_) : 1.0f;
        }
        ++phase_;
        const float gain = phase_ == kControlPeriod
            ? to_ : from_ + (to_ - from_) * (static_cast<float>(phase_) / static_cast<float>(kControlPeriod));
        if (phase_ == kControlPeriod)
            phase_ = 0;
        if (gain != 1.0f)
            for (float& sample : frame)
                sample *= gain;
    }

    [[nodiscard]] float Gain(float power) const noexcept {
        const double over = 10.0 * std::log10(static_cast<double>(power)) - thresholdDb_;
        const double reduction = kneeDb_ > 0.0 && over < 0.5 * kneeDb_
            ? slope_ * (over + 0.5 * kneeDb_) * (over + 0.5 * kneeDb_) / (2.0 * kneeDb_)
            : slope_ * over;
        return static_cast<float>(std::pow(10.0, -0.05 * std::max(reduction, 0.0)));
    }

    void Limit(std::array<float, Channels>& frame) noexcept {
        float peak = 0.0f;
        for (const float sample : frame)
            peak = std::max(peak, sample < 0.0f ? -sample : sample);
        const float need = peak > kCeiling ? kCeiling / peak : 1.0f;

        // The lowest need in the window: a queue of rising needs, each with
        // the frame it entered on.
        while (heldCount_ > 0 && held_[(heldFirst_ + heldCount_ - 1) & kMask].need >= need)
            --heldCount_;
        held_[(heldFirst_ + heldCount_) & kMask] = { need, frame_ };
        ++heldCount_;
        if (held_[heldFirst_].frame + static_cast<std::uint64_t>(window_) <= frame_) {
            heldFirst_ = (heldFirst_ + 1) & kMask;
            --heldCount_;
        }
        const float hold = held_[heldFirst_].need;

        // Immediate attack, exponential release. The release decays the
        // reduction, 1 minus the gain, rather than raising the gain toward 1:
        // near 1, a float gain's step would fall under its rounding and stall
        // short of 1. A reduction under 1e-6 is none.
        float released = reduction_ * decay_;
        if (released < 1.0e-6f)
            released = 0.0f;
        reduction_ = std::max(1.0f - hold, released);
        const float gain = 1.0f - reduction_;

        // The window's mean, exactly 1 while every gain in it is.
        const float leaving = gains_[static_cast<std::size_t>(windowIndex_)];
        gains_[static_cast<std::size_t>(windowIndex_)] = gain;
        windowIndex_ = windowIndex_ + 1 == window_ ? 0 : windowIndex_ + 1;
        reduced_ += (gain < 1.0f ? 1 : 0) - (leaving < 1.0f ? 1 : 0);
        sum_ += static_cast<double>(gain) - static_cast<double>(leaving);
        if (reduced_ == 0)
            sum_ = static_cast<double>(window_);
        const float applied = reduced_ == 0
            ? 1.0f : std::min(1.0f, static_cast<float>(sum_ / static_cast<double>(window_)));

        for (std::size_t channel = 0; channel < Channels; ++channel) {
            const float out = delayed_[channel][static_cast<std::size_t>(delayIndex_)];
            delayed_[channel][static_cast<std::size_t>(delayIndex_)] = frame[channel];
            frame[channel] = applied == 1.0f ? out : applied * out;
        }
        delayIndex_ = delayIndex_ + 1 == delay_ ? 0 : delayIndex_ + 1;
        ++frame_;
    }

    static constexpr std::size_t kMask = 2047; // a power of two above kMaximumDelay + 1
    struct Held {
        float need;
        std::uint64_t frame;
    };

    // The compressor.
    bool compressing_ = false;
    double slope_ = 0.0, thresholdDb_ = 0.0, kneeDb_ = 0.0;
    float kneeStart_ = 1.0f, average_ = 1.0f, attack_ = 1.0f, release_ = 1.0f;
    float averaged_ = 0.0f, envelope_ = 0.0f, from_ = 1.0f, to_ = 1.0f;
    int phase_ = 0;

    // The limiter.
    std::array<std::array<float, kMaximumDelay>, Channels> delayed_ {};
    std::array<Held, kMask + 1> held_ {};
    std::size_t heldFirst_ = 0, heldCount_ = 0;
    std::array<float, kMaximumDelay + 1> gains_ {};
    float reduction_ = 0.0f;
    double sum_ = 2.0;
    int reduced_ = 0;
    std::uint64_t frame_ = 0;
    int delayIndex_ = 0, windowIndex_ = 0;
    int delay_ = 1, window_ = 2;
    float decay_ = 0.0f;
};
} // namespace protocodus
