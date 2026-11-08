#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace acustra
{
enum class GuitarNoise { FretSqueak, FretTap, PalmHit, FistHit, Knock };

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
        if (index >= profiles.size() || !(velocity > 0.0f) || !std::isfinite(velocity))
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
        voice.slide = gesture == GuitarNoise::FretSqueak;
        const float duration = profile.seconds * (1.27f - 0.42f * strength)
            * (1.0f + 0.25f * draw(voice.random));
        voice.contact = std::max(3, static_cast<int>(duration * rateHz + 0.5f));
        voice.end = voice.contact;
        if (profile.rebound > 0.0f)
        {
            const float gap = 0.003f + 0.002f * (1.0f + draw(voice.random));
            voice.secondStart = voice.contact + static_cast<int>(gap * rateHz);
            voice.secondContact = std::max(3, voice.contact / 2);
            voice.secondGain = profile.rebound * (1.0f + 0.6f * draw(voice.random));
            voice.end = voice.secondStart + voice.secondContact;
        }
        const float pressure = level * (1.0f + 0.18f * draw(voice.random));
        voice.body = pressure * profile.body;
        voice.pickup = pressure * profile.pickup;
        voice.air = pressure * profile.air * (1.0f + 0.35f * draw(voice.random));
        voice.arm = profile.arm + 0.018f * draw(voice.random);
        const float centre = std::min(profile.colourHz * (0.56f + 0.65f * strength)
            * (1.0f + 0.23f * draw(voice.random)), 0.17f * rateHz);
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
            const float contact = first + second;
            const float white = draw(voice.random);
            voice.low += voice.slow * (white - voice.low);
            voice.high += voice.fast * (white - voice.high);
            const float rough = voice.noiseGain * (voice.high - voice.low);
            float force = contact * (1.0f + 0.18f * rough);
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
                const float edge = 2.0f * voice.phase - 1.0f;
                const float ridge = 1.5f * edge * edge - 0.5f;
                air = first * (0.65f * rough + 0.35f * ridge);
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
    };
    static constexpr std::array<Profile, 5> profiles {{
        { 0.0980f, 3050.0f, 0.0016f, 0.0022f, 0.0030f, -0.034f, 0.00f }, // squeak
        { 0.0024f, 4050.0f, 0.0100f, 0.0035f, 0.0035f, -0.044f, 0.21f }, // fret tap
        { 0.0082f,  620.0f, 0.0110f, 0.0038f, 0.0015f,  0.036f, 0.00f }, // palm
        { 0.0057f, 1080.0f, 0.0120f, 0.0038f, 0.0022f,  0.018f, 0.23f }, // fist
        { 0.0016f, 3150.0f, 0.0160f, 0.0028f, 0.0048f, -0.019f, 0.36f }, // knock
    }};
    struct Voice
    {
        std::uint64_t random {};
        int age {}, end {}, contact {}, secondStart {}, secondContact {}, channel {};
        float body {}, pickup {}, air {}, arm {}, secondGain {};
        float slow {}, fast {}, low {}, high {}, noiseGain {};
        float phase {}, ridgeRate {}, ridgeSweep {};
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
