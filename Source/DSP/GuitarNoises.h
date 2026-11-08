#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace acustra
{
enum class GuitarNoise
{
    FretMove, PalmKnock, FingerKnock, FistKnock, FingersSlap, PalmSlap, Count
};

// Short external contacts on the guitar. These drive the existing body and
// pickup observers. They do not own a string or feed energy back into its
// travelling waves. Forces and lever arms are authored gesture profiles.
class GuitarNoises
{
public:
    struct Drive
    {
        float force {}, moment {}, pickup {}, air {}, airSide {};
    };

    void restart() noexcept { seed_ = initialSeed; }
    void clear() noexcept { voices_ = {}; active_ = 0; }
    [[nodiscard]] bool active() const noexcept { return active_ != 0; }

    void stopChannels(int firstChannel, int lastChannel) noexcept
    {
        for (auto& voice : voices_)
            if (voice.remaining() > 0
                && voice.channel >= firstChannel && voice.channel <= lastChannel)
            {
                voice = {};
                --active_;
            }
    }

    void trigger(GuitarNoise gesture, float velocity, int channel, double rate) noexcept
    {
        const auto index = static_cast<std::size_t>(gesture);
        if (index >= profiles.size() || !(velocity > 0.0f) || !std::isfinite(velocity)
            || !std::isfinite(rate) || rate < 8000.0 || rate > 768000.0)
            return;
        const auto& profile = profiles[index];
        const float strength = std::min(velocity, 1.0f);
        const float level = strength * (0.35f + 0.65f * strength);
        const float rateHz = static_cast<float>(rate);
        auto& voice = *std::min_element(voices_.begin(), voices_.end(),
            [](const Voice& a, const Voice& b) { return a.remaining() < b.remaining(); });
        if (voice.remaining() == 0)
            ++active_;

        // One event seed per hit; later hits depend on neither block sizes
        // nor which captures are connected. Each voice has its own texture
        // stream. No sample or finite bank of recorded hits is selected.
        seed_ += 0x9e3779b97f4a7c15ULL;
        auto event = seed_;
        event = (event ^ (event >> 30)) * 0xbf58476d1ce4e5b9ULL;
        event = (event ^ (event >> 27)) * 0x94d049bb133111ebULL;
        event ^= event >> 31;
        voice = {};
        voice.random = event != 0 ? event : initialSeed;
        voice.channel = channel;
        voice.slide = profile.slide;
        voice.roughness = profile.roughness;
        const float duration = profile.seconds
            * (voice.slide ? 1.15f - 0.80f * strength : 1.27f - 0.42f * strength)
            * (1.0f + 0.12f * draw(voice.random));
        // For a similar distance of finger travel, crossing winding ridges
        // faster raises both their frequency and the friction-noise RMS.
        const float travelSpeed = voice.slide ? 0.098f / duration : 1.0f;
        voice.contact = std::max(3, static_cast<int>(duration * rateHz + 0.5f));
        voice.end = voice.contact;
        if (profile.rebound > 0.0f)
        {
            const float gap = 0.003f + 0.002f * (1.0f + draw(voice.random));
            voice.secondStart = voice.contact + static_cast<int>(gap * rateHz);
            voice.secondContact = std::max(3, voice.contact / 2);
            voice.secondGain = profile.rebound * (1.0f + 0.25f * draw(voice.random));
            voice.end = voice.secondStart + voice.secondContact;
        }
        // A finger slap is several fingertips arriving at different times.
        // A palm slap rolls from its heel toward the finger pads. These
        // finite contacts share a hand pressure but vary their timing and
        // force independently, rather than replaying a recorded transient.
        for (int finger = 0; finger < profile.extraContacts; ++finger)
        {
            auto& extra = voice.extra[static_cast<std::size_t>(finger)];
            extra.start = static_cast<int>(rateHz * profile.spreadSeconds
                * (static_cast<float>(finger) + 0.8f + 0.10f * draw(voice.random)));
            extra.samples = std::max(3, static_cast<int>(voice.contact
                * (0.62f + 0.08f * draw(voice.random))));
            extra.gain = (0.55f - 0.10f * static_cast<float>(finger))
                * (1.0f + 0.10f * draw(voice.random));
            voice.end = std::max(voice.end, extra.start + extra.samples);
        }
        // Keep random force/geometry variation smaller than ordinary
        // velocity steps. Contact texture still varies sample by sample.
        const float pressure = level * travelSpeed * (1.0f + 0.08f * draw(voice.random));
        voice.body = pressure * profile.body;
        voice.pickup = pressure * profile.pickup;
        voice.air = pressure * profile.air * (1.0f + 0.16f * draw(voice.random));
        voice.arm = profile.arm + 0.008f * draw(voice.random);
        const float centre = std::min(profile.colourHz
            * (voice.slide ? travelSpeed : 0.56f + 0.65f * strength)
            * (1.0f + 0.13f * draw(voice.random)), 0.17f * rateHz);
        // Difference of two first-order lowpasses, with the impulse energy
        // divided out so changing the host rate does not change force RMS.
        voice.slow = pole(0.48f * centre, rate);
        voice.fast = pole(2.0f * centre, rate);
        const double a = voice.slow, b = voice.fast;
        const double energy = a / (2.0 - a) + b / (2.0 - b)
            - 2.0 * a * b / (a + b - a * b);
        voice.noiseGain = static_cast<float>(std::sqrt(3.0 / energy));
        voice.phase = 0.5f + 0.5f * draw(voice.random);
        voice.ridgeRate = centre / rateHz;
        voice.ridgeSweep = 0.35f * draw(voice.random);
        for (std::size_t partial = 0; partial < voice.ridgeWeights.size(); ++partial)
        {
            const float harmonic = static_cast<float>(partial + 1);
            const float maximumFrequency = harmonic * voice.ridgeRate
                * (1.0f + std::abs(voice.ridgeSweep));
            const float available = std::clamp((0.45f - maximumFrequency) / 0.10f,
                                               0.0f, 1.0f);
            voice.ridgeWeights[partial] = available / (harmonic * harmonic);
        }
    }

    [[nodiscard]] Drive render() noexcept
    {
        Drive result {};
        for (auto& voice : voices_)
        {
            if (voice.remaining() == 0)
                continue;
            const int age = voice.age++;
            const float first = pulse(age, voice.contact);
            const float second = voice.secondGain * pulse(age - voice.secondStart,
                                                          voice.secondContact);
            float contact = first + second;
            for (const auto& extra : voice.extra)
                if (extra.samples > 0)
                    contact += extra.gain * pulse(age - extra.start, extra.samples);
            const float white = draw(voice.random);
            voice.low += voice.slow * (white - voice.low);
            voice.high += voice.fast * (white - voice.high);
            const float rough = voice.noiseGain * (voice.high - voice.low);
            float force = contact * std::max(0.0f, 1.0f + voice.roughness * rough);
            float air = contact * rough;
            if (voice.slide)
            {
                // A pad traversing wound-string ridges. An acceleration
                // contour and per-stroke speed sweep move the ridge spacing.
                const float speed = 0.62f + 0.38f * first;
                const float sweep = 1.0f + voice.ridgeSweep
                    * (2.0f * static_cast<float>(age) / static_cast<float>(voice.contact) - 1.0f);
                voice.phase += voice.ridgeRate * speed * sweep;
                if (voice.phase >= 1.0f)
                    voice.phase -= 1.0f;
                // Finite Fourier approximation to a rounded winding ridge.
                // Exclude harmonics above the available bandwidth, including
                // the entire speed sweep, so fast movement cannot fold a
                // sharp periodic edge into lower frequencies at low rates.
                const float cosine = std::cos(6.283185307179586f * voice.phase);
                const float cosine2 = 2.0f * cosine * cosine - 1.0f;
                const float cosine3 = (4.0f * cosine * cosine - 3.0f) * cosine;
                const float ridge = 0.60792710185f * (voice.ridgeWeights[0] * cosine
                    + voice.ridgeWeights[1] * cosine2 + voice.ridgeWeights[2] * cosine3);
                air = contact * (0.65f * rough + 0.35f * ridge);
                force = air;
            }
            const float heave = voice.body * force;
            const float direct = voice.air * air;
            result.force += heave;
            result.moment += heave * voice.arm;
            result.pickup += voice.pickup * (voice.slide ? air : force + 0.2f * air);
            result.air += direct;
            result.airSide += direct * (4.0f * voice.arm);
            if (voice.remaining() == 0)
                --active_;
        }
        return result;
    }

private:
    struct Profile
    {
        float seconds, colourHz, body, pickup, air, arm, rebound;
        bool slide;
        int extraContacts;
        float spreadSeconds, roughness;
    };
    // Position is a bounded effective force/moment lever arm into the
    // existing body model, not a measured two-dimensional tap-point map.
    // Winding-ridge rate rises with sliding speed; a slower pass lasts
    // longer and is quieter for comparable finger pressure and travel.
    // See Pakarinen, Penttinen & Bank (2007), "Analysis of handling noises
    // on wound strings". Contact/air levels here are authored, not fitted
    // measurements. Fretting gestures deliberately sit below body strikes.
    static constexpr std::array<Profile, static_cast<std::size_t>(GuitarNoise::Count)> profiles {{
        { 0.2800f, 3050.0f, 0.00048f, 0.00066f, 0.00090f, -0.034f, 0.00f, true,  0, 0.0000f, 0.18f }, // quiet winding movement
        { 0.0082f,  620.0f, 0.0110f, 0.0038f, 0.0015f,  0.036f, 0.00f, false, 0, 0.0000f, 0.18f }, // palm knock, lower bout
        { 0.0016f, 3150.0f, 0.0160f, 0.0028f, 0.0048f, -0.019f, 0.36f, false, 0, 0.0000f, 0.18f }, // finger knock, upper bout
        { 0.0057f, 1080.0f, 0.0100f, 0.0030f, 0.0022f,  0.075f, 0.23f, false, 0, 0.0000f, 0.18f }, // fist knock, side
        { 0.0018f, 4400.0f, 0.0080f, 0.0022f, 0.0040f, -0.055f, 0.00f, false, 3, 0.0026f, 0.42f }, // finger slap, upper bout
        { 0.0032f, 2650.0f, 0.0075f, 0.0045f, 0.0028f,  0.052f, 0.00f, false, 1, 0.0024f, 0.36f }, // palm slap, lower bout
    }};
    struct Contact { int start {}, samples {}; float gain {}; };
    struct Voice
    {
        std::uint64_t random {};
        int age {}, end {}, contact {}, secondStart {}, secondContact {}, channel {};
        float body {}, pickup {}, air {}, arm {}, secondGain {};
        float slow {}, fast {}, low {}, high {}, noiseGain {};
        float phase {}, ridgeRate {}, ridgeSweep {};
        std::array<float, 3> ridgeWeights {};
        float roughness {};
        std::array<Contact, 3> extra {};
        bool slide {};
        [[nodiscard]] int remaining() const noexcept { return end - age; }
    };

    static float pole(float frequency, double rate) noexcept
    {
        return static_cast<float>(1.0 - std::exp(-6.283185307179586 * frequency / rate));
    }
    static float pulse(int age, int samples) noexcept
    {
        if (age < 0 || age >= samples)
            return 0.0f;
        const float phase = static_cast<float>(age) / static_cast<float>(samples - 1);
        const float half = 4.0f * phase * (1.0f - phase);
        return half * half;
    }
    static float draw(std::uint64_t& state) noexcept
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        const auto value = (state * 0x2545f4914f6cdd1dULL) >> 40;
        return static_cast<float>(value) / 8388608.0f - 1.0f;
    }
    static constexpr std::uint64_t initialSeed = 0x6a09e667f3bcc909ULL;
    std::uint64_t seed_ { initialSeed };
    std::array<Voice, 12> voices_ {};
    int active_ {};
};
}
