// A battery of MIDI performances that exercises every path of the player
// (DSP/AcustraPerformer): single notes, same-sample chords of two to seven
// notes, rolled chords, alternating strums and the rest that restarts them,
// overlapping notes released at every release velocity, sustain,
// bridge-hand and vibrato sweeps, pitch bend under RPN range changes, the MPE
// lower zone, the string-per-channel mode, All Notes/Sound Off and resets, controller
// changes, every construction control switched under a ringing chord (with
// all five tunings) and the edge cases a host can send. PerformerTests plays it through
// the player alone; PluginProcessorTests plays it through the plug-in and
// requires the same samples. JUCE-free.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace acustra::battery
{
struct Event
{
    double seconds { 0.0 };
    std::array<std::uint8_t, 8> bytes {};
    int size { 0 };
    // Added to the event's offset in its block, to place it outside the
    // block the way a careless host can.
    int skew { 0 };
};

// Front-end controls between blocks, applied before the block that contains
// their time.
struct Control
{
    enum class Kind
    {
        GatherChords, // value 0/1
        Panic,
        CaptureMode,    // 0 stereo, 1 mono, 2 piezo
        Picking,        // 0 finger, 1 pick, 2 thumb
        Tuning,         // 0-4
        BodyAmount,     // percent
        Output,         // dB
        // The construction and the other knobs.
        Shape,          // 0 parlor .. 3 jumbo
        Wood,           // 0 spruce, 1 mahogany, 2 maple
        Width,          // percent
        Age,            // percent
        Pluck,          // percent (Pluck Position)
        Touch,          // percent
        PiezoMix        // percent
    };
    double seconds { 0.0 };
    Kind kind { Kind::Panic };
    float value { 0.0f };
};

struct Scenario
{
    const char* name { "" };
    double seconds { 1.0 };
    std::vector<Event> events;
    std::vector<Control> controls;
};

inline int sampleAt(double seconds, double sampleRate)
{
    return static_cast<int>(std::lround(seconds * sampleRate));
}

namespace detail
{
inline Event message(double seconds, std::uint8_t status, int data1, int data2,
                     int size = 3, int skew = 0)
{
    Event event;
    event.seconds = seconds;
    event.bytes[0] = status;
    event.bytes[1] = static_cast<std::uint8_t>(data1 & 0x7f);
    event.bytes[2] = static_cast<std::uint8_t>(data2 & 0x7f);
    event.size = size;
    event.skew = skew;
    return event;
}

inline std::uint8_t status(unsigned kind, int channel)
{
    return static_cast<std::uint8_t>(kind | static_cast<unsigned>(channel - 1));
}

struct Builder
{
    Scenario scenario;

    Builder(const char* name, double seconds)
    {
        scenario.name = name;
        scenario.seconds = seconds;
    }
    Builder& on(double t, int channel, int note, int velocity = 100)
    {
        scenario.events.push_back(message(t, status(0x90u, channel), note, velocity));
        return *this;
    }
    Builder& off(double t, int channel, int note, int releaseVelocity = 64)
    {
        scenario.events.push_back(
            message(t, status(0x80u, channel), note, releaseVelocity));
        return *this;
    }
    // A Note On at velocity zero.
    Builder& offZero(double t, int channel, int note)
    {
        scenario.events.push_back(message(t, status(0x90u, channel), note, 0));
        return *this;
    }
    Builder& cc(double t, int channel, int controller, int value)
    {
        scenario.events.push_back(
            message(t, status(0xb0u, channel), controller, value));
        return *this;
    }
    Builder& bend(double t, int channel, int raw14)
    {
        scenario.events.push_back(
            message(t, status(0xe0u, channel), raw14 & 0x7f, raw14 >> 7));
        return *this;
    }
    Builder& pressure(double t, int channel, int value)
    {
        scenario.events.push_back(
            message(t, status(0xd0u, channel), value, 0, 2));
        return *this;
    }
    Builder& rpn(double t, int channel, int parameter, int msb, int lsb = -1)
    {
        cc(t, channel, 101, parameter >> 7);
        cc(t, channel, 100, parameter & 0x7f);
        cc(t, channel, 6, msb);
        if (lsb >= 0)
            cc(t, channel, 38, lsb);
        return *this;
    }
    Builder& raw(double t, std::initializer_list<std::uint8_t> bytes, int skew = 0)
    {
        Event event;
        event.seconds = t;
        event.skew = skew;
        for (const auto byte : bytes)
            if (event.size < static_cast<int>(event.bytes.size()))
                event.bytes[static_cast<std::size_t>(event.size++)] = byte;
        scenario.events.push_back(event);
        return *this;
    }
    Builder& control(double t, Control::Kind kind, float value = 0.0f)
    {
        scenario.controls.push_back({ t, kind, value });
        return *this;
    }
    // Notes on one sample, inserted in the order given.
    template <std::size_t count>
    Builder& chord(double t, int channel, const std::array<int, count>& notes,
                   int velocity = 96)
    {
        for (const auto note : notes)
            on(t, channel, note, velocity);
        return *this;
    }
    template <std::size_t count>
    Builder& release(double t, int channel, const std::array<int, count>& notes)
    {
        for (const auto note : notes)
            off(t, channel, note);
        return *this;
    }
};
} // namespace detail

inline std::vector<Scenario> makeBattery()
{
    using detail::Builder;
    using Kind = Control::Kind;
    std::vector<Scenario> result;

    {
        Builder b("single-notes", 1.6);
        b.on(0.0, 1, 40, 100).on(0.25, 1, 52, 60).off(0.5, 1, 40, 64)
            .on(0.6, 1, 64, 127).off(0.7, 1, 52, 0).on(0.8, 1, 71, 30)
            .offZero(1.0, 1, 64).raw(1.1, { 0x80, 71 }) // a two-byte Note Off
            .on(1.2, 3, 47, 90).off(1.4, 3, 47, 120);
        result.push_back(std::move(b.scenario));
    }
    {
        // Two to seven notes on one sample, inserted out of order; each
        // change releases the last chord on the new chord's sample.
        Builder b("same-sample-chords", 3.2);
        b.chord(0.0, 1, std::array<int, 2> { 52, 45 });
        b.release(0.5, 1, std::array<int, 2> { 45, 52 })
            .chord(0.5, 1, std::array<int, 3> { 47, 40, 52 }, 110);
        b.release(1.0, 1, std::array<int, 3> { 40, 47, 52 })
            .chord(1.0, 1, std::array<int, 4> { 55, 45, 50, 40 }, 70);
        b.release(1.5, 1, std::array<int, 4> { 40, 45, 50, 55 })
            .chord(1.5, 1, std::array<int, 5> { 59, 40, 55, 45, 50 }, 90);
        b.release(2.0, 1, std::array<int, 5> { 40, 45, 50, 55, 59 })
            .chord(2.0, 1, std::array<int, 6> { 64, 40, 59, 45, 55, 50 }, 120);
        b.release(2.5, 1, std::array<int, 6> { 40, 45, 50, 55, 59, 64 })
            .chord(2.5, 1, std::array<int, 7> { 67, 40, 64, 45, 59, 50, 55 }, 80);
        // Two channels on one sample are no one hand's shape.
        b.chord(2.9, 2, std::array<int, 2> { 57, 62 }).on(2.9, 1, 69, 80);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("rolled-chords", 2.0);
        b.on(0.0, 1, 40).on(0.007, 1, 47).on(0.015, 1, 52).on(0.024, 1, 56);
        b.release(0.7, 1, std::array<int, 4> { 40, 47, 52, 56 });
        b.on(0.8, 1, 45).on(0.812, 1, 52).on(0.835, 1, 57).on(0.85, 1, 61)
            .on(0.851, 1, 64);
        b.on(1.3, 1, 60).on(1.31, 1, 60); // a key repeated inside a window
        result.push_back(std::move(b.scenario));
    }
    {
        // Strums alternate; a rest under two seconds keeps the direction and
        // one over two seconds starts again on a downstroke.
        Builder b("strums", 5.8);
        const std::array<int, 4> eMajor { 56, 40, 52, 47 };
        for (const double t : { 0.0, 0.4, 0.8, 1.2, 3.1, 5.3 })
        {
            b.chord(t, 1, eMajor, t < 1.0 ? 110 : 70);
            b.release(t + 0.35, 1, eMajor);
        }
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("release-velocities", 2.6);
        b.on(0.05, 1, 45, 100).on(0.3, 1, 47, 80)
            .off(0.31, 1, 45, 64).on(0.55, 1, 48, 80).off(0.8, 1, 48, 127)
            .off(1.0, 1, 47, 100);
        b.chord(1.2, 1, std::array<int, 3> { 55, 52, 59 }, 90);
        b.off(1.6, 1, 52, 90).off(1.6, 1, 55, 127).off(1.6, 1, 59, 0);
        b.on(1.9, 1, 57, 100).off(2.2, 1, 57, 127);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("sustain", 2.4);
        b.cc(0.0, 1, 64, 127).on(0.05, 1, 40).off(0.2, 1, 40).on(0.3, 1, 47)
            .off(0.4, 1, 47);
        b.chord(0.5, 1, std::array<int, 3> { 59, 52, 56 });
        b.release(0.7, 1, std::array<int, 3> { 52, 56, 59 });
        b.cc(1.2, 1, 64, 0).on(1.3, 1, 45).cc(1.35, 2, 64, 100).on(1.36, 2, 57)
            .off(1.5, 1, 45).off(1.55, 2, 57).cc(1.8, 2, 64, 10);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("bridge-hand", 2.2);
        for (int step = 0; step <= 400; ++step)
        {
            const int value = step <= 200 ? (step * 127) / 200
                                           : ((400 - step) * 127) / 200;
            b.cc(0.005 * step, 1, 2, value);
        }
        for (int note = 0; note < 8; ++note)
            b.on(0.25 * note + 0.001, 1, 40 + 5 * (note % 4), 100)
                .off(0.25 * note + 0.2, 1, 40 + 5 * (note % 4));
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("vibrato", 2.0);
        b.chord(0.0, 1, std::array<int, 3> { 52, 57, 45 });
        for (int step = 0; step <= 150; ++step)
            b.cc(0.01 * step, 1, 1, (step * 127) / 150);
        b.cc(1.6, 1, 1, 0);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("pitch-bend-rpn", 2.2);
        b.rpn(0.0, 1, 0, 12).on(0.01, 1, 45, 100);
        for (int step = 0; step <= 200; ++step)
        {
            const double phase = static_cast<double>(step) / 200.0;
            const int raw = static_cast<int>(8192.0 + 8191.0 * std::sin(6.283185307179586 * phase));
            b.bend(0.005 * step, 1, raw);
        }
        b.bend(1.0, 1, 0).rpn(1.0, 1, 0, 7, 50).bend(1.05, 1, 16383)
            .bend(1.1, 1, 8192);
        b.cc(1.2, 1, 99, 0).cc(1.2, 1, 98, 0).cc(1.2, 1, 6, 24).bend(1.21, 1, 12000);
        b.cc(1.3, 1, 101, 127).cc(1.3, 1, 100, 127).cc(1.3, 1, 6, 5)
            .bend(1.31, 1, 4000);
        b.rpn(1.4, 2, 0, 1).on(1.41, 2, 57, 90).bend(1.5, 2, 16383)
            .bend(1.6, 2, 0).cc(1.7, 2, 38, 99).bend(1.75, 2, 16383);
        b.off(2.0, 1, 45).off(2.0, 2, 57);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("mpe-lower-zone", 2.5);
        b.rpn(0.0, 1, 6, 5);
        b.cc(0.04, 4, 74, 20);
        b.on(0.05, 2, 40, 100).on(0.05, 3, 47, 90).on(0.05, 4, 52, 80);
        for (int step = 0; step <= 60; ++step)
        {
            b.bend(0.06 + 0.01 * step, 2, 8192 + step * 100);
            b.pressure(0.06 + 0.01 * step, 3, step * 2);
            b.cc(0.06 + 0.01 * step, 4, 74, 127 - step);
        }
        b.bend(0.5, 1, 12000).cc(0.55, 1, 1, 90).rpn(0.7, 3, 0, 12)
            .rpn(0.8, 1, 0, 7).bend(0.85, 1, 3000);
        b.rpn(1.0, 1, 6, 5); // the same layout again resets the ranges
        b.cc(1.1, 1, 64, 127);
        b.cc(1.2, 1, 121, 0);
        b.on(1.3, 5, 55, 100).on(1.3, 6, 59, 100).bend(1.35, 6, 9000);
        b.on(1.6, 2, 64, 100).cc(1.6, 1, 123, 0).on(1.6, 3, 45, 100);
        b.on(1.7, 7, 62, 100); // outside the zone
        b.rpn(1.8, 1, 6, 0).on(1.9, 2, 50, 100).bend(1.95, 2, 16383);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("string-per-channel", 1.8);
        b.cc(0.0, 1, 126, 6);
        for (int channel = 1; channel <= 6; ++channel)
            b.on(0.05, channel, 40 + 5 * (channel - 1) + (channel > 4 ? -1 : 0), 90);
        b.on(0.5, 1, 45, 90).on(0.5, 1, 50, 90).on(0.52, 2, 47, 90)
            .on(0.53, 2, 49, 90);
        b.cc(1.0, 1, 127, 0);
        b.chord(1.1, 1, std::array<int, 3> { 52, 45, 57 });
        result.push_back(std::move(b.scenario));
    }
    {
        // Rolled and interleaved chords around the controllers that end a
        // gathered chord, for the Gather Chords runs.
        Builder b("gather-boundaries", 3.2);
        b.on(0.0, 1, 40).on(0.006, 1, 47).on(0.012, 1, 52).on(0.02, 1, 56)
            .on(0.025, 1, 59);
        b.release(0.45, 1, std::array<int, 5> { 40, 47, 52, 56, 59 });
        b.on(0.5, 1, 45).on(0.51, 1, 50).on(0.515, 1, 45).on(0.52, 1, 55);
        b.on(1.0, 1, 43).on(1.01, 1, 47).on(1.02, 1, 50);
        b.on(1.5, 1, 48).cc(1.505, 1, 64, 127).on(1.51, 1, 52).on(1.52, 1, 55)
            .cc(1.7, 1, 64, 0);
        b.on(1.8, 1, 40).on(1.805, 2, 60).on(1.81, 1, 44).on(1.815, 2, 64);
        b.chord(2.1, 1, std::array<int, 4> { 52, 45, 57, 40 });
        b.on(2.45, 1, 57).on(2.46, 1, 59);
        b.on(2.8, 1, 50).rpn(2.805, 1, 0, 4).on(2.81, 1, 54);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("gather-switch", 1.6);
        b.on(0.0, 1, 45).on(0.01, 1, 52).control(0.02, Kind::GatherChords, 0.0f)
            .on(0.03, 1, 57).control(0.5, Kind::GatherChords, 1.0f)
            .on(0.6, 1, 40).on(0.61, 1, 47).on(0.62, 1, 52)
            .control(0.61, Kind::GatherChords, 0.0f).on(0.9, 1, 55)
            .control(1.0, Kind::GatherChords, 1.0f).on(1.05, 1, 59);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("notes-off-and-panic", 1.8);
        b.chord(0.0, 1, std::array<int, 3> { 40, 47, 52 });
        b.on(0.3, 1, 55).cc(0.3, 1, 123, 0).on(0.3, 1, 59);
        b.cc(0.5, 1, 64, 127).on(0.55, 1, 45).cc(0.6, 1, 120, 0);
        b.on(0.7, 1, 50).control(0.9, Kind::Panic).on(1.0, 1, 52);
        b.bend(1.1, 1, 12000).cc(1.2, 1, 121, 0);
        b.chord(1.3, 1, std::array<int, 3> { 59, 55, 50 });
        b.cc(1.6, 2, 123, 0).cc(1.65, 1, 123, 0);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("host-edge-cases", 1.6);
        b.on(0.0, 1, 45).raw(0.1, { 0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7 });
        b.off(0.2, 1, 52).on(0.2, 1, 52, 100); // zero length, Off first
        b.on(0.3, 1, 57, 80).on(0.3, 1, 57, 110); // one key twice
        b.off(0.4, 1, 62);                        // never pressed
        b.raw(0.45, { 0xc0, 5 }).raw(0.46, { 0xa0, 45, 60 }).pressure(0.47, 1, 90);
        b.on(0.5, 1, 40, 90);
        b.scenario.events.back().skew = -5; // before its block
        b.on(0.6, 1, 47, 90);
        b.scenario.events.back().skew = 100000; // after its block
        // 130 Note Ons on one sample overflow the group; 130 Note Offs too.
        for (int index = 0; index < 130; ++index)
            b.on(0.8, 1 + index / 100, 20 + index % 100, 70);
        for (int index = 0; index < 130; ++index)
            b.off(1.0, 1 + index / 100, 20 + index % 100);
        b.raw(1.2, { 0xf8 }).raw(1.2, { 0x90, 64 }).on(1.25, 1, 64);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("controls", 2.0);
        b.chord(0.0, 1, std::array<int, 3> { 45, 52, 57 });
        b.control(0.5, Kind::PiezoMix, 60.0f).on(0.55, 1, 64)
            .control(0.8, Kind::CaptureMode, 2.0f).on(0.85, 1, 59)
            .control(1.0, Kind::Picking, 1.0f).on(1.05, 1, 50)
            .control(1.2, Kind::BodyAmount, 20.0f)
            .control(1.3, Kind::Output, -3.0f)
            .control(1.5, Kind::Tuning, 1.0f).on(1.55, 1, 38);
        result.push_back(std::move(b.scenario));
    }
    {
        // Every construction control switched under a ringing chord, with
        // a note after each, and the two tunings nothing else plays.
        Builder b("construction", 3.0);
        b.chord(0.0, 1, std::array<int, 3> { 45, 52, 57 });
        b.control(0.2, Kind::Shape, 0.0f).on(0.25, 1, 64)
            .control(0.4, Kind::Wood, 2.0f).on(0.45, 1, 59)
            .on(0.85, 1, 50)
            .control(1.1, Kind::Width, 0.0f).on(1.15, 1, 57)
            .control(1.3, Kind::Age, 90.0f).on(1.35, 1, 62)
            .control(1.5, Kind::Pluck, 5.0f).on(1.55, 1, 60)
            .control(1.7, Kind::Touch, 100.0f).on(1.75, 1, 52)
            .control(1.9, Kind::Tuning, 3.0f)
            .chord(1.95, 1, std::array<int, 3> { 38, 43, 50 })
            .control(2.3, Kind::Shape, 3.0f)
            .control(2.4, Kind::Tuning, 4.0f)
            .chord(2.45, 1, std::array<int, 3> { 39, 44, 49 })
            .control(2.7, Kind::Wood, 1.0f).on(2.75, 1, 63);
        result.push_back(std::move(b.scenario));
    }
    return result;
}
} // namespace acustra::battery
