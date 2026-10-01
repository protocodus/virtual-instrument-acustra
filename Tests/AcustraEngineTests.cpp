#include "DSP/AcustraEngine.h"
#include "DSP/ConstructionLoudnessData.h"
#include "DSP/MeasuredBodyData.h"
#include "DSP/MeasuredBridgeData.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    // Empties every string's parallel-polarisation loop, as if the pluck had
    // put nothing in that plane.
    static void silenceParallelPolarisation(AcustraEngine& engine)
    {
        for (auto& voice : engine.voices_)
            voice.loops[1].reset();
    }
    static float highLossMix(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)].loops[0].highLossMix;
    }
    static std::uint32_t pluckDrawState(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)].randomState;
    }
    static float bridgeDisplacement(const AcustraEngine& engine)
    {
        return engine.bridgeLoad_.displacement;
    }
    // The body's left microphone for a sinusoidal bridge force at `f`,
    // through the engine's own force derivative, after half a second.
    static double bodyResponse(const AcustraEngine& engine, double f)
    {
        auto bank = engine.bodyBank_;
        bank.reset();
        AcustraEngine::FixedDerivative derivative;
        derivative.reset();
        const double rate = engine.sampleRate_;
        const auto ratio = static_cast<float>(rate / 48000.0);
        const int settle = static_cast<int>(0.5 * rate);
        const int length = static_cast<int>(0.2 * rate);
        std::complex<double> sum {};
        for (int i = 0; i < settle + length; ++i)
        {
            const double phase = 2.0 * std::numbers::pi * f * i / rate;
            const auto out = bank.render(
                derivative.process(static_cast<float>(std::sin(phase)), ratio), 0.0f);
            if (i >= settle)
                sum += static_cast<double>(out.left) * std::polar(1.0, -phase);
        }
        return std::abs(sum) * 2.0 / length;
    }
    // The bridge's own decay of a string's partial at f, in dB per second:
    // -20 log10 |R(f)| per period of the string's fundamental f0.
    static double bridgeDecay(const AcustraEngine& engine, double f, double f0, int string)
    {
        const auto y = engine.bridgePortMobility(static_cast<float>(f), string).normal;
        const std::complex<double> mobility(y.real(), y.imag());
        const double port = 1.0
            / engine.voices_[static_cast<std::size_t>(string)].characteristicImpedance;
        return -20.0 * std::log10(std::abs((port - mobility) / (port + mobility))) * f0;
    }
    // The normal plane's kink as a share of the string's period, and the
    // pluck point it was asked for (with the plane's -0.006 offset).
    static std::pair<double, double> kinkOnTheString(const AcustraEngine& engine, int string)
    {
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        const int length = std::clamp(static_cast<int>(std::round(voice.loops[0].targetDelay)),
                                      8, AcustraEngine::maximumDelaySamples - 3);
        return { static_cast<double>(voice.releaseShapePosition[0]) * length
                     / voice.contactPeriodSamples,
                 static_cast<double>(voice.pluckPoint) - 0.006 };
    }
    static bool idleFlushed(const AcustraEngine& engine)
    {
        return engine.idleFlushed_;
    }
    static float saddleHeightRatio(const AcustraEngine& engine)
    {
        return engine.saddleHeightRatio();
    }

    // Gives `to` the smoothed output levels `from` has reached (the
    // construction loudness references), so a test of body-bank scheduling
    // compares the banks alone and not two level glides' histories.
    // The output levels one engine has reached, with the microphone
    // samples already scaled by them and held for the piezo's pipeline
    // (outputLatencySamples): both are the output stage's, not the body's.
    static void copyOutputLevels(const AcustraEngine& from, AcustraEngine& to)
    {
        to.outputReference_ = from.outputReference_;
        to.monoReference_ = from.monoReference_;
        to.piezoTrim_ = from.piezoTrim_;
        to.micDelayLeft_ = from.micDelayLeft_;
        to.micDelayRight_ = from.micDelayRight_;
        to.micDelayMono_ = from.micDelayMono_;
        to.micDelayIndex_ = from.micDelayIndex_;
    }

    static void invalidateDispersionSolveCache(AcustraEngine& engine)
    {
        for (auto& voice : engine.voices_)
            voice.dispersionDesignArguments.fill(0.0);
        for (auto& solve : engine.dispersionSolves_)
            solve.valid = false;
    }

    // Generation 0 matches no key, so every configureVoice runs in full.
    static void invalidateVoiceConfigurations(AcustraEngine& engine)
    {
        for (auto& voice : engine.voices_)
            voice.configurationKey.generation = 0;
    }

    struct StringLoopSnapshot
    {
        double delay;
        double loopGain;
        double broadCoefficient;
        double broadMix;
        double highCoefficient;
        double highMix;
        double dispersionA1;
        double dispersionA2;
        double inharmonicity;
        double sampleRate { 48000.0 };
        // The string's own bending-loss section (unit gain at DC).
        double bendingGain { 1.0 };
        double bendingA1 { 0.0 };
        double bendingA2 { 0.0 };
        // The dispersion's second allpass section, where the loop runs one.
        bool secondDispersionActive { false };
        double secondDispersionA1 { 0.0 };
        double secondDispersionA2 { 0.0 };
    };

    // Completes a snapshot with the loop's sections that the positional
    // fields above leave out: its bending loss and its second dispersion
    // section.
    static StringLoopSnapshot withBendingLoss(StringLoopSnapshot snapshot,
                                              const AcustraEngine::StringLoop& loop)
    {
        snapshot.secondDispersionActive = loop.secondDispersionActive;
        snapshot.secondDispersionA1 = loop.secondDispersionA1;
        snapshot.secondDispersionA2 = loop.secondDispersionA2;
        if (loop.bendingLossActive)
        {
            snapshot.bendingGain = loop.bendingLossGain;
            snapshot.bendingA1 = loop.bendingLossA1;
            snapshot.bendingA2 = loop.bendingLossA2;
        }
        return snapshot;
    }

    struct BodyModeSnapshot
    {
        double frequency;
        double q;
        double residue;
    };

    struct ReleasedContactSnapshot
    {
        std::vector<double> history;
        double position, aperture, gain;
        // The release's slip and the full-velocity slip it is a ratio to
        // (initialisePluck); both zero when the line is the contact alone.
        double slipPole { 0.0 }, referencePole { 0.0 };
    };

    struct ReleasedContactOptions
    {
        PhysicalCalibration calibration { fittedPhysicalCalibration };
        float touch { EngineParameters {}.touch };
        float bend { 0.0f };
        float timbre { -1.0f };
        int fret { 0 };
        int polarisation { 0 };
        PickingTechnique picking { PickingTechnique::Thumb };
    };

    static ReleasedContactSnapshot releasedContact(int rate, int string,
                                                    const ReleasedContactOptions& options)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        auto calibration = options.calibration;
        // Recover the initializer's amplitude from its envelope metadata,
        // without duplicating the displacement/velocity law. No audio is
        // advanced, so this probe emits no release noise.
        calibration.steel.transientScale = 1.0f;
        engine.setPhysicalCalibration(calibration);
        EngineParameters parameters;
        parameters.picking = options.picking;
        parameters.touch = options.touch;
        engine.setParameters(parameters);
        engine.prepare(rate, 64);
        engine.setStringPerChannelMode(true);
        if (options.timbre >= 0.0f)
        {
            engine.setLowerZoneMemberCount(5);
            engine.setMpeTimbre(options.timbre, string + 1);
        }
        engine.setPitchBend(options.bend, options.timbre >= 0.0f ? 1 : string + 1);
        auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        engine.noteOn(voice.openMidi + options.fret, 0.6f, string + 1);
        const auto& loop = voice.loops[static_cast<std::size_t>(options.polarisation)];
        const int length = std::clamp(static_cast<int>(std::round(loop.currentDelay)),
                                      8, AcustraEngine::maximumDelaySamples - 3);
        const float touch = engine.effectiveTouch(voice.velocity);
        const auto& physical = calibration.steel;
        const float apertureSamples = 0.70f + 3.60f * (1.0f - touch)
            + std::clamp((3.0f - static_cast<float>(string)) / 2.0f, 0.0f, 1.0f)
            + 1.5f * std::clamp((static_cast<float>(voice.fret) - 13.0f) / 6.0f,
                                0.0f, 1.0f);
        const float contactSamples = options.picking == PickingTechnique::Pick
            ? 0.35f * apertureSamples
            : options.picking == PickingTechnique::Thumb
                ? std::sqrt(4.0f * apertureSamples * apertureSamples + 6.25f)
                : apertureSamples;
        // The envelope carries the stroke's force; the shape is displaced by
        // it through the stopped string's compliance at the pluck point,
        // referred to the open string (initialisePluck).
        const float scaleLength = 0.648f;
        const float soundingLength = scaleLength
            * std::exp2(-static_cast<float>(voice.fret) / 12.0f);
        const double releaseScale = (1.0f - voice.pluckPoint)
            / std::clamp(1.0f - voice.pluckPoint * soundingLength / scaleLength,
                         0.05f, 1.0f);
        ReleasedContactSnapshot result {
            {}, voice.releaseShapePosition[static_cast<std::size_t>(options.polarisation)],
            registeredAperture(contactSamples,
                physical.apertureScale,
                loop.currentDelay * 48000.0f / static_cast<float>(rate),
                calibration.apertureRegisterExponent),
            static_cast<double>(voice.excitationEnvelope)
                / (0.003f + 0.014f * touch) * releaseScale
                * std::sqrt(options.polarisation == 0
                    ? voice.polarisationMix : 1.0f - voice.polarisationMix),
            voice.releaseSlipPole, voice.releaseReferencePole
        };
        for (int sample = 0; sample < length; ++sample)
        {
            const int index = (loop.writeIndex - sample - 1
                + AcustraEngine::maximumDelaySamples) % AcustraEngine::maximumDelaySamples;
            result.history.push_back(loop.delay[static_cast<std::size_t>(index)]);
        }
        return result;
    }

    static ReleasedContactSnapshot releasedContact(int rate, int string)
    {
        return releasedContact(rate, string, ReleasedContactOptions {});
    }

    struct PluckSnapshot
    {
        double touch;
        double peakPosition;
        double peakDisplacement;
        double noiseEnvelope;
        double pluckPoint;
    };

    struct PreparedLossSnapshot
    {
        double beforeScale;
        double afterScale;
        double beforeA1;
        double beforeA2;
        double afterA1;
        double afterA2;
    };

    struct RetunedStringSnapshot
    {
        double impedance;
        double tailStiffness;
        double inharmonicity;
    };

    struct AttackPitchSnapshot
    {
        double energy;
        double cents;
        double decay;
    };

    static std::array<double, 2> longitudinalFrequencies(int midiNote)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        engine.prepare(48000.0, 64);
        const int stringIndex = engine.chooseString(midiNote);
        auto& voice = engine.voices_[static_cast<std::size_t>(stringIndex)];
        engine.configureVoice(voice, stringIndex, midiNote, true);
        std::array<double, 2> result {};
        for (std::size_t mode = 0; mode < result.size(); ++mode)
        {
            const double radius = std::sqrt(-voice.longitudinalA2[mode]);
            result[mode] = std::acos(std::clamp(static_cast<double>(
                voice.longitudinalA1[mode]) / (2.0 * radius), -1.0, 1.0))
                * 48000.0 / (2.0 * std::numbers::pi);
        }
        return result;
    }

    struct BendLifecycleSnapshot
    {
        double heldDelay;
        double afterMemberTrafficDelay;
        double afterMasterTrafficDelay;
        double reusedFingerDelay;
        float frozenMemberBend;
        bool frozen;
        bool pedalHeld;
        bool reusedFingerFrozen;
    };

    static StringLoopSnapshot configuredLoop(int midiNote, double rate,
                                             PhysicalCalibration calibration
                                                 = fittedPhysicalCalibration)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        engine.setPhysicalCalibration(calibration);
        engine.prepare(rate, 64);
        engine.setBridgeCouplingEnabled(false);
        EngineParameters parameters;
        engine.setParameters(parameters);
        const int stringIndex = engine.chooseString(midiNote);
        auto& voice = engine.voices_[static_cast<std::size_t>(stringIndex)];
        voice.attackPitchCents = 0.0f;
        engine.configureVoice(voice, stringIndex, midiNote, true);
        const auto& loop = voice.loops[0];
        return withBendingLoss({ loop.targetDelay, loop.loopGain,
                 loop.broadLossCoefficient,
                 loop.broadLossMix, loop.lowpassCoefficient,
                 loop.highLossMix, loop.dispersionA1, loop.dispersionA2,
                 voice.dispersionDesignInharmonicity, rate }, loop);
    }

    // Every string at every fret from open to the 20th at one rate, in the
    // standard tuning, configured the way configuredLoop configures a note.
    static std::vector<StringLoopSnapshot> fretboardLoops(double rate)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        engine.prepare(rate, 64);
        engine.setBridgeCouplingEnabled(false);
        EngineParameters parameters;
        engine.setParameters(parameters);
        std::vector<StringLoopSnapshot> loops;
        for (int stringIndex = 0; stringIndex < 6; ++stringIndex)
        for (int fret = 0; fret <= 20; ++fret)
        {
            auto& voice = engine.voices_[static_cast<std::size_t>(stringIndex)];
            voice.attackPitchCents = 0.0f;
            engine.configureVoice(voice, stringIndex, voice.openMidi + fret,
                                  true);
            const auto& loop = voice.loops[0];
            loops.push_back(withBendingLoss({ loop.targetDelay,
                    loop.loopGain, loop.broadLossCoefficient,
                    loop.broadLossMix, loop.lowpassCoefficient,
                    loop.highLossMix, loop.dispersionA1, loop.dispersionA2,
                    voice.dispersionDesignInharmonicity, rate }, loop));
        }
        return loops;
    }

    static std::array<double, 3> lossFilterCoefficients(float pole, double rate)
    {
        AcustraEngine::OnePole filter;
        filter.configureRate(pole, rate);
        if (!filter.remapped)
            return { 1.0 - pole, 0.0, pole };
        return { 1.0 - filter.ratePole - filter.delayedInputGain,
                 filter.delayedInputGain, filter.ratePole };
    }

    static std::vector<float> lossFilterImpulse(float pole, double rate)
    {
        AcustraEngine::OnePole filter;
        filter.configureRate(pole, rate);
        std::vector<float> impulse(4096);
        for (std::size_t index = 0; index < impulse.size(); ++index)
            impulse[index] = filter.process(index == 0 ? 1.0f : 0.0f, pole);
        return impulse;
    }

    // Both polarisations' loop lengths, with the bridge coupling off so that
    // the only difference between them is the end correction.
    static std::array<double, 2> polarisationDelays(
        int midiNote,
        PhysicalCalibration calibration = fittedPhysicalCalibration)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        engine.setPhysicalCalibration(calibration);
        engine.prepare(48000.0, 64);
        engine.setBridgeCouplingEnabled(false);
        EngineParameters parameters;
        engine.setParameters(parameters);
        const int stringIndex = engine.chooseString(midiNote);
        auto& voice = engine.voices_[static_cast<std::size_t>(stringIndex)];
        voice.attackPitchCents = 0.0f;
        engine.configureVoice(voice, stringIndex, midiNote, true);
        return { voice.loops[0].targetDelay, voice.loops[1].targetDelay };
    }

    static BodyModeSnapshot configuredBody(
        PhysicalCalibration calibration, int index,
        BodyShape shape = EngineParameters {}.shape)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        EngineParameters parameters;
        parameters.shape = shape;
        // The measured body at the wood it was built of, where Body
        // Material leaves it as measured: spruce for g21.
        parameters.bodyMaterial = BodyMaterial::Spruce;
        engine.setParameters(parameters);
        engine.setPhysicalCalibration(calibration);
        engine.prepare(48000.0, 64);
        const auto& mode = engine.bodyModes_[static_cast<std::size_t>(index)];
        const double radius = std::hypot(mode.poleReal, mode.poleImaginary);
        const double frequency = std::atan2(mode.poleImaginary, mode.poleReal)
                               * 48000.0 / (2.0 * std::numbers::pi);
        const double q = -std::numbers::pi * frequency
                       / (48000.0 * std::log(radius));
        return { frequency, q, std::hypot(mode.leftReal, mode.leftImaginary) };
    }

    static double captureVoicing(double frequency)
    {
        return AcustraEngine::captureVoicingGain(static_cast<float>(frequency));
    }

    static double bridgeAdmittance(PhysicalCalibration calibration)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        EngineParameters parameters;
        engine.setParameters(parameters);
        engine.setPhysicalCalibration(calibration);
        engine.prepare(48000.0, 64);
        return engine.bridgeLoad_.immediateHeave;
    }

    static double bridgeAdmittanceOf(const AcustraEngine& engine)
    {
        return engine.bridgeLoad_.immediateHeave;
    }

    static double bodyResidueOf(const AcustraEngine& engine, int index)
    {
        const auto& mode = engine.bodyModes_[static_cast<std::size_t>(index)];
        return std::hypot(mode.leftReal, mode.leftImaginary)
             + std::hypot(mode.rightReal, mode.rightImaginary)
             + std::hypot(mode.poleReal, mode.poleImaginary);
    }

    static constexpr int bodyModeCapacity = AcustraEngine::bodyModeCount;

    static int bodyModesInUse(const AcustraEngine& engine)
    {
        return engine.bodyBank_.count;
    }

    static int retainedTailCount(const AcustraEngine& engine)
    {
        return static_cast<int>(std::count_if(engine.voices_.begin(),
            engine.voices_.end(), [] (const auto& voice) { return voice.tailActive; }));
    }

    static float bodyFade(const AcustraEngine& engine) { return engine.bodyModelFade_; }
    static bool bodyUpdatePending(const AcustraEngine& engine)
    {
        return engine.bodyUpdatePending_;
    }

    // The bending-loss section each polarisation carries, and the design
    // inputs, for one configured note (bridge coupling off).
    struct BendingSections
    {
        std::array<double, 3> normal;
        std::array<double, 3> parallel;
        bool normalActive;
        bool parallelActive;
    };

    static BendingSections bendingSections(int midiNote, double rate,
                                           PhysicalCalibration calibration)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        engine.setPhysicalCalibration(calibration);
        engine.prepare(rate, 64);
        engine.setBridgeCouplingEnabled(false);
        EngineParameters parameters;
        engine.setParameters(parameters);
        const int stringIndex = engine.chooseString(midiNote);
        auto& voice = engine.voices_[static_cast<std::size_t>(stringIndex)];
        voice.attackPitchCents = 0.0f;
        engine.configureVoice(voice, stringIndex, midiNote, true);
        const auto section = [] (const AcustraEngine::StringLoop& loop)
        {
            return std::array<double, 3> { loop.bendingLossGain,
                                           loop.bendingLossA1,
                                           loop.bendingLossA2 };
        };
        return { section(voice.loops[0]), section(voice.loops[1]),
                 voice.loops[0].bendingLossActive,
                 voice.loops[1].bendingLossActive };
    }

    static double playedDelay(PhysicalCalibration calibration)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        engine.setPhysicalCalibration(calibration);
        engine.prepare(48000.0, 64);
        engine.noteOn(52, 0.8f);
        const auto selected = std::find_if(engine.voices_.begin(),
            engine.voices_.end(), [] (const auto& voice)
            {
                return voice.played && voice.midiNote == 52;
            });
        return selected == engine.voices_.end()
            ? 0.0 : selected->loops[0].targetDelay;
    }

    struct BentStringSnapshot
    {
        StringLoopSnapshot loop;
        double unbentInharmonicity;
        double tension;
        double impedanceScale;
        int fret;
        int stringIndex;
    };

    // One note taken on an MPE member channel, held, and reconfigured under a
    // manager bend and a member bend. Everything the bend convention touches
    // is read off the voice it produced.
    static BentStringSnapshot bentString(int midiNote,
                                         float masterBend, float memberBend,
                                         double rate = 48000.0)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(rate, 64);
        engine.setBridgeCouplingEnabled(false);
        engine.setLowerZoneMemberCount(2);
        engine.noteOn(midiNote, 0.8f, 2);
        auto selected = std::find_if(engine.voices_.begin(),
            engine.voices_.end(), [midiNote] (const auto& voice)
            {
                return voice.played && voice.midiNote == midiNote
                    && voice.midiChannel == 2;
            });
        if (selected == engine.voices_.end())
            return {};
        const int stringIndex = static_cast<int>(
            std::distance(engine.voices_.begin(), selected));
        selected->attackPitchCents = 0.0f;
        engine.configureVoice(*selected, stringIndex, midiNote, false);
        const double unbent = selected->dispersionDesignInharmonicity;
        engine.setPitchBend(masterBend, 1);
        engine.setPitchBend(memberBend, 2);
        engine.configureVoice(*selected, stringIndex, midiNote, false);
        const auto& loop = selected->loops[0];
        return { withBendingLoss({ loop.targetDelay, loop.loopGain,
                   loop.broadLossCoefficient,
                   loop.broadLossMix, loop.lowpassCoefficient,
                   loop.highLossMix, loop.dispersionA1, loop.dispersionA2,
                   selected->dispersionDesignInharmonicity, rate }, loop),
                 unbent, selected->tensionNewtons,
                 selected->bendImpedanceScale, selected->fret, stringIndex };
    }

    // A per-block trace of the loop the wheel is driving, taken from a real
    // render: the delay the loop is actually reading, slew and all, with
    // everything else that sets where it resonates. The vibrato is then
    // measured as the pitch it produces rather than as a quantity that stands
    // in for one.
    static std::vector<StringLoopSnapshot> vibratoLoopTrace(
        float wheel, double rate, double seconds, int block,
        int midiNote = 52)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(rate, block);
        engine.noteOn(midiNote, 0.8f);
        engine.setVibrato(wheel);
        std::vector<float> left(static_cast<std::size_t>(block));
        std::vector<float> right(static_cast<std::size_t>(block));
        std::vector<StringLoopSnapshot> trace;
        const int blocks = static_cast<int>(seconds * rate / block);
        for (int index = 0; index < blocks; ++index)
        {
            engine.process(left.data(), right.data(), block);
            const auto selected = std::find_if(engine.voices_.begin(),
                engine.voices_.end(), [midiNote] (const auto& voice)
                {
                    return voice.played && voice.midiNote == midiNote;
                });
            if (selected == engine.voices_.end())
                break;
            const auto& loop = selected->loops[0];
            trace.push_back(withBendingLoss({ loop.currentDelay,
                              loop.loopGain,
                              loop.broadLossCoefficient, loop.broadLossMix,
                              loop.lowpassCoefficient, loop.highLossMix,
                              loop.dispersionA1, loop.dispersionA2,
                              selected->dispersionDesignInharmonicity, rate },
                              loop));
        }
        return trace;
    }

    // Two notes on two member channels, one of them bent: what the bend does
    // to its own string and to the other one. Delay then tension, the bent
    // string before and after, then the other string before and after.
    static std::array<double, 8> memberBendIsolation()
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        engine.prepare(48000.0, 64);
        engine.setLowerZoneMemberCount(4);
        engine.noteOn(52, 0.8f, 2);
        engine.noteOn(59, 0.8f, 3);
        const auto find = [&engine] (int channel)
        {
            return std::find_if(engine.voices_.begin(), engine.voices_.end(),
                [channel] (const auto& voice)
                {
                    return voice.played && voice.midiChannel == channel;
                });
        };
        auto bent = find(2);
        auto other = find(3);
        if (bent == engine.voices_.end() || other == engine.voices_.end())
            return {};
        bent->attackPitchCents = 0.0f;
        other->attackPitchCents = 0.0f;
        const auto reconfigure = [&]
        {
            engine.configureVoice(*bent, static_cast<int>(
                std::distance(engine.voices_.begin(), bent)),
                bent->midiNote, false);
            engine.configureVoice(*other, static_cast<int>(
                std::distance(engine.voices_.begin(), other)),
                other->midiNote, false);
        };
        reconfigure();
        const double bentDelayBefore = bent->loops[0].targetDelay;
        const double otherDelayBefore = other->loops[0].targetDelay;
        const double bentTensionBefore = bent->tensionNewtons;
        const double otherTensionBefore = other->tensionNewtons;
        engine.setPitchBend(2.0f, 2);
        reconfigure();
        return { bentDelayBefore, bent->loops[0].targetDelay,
                 bentTensionBefore, bent->tensionNewtons,
                 otherDelayBefore, other->loops[0].targetDelay,
                 otherTensionBefore, other->tensionNewtons };
    }

    static double channelBentDelay(float masterBend, float memberBend)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(48000.0, 64);
        engine.setLowerZoneMemberCount(2);
        engine.noteOn(52, 0.8f, 2);
        const auto selected = std::find_if(engine.voices_.begin(),
            engine.voices_.end(), [] (const auto& voice)
            {
                return voice.played && voice.midiNote == 52
                    && voice.midiChannel == 2;
            });
        if (selected == engine.voices_.end())
            return 0.0;
        const int stringIndex = static_cast<int>(
            std::distance(engine.voices_.begin(), selected));
        selected->attackPitchCents = 0.0f;
        engine.setPitchBend(masterBend, 1);
        engine.setPitchBend(memberBend, 2);
        engine.configureVoice(*selected, stringIndex, 52, false);
        return selected->loops[0].targetDelay;
    }

    static double conventionalChannelDelay(float channelOneBend,
                                           float channelTwoBend)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        engine.prepare(48000.0, 64);
        engine.noteOn(52, 0.8f, 2);
        auto selected = std::find_if(engine.voices_.begin(),
            engine.voices_.end(), [] (const auto& voice)
            {
                return voice.played && voice.midiChannel == 2;
            });
        if (selected == engine.voices_.end())
            return 0.0;
        const int stringIndex = static_cast<int>(
            std::distance(engine.voices_.begin(), selected));
        selected->attackPitchCents = 0.0f;
        engine.setPitchBend(channelOneBend, 1);
        engine.setPitchBend(channelTwoBend, 2);
        engine.configureVoice(*selected, stringIndex, 52, false);
        return selected->loops[0].targetDelay;
    }

    static BendLifecycleSnapshot bendLifecycle(bool mpe)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        engine.prepare(48000.0, 64);
        if (mpe)
            engine.setLowerZoneMemberCount(2);
        engine.noteOn(52, 0.8f, 2);
        auto selected = std::find_if(engine.voices_.begin(),
            engine.voices_.end(), [] (const auto& voice)
            {
                return voice.played && voice.keyDown
                    && voice.midiChannel == 2;
            });
        if (selected == engine.voices_.end())
            return {};
        const int stringIndex = static_cast<int>(
            std::distance(engine.voices_.begin(), selected));
        selected->attackPitchCents = 0.0f;
        engine.setPitchBend(5.0f, 2);
        engine.configureVoice(*selected, stringIndex, 52, false);
        const double heldDelay = selected->loops[0].targetDelay;
        engine.setSustainPedal(true, mpe ? 1 : 2);
        engine.noteOff(52, 2);
        const bool frozen = selected->memberPitchBendFrozen;
        const bool pedalHeld = selected->pedalHeld;
        const float frozenBend = selected->frozenMemberPitchBendSemitones;

        engine.setPitchBend(-5.0f, 2);
        engine.configureVoice(*selected, stringIndex, 52, false);
        const double afterMember = selected->loops[0].targetDelay;
        engine.setPitchBend(1.0f, 1);
        engine.configureVoice(*selected, stringIndex, 52, false);
        const double afterMaster = selected->loops[0].targetDelay;

        engine.noteOn(52, 0.7f, 2);
        // The same note is replucked on the string still sounding it, so the
        // reused finger may well be the same voice.
        auto reused = std::find_if(engine.voices_.begin(),
            engine.voices_.end(), [] (const auto& voice)
            {
                return voice.played && voice.keyDown
                    && voice.midiChannel == 2;
            });
        double reusedDelay = 0.0;
        bool reusedFrozen = true;
        if (reused != engine.voices_.end())
        {
            const int reusedIndex = static_cast<int>(
                std::distance(engine.voices_.begin(), reused));
            reused->attackPitchCents = 0.0f;
            engine.configureVoice(*reused, reusedIndex, 52, false);
            reusedDelay = reused->loops[0].targetDelay;
            reusedFrozen = reused->memberPitchBendFrozen;
        }
        return { heldDelay, afterMember, afterMaster, reusedDelay,
                 frozenBend, frozen, pedalHeld, reusedFrozen };
    }

    static std::array<int, 3> lowerZoneTransitionCounts()
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        engine.prepare(48000.0, 64);
        engine.noteOn(52, 0.8f, 2);
        engine.noteOn(55, 0.8f, 8);
        engine.setLowerZoneMemberCount(2);
        const int enabled = engine.getActiveVoiceCount();
        engine.setLowerZoneMemberCount(7);
        const int resized = engine.getActiveVoiceCount();
        engine.noteOn(52, 0.8f, 2);
        engine.setLowerZoneMemberCount(0);
        return { enabled, resized, engine.getActiveVoiceCount() };
    }

    static bool allSoundOffPreservesControllers()
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        engine.prepare(48000.0, 64);
        engine.setPitchBend(7.0f, 2);
        engine.setSustainPedal(true, 2);
        engine.noteOn(52, 0.8f, 2);
        engine.allSoundOff(2);
        return engine.pitchBendSemitones_[1] == 7.0f
            && engine.sustainPedals_[1];
    }

    static float apertureReferenceDelay()
    {
        return 48000.0f / AcustraEngine::midiFrequency(61);
    }

    static float registeredAperture(float apertureSamples, float apertureScale,
                                    float currentReferenceLength,
                                    float exponent)
    {
        return AcustraEngine::registeredPluckAperture(
            apertureSamples, apertureScale, apertureReferenceDelay(),
            currentReferenceLength, exponent);
    }

    static PluckSnapshot pluck(PhysicalCalibration calibration, float velocity,
                               int midiNote = 52,
                               PickingTechnique picking = PickingTechnique::Finger)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        engine.setPhysicalCalibration(calibration);
        EngineParameters parameters;
        parameters.picking = picking;
        engine.setParameters(parameters);
        engine.prepare(48000.0, 64);
        engine.setBridgeCouplingEnabled(false);
        engine.noteOn(midiNote, velocity);

        const auto selected = std::find_if(engine.voices_.begin(),
            engine.voices_.end(), [midiNote] (const auto& voice)
            {
                return voice.played && voice.midiNote == midiNote;
            });
        if (selected == engine.voices_.end())
            return {};
        const auto& voice = *selected;
        const auto& loop = voice.loops[0];
        const int length = std::clamp(
            static_cast<int>(std::round(loop.targetDelay)), 8,
            AcustraEngine::maximumDelaySamples - 3);
        const auto at = [&] (int sample)
        {
            int index = loop.writeIndex - sample;
            while (index < 0)
                index += AcustraEngine::maximumDelaySamples;
            while (index >= AcustraEngine::maximumDelaySamples)
                index -= AcustraEngine::maximumDelaySamples;
            return static_cast<double>(
                loop.delay[static_cast<std::size_t>(index)]);
        };
        double peakValue = 0.0;
        int peakSample = 0;
        for (int sample = 1; sample <= length; ++sample)
        {
            const double value = at(sample);
            if (value > peakValue)
            {
                peakValue = value;
                peakSample = sample;
            }
        }
        return {
            engine.effectiveTouch(voice.velocity),
            static_cast<double>(peakSample) / length,
            peakValue,
            voice.excitationEnvelope,
            voice.pluckPoint
        };
    }

    static double lastPluckPoint(const AcustraEngine& engine)
    {
        const AcustraEngine::Voice* latest = nullptr;
        for (const auto& voice : engine.voices_)
            if (voice.played && (latest == nullptr
                                 || voice.startOrder > latest->startOrder))
                latest = &voice;
        return latest == nullptr ? -1.0 : latest->pluckPoint;
    }

    static PreparedLossSnapshot changePreparedLoss(
        PhysicalCalibration before, PhysicalCalibration after)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        EngineParameters parameters;
        engine.setParameters(parameters);
        engine.setPhysicalCalibration(before);
        engine.prepare(48000.0, 64);
        engine.setBridgeCouplingEnabled(false);
        const auto& originalVoice = engine.voices_[0];
        const PreparedLossSnapshot original {
            originalVoice.dispersionDesignFrequencyLossScale, 0.0,
            originalVoice.loops[0].dispersionA1,
            originalVoice.loops[0].dispersionA2, 0.0, 0.0
        };

        engine.setPhysicalCalibration(after);
        const auto& updatedVoice = engine.voices_[0];
        return {
            original.beforeScale,
            updatedVoice.dispersionDesignFrequencyLossScale,
            original.beforeA1,
            original.beforeA2,
            updatedVoice.loops[0].dispersionA1,
            updatedVoice.loops[0].dispersionA2
        };
    }

    static PhysicalCalibration calibration(const AcustraEngine& engine)
    {
        return engine.physicalCalibration_;
    }

    static RetunedStringSnapshot retunedString(Tuning tuning, int string)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        EngineParameters parameters;
        parameters.tuning = tuning;
        engine.setParameters(parameters);
        engine.prepare(48000.0, 64);
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        return { voice.characteristicImpedance, voice.bridgeTailStiffness,
                 voice.dispersionDesignInharmonicity };
    }

    static std::vector<AttackPitchSnapshot> attackPitchTrace(
        float velocity, int samples)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(48000.0, 1);
        engine.noteOn(52, velocity);
        const auto selected = std::find_if(engine.voices_.begin(),
            engine.voices_.end(), [] (const auto& voice)
            {
                return voice.played && voice.midiNote == 52;
            });
        if (selected == engine.voices_.end())
            return {};

        std::vector<AttackPitchSnapshot> trace {{
            selected->attackSlopeEnergy, selected->attackPitchCents,
            selected->attackPitchDecay
        }};
        for (int sample = 0; sample < samples; ++sample)
        {
            float left = 0.0f;
            float right = 0.0f;
            engine.process(&left, &right, 1);
            if ((sample + 1) % AcustraEngine::controlPeriod == 0)
                trace.push_back({ selected->attackSlopeEnergy,
                                  selected->attackPitchCents,
                                  selected->attackPitchDecay });
        }
        return trace;
    }

    struct StolenStringSnapshot
    {
        double heldBeforeSteal;      // stored energy in the string's own loops
        double keptInTail;           // stored energy carried into the tail
        double tailEnergyAfterDecay; // the same tail 0.5 s later
        double heldParallelBeforeSteal; // the same, in the parallel plane
        double keptInParallelTail;
        double parallelTailEnergyAfterDecay;
        bool tailActive;
        bool tailActiveAfterRepluck; // a repluck of the same note lands the hand
    };

    static double loopEnergy(const AcustraEngine::StringLoop& loop)
    {
        const int length = std::clamp(
            static_cast<int>(std::round(loop.targetDelay)), 8,
            AcustraEngine::maximumDelaySamples - 3);
        double total = 0.0;
        for (int sample = 1; sample <= length; ++sample)
        {
            int index = loop.writeIndex - sample;
            while (index < 0) index += AcustraEngine::maximumDelaySamples;
            const double value = loop.delay[static_cast<std::size_t>(index)];
            total += value * value;
        }
        return total;
    }

    struct TailPortBalance
    {
        double relativeError;
        bool capturedImpedance;
        int measuredFrames;
    };

    static TailPortBalance tailPortBalance(double rate, EngineParameters parameters,
                                           int bendChange)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        engine.setParameters(parameters);
        engine.prepare(rate, 1);
        engine.setStringPerChannelMode(true);
        engine.setLowerZoneMemberCount(6);
        // The released static force (initialisePluck) is an external force
        // on the saddle, outside the strings' wave-norm identity, as the
        // excitation's sources below are.
        engine.releaseStepEnabled_ = false;
        const int string = bendChange == 0 ? 0 : 5;
        const int note = bendChange == 0 ? 40 : 64;
        const int channel = string + 1;
        engine.setPitchBend(bendChange < 0 ? 2.0f : 0.0f, channel);
        engine.noteOn(note, 0.7f, channel);
        const auto removeExtraSources = [&]
        {
            for (auto& voice : engine.voices_)
            {
                voice.excitationEnvelope = 0.0f;
                voice.attackPitchCents = voice.attackSlopeEnergy = 0.0f;
                voice.observedSlopeEnergy = 0.0f;
            }
            engine.updateControlState();
        };
        removeExtraSources();
        float left = 0.0f, right = 0.0f;
        for (int sample = 0; sample < static_cast<int>(0.3 * rate); ++sample)
            engine.process(&left, &right, 1);
        const auto& retained = engine.voices_[static_cast<std::size_t>(string)];
        const float expectedTailZ = retained.characteristicImpedance
                                 * retained.appliedBendImpedanceScale;
        engine.setPitchBend(bendChange > 0 ? 2.0f : 0.0f, channel);
        engine.noteOn(bendChange == 0 ? note : note + 3, 0.7f, channel);
        removeExtraSources();
        const bool captured = retained.tailActive
            && retained.tailCharacteristicImpedance == expectedTailZ;

        double error = 0.0, norm = 0.0;
        int measured = 0;
        for (int sample = 0; sample < 2048; ++sample)
        {
            // Let the real engine perform control updates. Audit samples
            // between them so copies see exactly the same loop coefficients.
            if (engine.controlCounter_ + 1 >= AcustraEngine::controlPeriod)
            {
                engine.process(&left, &right, 1);
                continue;
            }
            std::array<double, 24> incoming {}, impedance {};
            for (int i = 0; i < 6; ++i)
            {
                const auto& voice = engine.voices_[static_cast<std::size_t>(i)];
                for (int axis = 0; axis < 2; ++axis)
                {
                    auto copy = voice.loops[static_cast<std::size_t>(axis)];
                    incoming[4 * i + axis] = copy.advance(engine.delaySmoothing_, 1.0f);
                    impedance[4 * i + axis] = voice.characteristicImpedance
                                           * voice.appliedBendImpedanceScale;
                }
                if (voice.tailActive)
                {
                    // Both retained planes, each weighted by the branch's
                    // independently observed pre-capture port, not the new
                    // note's or a copied field.
                    auto normal = voice.tailLoop;
                    incoming[4 * i + 2] = normal.advance(engine.delaySmoothing_,
                                                         voice.tailDamping);
                    impedance[4 * i + 2] = expectedTailZ;
                    auto parallel = voice.tailParallelLoop;
                    incoming[4 * i + 3] = parallel.advance(engine.delaySmoothing_,
                                                           voice.tailDamping);
                    impedance[4 * i + 3] = expectedTailZ;
                }
            }
            engine.process(&left, &right, 1);
            if (!retained.tailActive)
                break;
            double waveFlux = 0.0;
            for (int port = 0; port < 24; ++port)
            {
                if (impedance[port] == 0.0)
                    continue;
                const auto& voice = engine.voices_[static_cast<std::size_t>(port / 4)];
                const int axis = port % 4;
                const auto& loop = axis < 2 ? voice.loops[static_cast<std::size_t>(axis)]
                                 : axis == 2 ? voice.tailLoop : voice.tailParallelLoop;
                const double outgoing = loop.delay[static_cast<std::size_t>(
                    (loop.writeIndex + AcustraEngine::maximumDelaySamples - 1)
                    % AcustraEngine::maximumDelaySamples)];
                const double a2 = incoming[port] * incoming[port];
                const double c2 = outgoing * outgoing;
                waveFlux += impedance[port] * (a2 - c2);
                norm += impedance[port] * (a2 + c2);
            }
            const auto& bridge = engine.bridgeLoad_;
            const double bridgeFlux = static_cast<double>(bridge.displacement)
                    * bridge.mainIntegratedForce
                + static_cast<double>(bridge.rotation) * bridge.mainIntegratedMoment;
            error += std::abs(waveFlux - bridgeFlux);
            ++measured;
        }
        return { error / std::max(norm, 1.0e-30), captured, measured };
    }

    static StolenStringSnapshot stealStringTail(double rate)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(rate, 128);
        const std::array<int, 6> first { 40, 47, 52, 56, 59, 64 };
        const std::array<int, 6> second { 43, 50, 55, 58, 62, 67 };
        std::vector<float> l(128), r(128);
        const auto run = [&] (double seconds)
        {
            for (int i = 0; i < static_cast<int>(seconds * rate); i += 128)
                engine.process(l.data(), r.data(), 128);
        };
        for (const int note : first) engine.noteOn(note, 0.8f);
        run(0.5);
        StolenStringSnapshot out {};
        out.heldBeforeSteal = loopEnergy(engine.voices_[0].loops[0]);
        out.heldParallelBeforeSteal = loopEnergy(engine.voices_[0].loops[1]);
        for (const int note : second) engine.noteOn(note, 0.8f);
        out.tailActive = engine.voices_[0].tailActive;
        out.keptInTail = loopEnergy(engine.voices_[0].tailLoop);
        out.keptInParallelTail = loopEnergy(engine.voices_[0].tailParallelLoop);
        run(0.5);
        out.tailEnergyAfterDecay = engine.voices_[0].tailActive
            ? loopEnergy(engine.voices_[0].tailLoop) : 0.0;
        out.parallelTailEnergyAfterDecay = engine.voices_[0].tailActive
            ? loopEnergy(engine.voices_[0].tailParallelLoop) : 0.0;

        auto repluckOwner = std::make_unique<AcustraEngine>();
        auto& repluck = *repluckOwner;
        repluck.setParameters(parameters);
        repluck.prepare(rate, 128);
        repluck.noteOn(52, 0.8f);
        for (int i = 0; i < static_cast<int>(0.5 * rate); i += 128)
            repluck.process(l.data(), r.data(), 128);
        repluck.noteOff(52);
        repluck.noteOn(52, 0.8f);
        out.tailActiveAfterRepluck = false;
        for (const auto& voice : repluck.voices_)
            out.tailActiveAfterRepluck |= voice.tailActive;
        return out;
    }

    // The energy the picking hand's contact leaves in a stolen or replucked
    // voice's tail loop, at capture and 60 ms later, on the string doing the
    // repluck (voice 0) rather than the new pluck (voice 1's own string).
    static std::pair<double, double> repluckTailEnergyAt60ms(double rate)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(rate, 128);
        std::vector<float> l(128), r(128);
        engine.noteOn(40, 0.8f);
        for (int i = 0; i < static_cast<int>(0.3 * rate); i += 128)
            engine.process(l.data(), r.data(), 128);
        engine.noteOn(40, 0.8f); // repluck: captureTail runs on voice 0
        const double atCapture = loopEnergy(engine.voices_[0].tailLoop);
        for (int i = 0; i < static_cast<int>(0.06 * rate); i += 128)
            engine.process(l.data(), r.data(), 128);
        const double after60ms = engine.voices_[0].tailActive
            ? loopEnergy(engine.voices_[0].tailLoop) : 0.0;
        return { atCapture, after60ms };
    }

    static constexpr int controlPeriodSamples() noexcept
    {
        return AcustraEngine::controlPeriod;
    }

    // Fires one strum of chord (rank order = array order) and returns each
    // string's actually-scheduled release delay in samples, jitter included:
    // strumDelaySamples() alone only returns the deterministic baseline.
    template <std::size_t Count>
    static std::array<int, Count> strumSchedule(
        AcustraEngine& engine, const std::array<int, Count>& chord,
        float velocity) noexcept
    {
        std::array<int, Count> delays {};
        engine.beginStrum();
        for (std::size_t i = 0; i < Count; ++i)
        {
            const int baseline = engine.strumDelaySamples(static_cast<int>(i), velocity);
            engine.noteOn(chord[i], velocity, 1, baseline, true);
            const AcustraEngine::Voice* latest = nullptr;
            for (const auto& voice : engine.voices_)
                if (voice.played && voice.midiNote == chord[i]
                    && (latest == nullptr || voice.startOrder > latest->startOrder))
                    latest = &voice;
            // pluckDelay is (scheduled delay + 1) while waiting, 0 once fired
            // (rank 0 at minimum jitter can fire on the same sample).
            delays[i] = latest == nullptr ? 0
                : std::max(0, latest->pluckDelay - 1);
        }
        return delays;
    }

    // One note plucked on midiChannel with CC74 pre-set to timbre (negative:
    // never sent). A fresh engine's voice.randomState starts at the same
    // seed every time and initialisePluck's takeOffset is the first draw
    // from it, so two calls with the same midiChannel and midiNote draw the
    // identical offset and differ only by what timbre itself moved.
    static double mpeTimbrePluckPoint(float timbre, int midiChannel,
                                      int midiNote = 52,
                                      PickingTechnique picking = PickingTechnique::Finger)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        EngineParameters parameters;
        parameters.picking = picking;
        engine.setParameters(parameters);
        engine.prepare(48000.0, 64);
        if (midiChannel > 1)
            engine.setLowerZoneMemberCount(2);
        if (timbre >= 0.0f)
            engine.setMpeTimbre(timbre, midiChannel);
        engine.noteOn(midiNote, 0.8f, midiChannel);
        return lastPluckPoint(engine);
    }

    // The wheel's own vibrato depth for one held, fretted note, with the
    // transient forced fully open and the phase parked at its peak so the
    // comparison is not diluted by the onset ramp. pressure negative means
    // no channel-pressure message was ever sent.
    static double vibratoDepthCents(float wheel, float pressure,
                                    int midiChannel)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(48000.0, 64);
        if (midiChannel > 1)
            engine.setLowerZoneMemberCount(2);
        if (pressure >= 0.0f)
            engine.setMpePressure(pressure, midiChannel);
        engine.noteOn(52, 0.8f, midiChannel);
        engine.setVibrato(wheel);
        const auto selected = std::find_if(engine.voices_.begin(),
            engine.voices_.end(), [midiChannel] (const auto& voice)
            { return voice.played && voice.midiChannel == midiChannel; });
        if (selected == engine.voices_.end())
            return 0.0;
        engine.vibratoOnset_ = 1.0f;
        engine.vibratoPhase_ = std::numbers::pi_v<float>;
        return 100.0 * static_cast<double>(
            engine.vibratoSemitones(*selected, selected->fret));
    }

    struct StringModeSnapshot
    {
        int activeWithAllocator;
        int activeWithModeForcingAnUnfrettableString;
        bool ownNoteLandedOnItsOwnString;
        bool leakedOntoAnotherString;
        int activeAfterModeOff;
    };

    // Channel 6 is string index 5, the highest string in standard tuning; a
    // low note is far below any fret it can reach there.
    static StringModeSnapshot stringPerChannelBehaviour()
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        engine.prepare(48000.0, 64);
        engine.noteOn(40, 0.8f, 6);
        const int before = engine.getActiveVoiceCount();
        engine.allSoundOff(6);

        engine.setStringPerChannelMode(true);
        engine.noteOn(40, 0.8f, 6);
        const int forced = engine.getActiveVoiceCount();

        const int openLowE = engine.voices_[0].openMidi;
        engine.noteOn(openLowE + 2, 0.8f, 1);
        const bool landed = engine.voices_[0].played
            && engine.voices_[0].midiNote == openLowE + 2;
        bool leaked = false;
        for (int string = 1; string < AcustraEngine::stringCount; ++string)
            leaked |= engine.voices_[static_cast<std::size_t>(string)].played;

        engine.setStringPerChannelMode(false);
        engine.allSoundOff(1);
        engine.noteOn(40, 0.8f, 6);
        const int after = engine.getActiveVoiceCount();

        return { before, forced, landed, leaked, after };
    }

    static std::array<float, 8> bridgePortWaves(const AcustraEngine& engine)
    {
        const auto& bridge = engine.bridgeLoad_;
        return { bridge.displacement, bridge.rotation,
                 bridge.mainIntegratedForce, bridge.mainIntegratedMoment,
                 bridge.bodyIntegratedForce, bridge.bodyIntegratedMoment,
                 bridge.tailIntegratedForce, bridge.tailIntegratedMoment };
    }

    // The normal-polarisation line a fresh note writes, newest sample first,
    // over its round trip: what the bridge will read over the first period.
    static std::vector<double> pluckedLine(PhysicalCalibration calibration,
                                           PickingTechnique picking,
                                           int midiNote, float velocity,
                                           double rate = 48000.0,
                                           float touch = EngineParameters {}.touch)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        engine.setPhysicalCalibration(calibration);
        EngineParameters parameters;
        parameters.picking = picking;
        parameters.touch = touch;
        engine.setParameters(parameters);
        engine.prepare(rate, 64);
        engine.noteOn(midiNote, velocity);
        std::vector<double> line;
        for (const auto& voice : engine.voices_)
        {
            if (!voice.played || voice.midiNote != midiNote)
                continue;
            // The shape was written over the delay current at the pluck;
            // the steel attack-pitch glide has since moved targetDelay.
            const auto& loop = voice.loops[0];
            const int length = static_cast<int>(std::round(loop.currentDelay));
            for (int sample = 1; sample <= length; ++sample)
            {
                int index = loop.writeIndex - sample;
                while (index < 0)
                    index += AcustraEngine::maximumDelaySamples;
                line.push_back(loop.delay[static_cast<std::size_t>(index)]);
            }
        }
        return line;
    }

    // A stroke on one string at one fret, at a Pluck Position: the point it
    // met the string and the normal-polarisation line it wrote, newest sample
    // first, over the round trip (as pluckedLine). Every engine is fresh, so
    // every stroke draws the same take offset.
    static std::pair<double, std::vector<double>> pluckAtFret(
        PickingTechnique picking, float pluckPosition, int string, int fret)
    {
        auto engineOwner = std::make_unique<AcustraEngine>();
        auto& engine = *engineOwner;
        EngineParameters parameters;
        parameters.picking = picking;
        parameters.pluckPosition = pluckPosition;
        engine.setParameters(parameters);
        engine.prepare(48000.0, 64);
        engine.setStringPerChannelMode(true);
        auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        engine.noteOn(voice.openMidi + fret, 0.8f, string + 1);
        std::vector<double> line;
        const auto& loop = voice.loops[0];
        const int length = static_cast<int>(std::round(loop.currentDelay));
        for (int sample = 1; sample <= length; ++sample)
        {
            int index = loop.writeIndex - sample;
            while (index < 0)
                index += AcustraEngine::maximumDelaySamples;
            line.push_back(loop.delay[static_cast<std::size_t>(index)]);
        }
        return { static_cast<double>(voice.pluckPoint), line };
    }

    static std::vector<float> radiationHistory(const AcustraEngine& engine)
    {
        std::vector<float> state;
        for (const auto* bank : { &engine.bodyBank_, &engine.fadingBodyBank_ })
            for (int index = 0; index < bank->count; ++index)
            {
                const auto mode = static_cast<std::size_t>(index);
                state.push_back(bank->real[mode]);
                state.push_back(bank->imaginary[mode]);
                state.push_back(bank->momentReal[mode]);
                state.push_back(bank->momentImaginary[mode]);
            }
        for (const auto* derivative : {
                 &engine.bridgeVelocityDerivative_, &engine.bridgeRotationDerivative_,
                 &engine.bridgeForceDerivative_, &engine.bridgeForceMomentDerivative_,
                 &engine.bridgeBodyForceDerivative_, &engine.bridgeBodyMomentDerivative_,
                 &engine.bridgeTailForceDerivative_, &engine.bridgeTailMomentDerivative_ })
        {
            state.insert(state.end(), derivative->history.begin(), derivative->history.end());
            state.push_back(static_cast<float>(derivative->index));
        }
        return state;
    }

};
} // namespace acustra

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 127;
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct Audio
{
    std::vector<float> left;
    std::vector<float> right;
};

Audio renderAtRate(acustra::EngineParameters parameters, int midiNote,
                   float velocity, double seconds, double rate,
                   int renderBlock = blockSize,
                   bool bridgeCoupling = true,
                   acustra::PhysicalCalibration calibration
                       = acustra::fittedPhysicalCalibration)
{
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.setPhysicalCalibration(calibration);
    engine.prepare(rate, renderBlock);
    engine.setParameters(parameters);
    engine.setBridgeCouplingEnabled(bridgeCoupling);
    engine.noteOn(midiNote, velocity);
    const int samples = static_cast<int>(seconds * rate);
    Audio result { std::vector<float>(static_cast<std::size_t>(samples)),
                   std::vector<float>(static_cast<std::size_t>(samples)) };
    for (int offset = 0; offset < samples; offset += renderBlock)
    {
        const int count = std::min(renderBlock, samples - offset);
        engine.process(result.left.data() + offset,
                       result.right.data() + offset, count);
    }
    return result;
}

Audio render(acustra::EngineParameters parameters, int midiNote,
             float velocity, double seconds, int renderBlock = blockSize,
             bool bridgeCoupling = true)
{
    return renderAtRate(parameters, midiNote, velocity, seconds,
                        sampleRate, renderBlock, bridgeCoupling);
}

Audio renderCalibrated(acustra::EngineParameters parameters,
                       acustra::PhysicalCalibration calibration,
                       int midiNote, float velocity, double seconds)
{
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.setPhysicalCalibration(calibration);
    engine.setParameters(parameters);
    engine.prepare(sampleRate, blockSize);
    engine.noteOn(midiNote, velocity);
    const int samples = static_cast<int>(seconds * sampleRate);
    Audio result { std::vector<float>(static_cast<std::size_t>(samples)),
                   std::vector<float>(static_cast<std::size_t>(samples)) };
    for (int offset = 0; offset < samples; offset += blockSize)
    {
        const int count = std::min(blockSize, samples - offset);
        engine.process(result.left.data() + offset,
                       result.right.data() + offset, count);
    }
    return result;
}

Audio renderWithInitialParameters(acustra::EngineParameters parameters,
                                  int midiNote, float velocity,
                                  double seconds)
{
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.setParameters(parameters);
    engine.prepare(sampleRate, blockSize);
    engine.noteOn(midiNote, velocity);
    const int samples = static_cast<int>(seconds * sampleRate);
    Audio result { std::vector<float>(static_cast<std::size_t>(samples)),
                   std::vector<float>(static_cast<std::size_t>(samples)) };
    for (int offset = 0; offset < samples; offset += blockSize)
    {
        const int count = std::min(blockSize, samples - offset);
        engine.process(result.left.data() + offset,
                       result.right.data() + offset, count);
    }
    return result;
}

Audio renderWithSympatheticStrings(int midiNote, bool enabled,
                                   double seconds)
{
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.prepare(sampleRate, blockSize);
    engine.setSympatheticStringsEnabled(enabled);
    engine.noteOn(midiNote, 1.0f);
    const int samples = static_cast<int>(seconds * sampleRate);
    Audio result { std::vector<float>(static_cast<std::size_t>(samples)),
                   std::vector<float>(static_cast<std::size_t>(samples)) };
    for (int offset = 0; offset < samples; offset += blockSize)
    {
        const int count = std::min(blockSize, samples - offset);
        engine.process(result.left.data() + offset,
                       result.right.data() + offset, count);
    }
    return result;
}

double peak(const Audio& audio)
{
    double result = 0.0;
    for (std::size_t i = 0; i < audio.left.size(); ++i)
        result = std::max(result, static_cast<double>(std::max(
            std::abs(audio.left[i]), std::abs(audio.right[i]))));
    return result;
}

double peak(const Audio& audio, int begin, int end)
{
    begin = std::clamp(begin, 0, static_cast<int>(audio.left.size()));
    end = std::clamp(end, begin, static_cast<int>(audio.left.size()));
    double result = 0.0;
    for (int sample = begin; sample < end; ++sample)
    {
        const auto index = static_cast<std::size_t>(sample);
        result = std::max(result, static_cast<double>(std::max(
            std::abs(audio.left[index]), std::abs(audio.right[index]))));
    }
    return result;
}

double rms(const Audio& audio, int begin, int end)
{
    begin = std::clamp(begin, 0, static_cast<int>(audio.left.size()));
    end = std::clamp(end, begin, static_cast<int>(audio.left.size()));
    double energy = 0.0;
    for (int sample = begin; sample < end; ++sample)
    {
        const double value = 0.5 * (audio.left[static_cast<std::size_t>(sample)]
                                  + audio.right[static_cast<std::size_t>(sample)]);
        energy += value * value;
    }
    return std::sqrt(energy / std::max(1, end - begin));
}

double differenceRms(const Audio& a, const Audio& b, int begin, int end)
{
    const int size = static_cast<int>(
        std::min(a.left.size(), b.left.size()));
    begin = std::clamp(begin, 0, size);
    end = std::clamp(end, begin, size);
    double energy = 0.0;
    for (int sample = begin; sample < end; ++sample)
    {
        const auto index = static_cast<std::size_t>(sample);
        const double delta = 0.5
            * ((a.left[index] - b.left[index])
             + (a.right[index] - b.right[index]));
        energy += delta * delta;
    }
    return std::sqrt(energy / std::max(1, end - begin));
}

double differenceRms(const Audio& audio, int begin, int end)
{
    begin = std::clamp(begin, 1, static_cast<int>(audio.left.size()));
    end = std::clamp(end, begin, static_cast<int>(audio.left.size()));
    double energy = 0.0;
    for (int sample = begin; sample < end; ++sample)
    {
        const auto index = static_cast<std::size_t>(sample);
        const auto previous = static_cast<std::size_t>(sample - 1);
        const double value = 0.5
            * ((audio.left[index] - audio.left[previous])
             + (audio.right[index] - audio.right[previous]));
        energy += value * value;
    }
    return std::sqrt(energy / std::max(1, end - begin));
}

double onsetBandRms(const Audio& audio, double rate)
{
    constexpr double duration = 0.020;
    constexpr double highestFrequency = 12000.0;
    const int count = std::min(
        static_cast<int>(std::round(duration * rate)),
        static_cast<int>(audio.left.size()));
    if (count < 2)
        return 0.0;

    std::vector<double> windowed(static_cast<std::size_t>(count));
    double windowEnergy = 0.0;
    for (int sample = 0; sample < count; ++sample)
    {
        const auto index = static_cast<std::size_t>(sample);
        const double mono = 0.5 * (audio.left[index] + audio.right[index]);
        const double window = 0.5 - 0.5 * std::cos(
            2.0 * std::numbers::pi * sample / (count - 1));
        windowed[index] = mono * window;
        windowEnergy += window * window;
    }

    double bandEnergy = 0.0;
    const int highestBin = static_cast<int>(
        std::floor(highestFrequency * duration));
    for (int bin = 1; bin <= highestBin; ++bin)
    {
        const double angle = -2.0 * std::numbers::pi * bin / count;
        const double stepReal = std::cos(angle);
        const double stepImaginary = std::sin(angle);
        double oscillatorReal = 1.0;
        double oscillatorImaginary = 0.0;
        double real = 0.0;
        double imaginary = 0.0;
        for (const double value : windowed)
        {
            real += value * oscillatorReal;
            imaginary += value * oscillatorImaginary;
            const double nextReal = oscillatorReal * stepReal
                                  - oscillatorImaginary * stepImaginary;
            oscillatorImaginary = oscillatorReal * stepImaginary
                                 + oscillatorImaginary * stepReal;
            oscillatorReal = nextReal;
        }
        bandEnergy += 2.0 * (real * real + imaginary * imaginary);
    }
    return std::sqrt(bandEnergy / (count * windowEnergy));
}

double tailBandRms(const Audio& audio, double rate, double beginSeconds,
                   double endSeconds, double lowFrequency,
                   double highFrequency)
{
    const int begin = std::clamp(static_cast<int>(beginSeconds * rate), 0,
                                 static_cast<int>(audio.left.size()));
    const int end = std::clamp(static_cast<int>(endSeconds * rate), begin,
                               static_cast<int>(audio.left.size()));
    const int count = end - begin;
    if (count < 64)
        return 0.0;
    const double duration = count / rate;
    std::vector<double> windowed(static_cast<std::size_t>(count));
    double windowEnergy = 0.0;
    for (int sample = 0; sample < count; ++sample)
    {
        const auto index = static_cast<std::size_t>(begin + sample);
        const double mono = 0.5 * (audio.left[index] + audio.right[index]);
        const double window = 0.5 - 0.5 * std::cos(
            2.0 * std::numbers::pi * sample / (count - 1));
        windowed[static_cast<std::size_t>(sample)] = mono * window;
        windowEnergy += window * window;
    }
    const int lowestBin = std::max(1,
        static_cast<int>(std::ceil(lowFrequency * duration)));
    const int highestBin = std::min(count / 2 - 1,
        static_cast<int>(std::floor(highFrequency * duration)));
    double bandEnergy = 0.0;
    for (int bin = lowestBin; bin <= highestBin; ++bin)
    {
        const double angle = -2.0 * std::numbers::pi * bin / count;
        const double stepReal = std::cos(angle);
        const double stepImaginary = std::sin(angle);
        double oscillatorReal = 1.0;
        double oscillatorImaginary = 0.0;
        double real = 0.0;
        double imaginary = 0.0;
        for (const double value : windowed)
        {
            real += value * oscillatorReal;
            imaginary += value * oscillatorImaginary;
            const double nextReal = oscillatorReal * stepReal
                                  - oscillatorImaginary * stepImaginary;
            oscillatorImaginary = oscillatorReal * stepImaginary
                                 + oscillatorImaginary * stepReal;
            oscillatorReal = nextReal;
        }
        bandEnergy += 2.0 * (real * real + imaginary * imaginary);
    }
    return std::sqrt(bandEnergy / (count * windowEnergy));
}

double stereoDifferenceRms(const Audio& audio, int begin, int end)
{
    begin = std::clamp(begin, 0, static_cast<int>(audio.left.size()));
    end = std::clamp(end, begin, static_cast<int>(audio.left.size()));
    double energy = 0.0;
    for (int sample = begin; sample < end; ++sample)
    {
        const auto index = static_cast<std::size_t>(sample);
        const double difference = audio.left[index] - audio.right[index];
        energy += difference * difference;
    }
    return std::sqrt(energy / std::max(1, end - begin));
}

double normalisedDifference(const Audio& a, const Audio& b)
{
    const auto count = std::min(a.left.size(), b.left.size());
    double difference = 0.0;
    double reference = 0.0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const double av = 0.5 * (a.left[i] + a.right[i]);
        const double bv = 0.5 * (b.left[i] + b.right[i]);
        const double delta = av - bv;
        difference += delta * delta;
        reference += av * av + bv * bv;
    }
    return std::sqrt(difference / std::max(reference, 1.0e-30));
}

double spectralPeakFrequency(const Audio& audio, double expectedHz,
                             double searchCents, double beginSeconds,
                             double endSeconds, double rate = sampleRate)
{
    const int begin = std::clamp(
        static_cast<int>(beginSeconds * rate), 0,
        static_cast<int>(audio.left.size()));
    const int end = std::clamp(
        static_cast<int>(endSeconds * rate), begin,
        static_cast<int>(audio.left.size()));
    std::vector<double> windowed(static_cast<std::size_t>(end - begin));
    for (int sample = begin; sample < end; ++sample)
    {
        const auto index = static_cast<std::size_t>(sample);
        const int local = sample - begin;
        const double window = 0.5 - 0.5 * std::cos(
            2.0 * std::numbers::pi * local
            / std::max(1, end - begin - 1));
        windowed[static_cast<std::size_t>(local)] = 0.5 * window
            * (audio.left[index] + audio.right[index]);
    }

    const auto powerAt = [&] (double frequency)
    {
        const double angle = -2.0 * std::numbers::pi
                           * frequency / rate;
        const double stepReal = std::cos(angle);
        const double stepImaginary = std::sin(angle);
        double oscillatorReal = 1.0;
        double oscillatorImaginary = 0.0;
        double real = 0.0;
        double imaginary = 0.0;
        for (const double value : windowed)
        {
            real += value * oscillatorReal;
            imaginary += value * oscillatorImaginary;
            const double nextReal = oscillatorReal * stepReal
                                  - oscillatorImaginary * stepImaginary;
            oscillatorImaginary = oscillatorReal * stepImaginary
                                 + oscillatorImaginary * stepReal;
            oscillatorReal = nextReal;
        }
        return real * real + imaginary * imaginary;
    };

    double lower = expectedHz * std::exp2(-searchCents / 1200.0);
    double upper = expectedHz * std::exp2(searchCents / 1200.0);
    constexpr double golden = 0.6180339887498948482;
    double left = upper - golden * (upper - lower);
    double right = lower + golden * (upper - lower);
    double leftPower = powerAt(left);
    double rightPower = powerAt(right);
    for (int iteration = 0; iteration < 42; ++iteration)
    {
        if (leftPower < rightPower)
        {
            lower = left;
            left = right;
            leftPower = rightPower;
            right = lower + golden * (upper - lower);
            rightPower = powerAt(right);
        }
        else
        {
            upper = right;
            right = left;
            rightPower = leftPower;
            left = upper - golden * (upper - lower);
            leftPower = powerAt(left);
        }
    }
    return 0.5 * (lower + upper);
}

double spectralAmplitudeAt(const Audio& audio, double frequency,
                           double beginSeconds, double endSeconds,
                           double rate = sampleRate)
{
    const int begin = std::clamp(
        static_cast<int>(beginSeconds * rate), 0,
        static_cast<int>(audio.left.size()));
    const int end = std::clamp(
        static_cast<int>(endSeconds * rate), begin,
        static_cast<int>(audio.left.size()));
    const double angle = -2.0 * std::numbers::pi * frequency / rate;
    const double stepReal = std::cos(angle);
    const double stepImaginary = std::sin(angle);
    double oscillatorReal = 1.0;
    double oscillatorImaginary = 0.0;
    double real = 0.0;
    double imaginary = 0.0;
    for (int sample = begin; sample < end; ++sample)
    {
        const int local = sample - begin;
        const double window = 0.5 - 0.5 * std::cos(
            2.0 * std::numbers::pi * local / std::max(1, end - begin - 1));
        const auto index = static_cast<std::size_t>(sample);
        const double value = 0.5 * window
            * (audio.left[index] + audio.right[index]);
        real += value * oscillatorReal;
        imaginary += value * oscillatorImaginary;
        const double nextReal = oscillatorReal * stepReal
                              - oscillatorImaginary * stepImaginary;
        oscillatorImaginary = oscillatorReal * stepImaginary
                             + oscillatorImaginary * stepReal;
        oscillatorReal = nextReal;
    }
    return std::hypot(real, imaginary);
}

double spectralDecayRate(const Audio& audio, double expectedHz,
                         double searchCents, double rate)
{
    // A tension glide moves the partial between these windows. Measuring
    // both at the early frequency treats detuning as lost amplitude; locate
    // the partial again in the late window before comparing equal-length
    // Hann-window amplitudes, whose centres are 0.8 s apart.
    const double earlyHz = spectralPeakFrequency(
        audio, expectedHz, searchCents, 0.20, 0.60, rate);
    const double lateHz = spectralPeakFrequency(
        audio, expectedHz, searchCents, 1.00, 1.40, rate);
    return 20.0 * std::log10(
        spectralAmplitudeAt(audio, earlyHz, 0.20, 0.60, rate)
        / spectralAmplitudeAt(audio, lateHz, 1.00, 1.40, rate)) / 0.8;
}

void testDecayEstimatorFollowsPitchGlides()
{
    constexpr double decayDbPerSecond = 8.0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (double glideHzPerSecond : { -3.0, 0.0, 3.0 })
        {
            const int samples = static_cast<int>(1.6 * rate);
            Audio audio { std::vector<float>(static_cast<std::size_t>(samples)),
                          std::vector<float>(static_cast<std::size_t>(samples)) };
            for (int sample = 0; sample < samples; ++sample)
            {
                const double t = sample / rate;
                const float value = static_cast<float>(
                    std::pow(10.0, -decayDbPerSecond * t / 20.0)
                    * std::cos(2.0 * std::numbers::pi
                        * (990.0 * t + 0.5 * glideHzPerSecond * t * t)));
                audio.left[static_cast<std::size_t>(sample)] = value;
                audio.right[static_cast<std::size_t>(sample)] = value;
            }
            const double measured = spectralDecayRate(audio, 990.0, 30.0, rate);
            expect(std::abs(measured - decayDbPerSecond) < 0.001,
                   "a pitch glide was mistaken for exponential amplitude loss");
        }
}

void testSilenceAndFiniteOutput()
{
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.prepare(sampleRate, blockSize);
    std::vector<float> left(blockSize, 1.0f);
    std::vector<float> right(blockSize, 1.0f);
    engine.process(left.data(), right.data(), blockSize);
    expect(std::all_of(left.begin(), left.end(), [] (float value)
        { return value == 0.0f; }), "a reset engine was not exactly silent");
    expect(std::all_of(right.begin(), right.end(), [] (float value)
        { return value == 0.0f; }), "right reset channel was not exactly silent");

    engine.noteOn(40, 0.9f);
    engine.process(left.data(), right.data(), blockSize);
    expect(std::all_of(left.begin(), left.end(), [] (float value)
        { return std::isfinite(value); }), "note output contained NaN or infinity");
}

// prepare() models 8 to 384 kHz. A finite rate outside that is clamped to
// the nearer end, so pitch changes continuously across the bounds, and only
// a rate that is no rate at all (NaN, infinite, zero or negative) falls back
// to 48 kHz (audit F31).
void testUnsupportedSampleRatesClampToTheModelledRange()
{
    const auto play = [] (double rate)
    {
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->prepare(rate, 128);
        engine->noteOn(69, 0.8f);
        std::vector<float> left(128), right(128), all;
        for (int block = 0; block < 20; ++block)
        {
            engine->process(left.data(), right.data(), 128);
            all.insert(all.end(), left.begin(), left.end());
            all.insert(all.end(), right.begin(), right.end());
        }
        return std::pair { engine->sampleRate(), all };
    };
    const auto low = play(8000.0);
    const auto high = play(384000.0);
    const auto fallback = play(48000.0);
    for (const double rate : { 4000.0, 7999.0, 1.0 })
        expect(play(rate) == low, "a finite rate of " + std::to_string(rate)
               + " Hz was not clamped to the 8 kHz bound");
    expect(play(768000.0) == high, "768 kHz was not clamped to 384 kHz");
    for (const double rate : { std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity(),
                               0.0, -44100.0 })
        expect(play(rate) == fallback,
               "a rate that is no rate did not fall back to 48 kHz");
    expect(low.first == 8000.0 && fallback.first == 48000.0,
           "sampleRate() did not report the rate the engine models");
}

// prepare() starts a performance over: an engine that has played, and been
// prepared at other rates in between, then plays exactly as a new one does,
// strum timing and contact noise included, so a host's re-prepare before an
// offline bounce cannot change it (audit F26). reset() alone keeps drawing,
// so repeated strums after a panic still vary.
void testPrepareRestartsThePerformanceExactly()
{
    struct Config { const char* name; acustra::EngineParameters parameters; };
    std::vector<Config> configs;
    configs.push_back({ "default", {} });
    {
        acustra::EngineParameters bellido;
        bellido.guitarModel = acustra::GuitarModel::Bellido1978;
        configs.push_back({ "Bellido 1978", bellido });
        acustra::EngineParameters jumbo;
        jumbo.shape = acustra::BodyShape::Jumbo;
        jumbo.bodyMaterial = acustra::BodyMaterial::Maple;
        jumbo.picking = acustra::PickingTechnique::Pick;
        configs.push_back({ "jumbo maple pick", jumbo });
    }
    const auto strum = [] (acustra::AcustraEngine& engine, float seconds)
    {
        engine.beginStrum();
        const int notes[] { 45, 52, 57, 61, 64 };
        for (int rank = 0; rank < 5; ++rank)
            engine.noteOn(notes[rank], 0.8f, 1,
                          engine.strumDelaySamples(rank, 0.8f), true);
        std::vector<float> left(64), right(64), all;
        const int blocks = static_cast<int>(seconds * engine.sampleRate() / 64.0);
        for (int block = 0; block < blocks; ++block)
        {
            if (block == blocks / 2)
                engine.noteOn(57, 0.6f);
            engine.process(left.data(), right.data(), 64);
            all.insert(all.end(), left.begin(), left.end());
            all.insert(all.end(), right.begin(), right.end());
        }
        return all;
    };
    for (const auto& config : configs)
    {
        auto fresh = std::make_unique<acustra::AcustraEngine>();
        fresh->setParameters(config.parameters);
        fresh->prepare(48000.0, 64);
        const auto reference = strum(*fresh, 0.5f);

        auto replayed = std::make_unique<acustra::AcustraEngine>();
        replayed->setParameters(config.parameters);
        replayed->prepare(48000.0, 64);
        static_cast<void>(strum(*replayed, 0.3f));
        replayed->prepare(96000.0, 64);
        static_cast<void>(strum(*replayed, 0.1f));
        replayed->prepare(44100.0, 64);
        replayed->prepare(48000.0, 64);
        expect(strum(*replayed, 0.5f) == reference,
               std::string(config.name) + ": a re-prepared engine did not "
               "play exactly as a new one");

        // A panic is not a new performance: the next strum draws on.
        fresh->reset();
        expect(strum(*fresh, 0.5f) != reference,
               std::string(config.name) + ": reset() repeated the last strum");
    }
}

constexpr acustra::Tuning allTunings[] { acustra::Tuning::Standard,
    acustra::Tuning::DropD, acustra::Tuning::Dadgad, acustra::Tuning::OpenG,
    acustra::Tuning::HalfStepDown };

std::string tuningName(acustra::Tuning tuning)
{
    constexpr const char* names[] { "Standard", "Drop D", "DADGAD", "Open G",
                                    "Half-step down" };
    return names[static_cast<int>(tuning)];
}

// Every tuning: each open note is taken by its own string (open, so the
// chord of open strings lands one per string), and the note under the lowest
// string is refused. Open G and Half-step down were played by no test before
// (audit F28).
void testPlayableRangeFollowsTuning()
{
    for (const auto tuning : allTunings)
    {
        const std::string name = tuningName(tuning);
        acustra::EngineParameters parameters;
        parameters.tuning = tuning;
        const auto open = acustra::AcustraEngine::openNotes(tuning);
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        engine.setParameters(parameters);
        engine.prepare(sampleRate, blockSize);
        engine.noteOn(open[0] - 1, 0.8f);
        expect(engine.getActiveVoiceCount() == 0,
               name + " accepted a note below its lowest string");
        for (int string = 0; string < acustra::AcustraEngine::stringCount; ++string)
        {
            const int note = open[static_cast<std::size_t>(string)];
            engine.noteOn(note, 0.8f);
            expect(engine.heldString(note) == string,
                   name + " did not play open note " + std::to_string(note)
                       + " on its own string");
        }
        expect(engine.getActiveVoiceCount() == acustra::AcustraEngine::stringCount,
               name + " did not ring all six open strings");
    }
}

// A retuned steel string keeps its gauge: against Standard its impedance
// follows the frequency ratio (the same linear mass), its tension the ratio
// squared and its inharmonicity the inverse, on every string every tuning
// moves.
void testSteelRetuningPreservesStringMass()
{
    const auto standardNotes = acustra::AcustraEngine::openNotes(acustra::Tuning::Standard);
    int retuned = 0;
    for (const auto tuning : allTunings)
    {
        const auto notes = acustra::AcustraEngine::openNotes(tuning);
        for (int string = 0; string < acustra::AcustraEngine::stringCount; ++string)
        {
            const int semitones = notes[static_cast<std::size_t>(string)]
                                - standardNotes[static_cast<std::size_t>(string)];
            if (semitones == 0)
                continue;
            ++retuned;
            const auto standard = acustra::AcustraEngineTestAccess::retunedString(
                acustra::Tuning::Standard, string);
            const auto moved = acustra::AcustraEngineTestAccess::retunedString(
                tuning, string);
            const double frequencyRatio = std::exp2(semitones / 12.0);
            const std::string name = tuningName(tuning) + " string "
                + std::to_string(string + 1);
            expect(std::abs(moved.impedance / standard.impedance - frequencyRatio)
                       < 2.0e-5,
                   name + ": steel impedance did not preserve linear mass");
            expect(std::abs(moved.tailStiffness / standard.tailStiffness
                            - frequencyRatio * frequencyRatio) < 2.0e-5,
                   name + ": steel tension did not follow the retuned frequency");
            expect(std::abs(moved.inharmonicity / standard.inharmonicity
                            - 1.0 / (frequencyRatio * frequencyRatio)) < 2.0e-4,
                   name + ": steel stiffness retained standard-tuning tension");
        }
    }
    // Drop D moves one string, DADGAD three, Open G three, Half-step six.
    expect(retuned == 13, "the tunings retune " + std::to_string(retuned)
                              + " strings, not 13");
}

void testAudiblePhysicalDecay()
{
    const auto audio = render({}, 40, 0.9f, 2.0);
    const double ordinaryPeak = peak(audio);
    expect(ordinaryPeak > 0.015,
           "a steel E2 pluck fell below the calibrated output reference: "
               + std::to_string(ordinaryPeak));
    const double early = rms(audio, 1200, 7200);
    const double late = rms(audio, 72000, 90000);
    expect(early > 1.0e-7, "pluck had no measurable early body response");
    expect(late < early, "an unforced held string gained energy over time");
    expect(ordinaryPeak < 2.0,
           "ordinary pluck exceeded the safety headroom");
}

void testPhysicalPluckOnsetIsBounded()
{
    acustra::EngineParameters parameters;
    parameters.touch = 0.72f;
    for (int midiNote = 40; midiNote <= 84; ++midiNote)
    {
        const auto audio = render(parameters, midiNote, 1.0f, 0.10);
        // The released-from-rest initial condition and derivative priming
        // must not manufacture a keyed discontinuity at note-on.
        expect(std::max(std::abs(audio.left.front()),
                        std::abs(audio.right.front())) < 1.0e-9f,
               "physical pluck did not begin from rest at MIDI "
                   + std::to_string(midiNote));
        double maximumStep = 0.0;
        const int end = static_cast<int>(0.100 * sampleRate);
        for (int sample = 1; sample < end; ++sample)
        {
            const auto index = static_cast<std::size_t>(sample);
            const auto previous = static_cast<std::size_t>(sample - 1);
            maximumStep = std::max(maximumStep,
                static_cast<double>(std::max(
                    std::abs(audio.left[index] - audio.left[previous]),
                    std::abs(audio.right[index] - audio.right[previous]))));
        }
        // The physical pick/contact burst can have a steep legitimate
        // derivative, but must remain inside the safety envelope.
        expect(maximumStep < 0.25,
               "physical pluck exceeded the bounded transient gate at MIDI "
                   + std::to_string(midiNote) + ": "
                   + std::to_string(maximumStep));
    }
}

void testBridgeObservableIsSampleRateNormalised()
{
    auto bodyOnlyCalibration = acustra::fittedPhysicalCalibration;
    bodyOnlyCalibration.directGain = 0.0f;
    for (const bool anchoredTouch : { false, true })
    {
        acustra::EngineParameters parameters;
        if (anchoredTouch)
            parameters.touch = 0.72f;
        for (const int midiNote : { 40, 52, 64, 83 })
        {
            const auto reference = renderAtRate(
                parameters, midiNote, 0.84f, 0.25, 48000.0,
                blockSize, true, bodyOnlyCalibration);
            const double referenceRms = rms(reference, 2400, 9600);
            const double referenceBand = onsetBandRms(reference, 48000.0);
            const double referencePeak = peak(reference, 0, 960);
            const double referenceEdge = differenceRms(reference, 1, 960)
                / std::max(rms(reference, 0, 960), 1.0e-12);
            for (const double rate : { 44100.0, 96000.0,
                                       192000.0, 384000.0 })
            {
                const auto audio = renderAtRate(
                    parameters, midiNote, 0.84f, 0.25, rate,
                    blockSize, true, bodyOnlyCalibration);
                const double levelRatio = rms(audio,
                    static_cast<int>(0.05 * rate),
                    static_cast<int>(0.20 * rate))
                    / std::max(referenceRms, 1.0e-12);
                const double bandRatio = onsetBandRms(audio, rate)
                    / std::max(referenceBand, 1.0e-12);
                const double onsetRatio = peak(audio, 0,
                    static_cast<int>(0.02 * rate))
                    / std::max(referencePeak, 1.0e-12);
                const double edge = differenceRms(audio, 1,
                    static_cast<int>(0.02 * rate))
                    * (rate / 48000.0)
                    / std::max(rms(audio, 0,
                        static_cast<int>(0.02 * rate)), 1.0e-12);
                const double edgeRatio = edge
                    / std::max(referenceEdge, 1.0e-12);
                const std::string context = "MIDI "
                    + std::to_string(midiNote) + " at "
                    + std::to_string(static_cast<int>(rate)) + " Hz"
                    + (anchoredTouch ? " anchored touch" : " default touch");
                expect(levelRatio > 0.78 && levelRatio < 1.30,
                       "sustain level changed with host rate for " + context
                           + ": " + std::to_string(levelRatio));
                expect(bandRatio > 0.80 && bandRatio < 1.20,
                       "audible onset energy changed with host rate for "
                           + context + ": " + std::to_string(bandRatio));
                expect(onsetRatio < 1.43,
                       "onset peak grew with host rate for " + context
                           + ": " + std::to_string(onsetRatio));
                expect(edgeRatio < 1.60,
                       "onset edge grew with host rate for " + context
                           + ": " + std::to_string(edgeRatio));
            }
        }
    }
}

void testZeroWidthCollapsesToMono()
{
    acustra::EngineParameters parameters;
    parameters.stereoWidth = 0.0f;
    const auto audio = render(parameters, 52, 0.8f, 0.4);
    const int begin = static_cast<int>(0.18 * sampleRate);
    const double mono = rms(audio, begin, static_cast<int>(audio.left.size()));
    const double difference = stereoDifferenceRms(
        audio, begin, static_cast<int>(audio.left.size()));
    expect(difference < 1.0e-3 * std::max(mono, 1.0e-12),
           "Stereo Width zero did not collapse all radiation paths to mono");
}

void testReleaseEventuallyReturnsTheString()
{
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.prepare(sampleRate, blockSize);
    engine.noteOn(52, 0.8f); // fretted, therefore finger-damped on release
    std::vector<float> left(blockSize);
    std::vector<float> right(blockSize);
    for (int block = 0; block < 40; ++block)
        engine.process(left.data(), right.data(), blockSize);
    engine.noteOff(52);
    for (int block = 0; block < 1200; ++block)
        engine.process(left.data(), right.data(), blockSize);
    expect(engine.getActiveVoiceCount() == 0,
           "released fretted string never returned to sympathetic open state");
}

void testPhysicalVoiceOwnsReleaseLifecycle()
{
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.prepare(sampleRate, blockSize);
    std::vector<float> left(blockSize);
    std::vector<float> right(blockSize);

    engine.noteOn(52, 0.8f);
    engine.noteOn(52, 0.7f);
    engine.noteOff(52);
    for (int block = 0; block < 140; ++block)
        engine.process(left.data(), right.data(), blockSize);
    expect(engine.getActiveVoiceCount() == 1,
           "one duplicate-note owner released both key instances");

    engine.noteOff(52);
    for (int block = 0; block < 115; ++block)
        engine.process(left.data(), right.data(), blockSize);
    expect(engine.getActiveVoiceCount() == 0,
           "allocator ownership outlived the audible physical release");

    engine.noteOn(52, 0.8f);
    engine.setSustainPedal(true);
    engine.noteOff(52);
    for (int block = 0; block < 140; ++block)
        engine.process(left.data(), right.data(), blockSize);
    expect(engine.getActiveVoiceCount() == 1,
           "sustain pedal failed to retain physical-note ownership");
    engine.setSustainPedal(false);
    for (int block = 0; block < 115; ++block)
        engine.process(left.data(), right.data(), blockSize);
    expect(engine.getActiveVoiceCount() == 0,
           "pedal-up did not release physical-note ownership");
}

void testMidiChannelOwnershipAndAdditiveBend()
{
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.prepare(sampleRate, blockSize);
    engine.noteOn(52, 0.8f, 2);
    engine.noteOn(52, 0.8f, 3);
    expect(engine.getActiveVoiceCount() == 2,
           "same pitch on two member channels shared one owner");
    engine.noteOff(52, 2);
    engine.allSoundOff(2);
    expect(engine.getActiveVoiceCount() == 1,
           "member Note Off or All Sound Off affected another owner");

    const double additive = acustra::AcustraEngineTestAccess::channelBentDelay(
        0.5f, 0.5f);
    const double masterOnly = acustra::AcustraEngineTestAccess::channelBentDelay(
        1.0f, 0.0f);
    const double memberOnly = acustra::AcustraEngineTestAccess::channelBentDelay(
        0.0f, 0.5f);
    // Re-pinned by the slide-or-bend convention (see configureVoice): the
    // manager's bend is a slide and a member's own bend is a string bend, so
    // the two stay additive in pitch while only the member half moves the
    // tension - and through it the inharmonicity the loop's tuning solves
    // against.
    std::cout << "Acustra additive bend: steel " << additive << " vs "
              << masterOnly << " vs " << memberOnly << "\n";
    // The pair differs in the loop's tuned delay - 0.28 samples in 262
    // - because the member half's tension changes the inharmonicity the
    // tuning solves against, not because the pitch differs: the delay is
    // whatever puts the fundamental where it was asked for. That the pitch
    // itself is additive is measured in
    // testAMemberBendIsATensionBendByGrimes, which can resolve the loop.
    expect(std::abs(additive - masterOnly) < 0.5,
           "a member bend's tension moved the tuned delay by "
               + std::to_string(std::abs(additive - masterOnly))
               + " samples against the same interval slid");
    expect(std::abs(additive - memberOnly) > 1.0e-3,
           "member pitch bend did not reach its owned string");

    const double conventionalBaseline =
        acustra::AcustraEngineTestAccess::conventionalChannelDelay(0.0f, 0.0f);
    const double unrelatedMaster =
        acustra::AcustraEngineTestAccess::conventionalChannelDelay(12.0f, 0.0f);
    const double ownedConventional =
        acustra::AcustraEngineTestAccess::conventionalChannelDelay(0.0f, 12.0f);
    expect(std::abs(conventionalBaseline - unrelatedMaster) < 1.0e-6
               && std::abs(conventionalBaseline - ownedConventional) > 1.0e-3,
           "conventional channel 1 acted as an MPE master before zone setup");

    const auto mpe = acustra::AcustraEngineTestAccess::bendLifecycle(true);
    expect(mpe.frozen && mpe.pedalHeld
               && std::abs(mpe.frozenMemberBend - 5.0f) < 1.0e-6f,
           "MPE Note Off did not latch the performed member bend under pedal");
    expect(std::abs(mpe.heldDelay - mpe.afterMemberTrafficDelay) < 1.0e-6,
           "idle member wheel traffic retuned a released MPE tail");
    expect(mpe.afterMasterTrafficDelay < mpe.afterMemberTrafficDelay - 1.0e-3,
           "live MPE master bend did not move the frozen member tail");
    expect(!mpe.reusedFingerFrozen && mpe.reusedFingerDelay
               > mpe.afterMasterTrafficDelay + 1.0e-3,
           "a reused MPE channel inherited the old tail's frozen member bend");

    const auto conventional =
        acustra::AcustraEngineTestAccess::bendLifecycle(false);
    expect(!conventional.frozen
               && std::abs(conventional.heldDelay
                           - conventional.afterMemberTrafficDelay) > 1.0e-3,
           "a conventional release tail stopped following its channel wheel");

    expect(acustra::AcustraEngineTestAccess::lowerZoneTransitionCounts()
               == std::array<int, 3> { 1, 0, 0 },
           "lower-zone enable/resize/deactivate did not stop affected channels");
    expect(acustra::AcustraEngineTestAccess::allSoundOffPreservesControllers(),
           "All Sound Off reset pitch-bend or sustain controller state");
    const double maximumUp =
        acustra::AcustraEngineTestAccess::channelBentDelay(96.0f, 96.0f);
    const double maximumDown =
        acustra::AcustraEngineTestAccess::channelBentDelay(-96.0f, -96.0f);
    expect(std::isfinite(maximumUp) && std::isfinite(maximumDown)
               && maximumUp >= 3.0 && maximumDown <= 8189.0,
           "legal 96-semitone MPE ranges escaped the finite delay bounds");
}

void testSharedBodyExcitesIdleStrings()
{
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.prepare(sampleRate, blockSize);
    // E3 is played on the D string, leaving low E idle at the note's second
    // harmonic. This resonant case exposes the frequency-selective coupling.
    engine.noteOn(52, 1.0f);
    std::vector<float> left(blockSize);
    std::vector<float> right(blockSize);
    int maximumSympatheticStrings = 0;
    for (int block = 0; block < 220; ++block)
    {
        engine.process(left.data(), right.data(), blockSize);
        maximumSympatheticStrings = std::max(maximumSympatheticStrings,
            engine.getSympatheticStringCount());
    }
    expect(maximumSympatheticStrings > 0,
           "shared bridge/body did not excite any idle open string");
}

void testSympatheticStringsAreAudibleButBounded()
{
    auto activeOnOwner = std::make_unique<acustra::AcustraEngine>();
    auto& activeOn = *activeOnOwner;
    auto activeOffOwner = std::make_unique<acustra::AcustraEngine>();
    auto& activeOff = *activeOffOwner;
    activeOn.prepare(sampleRate, blockSize);
    activeOff.prepare(sampleRate, blockSize);
    activeOff.setSympatheticStringsEnabled(false);
    activeOn.noteOn(52, 1.0f);
    activeOff.noteOn(52, 1.0f);
    std::vector<float> probeOnLeft(blockSize);
    std::vector<float> probeOnRight(blockSize);
    std::vector<float> probeOffLeft(blockSize);
    std::vector<float> probeOffRight(blockSize);
    double maximumActiveForce = 0.0;
    double maximumActiveForceDifference = 0.0;
    for (int block = 0; block < 128; ++block)
    {
        activeOn.process(probeOnLeft.data(), probeOnRight.data(), blockSize);
        activeOff.process(probeOffLeft.data(), probeOffRight.data(), blockSize);
        const double onForce = activeOn.getLastBridgeBodyForce();
        const double offForce = activeOff.getLastBridgeBodyForce();
        maximumActiveForce = std::max(maximumActiveForce,
                                      std::abs(onForce));
        maximumActiveForceDifference = std::max(
            maximumActiveForceDifference, std::abs(onForce - offForce));
    }
    // The idle strings are members of the junction, so taking them out
    // changes the load the played string sees: the bypass must move the
    // bridge force, and it must move it by less than the note itself.
    expect(maximumActiveForceDifference > 1.0e-6 * maximumActiveForce,
           "sympathetic bypass did not unload the bridge");
    expect(maximumActiveForceDifference < maximumActiveForce,
           "sympathetic bypass changed the bridge force by more than the note");

    constexpr double seconds = 4.0;
    const int begin = static_cast<int>(0.30 * sampleRate);
    const int end = static_cast<int>(seconds * sampleRate);
    const auto resonantOn = renderWithSympatheticStrings(52, true, seconds);
    const auto resonantOff = renderWithSympatheticStrings(52, false, seconds);
    const auto offResonantOn = renderWithSympatheticStrings(53, true, seconds);
    const auto offResonantOff = renderWithSympatheticStrings(53, false, seconds);
    const double resonantOnRms = rms(resonantOn, begin, end);
    const double resonantOffRms = rms(resonantOff, begin, end);
    const double offResonantOnRms = rms(offResonantOn, begin, end);
    const double resonantRatio = differenceRms(
        resonantOn, resonantOff, begin, end)
        / std::max(resonantOnRms, 1.0e-12);
    const double offResonantRatio = differenceRms(
        offResonantOn, offResonantOff, begin, end)
        / std::max(offResonantOnRms, 1.0e-12);
    std::cout << "Acustra sympathetic tail ratios: resonant="
              << resonantRatio << ", off-resonant=" << offResonantRatio
              << ", resonant RMS on/off=" << resonantOnRms << '/'
              << resonantOffRms << '\n';
    expect(resonantRatio > 0.05,
           "resonant open-string radiation was below -26 dB of the tail");
    // This ceiling is on the balance of the tail, not on the coupling. It
    // moves with the body: holding the sympathetic path fixed and only
    // tilting the body's radiation down takes it from 0.806 to 0.927, while
    // doubling the low-mode gain at a fixed tilt moves it 0.806 to 0.819. A
    // darker, bassier body favours the open strings' long low ring over a
    // fretted note's own tail, which is a guitar doing what a guitar does.
    // It was re-pinned from 0.90 when a listening verdict chose that body.
    expect(offResonantRatio < 0.30,
           "off-resonant open strings coloured too much of the tail");
    // The assertions that make this a sympathy test rather than a loudness
    // one are these two, and they are tightened in exchange.
    expect(resonantRatio > 5.0 * offResonantRatio,
           "open-string radiation did not discriminate a harmonic match");
}

void testPassiveBridgeBranchesBalance()
{
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.prepare(sampleRate, 1);
    acustra::EngineParameters parameters;
    parameters.touch = 0.72f;
    engine.setParameters(parameters);
    engine.noteOn(40, 0.72f);

    double totalWork = 0.0;
    double bodyWork = 0.0;
    double tailWork = 0.0;
    double minimumTotalWork = 0.0;
    double minimumBodyWork = 0.0;
    double minimumTailWork = 0.0;
    double maximumTailWork = 0.0;
    double maximumForce = 0.0;
    double maximumBalanceError = 0.0;
    for (int sample = 0; sample < static_cast<int>(4.0 * sampleRate);
         ++sample)
    {
        float left = 0.0f;
        float right = 0.0f;
        engine.process(&left, &right, 1);
        totalWork += engine.getLastBridgePower() / sampleRate;
        bodyWork += engine.getLastBridgeBodyPower() / sampleRate;
        tailWork += engine.getLastBridgeTailPower() / sampleRate;
        minimumTotalWork = std::min(minimumTotalWork, totalWork);
        minimumBodyWork = std::min(minimumBodyWork, bodyWork);
        minimumTailWork = std::min(minimumTailWork, tailWork);
        maximumTailWork = std::max(maximumTailWork, tailWork);
        const double totalForce = engine.getLastBridgeReactionForce();
        const double branchForce = engine.getLastBridgeBodyForce()
                                 + engine.getLastBridgeTailForce();
        maximumForce = std::max(maximumForce, std::abs(totalForce));
        maximumBalanceError = std::max(maximumBalanceError,
            std::abs(totalForce - branchForce));
    }

    expect(minimumTotalWork >= -1.0e-14,
           "bridge termination generated cumulative work");
    expect(minimumBodyWork >= -1.0e-14,
           "measured body branch generated cumulative work");
    expect(minimumTailWork >= -1.0e-4 * maximumTailWork - 1.0e-15,
           "xi_b tail acquired negative stored energy");
    expect(maximumBalanceError < 1.0e-4 * maximumForce + 1.0e-10,
           "bridge/body/tail force balance did not close");
}

// Measure the solver's actual zero-state port trajectories independently of
// its acoustic derivative histories. In particular, a note-on can re-reference
// acoustic motion but cannot remove positive work from the passive ledger.
void testPowerObserversKeepInitialAndRepeatedPluckWork()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        acustra::EngineParameters parameters;
        parameters.touch = 0.72f;
        engine.setParameters(parameters);
        engine.prepare(rate, 1);
        std::vector<std::array<float, 8>> trajectory;
        const int frames = static_cast<int>(0.05 * rate);
        trajectory.reserve(static_cast<std::size_t>(frames));
        bool agrees = true;
        double largestError = 0.0;
        for (int sample = 0; sample < frames; ++sample)
        {
            if (sample == 0) engine.noteOn(40, 0.72f);
            if (sample == static_cast<int>(0.020 * rate)) engine.noteOn(55, 1.0f);
            if (sample == static_cast<int>(0.030 * rate)) engine.noteOff(40);
            if (sample == static_cast<int>(0.035 * rate)) engine.noteOn(55, 0.95f);
            float left {}, right {};
            engine.process(&left, &right, 1);
            trajectory.push_back(acustra::AcustraEngineTestAccess::bridgePortWaves(engine));
            const double delayedAt = static_cast<double>(sample) - rate / 48000.0;
            const int before = static_cast<int>(std::floor(delayedAt));
            const double fraction = delayedAt - before;
            std::array<double, 8> rates {}, rateErrorBounds {};
            for (std::size_t coordinate = 0; coordinate < rates.size(); ++coordinate)
            {
                const auto valueAt = [&] (int at)
                {
                    return at < 0 ? 0.0 : static_cast<double>(
                        trajectory[static_cast<std::size_t>(at)][coordinate]);
                };
                const double delayed = (1.0 - fraction) * valueAt(before)
                                     + fraction * valueAt(before + 1);
                rates[coordinate] = trajectory.back()[coordinate] - delayed;
                // The production interpolated subtraction rounds at float
                // precision. Bound its absolute forward error from the
                // input magnitudes, including near-zero differences.
                rateErrorBounds[coordinate] = 8.0
                    * std::numeric_limits<float>::epsilon()
                    * (std::abs(trajectory.back()[coordinate])
                       + std::abs(valueAt(before)) + std::abs(valueAt(before + 1)));
            }
            const std::array<double, 3> observed {
                engine.getLastBridgePower(), engine.getLastBridgeBodyPower(),
                engine.getLastBridgeTailPower()
            };
            for (std::size_t branch = 0; branch < observed.size(); ++branch)
            {
                const double heave = rates[0] * rates[2 + 2 * branch];
                const double rock = rates[1] * rates[3 + 2 * branch];
                const double error = std::abs(observed[branch] - heave - rock);
                largestError = std::max(largestError, error);
                const auto productError = [&] (std::size_t motion, std::size_t force)
                {
                    return rateErrorBounds[motion] * std::abs(rates[force])
                        + rateErrorBounds[force] * std::abs(rates[motion])
                        + rateErrorBounds[motion] * rateErrorBounds[force];
                };
                const double bound = productError(0, 2 + 2 * branch)
                    + productError(1, 3 + 2 * branch)
                    + 4.0 * std::numeric_limits<float>::epsilon()
                        * (std::abs(heave) + std::abs(rock)) + 1.0e-22;
                agrees = agrees && error <= bound;
            }
        }
        expect(agrees, "power observer lost initial/repeated-pluck work at "
            + std::to_string(rate) + ", maximum error " + std::to_string(largestError));
    }
}

void testRetainedTailClosesTheWaveNormBalance()
{
    // The collapsed loops hold displacement waves, so this is a wave-norm
    // identity, not calibrated joules or a complete string-energy ledger:
    // sum Z*(a^2-c^2) = [x,r] dot [integrated force, integrated moment].
    // Include both polarisations and both retained planes. A rigid
    // horizontal termination contributes exactly zero flux; omitting a
    // branch would incorrectly report an energy error for a moving saddle.
    // Reading incidents from copies and actual emitted delay samples catches
    // a tail receiving a full return while its impedance is absent from G.
    // With one impedance counted for two loops, the discrepancy is Z*x_u^2.
    double worst = 0.0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int bendChange : { -1, 0, 1 })
        {
            const acustra::EngineParameters parameters;
            const auto result = acustra::AcustraEngineTestAccess::tailPortBalance(
                rate, parameters, bendChange);
            const std::string name = "tail at rate "
                + std::to_string(static_cast<int>(rate))
                + " bend direction " + std::to_string(bendChange);
            expect(result.capturedImpedance,
                   name + " did not preserve the retained branch's old impedance");
            expect(result.measuredFrames > 1900,
                   name + " supplied too few fixed-membership overlap samples");
            expect(result.relativeError < 1.0e-7,
                   name + " did not close the actual incoming/outgoing wave-norm balance");
            worst = std::max(worst, result.relativeError);
        }
    std::cout << "Acustra retained-tail maximum relative wave-norm error: "
              << worst << '\n';
}

void testConstructionControlsChangeTheModel()
{
    acustra::EngineParameters base;
    base.shape = acustra::BodyShape::Parlor;
    base.bodyMaterial = acustra::BodyMaterial::Mahogany;
    const auto parlor = render(base, 52, 0.82f, 0.7);
    base.shape = acustra::BodyShape::Jumbo;
    base.bodyMaterial = acustra::BodyMaterial::Maple;
    const auto jumbo = render(base, 52, 0.82f, 0.7);
    // The shapes differ most in the low body modes, which the 3.25 mm
    // anchor stub drives less than the 17 mm spring did: 0.070 here against
    // 0.09 before, so the floor sits below that rather than above it.
    expect(normalisedDifference(parlor, jumbo) > 0.05,
           "Shape/Material acted like inert labels");
}

void testAgeRemovesUpperStringEnergy()
{
    acustra::EngineParameters parameters;
    parameters.stringAge = 0.0f;
    const auto fresh = render(parameters, 64, 0.88f, 0.45);
    parameters.stringAge = 1.0f;
    const auto old = render(parameters, 64, 0.88f, 0.45);
    const int begin = 2400;
    const int end = 18000;
    const double freshRatio = differenceRms(fresh, begin, end)
                            / std::max(rms(fresh, begin, end), 1.0e-12);
    const double oldRatio = differenceRms(old, begin, end)
                          / std::max(rms(old, begin, end), 1.0e-12);
    expect(oldRatio < freshRatio,
           "old strings were not spectrally darker than fresh strings");
}

void testAttackPitchIsBoundedAndVelocityResponsive()
{
    const auto quietSteel = acustra::AcustraEngineTestAccess::attackPitchTrace(
        0.25f, 0);
    const auto hardSteel = acustra::AcustraEngineTestAccess::attackPitchTrace(
        1.0f, static_cast<int>(0.45 * sampleRate));
    expect(quietSteel.size() == 1 && hardSteel.size() > 2,
           "steel attack-pitch trace did not capture the played voice");
    if (!quietSteel.empty() && !hardSteel.empty())
    {
        expect(hardSteel.front().energy > 2.0 * quietSteel.front().energy
                   && hardSteel.front().cents
                        > 2.0 * quietSteel.front().cents,
               "Kirchhoff-Carrier pitch cue did not follow note velocity");
        // 7.8 cents since the 2026-09-04 refit, which moved
        // steelDisplacementScaleMetres from 0.00617 to 0.00774. The larger
        // excursion is the direction the recordings point: the archtop's
        // loudest steel layer moves 1.81 times (quartiles 1.15-2.57) the
        // engine's previously fitted excursion by its own early-minus-late
        // fundamental, and the corpus agrees - the pitch-trajectory term
        // improves from 0.5226 to 0.5182 on training. 4.1 cents since the
        // Finger moved from 74 to 149 mm from the bridge by ear (2026-09-25):
        // at the same fitted displacement a triangle's mean-square slope
        // goes as 1/(a (L - a)), 0.57 of what it was. Rescaling the
        // displacement to keep the cue reads worse on the benchmark (the
        // flat-top pitch-trajectory term 0.571 -> 0.608).
        expect(std::abs(hardSteel.front().cents - 4.1) < 1.0,
               "fitted steel displacement missed the onset cue: "
                   + std::to_string(hardSteel.front().cents) + " cents");

        for (std::size_t index = 0; index < hardSteel.size(); ++index)
        {
            const auto& state = hardSteel[index];
            expect(std::isfinite(state.energy) && state.energy >= 0.0
                       && std::isfinite(state.cents) && state.cents >= 0.0
                       && state.cents <= 20.0,
                   "energy-following attack pitch escaped its finite bounds");
            if (index > 0)
            {
                expect(state.energy
                           <= hardSteel[index - 1].energy + 1.0e-7,
                       "attack slope-energy envelope increased without a repick");
                expect(state.cents
                           <= hardSteel[index - 1].cents + 1.0e-5,
                       "attack pitch increased without a repick");
            }
        }
        expect(hardSteel.back().cents < 0.8 * hardSteel.front().cents,
               "energy-following attack pitch did not relax during sustain");
    }
}

void testSteadyPitchIsCompensated()
{
    for (const int midiNote : { 40, 45, 52, 64 })
    {
        const double expected = 440.0
            * std::exp2((static_cast<double>(midiNote) - 69.0) / 12.0);
        const auto audio = render({}, midiNote, 0.72f, 1.0,
                                  blockSize, false);
        // Autocorrelation returns a sharper weighted pseudo-period for a
        // deliberately stretched string. Search H1 itself instead.
        const double actual = spectralPeakFrequency(
            audio, expected, 25.0, 0.55, 0.92);
        const double cents = 1200.0 * std::log2(actual / expected);
        expect(std::abs(cents) < 1.5,
               "phase-compensated loop missed steady pitch for MIDI "
                   + std::to_string(midiNote) + " by "
                   + std::to_string(cents) + " cents");
    }
}

void testPhysicalSustainSettlesNearRequestedPitch()
{
    // Measure the string that was played. The idle strings ring at their own
    // open pitches, and their harmonics are just intervals while the fretboard
    // is equal-tempered: the low E's third harmonic is a just twelfth, two
    // cents above an equal-tempered B3, and playing B3 drives it. The peak of
    // the summed radiation therefore sits a couple of cents sharp of the note,
    // which is what a guitar does and not a tuning error. Reading it as one
    // put a 2-cent bound on a real behaviour and left the played string's own
    // pitch untested.
    const auto isolated = [] (int midiNote, bool sympathetic)
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        engine.prepare(sampleRate, blockSize);
        engine.setSympatheticStringsEnabled(sympathetic);
        engine.noteOn(midiNote, 0.72f);
        const int samples = static_cast<int>(1.5 * sampleRate);
        Audio audio { std::vector<float>(static_cast<std::size_t>(samples)),
                      std::vector<float>(static_cast<std::size_t>(samples)) };
        for (int offset = 0; offset < samples; offset += blockSize)
            engine.process(audio.left.data() + offset,
                           audio.right.data() + offset,
                           std::min(blockSize, samples - offset));
        return audio;
    };
    for (const int midiNote : { 40, 45, 52, 59, 64, 71 })
    {
        const double expected = 440.0
            * std::exp2((static_cast<double>(midiNote) - 69.0) / 12.0);
        const double alone = 1200.0 * std::log2(spectralPeakFrequency(
            isolated(midiNote, false), expected, 20.0, 0.82, 1.32) / expected);
        // The bridge phase this loop is tuned against is the string's own
        // point on the saddle, and B3 ends a quarter of the way in from the
        // treble impact, three semitones above the archive's 208.7 Hz
        // rocking mode. With the rocking bank in, that termination is
        // reactive enough for the string to form a resolvable pair with it,
        // where the one-point bridge read a single peak 0.09 cents flat; the
        // pull vanishes with the mobility scale (0.06 cents at its 0.25
        // floor), so this is the coupling and not a compensation error.
        // The bound is 1.6 rather than 1.5 because the whole engine moved
        // +0.31 cents when the radiating polarisation stopped carrying the
        // old authored split's 0.32-cent detune of itself (see
        // polarisationEndCorrectionMetres). B3 is the only note near the
        // bound and is what sets it: at the shipping calibration the six
        // read +0.432 (MIDI 40), +0.274 (45), +0.322 (52), +1.515 (59),
        // +0.087 (64) and -0.214 (71) cents, so the other five sit inside
        // half a cent and B3 has 0.08 cents of headroom. That coupling has
        // since been measured: with the parallel polarisation on the rocking
        // coordinate the open B forms a mode pair through it, a
        // normal-dominated member that sheds 0.24 dB a period and a
        // parallel-dominated one 6.4 cents above that sheds 0.011, so the
        // sustain this window reads was 6.6 cents sharp before the pair was
        // tuned by its sustained, energy-weighted centre
        // (coupledPolarisationDetune). Tuned, B3 read +1.67 cents, where the
        // uncoupled string read +1.52: the same reactive-termination pair as
        // before, read through a doublet. With the end correction at zero
        // (chosen by ear) the two planes start in tune, the rocking mixes
        // them into a closer pair, and B3 reads +2.53 against that pair's
        // sustained centre; the other five stay inside a cent. The bound is
        // 2.8 for that reason.
        expect(std::abs(alone) < 2.8,
               "the played steel string missed settled pitch for MIDI "
                   + std::to_string(midiNote) + " by "
                   + std::to_string(alone) + " cents");
        // With the idle strings sounding the peak may be pulled, but only as
        // far as their own just harmonics reach.
        const double loaded = 1200.0 * std::log2(spectralPeakFrequency(
            isolated(midiNote, true), expected, 20.0, 0.82, 1.32) / expected);
        // At exact coincidence (E3 on the low E's second partial, B3 on its
        // third) the played and idle strings form a coupled pair whose modes
        // split about the common frequency; the played string's energy sits
        // in the lower member, measured at -4.8 cents with the upper member
        // 12 cents above it and 24 dB down. That split is the coupling, not
        // a tuning error, so the bound admits it.
        // B3 adds its own bridge pair to that split; measured at -6.00.
        expect(std::abs(loaded) < 6.5,
               "sympathetic strings pulled MIDI " + std::to_string(midiNote)
                   + " by " + std::to_string(loaded) + " cents");
    }
}

void testLoadedE2IsCentredAndNotSplit()
{
    const double expected = 440.0 * std::exp2((40.0 - 69.0) / 12.0);
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.prepare(sampleRate, 1);
    engine.noteOn(40, 0.72f);
    Audio audio { std::vector<float>(static_cast<std::size_t>(4.0 * sampleRate)),
                  std::vector<float>(static_cast<std::size_t>(4.0 * sampleRate)) };
    for (std::size_t sample = 0; sample < audio.left.size(); ++sample)
    {
        float left = 0.0f;
        float right = 0.0f;
        engine.process(&left, &right, 1);
        const float force = engine.getLastBridgeReactionForce();
        audio.left[sample] = force;
        audio.right[sample] = force;
    }
    const double early = spectralPeakFrequency(
        audio, expected, 8.0, 0.05, 0.20);
    const double late = spectralPeakFrequency(
        audio, expected, 8.0, 2.0, 4.0);
    const double earlyCents = 1200.0 * std::log2(early / expected);
    const double lateCents = 1200.0 * std::log2(late / expected);
    expect(std::abs(earlyCents) < 3.0,
           "loaded steel E2 early mode missed tuning by "
               + std::to_string(earlyCents) + " cents");
    expect(std::abs(lateCents) < 1.5,
           "loaded steel E2 late mode missed tuning by "
               + std::to_string(lateCents) + " cents");

    const double primary = spectralAmplitudeAt(audio, late, 2.0, 4.0);
    double side = 0.0;
    for (int cents = -120; cents <= 120; cents += 2)
    {
        if (std::abs(cents) < 25)
            continue;
        const double frequency = late * std::exp2(cents / 1200.0);
        side = std::max(side,
            spectralAmplitudeAt(audio, frequency, 2.0, 4.0));
    }
    const double sideDb = 20.0 * std::log10(
        std::max(side, 1.0e-30) / std::max(primary, 1.0e-30));
    expect(sideDb < -25.0,
           "loaded steel E2 retained a strong split side mode at "
               + std::to_string(sideDb) + " dB");
}

void testSteelDispersionTracksTheStiffStringLaw()
{
    acustra::EngineParameters parameters;
    // The normal plane carries the tuning; the parallel plane shares its
    // dispersion, and a steel pluck puts most of its energy there, so it is
    // emptied for this read.
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.prepare(sampleRate, blockSize);
    engine.setParameters(parameters);
    engine.setBridgeCouplingEnabled(false);
    engine.noteOn(40, 0.72f);
    acustra::AcustraEngineTestAccess::silenceParallelPolarisation(engine);
    const int samples = static_cast<int>(1.0 * sampleRate);
    Audio audio { std::vector<float>(static_cast<std::size_t>(samples)),
                  std::vector<float>(static_cast<std::size_t>(samples)) };
    for (int offset = 0; offset < samples; offset += blockSize)
        engine.process(audio.left.data() + offset, audio.right.data() + offset,
                       std::min(blockSize, samples - offset));
    const double fundamental = 440.0 * std::exp2((40.0 - 69.0) / 12.0);
    constexpr double length = 0.648;
    constexpr double tension = 110.759;
    constexpr double youngsModulus = 2.0e11;
    constexpr double bendingDiameter = 0.477159e-3;
    const double diameterSquared = bendingDiameter * bendingDiameter;
    const double inharmonicity = acustra::fittedPhysicalCalibration.steel
        .stiffnessScale * std::pow(std::numbers::pi, 3.0)
        * youngsModulus * diameterSquared * diameterSquared
        / (64.0 * tension * length * length);

    for (int partial = 2; partial <= 12; ++partial)
    {
        const double number = static_cast<double>(partial);
        const double expected = fundamental * number * std::sqrt(
            (1.0 + inharmonicity * number * number)
            / (1.0 + inharmonicity));
        const double actual = spectralPeakFrequency(
            audio, expected, 8.0, 0.55, 0.92);
        const double cents = 1200.0 * std::log2(actual / expected);
        expect(std::abs(cents) < 2.0,
               "steel E2 dispersion missed partial "
                   + std::to_string(partial) + " by "
                   + std::to_string(cents) + " cents");
    }
}

double loopPhase(const acustra::AcustraEngineTestAccess::StringLoopSnapshot& loop,
                 double omega)
{
    const double lossOmega = loop.sampleRate == 48000.0 ? omega
        : 2.0 * std::atan((loop.sampleRate / 48000.0) * std::tan(0.5 * omega));
    const auto mixedPolePhase = [lossOmega] (double coefficient, double mix)
    {
        const double denominatorReal = 1.0 - coefficient * std::cos(lossOmega);
        const double denominatorImaginary = coefficient * std::sin(lossOmega);
        const double norm = denominatorReal * denominatorReal
                          + denominatorImaginary * denominatorImaginary;
        const double lowReal = (1.0 - coefficient) * denominatorReal / norm;
        const double lowImaginary = -(1.0 - coefficient)
                                  * denominatorImaginary / norm;
        return -std::atan2(mix * lowImaginary,
                           (1.0 - mix) + mix * lowReal);
    };

    // The loop's fractional delay: an integer tap plus a second-order Thiran
    // allpass, split the way delayAnchor() in AcustraEngine.cpp splits it.
    const int anchor = static_cast<int>(std::floor(loop.delay - 1.1));
    const double fraction = loop.delay - static_cast<double>(anchor);
    const double thiran1 = -2.0 * (fraction - 2.0) / (fraction + 1.0);
    const double thiran2 = (fraction - 2.0) * (fraction - 1.0)
                         / ((fraction + 1.0) * (fraction + 2.0));
    const double thiranNumerator = std::atan2(
        -thiran1 * std::sin(omega) - std::sin(2.0 * omega),
        thiran2 + thiran1 * std::cos(omega) + std::cos(2.0 * omega));
    const double thiranDenominator = std::atan2(
        -thiran1 * std::sin(omega) - thiran2 * std::sin(2.0 * omega),
        1.0 + thiran1 * std::cos(omega) + thiran2 * std::cos(2.0 * omega));
    double thiranLag = thiranDenominator - thiranNumerator;
    while (thiranLag < 0.0)
        thiranLag += 2.0 * std::numbers::pi;
    while (thiranLag >= 2.0 * std::numbers::pi)
        thiranLag -= 2.0 * std::numbers::pi;
    const double delayPhase = static_cast<double>(anchor) * omega + thiranLag;

    const double cosine = std::cos(omega);
    const double sine = std::sin(omega);
    const double cosine2 = std::cos(2.0 * omega);
    const double sine2 = std::sin(2.0 * omega);
    // The dispersion's allpass sections in cascade: the second only where
    // the loop runs it.
    const auto sectionPhase = [&] (double a1, double a2)
    {
        const double numeratorPhase = std::atan2(
            -a1 * sine - sine2, a2 + a1 * cosine + cosine2);
        const double denominatorPhase = std::atan2(
            -a1 * sine - a2 * sine2, 1.0 + a1 * cosine + a2 * cosine2);
        double phase = denominatorPhase - numeratorPhase;
        while (phase < 0.0)
            phase += 2.0 * std::numbers::pi;
        while (phase >= 2.0 * std::numbers::pi)
            phase -= 2.0 * std::numbers::pi;
        return phase;
    };
    const double allpassPhase
        = sectionPhase(loop.dispersionA1, loop.dispersionA2)
        + (loop.secondDispersionActive
               ? sectionPhase(loop.secondDispersionA1, loop.secondDispersionA2)
               : 0.0);

    // The bending-loss section is designed at the host rate, so its lag is
    // read at the host frequency.
    const double bendingLag = std::atan2(
        -loop.bendingA1 * sine - loop.bendingA2 * sine2,
        1.0 + loop.bendingA1 * cosine + loop.bendingA2 * cosine2);

    return delayPhase
        + mixedPolePhase(loop.broadCoefficient, loop.broadMix)
        + mixedPolePhase(loop.highCoefficient, loop.highMix)
        + bendingLag
        + allpassPhase;
}

// The bracket is widened by the caller when the resonance can sit further
// from the nominal than a note's own tuning residual - under a vibrato it can
// be a fifth of a semitone away. Bisection converges to the same place from
// either bracket; what the width has to guarantee is only that the root is
// inside it.
double loopResonance(
    const acustra::AcustraEngineTestAccess::StringLoopSnapshot& loop,
    int partial, double expectedOmega, double windowCents = 12.0)
{
    double lower = expectedOmega * std::exp2(-windowCents / 1200.0);
    double upper = expectedOmega * std::exp2(windowCents / 1200.0);
    const double target = 2.0 * std::numbers::pi * partial;
    for (int iteration = 0; iteration < 52; ++iteration)
    {
        const double middle = 0.5 * (lower + upper);
        if (loopPhase(loop, middle) < target)
            lower = middle;
        else
            upper = middle;
    }
    return 0.5 * (lower + upper);
}

void testLossFiltersPreserveTheReferenceTransfer()
{
    using Access = acustra::AcustraEngineTestAccess;
    // Every string/age/mute/calibration combination is a subset of these
    // reference cutoffs (500..21120 Hz) and convex loss mixes (0..1). Sweep
    // the full supported rate range, including the rate where the mapped
    // pole crosses zero: a coefficient/mix rewrite is singular there.
    std::vector<double> rates { 8000, 44100, 48000, 96000, 192000, 384000 };
    for (int index = 0; index <= 80; ++index)
        rates.push_back(8000.0 * std::pow(48.0, index / 80.0));
    double maximumMagnitude = 0.0;
    double maximumError = 0.0;
    for (int cutoffIndex = 0; cutoffIndex <= 32; ++cutoffIndex)
    {
        const double cutoff = 500.0 * std::pow(21120.0 / 500.0, cutoffIndex / 32.0);
        const float pole = static_cast<float>(
            std::exp(-2.0 * std::numbers::pi * cutoff / 48000.0));
        const double zeroPoleRate = 48000.0 * (1.0 - pole) / (1.0 + pole);
        auto testedRates = rates;
        if (zeroPoleRate >= 8000.0)
            testedRates.push_back(zeroPoleRate);
        for (double rate : testedRates)
        {
            const auto coefficients = Access::lossFilterCoefficients(pole, rate);
            expect(std::abs(coefficients[2]) < 1.0,
                   "a remapped loss pole left the stable unit circle");
            for (int bin = 0; bin <= 128; ++bin)
            {
                const double omega = std::numbers::pi * bin / 128.0;
                const std::complex<double> z = std::polar(1.0, -omega);
                const auto actual = (coefficients[0] + coefficients[1] * z)
                                  / (1.0 - coefficients[2] * z);
                const double referenceOmega = rate == 48000.0 ? omega
                    : 2.0 * std::atan((rate / 48000.0) * std::tan(0.5 * omega));
                const auto expected = (1.0 - pole)
                    / (1.0 - static_cast<double>(pole)
                        * std::polar(1.0, -referenceOmega));
                maximumError = std::max(maximumError, std::abs(actual - expected));
                for (double mix : { 0.0, 0.1, 0.5, 0.95, 1.0 })
                    maximumMagnitude = std::max(maximumMagnitude,
                        std::abs(1.0 - mix + mix * actual));
            }
        }
    }
    expect(maximumMagnitude < 1.0 + 1.0e-12,
           "a remapped loss shelf became active instead of passive");
    expect(maximumError < 0.00001,
           "a remapped loss shelf no longer represents its 48 kHz transfer");

    // Check the production recurrence, including its delayed-input state,
    // against the transfer above; inspecting coefficients alone misses a
    // misplaced numerator tap. At 48 kHz also require the exact legacy path.
    for (double rate : { 8000.0, 42300.0, 44100.0, 48000.0, 96000.0, 384000.0 })
        for (double cutoff : { 500.0, 1200.0, 21120.0 })
        {
            const float pole = static_cast<float>(
                std::exp(-2.0 * std::numbers::pi * cutoff / 48000.0));
            const auto impulse = Access::lossFilterImpulse(pole, rate);
            const auto coefficients = Access::lossFilterCoefficients(pole, rate);
            if (rate == 48000.0)
            {
                float state = 0.0f;
                for (std::size_t index = 0; index < impulse.size(); ++index)
                {
                    const float input = index == 0 ? 1.0f : 0.0f;
                    state = input + pole * (state - input);
                    expect(impulse[index] == state,
                           "48 kHz loss filtering changed its legacy samples");
                }
            }
            for (int bin : { 0, 1, 17, 64, 128 })
            {
                const double omega = std::numbers::pi * bin / 128.0;
                std::complex<double> actual {};
                for (std::size_t index = 0; index < impulse.size(); ++index)
                    actual += static_cast<double>(impulse[index])
                            * std::polar(1.0, -omega * index);
                const std::complex<double> z = std::polar(1.0, -omega);
                const auto expected = (coefficients[0] + coefficients[1] * z)
                                    / (1.0 - coefficients[2] * z);
                expect(std::abs(actual - expected) < 0.00001,
                       "the loss-filter recurrence changed its mapped transfer");
            }
        }
}

void testTheFractionalDelayReadIsLossless()
{
    // A fractional read must not add interpolation-dependent loss: the
    // second-order Thiran allpass has unit magnitude at every frequency.
    // Isolate the string loop from bridge loading and sympathetic strings,
    // and observe saddle force: the microphone filter changes partial levels
    // during pitch glides and adds its own decay.
    // The loss shelves now retain the same calibrated 48 kHz transfer across
    // hosts, but bilinear warping leaves H8 decay spread at 4.1..12.9% over
    // these rates and notes. The H1 spread is below 0.20%; neither tolerance
    // is a claim of perfect sample-rate independence.
    struct Reading { double perSecond; double perPass; };
    const auto decay = [] (int midiNote, double rate, int partial)
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        acustra::EngineParameters parameters;
        parameters.capture = acustra::CaptureType::SaddlePiezo;
        // Select before prepare so the capture fade never includes the mic.
        engine.setParameters(parameters);
        engine.prepare(rate, blockSize);
        engine.setBridgeCouplingEnabled(false);
        engine.setSympatheticStringsEnabled(false);
        engine.noteOn(midiNote, 0.8f);
        const int samples = static_cast<int>(1.6 * rate);
        Audio audio { std::vector<float>(static_cast<std::size_t>(samples)),
                      std::vector<float>(static_cast<std::size_t>(samples)) };
        for (int offset = 0; offset < samples; offset += blockSize)
            engine.process(audio.left.data() + offset,
                           audio.right.data() + offset,
                           std::min(blockSize, samples - offset));
        const double fundamental = 440.0
            * std::exp2((static_cast<double>(midiNote) - 69.0) / 12.0);
        // Stiffness stretches H8 and the tension glide moves every partial
        // during the note. Follow its frequency separately in each window.
        const double perSecond = spectralDecayRate(
            audio, partial * fundamental, 60.0, rate);
        return Reading { perSecond, perSecond / fundamental };
    };

    for (const int partial : { 1, 8 })
    {
        double lowestPass = 1.0e9;
        double highestPass = 0.0;
        for (int midiNote = 76; midiNote <= 84; ++midiNote)
        {
            double lowest = 1.0e9;
            double highest = -1.0e9;
            for (const double rate : { 44100.0, 48000.0, 96000.0 })
            {
                const auto reading = decay(midiNote, rate, partial);
                expect(reading.perSecond > 0.0,
                       "steel MIDI " + std::to_string(midiNote) + " H"
                           + std::to_string(partial) + " did not decay at "
                           + std::to_string(static_cast<int>(rate)));
                lowest = std::min(lowest, reading.perSecond);
                highest = std::max(highest, reading.perSecond);
                lowestPass = std::min(lowestPass, reading.perPass);
                highestPass = std::max(highestPass, reading.perPass);
            }
            // Restore the original 1.5% fundamental bound. The apparent
            // 1.87/2.78/1.77% spread at MIDI 82/83/84 was detuning in the
            // fixed-frequency estimator. Tracking alone puts all three below
            // 0.2%; with the reference loss mapping they remain below 0.21%.
            const double spread = 2.0 * (highest - lowest) / (highest + lowest);
            expect(spread < (partial == 1 ? 0.015 : 0.15),
                   "steel MIDI " + std::to_string(midiNote) + " H"
                       + std::to_string(partial)
                       + " decayed at rates that differ by "
                       + std::to_string(100.0 * spread) + "%");
        }
        // Loss per round trip across the nine notes and three rates. It is
        // physical - a higher note's H8 sits at a higher frequency, where the
        // string loses more. The isolated saddle observer spans 1.40:1 on
        // H1 and 1.16:1 on H8, not the 47:1 the interpolated read produced
        // out of loop fractions alone.
        expect(highestPass / lowestPass < 2.5,
               "steel H" + std::to_string(partial)
                   + " loss per round trip ranged over a factor of "
                   + std::to_string(highestPass / lowestPass));
    }
}

void testASlewingDelayDoesNotClickAboveFourteenKilohertz()
{
    // The loop's integer tap moves whenever a slewing delay carries the
    // Thiran allpass's own delay out of its band, and the allpass's
    // coefficients move with it. Read in direct form I from the line, the
    // filter's memory of its input is the memory the new tap would have had,
    // so the move costs no transient. Measured where a transient would show:
    // the top band, in 5 ms frames, against the same string held still.
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const int block = 64;
        const auto bent = [&] (bool bend)
        {
            auto engineOwner = std::make_unique<acustra::AcustraEngine>();
            auto& engine = *engineOwner;
            acustra::EngineParameters parameters;
            engine.setParameters(parameters);
            engine.prepare(rate, block);
            engine.setBridgeCouplingEnabled(false);
            engine.setSympatheticStringsEnabled(false);
            engine.noteOn(52, 0.8f);
            const int samples = static_cast<int>(2.0 * rate);
            Audio audio {
                std::vector<float>(static_cast<std::size_t>(samples)),
                std::vector<float>(static_cast<std::size_t>(samples)) };
            for (int offset = 0; offset < samples; offset += block)
            {
                if (bend)
                    engine.setPitchBend(static_cast<float>(2.0 * std::clamp(
                        (offset / rate - 0.2) / 1.0, 0.0, 1.0)));
                engine.process(audio.left.data() + offset,
                               audio.right.data() + offset,
                               std::min(block, samples - offset));
            }
            return audio;
        };
        // 200 cents over a second: about eighteen tap moves on this string.
        const auto slewing = bent(true);
        const auto still = bent(false);
        const auto frames = [&] (const Audio& audio, double begin, double end)
        {
            std::vector<double> result;
            for (double at = begin; at + 0.005 < end; at += 0.0025)
                result.push_back(tailBandRms(audio, rate, at, at + 0.005,
                                             14000.0, 0.45 * rate));
            return result;
        };
        const auto slewingFrames = frames(slewing, 0.25, 1.15);
        const auto stillFrames = frames(still, 0.25, 1.15);
        const double loudest = *std::max_element(
            slewingFrames.begin(), slewingFrames.end());
        const double reference = *std::max_element(
            stillFrames.begin(), stillFrames.end());
        expect(reference > 0.0, "the unbent reference string was silent");
        expect(loudest < 1.5 * reference,
               "a 200-cent bend at "
                   + std::to_string(static_cast<int>(rate))
                   + " Hz put " + std::to_string(loudest / reference)
                   + " times the unbent string's top-band peak into one frame");
        // And the steel attack glide, which slews the delay by its own few
        // cents while the pluck is still loud: no frame rises on the two
        // before it by more than the ordinary beating of the band does.
        // Read while the band is still there: with the strings' bending loss
        // (FittedPhysicalData.h, 2026-09-28) an E3's partials above 14 kHz
        // fall 60 dB under their attack within 30-65 ms and then sit about
        // 110 dB under the note, where frame-to-frame ratios of that residue
        // reached 2.2-5.9 at 170-215 ms and say nothing about a click. Over
        // the frames above that floor the largest rise is 0.33-0.72; without
        // the loss the band stays above it to 300 ms and rises 1.40-1.53.
        // The floor is the settled band's own loudest frame (200-300 ms), and
        // a frame counts while it stands 12 dB over it as well as within
        // 60 dB of the attack: since the Finger's contact burst lost its
        // white share (2026-09-30) the attack's top band starts 15 dB lower
        // while that residue is where it was, 47 dB under the new attack,
        // and its beating is no more a click than it was before.
        const auto attack = frames(bent(false), 0.005, 0.30);
        double attackPeak = 0.0, settledPeak = 0.0;
        for (std::size_t i = 0; i < std::min<std::size_t>(4, attack.size()); ++i)
            attackPeak = std::max(attackPeak, attack[i]);
        for (std::size_t i = 0; i < attack.size(); ++i)
            if (0.005 + 0.0025 * static_cast<double>(i) >= 0.2)
                settledPeak = std::max(settledPeak, attack[i]);
        expect(settledPeak < 1.0e-2 * attackPeak,
               "the steel attack's top band did not settle 40 dB under its attack");
        const double floor = std::max(1.0e-3 * attackPeak, 4.0 * settledPeak);
        double rise = 0.0;
        int counted = 0;
        for (std::size_t i = 2; i < attack.size(); ++i)
            if (attack[i - 2] >= floor)
            {
                rise = std::max(rise, attack[i] / std::max(attack[i - 2], 1.0e-30));
                ++counted;
            }
        expect(counted >= 4, "the steel attack's top band was never read above its floor");
        std::cout << "Acustra steel attack glide at " << static_cast<int>(rate)
                  << " Hz: largest top-band rise " << rise << " over " << counted
                  << " frames\n";
        expect(rise < 2.0,
               "the steel attack glide at "
                   + std::to_string(static_cast<int>(rate))
                   + " Hz raised the top band by a factor of "
                   + std::to_string(rise) + " in one frame");
    }
}

void testDispersionSolveCacheMatchesForcedRecomputation()
{
    // The reference always misses the memoization key when the unchanged
    // design predicate requests a solve. This exercises the former solver
    // path through public operations rather than mirroring that predicate.
    auto cachedOwner = std::make_unique<acustra::AcustraEngine>();
    auto& cached = *cachedOwner;
    auto referenceOwner = std::make_unique<acustra::AcustraEngine>();
    auto& reference = *referenceOwner;
    std::array<float, blockSize> left {}, right {}, expectedLeft {}, expectedRight {};
    bool heardSignal = false;
    const auto apply = [&] (const auto& operation)
    {
        operation(cached);
        acustra::AcustraEngineTestAccess::invalidateDispersionSolveCache(reference);
        operation(reference);
    };
    const auto render = [&] (const std::string& context)
    {
        for (int block = 0; block < 8; ++block)
        {
            cached.process(left.data(), right.data(), blockSize);
            acustra::AcustraEngineTestAccess::invalidateDispersionSolveCache(reference);
            reference.process(expectedLeft.data(), expectedRight.data(), blockSize);
            expect(left == expectedLeft && right == expectedRight,
                   "dispersion cache changed stereo output: " + context);
            heardSignal = heardSignal || std::any_of(left.begin(), left.end(),
                                                     [] (float value) { return value != 0.0f; });
        }
    };
    acustra::EngineParameters parameters;
    auto calibration = acustra::fittedPhysicalCalibration;
    apply([&] (auto& engine) { engine.setPhysicalCalibration(calibration); });
    apply([&] (auto& engine) { engine.setParameters(parameters); });
    // Each preceding reset leaves open-string keys warm, so the next
    // prepare must distinguish an otherwise identical design at a new Fs.
    for (const double rate : { 48000.0, 96000.0, 44100.0 })
    {
        const std::string context = std::to_string(rate);
        apply([&] (auto& engine) { engine.prepare(rate, blockSize); });
        apply([] (auto& engine) { engine.setStringPerChannelMode(true); });
        for (int repeat = 0; repeat < 3; ++repeat)
        {
            apply([] (auto& engine) { engine.noteOn(40, 0.6f, 1); });
            render(context + " repeated open note");
        }
        apply([] (auto& engine) { engine.noteOn(45, 0.7f, 1, 37); });
        render(context + " scheduled fret");
        apply([] (auto& engine) { engine.setStringPerChannelMode(false); });
        apply([] (auto& engine) { engine.noteOn(88, 0.6f); });
        render(context + " natural harmonic");
        apply([] (auto& engine) { engine.reset(); });
        // Keep B, note, age and loss mix fixed while changing only the
        // design's high-loss coefficient. The initial steel cutoff is
        // Nyquist-clamped, so 1.0 deliberately moves it off that plateau.
        calibration.highLossCutoffScale = calibration.highLossCutoffScale == 1.0f
            ? 1.5f : 1.0f;
        apply([&] (auto& engine) { engine.setPhysicalCalibration(calibration); });
        apply([] (auto& engine) { engine.noteOn(40, 0.6f); });
        render(context + " cutoff coefficient");
        calibration.steel.stiffnessScale *= 1.01f;
        calibration.steel.frequencyLossScale *= 1.05f;
        apply([&] (auto& engine) { engine.setPhysicalCalibration(calibration); });
        parameters.stringAge += 0.03f;
        parameters.tuning = acustra::Tuning::DropD;
        apply([&] (auto& engine) { engine.setParameters(parameters); });
        apply([] (auto& engine) { engine.setLowerZoneMemberCount(2); });
        apply([] (auto& engine) { engine.noteOn(52, 0.6f, 2); });
        for (const float bend : { 0.0005f, 0.02f, 2.0f, 0.0f })
        {
            apply([&] (auto& engine) { engine.setPitchBend(bend, 2); });
            render(context + " member bend " + std::to_string(bend));
        }
        apply([] (auto& engine) { engine.setPitchBend(-0.03f, 1); });
        render(context + " manager slide");
        apply([] (auto& engine) { engine.reset(); });
    }
    expect(heardSignal, "dispersion cache comparison rendered no signal");
}

void testConfigurationKeysAndObserversLeaveTheOutputUnchanged()
{
    // cached keeps its voice configurations while their keys hold and runs
    // without the port observers; reference reconfigures every string on
    // every control update and observes. Their outputs must be identical in
    // every bit, through settling attack glides, bends, vibrato, the bridge
    // hand, construction and tuning changes, releases and a second rate.
    auto cachedOwner = std::make_unique<acustra::AcustraEngine>();
    auto& cached = *cachedOwner;
    auto referenceOwner = std::make_unique<acustra::AcustraEngine>();
    auto& reference = *referenceOwner;
    cached.setPortObserversEnabled(false);
    std::array<float, blockSize> left {}, right {}, expectedLeft {}, expectedRight {};
    bool heardSignal = false;
    const auto apply = [&] (const auto& operation)
    {
        operation(cached);
        acustra::AcustraEngineTestAccess::invalidateVoiceConfigurations(reference);
        operation(reference);
    };
    const auto render = [&] (const std::string& context, int blocks)
    {
        for (int block = 0; block < blocks; ++block)
        {
            cached.process(left.data(), right.data(), blockSize);
            for (int part = 0; part < blockSize; part += 16)
            {
                acustra::AcustraEngineTestAccess::invalidateVoiceConfigurations(reference);
                reference.process(expectedLeft.data() + part, expectedRight.data() + part,
                                  std::min(16, blockSize - part));
            }
            expect(left == expectedLeft && right == expectedRight,
                   "configuration keys or observers changed the output: " + context);
            heardSignal = heardSignal || std::any_of(left.begin(), left.end(),
                                                     [] (float value) { return value != 0.0f; });
        }
    };
    for (const double rate : { 48000.0, 96000.0 })
    {
        const std::string context = std::to_string(rate);
        acustra::EngineParameters parameters;
        apply([&] (auto& engine) { engine.setParameters(parameters); });
        apply([&] (auto& engine) { engine.prepare(rate, blockSize); });
        apply([] (auto& engine)
        {
            engine.beginStrum();
            for (int note : { 40, 47, 52, 56, 59, 64 })
                engine.noteOn(note, 0.8f, 1, 40 * (note - 40), true);
        });
        // Long enough for held steel notes' attack glides to settle, which
        // is when their keys start to hold.
        render(context + " held chord", static_cast<int>(3.0 * rate) / blockSize);
        apply([] (auto& engine) { engine.setVibrato(0.7f); });
        render(context + " vibrato", 40);
        apply([] (auto& engine) { engine.setVibrato(0.0f); });
        for (const float bend : { 0.0005f, 0.5f, 2.0f, 0.0f })
        {
            apply([&] (auto& engine) { engine.setPitchBend(bend, 1); });
            render(context + " bend " + std::to_string(bend), 12);
        }
        for (const float pressure : { 0.3f, 0.8f, 0.0f })
        {
            apply([&] (auto& engine) { engine.setPalmMutePressure(pressure); });
            render(context + " bridge hand " + std::to_string(pressure), 12);
        }
        parameters.shape = acustra::BodyShape::Parlor;
        apply([&] (auto& engine) { engine.setParameters(parameters); });
        render(context + " shape", 30);
        parameters.stringAge += 0.2f;
        apply([&] (auto& engine) { engine.setParameters(parameters); });
        render(context + " age", 30);
        parameters.tuning = acustra::Tuning::DropD;
        apply([&] (auto& engine) { engine.setParameters(parameters); });
        render(context + " tuning", 30);
        apply([] (auto& engine) { engine.setSustainPedal(true); });
        apply([] (auto& engine) { engine.noteOff(47); engine.noteOff(59); });
        render(context + " pedalled release", 20);
        apply([] (auto& engine) { engine.setSustainPedal(false); });
        render(context + " release", 60);
        apply([] (auto& engine) { engine.noteOn(62, 0.6f); });
        render(context + " new note", 60);
        apply([] (auto& engine) { engine.allNotesOff(); });
        render(context + " all notes off", 60);
        apply([] (auto& engine) { engine.reset(); });
    }
    expect(heardSignal, "configuration key comparison rendered no signal");
}

void testDispersionAcrossRatesAndNotes()
{
    constexpr double rates[] { 44100.0, 48000.0, 384000.0 };
    for (const int midiNote : { 40, 84 })
    for (const double rate : rates)
    {
        const bool bass = midiNote == 40;
        const int openMidi = bass ? 40 : 64;
        const int fret = midiNote - openMidi;
        const double scaleLength = 0.648;
        const double diameter = bass ? 0.477159e-3 : 0.305e-3;
        const double youngsModulus = 2.0e11;
        const double tension = bass ? 110.759 : 104.088;
        const double soundingLength = scaleLength
            * std::exp2(-static_cast<double>(fret) / 12.0);
        const double inharmonicity
            = acustra::fittedPhysicalCalibration.steel.stiffnessScale
                * std::pow(std::numbers::pi, 3.0)
                * youngsModulus * std::pow(diameter, 4.0)
                / (64.0 * tension * soundingLength * soundingLength);
        const double fundamental = 440.0
            * std::exp2((static_cast<double>(midiNote) - 69.0) / 12.0);
        const auto loop = acustra::AcustraEngineTestAccess::configuredLoop(
            midiNote, rate);
        const double nominalOmega = 2.0 * std::numbers::pi
                                  * fundamental / rate;
        const double actualFundamental = loopResonance(loop, 1, nominalOmega);

        for (int partial = 2; partial <= 12; ++partial)
        {
            const double number = static_cast<double>(partial);
            const double expected = actualFundamental * number * std::sqrt(
                (1.0 + inharmonicity * number * number)
                / (1.0 + inharmonicity));
            const double actual = loopResonance(loop, partial, expected);
            const double cents = 1200.0 * std::log2(actual / expected);
            // The least-squares fit's own residual (under a cent here, see
            // calibrateDispersion) and the loop's single-precision
            // coefficients bound what is left; 3 cents is still under a
            // third of the H16+ gap already documented.
            expect(std::abs(cents) < 3.0,
                   "steel/"
                       + std::to_string(static_cast<int>(rate)) + " MIDI "
                       + std::to_string(midiNote) + " partial "
                       + std::to_string(partial) + " missed by "
                       + std::to_string(cents) + " cents");
        }
    }
}

// The same law across the whole fretboard. Fretting shortens the string, and
// B grows as the inverse square of the sounding length, so the wound strings
// high on the neck are the most dispersive notes the model plays: E2 at the
// 20th fret has B n^2 near 0.12 at H12. One allpass section fitted at three
// partials missed H3-H5 there by 16-18 cents between its collocation points,
// and this test failed E2 from the 15th fret up, A2 at the 19th and 20th and
// B3 at the 20th;
// with the second section switched in on the stiff notes (calibrateDispersion)
// every string and fret is held to the same 3 cents as above, H2 up to H12,
// at the three common host rates.
void testDispersionAcrossTheFretboard()
{
    using Access = acustra::AcustraEngineTestAccess;
    // The standard tuning's open strings, bass first.
    constexpr int openMidi[] { 40, 45, 50, 55, 59, 64 };
    constexpr int frets = 21;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto loops = Access::fretboardLoops(rate);
        for (int stringIndex = 0; stringIndex < 6; ++stringIndex)
        for (int fret = 0; fret < frets; ++fret)
        {
            const auto& loop = loops[static_cast<std::size_t>(
                stringIndex * frets + fret)];
            const double inharmonicity = loop.inharmonicity;
            const double nominalOmega = 2.0 * std::numbers::pi * 440.0
                * std::exp2(static_cast<double>(openMidi[stringIndex] + fret
                                                - 69) / 12.0) / rate;
            const double fundamental = loopResonance(loop, 1, nominalOmega);
            for (int partial = 2; partial <= 12; ++partial)
            {
                const double number = static_cast<double>(partial);
                const double expected = fundamental * number * std::sqrt(
                    (1.0 + inharmonicity * number * number)
                    / (1.0 + inharmonicity));
                // Wide enough to measure a miss several times the bound.
                const double actual = loopResonance(loop, partial, expected,
                                                    40.0);
                const double cents = 1200.0 * std::log2(actual / expected);
                expect(std::abs(cents) < 3.0,
                       "steel/" + std::to_string(static_cast<int>(rate))
                           + " string " + std::to_string(stringIndex + 1)
                           + " (from the bass) fret " + std::to_string(fret)
                           + " partial " + std::to_string(partial)
                           + " missed by " + std::to_string(cents)
                           + " cents");
            }
        }
    }
}

// A bend is the string stretched, not the neck slid. Grimes (PLoS ONE
// 9(7):e102088, 2014, Eq. 6) fixes what tension a bent string carries and
// therefore what its inharmonicity and its impedance do, so the model's
// tension is checked against his law rather than against itself.
void testAMemberBendIsATensionBendByGrimes()
{
    // The engine's own tables: the effective core diameters the wound
    // strings' bending model uses (steelBendingDiameter), 200 GPa, and the
    // published EJ16 tensions (steelTensionNewtons in AcustraEngine.cpp).
    constexpr double bendingDiameter[] { 0.477159e-3, 0.437895e-3,
                                         0.412021e-3, 0.38e-3,
                                         0.406e-3, 0.305e-3 };
    constexpr double openTension[] { 110.759, 128.554, 133.002,
                                     133.892, 103.643, 104.088 };
    double worstGrimes = 0.0;
    double worstImpedance = 0.0;
    double worstInharmonicity = 0.0;
    for (const int midiNote : { 47, 52, 64, 76 })
    {
        const auto unbent = acustra::AcustraEngineTestAccess::bentString(
            midiNote, 0.0f, 0.0f);
        const auto index = static_cast<std::size_t>(unbent.stringIndex);
        expect(unbent.stringIndex >= 0 && unbent.stringIndex < 6,
               "the bend test could not take MIDI "
                   + std::to_string(midiNote));
        const double rigidity = 2.0e11 * 0.25 * std::numbers::pi
            * bendingDiameter[index] * bendingDiameter[index];
        const double unbentTension = unbent.tension;
        expect(std::abs(unbentTension - openTension[index]) < 1.0e-3,
               "an unbent string was not at its published tension");
        for (int step = 1; step <= 20; ++step)
        {
            const double semitones = 0.1 * static_cast<double>(step);
            const auto bent = acustra::AcustraEngineTestAccess::bentString(
                midiNote, 0.0f,
                static_cast<float>(semitones));
            const double strain = (bent.tension - unbentTension) / rigidity;
            // Grimes Eq. 6: the stretched string sounds at
            // f0 * sqrt((T/T0) / (1 + dT/EA)).
            const double grimesCents = 1200.0 * std::log2(std::sqrt(
                (bent.tension / unbentTension) / (1.0 + strain)));
            worstGrimes = std::max(worstGrimes,
                std::abs(grimesCents - 100.0 * semitones));
            // Z = sqrt(T mu) with his stretched mass per length is Z0 times
            // that same ratio.
            worstImpedance = std::max(worstImpedance, std::abs(
                1200.0 * std::log2(bent.impedanceScale)
                - 100.0 * semitones));
            // B goes as 1/T. The design is re-solved when B has moved by
            // 0.2%, so it trails the exact law by up to that much.
            worstInharmonicity = std::max(worstInharmonicity, std::abs(
                bent.loop.inharmonicity * bent.tension
                / (unbent.loop.inharmonicity * unbentTension) - 1.0));
            // The manager's half of an MPE bend is a slide: the same
            // interval taken on channel 1 leaves the tension where it was.
            const auto slid = acustra::AcustraEngineTestAccess::bentString(
                midiNote,
                static_cast<float>(semitones), 0.0f);
            expect(slid.tension == unbentTension
                       && slid.impedanceScale == 1.0,
                   "a manager bend moved the string's tension");
            expect(std::abs(slid.loop.delay - bent.loop.delay)
                       < 0.05 * slid.loop.delay,
                   "slide and bend of the same interval landed a long way "
                   "apart in delay");
        }
    }
    expect(worstGrimes < 2.0,
           "the bend's tension missed Grimes' pitch by "
               + std::to_string(worstGrimes) + " cents over a whole tone");
    expect(worstImpedance < 0.01,
           "the bent impedance missed sqrt(T*mu) by "
               + std::to_string(worstImpedance) + " cents");
    expect(worstInharmonicity < 0.005,
           "the bent inharmonicity left the 1/T law by "
               + std::to_string(100.0 * worstInharmonicity) + "%");
    std::cout << "Acustra bend vs Grimes: pitch " << worstGrimes
              << " cents, impedance " << worstImpedance
              << " cents, B*T " << 100.0 * worstInharmonicity << "%\n";

    // Pitch stays additive across the two halves of an MPE bend even though
    // only the member half moves the tension: the loop is tuned to whatever
    // delay puts the fundamental where the wheels asked for it.
    double highestSplit = -1.0e9;
    double lowestSplit = 1.0e9;
    for (const auto split : { std::pair { 0.0f, 1.0f },
                              std::pair { 0.25f, 0.75f },
                              std::pair { 0.5f, 0.5f },
                              std::pair { 1.0f, 0.0f } })
    {
        const auto snapshot = acustra::AcustraEngineTestAccess::bentString(
            52, split.first, split.second);
        const double nominal = 440.0 * std::exp2((52.0 - 69.0) / 12.0)
                             * std::exp2(1.0 / 12.0);
        const double resolved = loopResonance(snapshot.loop, 1,
            2.0 * std::numbers::pi * nominal / sampleRate)
            * sampleRate / (2.0 * std::numbers::pi);
        const double cents = 1200.0 * std::log2(resolved / nominal);
        highestSplit = std::max(highestSplit, cents);
        lowestSplit = std::min(lowestSplit, cents);
    }
    // The offset from nominal the four share is the loop's own tuning
    // residual and its polarisation split, which every note carries; what
    // additivity means here is that the four splits land together.
    expect(highestSplit - lowestSplit < 0.05,
           "a semitone split between manager and member bends spread the "
           "pitch over " + std::to_string(highestSplit - lowestSplit)
               + " cents");
    std::cout << "Acustra bend additivity: four manager/member splits of a "
                 "semitone spread over " << highestSplit - lowestSplit
              << " cents at " << lowestSplit << " to " << highestSplit
              << " cents from nominal\n";
}

// The twelfth partial's stretch is the audible half of B moving with the
// tension: a whole-tone bend raises T by 27% and the stretch has to fall by
// what that B predicts.
void testABendMovesTheTwelfthPartialStretch()
{
    for (const int midiNote : { 40, 52, 64 })
    {
        double stretch[2] {};
        double predicted[2] {};
        for (int bent = 0; bent < 2; ++bent)
        {
            const double semitones = bent == 0 ? 0.0 : 2.0;
            const auto snapshot = acustra::AcustraEngineTestAccess::bentString(
                midiNote, 0.0f,
                static_cast<float>(semitones));
            const double fundamental = 440.0
                * std::exp2((static_cast<double>(midiNote) - 69.0) / 12.0)
                * std::exp2(semitones / 12.0);
            const double nominalOmega = 2.0 * std::numbers::pi
                                      * fundamental / sampleRate;
            const double first = loopResonance(snapshot.loop, 1, nominalOmega);
            const double b = snapshot.loop.inharmonicity;
            const double ratio = std::sqrt((1.0 + b * 144.0) / (1.0 + b));
            const double twelfth = loopResonance(snapshot.loop, 12,
                                                 first * 12.0 * ratio);
            stretch[bent] = 1200.0 * std::log2(twelfth / (12.0 * first));
            predicted[bent] = 1200.0 * std::log2(ratio);
        }
        const double moved = stretch[1] - stretch[0];
        const double expected = predicted[1] - predicted[0];
        // -0.3 rather than -0.5 since the 2026-09-04 refit halved steel's
        // stiffness scale (1.48178 to 0.74936). A less inharmonic string
        // stretches its twelfth partial less to begin with, so the same
        // whole-tone bend moves that stretch by less; the test's own point,
        // that the bend moves it in the direction the tension predicts and by
        // the predicted amount, is unchanged.
        expect(expected < -0.3,
               "a whole-tone bend was predicted to move MIDI "
                   + std::to_string(midiNote) + "'s H12 stretch by only "
                   + std::to_string(expected) + " cents");
        expect(std::abs(moved - expected) < 0.5,
               "MIDI " + std::to_string(midiNote)
                   + "'s H12 stretch moved by " + std::to_string(moved)
                   + " cents under a whole-tone bend where its tension "
                     "predicts " + std::to_string(expected));
        std::cout << "Acustra H12 stretch MIDI " << midiNote << ": "
                  << stretch[0] << " -> " << stretch[1] << " cents, "
                  << "predicted " << predicted[0] << " -> " << predicted[1]
                  << "\n";
    }
}

// The junction sums the strings' impedances every sample, and a whole-tone
// bend moves one string's by 12%. Stepped once a control period that is a
// step in the port; followed at the delay's own rate it is not.
void testABendDoesNotStepTheJunctionPort()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const int block = 64;
        const auto bendTo = [&] (bool member, float semitones,
                                 bool instant = false)
        {
            auto engineOwner = std::make_unique<acustra::AcustraEngine>();
            auto& engine = *engineOwner;
            acustra::EngineParameters parameters;
            engine.setParameters(parameters);
            engine.prepare(rate, block);
            engine.setLowerZoneMemberCount(4);
            engine.noteOn(52, 0.85f, 2);
            // What is measured is the junction port's step, which both
            // planes' ports would carry; the parallel plane's own wave only
            // adds the doublet's beat to every frame, so it is emptied.
            acustra::AcustraEngineTestAccess::silenceParallelPolarisation(engine);
            const int samples = static_cast<int>(2.0 * rate);
            Audio audio {
                std::vector<float>(static_cast<std::size_t>(samples)),
                std::vector<float>(static_cast<std::size_t>(samples)) };
            for (int offset = 0; offset < samples; offset += block)
            {
                const double at = offset / rate;
                const float ramp = instant
                    ? (at >= 0.5 ? 1.0f : 0.0f)
                    : static_cast<float>(
                        std::clamp((at - 0.5) / 0.3, 0.0, 1.0));
                engine.setPitchBend(semitones * ramp, member ? 2 : 1);
                engine.process(audio.left.data() + offset,
                               audio.right.data() + offset,
                               std::min(block, samples - offset));
            }
            return audio;
        };
        double peakAt = 0.0;
        const auto peak = [&] (const Audio& audio, double begin, double end)
        {
            double loudest = 0.0;
            const auto first = static_cast<std::size_t>(begin * rate);
            const auto last = std::min(audio.left.size(),
                static_cast<std::size_t>(end * rate));
            for (auto index = first; index < last; ++index)
            {
                const double here = std::max(
                    std::abs(static_cast<double>(audio.left[index])),
                    std::abs(static_cast<double>(audio.right[index])));
                if (here > loudest)
                {
                    loudest = here;
                    peakAt = static_cast<double>(index) / rate;
                }
            }
            return loudest;
        };
        const auto held = bendTo(true, 0.0f);
        const auto bent = bendTo(true, 2.0f);
        const auto slid = bendTo(false, 2.0f);
        // The same note over the same window, bent and not: what the bend
        // adds is all that is being measured.
        const double reference = peak(held, 0.5, 1.2);
        const double bentPeak = peak(bent, 0.5, 1.2);
        const double slidPeak = peak(slid, 0.5, 1.2);
        expect(reference > 0.0, "the held note was silent");
        // The bend and the slide of the same interval reach the same pitch
        // at the same moment, so what a peak measures is the interval, not
        // the mechanism: both cross the same body mode 0.2 s into the ramp
        // and both are louder there than the note held still. The tension
        // route's own contribution is the difference between them, which is
        // the string's 12.3% higher impedance in the junction's force, the
        // same allowance the stepped bend below is held to (the ramped one
        // reads 1.06).
        expect(bentPeak <= slidPeak * 1.13,
               "the tension bend peaked "
                   + std::to_string(bentPeak / slidPeak)
                   + " times the slide of the same interval");
        // Their moments are compared where the slide's crossing is a peak
        // at all: under the resolved body's narrower low modes the slide's
        // crossing can stay under the held note's own level at the window's
        // start (0.999 of it), and the loudest sample is then that start,
        // not a crossing. The moment is the loudest 10 ms frame's, not the
        // loudest sample's: through the upper-bout microphone the slide's
        // envelope has two crests 50 ms apart equal to 0.0001, and which
        // one a sample grid lands on is not the mechanism.
        // With steel's plate modes damped to the anechoic flamencas'
        // (2026-09-28) the crossing's envelope has two crests 27.5 ms apart
        // in both renders, at 96 kHz within 0.08% of each other in the bend,
        // so the bend is held to be at its crest, within 1% of its loudest
        // frame, at the moment the slide is loudest.
        const auto frameEnergy = [&] (const Audio& audio, double begin)
        {
            const auto first = static_cast<std::size_t>(begin * rate);
            const auto last = static_cast<std::size_t>((begin + 0.01) * rate);
            double energy = 0.0;
            for (auto index = first; index < last; ++index)
                energy += static_cast<double>(audio.left[index]) * audio.left[index]
                        + static_cast<double>(audio.right[index]) * audio.right[index];
            return energy;
        };
        const auto loudestFrame = [&] (const Audio& audio)
        {
            double loudest = 0.0, at = 0.0;
            for (double begin = 0.5; begin + 0.01 <= 1.2; begin += 0.0025)
            {
                const double energy = frameEnergy(audio, begin);
                if (energy > loudest)
                {
                    loudest = energy;
                    at = begin;
                }
            }
            return at;
        };
        if (slidPeak > reference * 1.02)
        {
            const double bendCrest = frameEnergy(bent, loudestFrame(slid))
                / frameEnergy(bent, loudestFrame(bent));
            expect(std::abs(loudestFrame(bent) - loudestFrame(slid)) < 0.01
                       || bendCrest >= 0.99,
                   "the tension bend's loudest moment was "
                       + std::to_string(loudestFrame(bent) - loudestFrame(slid))
                       + " s away from the slide's, where it stood at "
                       + std::to_string(bendCrest) + " of its peak");
        }
        // A step in the port would arrive as a transient rather than as a
        // level: measured where one would show, in the rise from one 5 ms
        // frame to the frame two hops before it.
        const auto rise = [&] (const Audio& audio, double until = 1.2)
        {
            std::vector<double> frames;
            for (double at = 0.45; at + 0.005 < until; at += 0.0025)
                frames.push_back(tailBandRms(audio, rate, at, at + 0.005,
                                             20.0, 0.45 * rate));
            double worst = 0.0;
            for (std::size_t index = 2; index < frames.size(); ++index)
                worst = std::max(worst,
                    frames[index] / std::max(frames[index - 2], 1.0e-30));
            return worst;
        };
        const double bentRise = rise(bent);
        const double slidRise = rise(slid);
        const double heldRise = rise(held);
        // And the hostile case the port slew exists for: the whole interval
        // arriving in one message, so the string's impedance is asked to
        // move 12% between one sample and the next.
        // A step in the port shows within the delay's slew of the message at
        // 0.5 s; hundreds of milliseconds later the frames only carry the
        // doublet's own beat, whose nulls make a 5 ms rise of any size (at
        // 96 kHz the bend's largest one sits 435 ms after the message). The
        // stepped pair is therefore read over the 200 ms after it.
        const auto stepped = bendTo(true, 2.0f, true);
        const auto steppedSlide = bendTo(false, 2.0f, true);
        const double steppedRise = rise(stepped, 0.70);
        const double steppedSlideRise = rise(steppedSlide, 0.70);
        // Both reach their worst frame at the same moment, where the new
        // pitch lands on a body mode; there the bent string's 12.3% higher
        // impedance makes the junction's force that much larger, which is
        // the mechanism the peak bound below allows 1.13 for. With the
        // parallel polarisation radiating, the note's doublet sets which
        // frame precedes the landing, and the 48 kHz stepped bend rises 1.09
        // times the slide in the same frame (level 1.077 times it). The
        // upper-bout microphone of the Stereo pair (2026-09-25) hears the
        // rocking modes the bent port rings harder than the bass-bridge one
        // did: at 44.1 kHz the stepped bend rises 1.15 times the slide
        // (1.135 on that microphone alone), where the saddle's own force,
        // the piezo, rises 0.85 times it, so the port itself does not step.
        expect(steppedRise < std::max(steppedSlideRise, heldRise) * 1.16,
               "a whole tone arriving in one message at "
                   + std::to_string(static_cast<int>(rate))
                   + " Hz raised one 5 ms frame by a factor of "
                   + std::to_string(steppedRise) + " against the slide's "
                   + std::to_string(steppedSlideRise));
        const double steppedPeak = peak(stepped, 0.5, 1.2);
        const double steppedAt = peakAt;
        const double steppedSlidePeak = peak(steppedSlide, 0.5, 1.2);
        const double steppedSlideAt = peakAt;
        // Both land 20 ms after the message, where the slewed delay has
        // arrived; what separates them is that a bent string presents 12.3%
        // more impedance and the junction's force is proportional to it, so
        // that much more level is the mechanism rather than a transient.
        // "The same moment" is within two periods of the bent note: the
        // loudest sample can fall on either of two adjacent crests.
        expect(steppedPeak <= steppedSlidePeak * 1.13
                   && std::abs(steppedAt - steppedSlideAt) < 0.0125,
               "a whole tone arriving in one message peaked "
                   + std::to_string(steppedPeak / steppedSlidePeak)
                   + " times the slide of the same interval, "
                   + std::to_string(steppedAt - steppedSlideAt)
                   + " s away from it");
        std::cout << "  stepped peaks: bend " << steppedPeak / reference
                  << ", slide " << steppedSlidePeak / reference
                  << " of the held note, both at " << steppedAt << " s\n";
        // The bend moves the port this string presents, and on a two-point
        // bridge that moves its moment as well as the force sum. The rocking
        // modes are the high-Q ones, so the same 12% move rings them harder:
        // the ramped bend's worst 5 ms rise is 3.63 against the slide's 3.38
        // at 44.1 kHz and 3.87 against 3.55 at 48 kHz, where the one-point
        // bridge sat inside 5% of the slide. It is still the slide's own
        // shape - both cross the same body mode at the same moment, within
        // 10 ms of each other - so the bound follows the mechanism rather
        // than the topology.
        expect(bentRise < std::max(slidRise, heldRise) * 1.10,
               "a whole-tone tension bend at "
                   + std::to_string(static_cast<int>(rate))
                   + " Hz raised one 5 ms frame by a factor of "
                   + std::to_string(bentRise) + " against the slide's "
                   + std::to_string(slidRise) + " and the held note's "
                   + std::to_string(heldRise));
        std::cout << "Acustra bend peak at " << static_cast<int>(rate)
                  << " Hz: bend " << bentPeak / reference << ", slide "
                  << slidPeak / reference << " of the held note, both at "
                  << loudestFrame(bent) << " s; frame rise " << bentRise << " vs "
                  << slidRise << " and " << heldRise << ", stepped "
                  << steppedRise << " vs " << steppedSlideRise << "\n";
    }
}

// A hostile wheel: the tension the model follows saturates where Grimes' law
// stops describing a string, which is inside the bend range MIDI can ask for.
// A finger pushes a string only so far: plain steel parts at roughly 1.7
// to 2 times its tuning tension, and real bends stay within 3 to 4
// semitones. So the tension a member bend asks for stops at twice the open
// string's r^2, six semitones up - or sooner, half way to Grimes'
// singularity, on a string too stretchy to get there - and the rest of a
// wider glide is a slide, carried by the delay alone (audit F8).
void testAnExtremeBendSaturatesInsideTheBendRange()
{
    constexpr double bendingDiameter[] { 0.477159e-3, 0.437895e-3,
                                         0.412021e-3, 0.38e-3,
                                         0.406e-3, 0.305e-3 };
    for (const int midiNote : { 40, 64 })
    {
        const auto unbent = acustra::AcustraEngineTestAccess::bentString(
            midiNote, 0.0f, 0.0f);
        const auto index = static_cast<std::size_t>(unbent.stringIndex);
        const double rigidity = 2.0e11 * 0.25 * std::numbers::pi
            * bendingDiameter[index] * bendingDiameter[index];
        // Grimes' dT = T0 (r^2 - 1) / (1 - r^2 T0/EA) at r^2 = 2.
        const double limit = unbent.tension
            + unbent.tension / (1.0 - 2.0 * unbent.tension / rigidity);
        for (const float semitones : { 6.0f, 12.0f, 48.0f, 96.0f })
        {
            const auto bent = acustra::AcustraEngineTestAccess::bentString(
                midiNote, 0.0f, semitones);
            expect(std::isfinite(bent.tension)
                       && std::abs(bent.tension - limit) < 2.0e-3 * limit,
                   "a " + std::to_string(semitones) + "-semitone bend did not "
                   "stop the tension at twice the open string's r^2 ("
                       + std::to_string(bent.tension) + " N against "
                       + std::to_string(limit) + ")");
            expect(bent.loop.delay >= 3.0 && std::isfinite(bent.loop.delay)
                       && bent.impedanceScale > 1.0
                       && bent.impedanceScale < 1.42
                       && std::isfinite(bent.impedanceScale),
                   "a " + std::to_string(semitones)
                       + "-semitone bend left the loop or its port unbounded");
        }
        const auto slack = acustra::AcustraEngineTestAccess::bentString(
            midiNote, 0.0f, -96.0f);
        expect(slack.tension > 0.0 && slack.tension < unbent.tension
                   && std::isfinite(slack.loop.delay),
               "a 96-semitone downward bend left the string's tension "
               "unphysical");
        std::cout << "Acustra bend tension limit MIDI " << midiNote << ": "
                  << limit << " N, " << limit / unbent.tension
                  << " times the open string's\n";
    }
}

// A wide member glide is a bend up to the tension limit and a slide past
// it, so it is about as loud as the same glide on a conventional channel,
// which is a slide throughout, and it stays below the output limiter. At
// 170 times the tuning tension it was 25 dB louder and pinned the limiter.
// The +48 glide read 4.89 dB before the capture voicing was refitted with
// the released static force (2026-10-01, second entry) and 5.04 after: the
// voicing, heard at the two glides' different final pitches, moves all
// three widths by 0.06-0.15 dB, which is not the failure this bounds.
void testAWideMemberGlideStaysAsLoudAsASlide()
{
    for (const float semitones : { 12.0f, 24.0f, 48.0f })
    {
        const auto glide = [&] (bool member)
        {
            auto engine = std::make_unique<acustra::AcustraEngine>();
            acustra::EngineParameters parameters;
            engine->setParameters(parameters);
            engine->prepare(48000.0, 64);
            const int channel = member ? 2 : 1;
            if (member)
                engine->setLowerZoneMemberCount(15);
            engine->noteOn(57, 0.8f, channel);
            std::vector<float> left(64), right(64);
            double energy = 0.0, peak = 0.0;
            for (int block = 0; block < 1500; ++block)
            {
                if (block >= 225 && block < 300)
                    engine->setPitchBend(semitones
                        * static_cast<float>(block - 224) / 75.0f, channel);
                engine->process(left.data(), right.data(), 64);
                if (block >= 300 && block < 750)
                    for (int n = 0; n < 64; ++n)
                    {
                        energy += left[static_cast<std::size_t>(n)] * left[static_cast<std::size_t>(n)]
                            + right[static_cast<std::size_t>(n)] * right[static_cast<std::size_t>(n)];
                        peak = std::max({ peak,
                            static_cast<double>(std::abs(left[static_cast<std::size_t>(n)])),
                            static_cast<double>(std::abs(right[static_cast<std::size_t>(n)])) });
                    }
            }
            return std::pair { energy, peak };
        };
        const auto [memberEnergy, memberPeak] = glide(true);
        const auto [slideEnergy, slidePeak] = glide(false);
        const double louder = 10.0 * std::log10(memberEnergy / slideEnergy);
        std::cout << "Acustra steel A3 member glide +" << semitones << ": " << louder
                  << " dB against a slide, peak " << memberPeak << '\n';
        expect(louder < 5.5 && memberPeak < 0.89125094,
               "a +" + std::to_string(semitones) + " member glide was "
                   + std::to_string(louder) + " dB louder than a slide, peak "
                   + std::to_string(memberPeak));
    }
}

// CC1 is the left hand's vibrato. Zero is the wheel not touched.
void testTheVibratoWheelAtZeroIsExact()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const int block = 64;
        const auto play = [&] (bool sendZero)
        {
            auto engineOwner = std::make_unique<acustra::AcustraEngine>();
            auto& engine = *engineOwner;
            acustra::EngineParameters parameters;
            engine.setParameters(parameters);
            engine.prepare(rate, block);
            engine.noteOn(52, 0.85f);
            const int samples = static_cast<int>(1.0 * rate);
            Audio audio {
                std::vector<float>(static_cast<std::size_t>(samples)),
                std::vector<float>(static_cast<std::size_t>(samples)) };
            for (int offset = 0; offset < samples; offset += block)
            {
                if (sendZero)
                    engine.setVibrato(0.0f);
                engine.process(audio.left.data() + offset,
                               audio.right.data() + offset,
                               std::min(block, samples - offset));
            }
            return audio;
        };
        const auto untouched = play(false);
        const auto zeroed = play(true);
        expect(untouched.left == zeroed.left && untouched.right == zeroed.right,
               "CC1 at zero changed the sound at "
                   + std::to_string(static_cast<int>(rate)) + " Hz");
    }
}

void testMpeTimbreSetsPerNotePluckPointOnMemberChannelOnly()
{
    using acustra::AcustraEngineTestAccess;
    constexpr float span = 0.46f - 0.05f;

    const double conventionalLow
        = AcustraEngineTestAccess::mpeTimbrePluckPoint(0.1f, 1);
    const double conventionalHigh
        = AcustraEngineTestAccess::mpeTimbrePluckPoint(0.9f, 1);
    expect(conventionalLow == conventionalHigh,
           "CC74 moved the pluck point on a conventional, non-member channel");

    const double memberUnset
        = AcustraEngineTestAccess::mpeTimbrePluckPoint(-1.0f, 2);
    expect(memberUnset == conventionalLow,
           "an MPE member channel that never received CC74 did not fall back "
           "to the panel pluck position");

    const double memberLow
        = AcustraEngineTestAccess::mpeTimbrePluckPoint(0.1f, 2);
    const double memberHigh
        = AcustraEngineTestAccess::mpeTimbrePluckPoint(0.9f, 2);
    expect(memberLow >= 0.05 - 1.0e-6 && memberLow <= 0.46 + 1.0e-6
               && memberHigh >= 0.05 - 1.0e-6 && memberHigh <= 0.46 + 1.0e-6,
           "CC74's pluck point left its published 0.05-0.46 band");
    expect(std::abs((memberHigh - memberLow) - 0.8 * span) < 1.0e-4,
           "CC74 did not move the pluck point across its own 0.05-0.46 span");
    for (const auto picking : { acustra::PickingTechnique::Pick,
                                acustra::PickingTechnique::Thumb })
    {
        expect(AcustraEngineTestAccess::mpeTimbrePluckPoint(0.1f, 2, 52, picking) == memberLow
                   && AcustraEngineTestAccess::mpeTimbrePluckPoint(0.9f, 2, 52, picking) == memberHigh,
               "picking style overrode an explicit MPE pluck position");
    }
}

void testMpePressureBiasesVibratoDepthWithinTheWheelsOwnBound()
{
    using acustra::AcustraEngineTestAccess;

    const double conventionalLight
        = AcustraEngineTestAccess::vibratoDepthCents(1.0f, 0.0f, 1);
    const double conventionalFirm
        = AcustraEngineTestAccess::vibratoDepthCents(1.0f, 1.0f, 1);
    expect(conventionalLight == conventionalFirm,
           "channel pressure moved the vibrato depth on a conventional, "
           "non-member channel");

    const double memberUnset
        = AcustraEngineTestAccess::vibratoDepthCents(1.0f, -1.0f, 2);
    const double memberFull
        = AcustraEngineTestAccess::vibratoDepthCents(1.0f, 1.0f, 2);
    expect(memberUnset == memberFull,
           "full member pressure did not reproduce the wheel's own unbiased "
           "depth");
    expect(std::abs(memberFull - 20.0) < 1.0e-3,
           "the wheel's own top depth moved off its published 20 cents");

    const double memberLight
        = AcustraEngineTestAccess::vibratoDepthCents(1.0f, 0.0f, 2);
    expect(std::abs(memberLight - 10.0) < 1.0e-3,
           "a light grip did not sit at the authored 50% depth floor");
}

void testStringPerChannelModeIsOptInAndBypassesTheAllocator()
{
    using acustra::AcustraEngineTestAccess;
    const auto snapshot = AcustraEngineTestAccess::stringPerChannelBehaviour();
    expect(snapshot.activeWithAllocator == 1,
           "the fret-distance allocator, left alone, dropped a note it can "
           "reach on some other string");
    expect(snapshot.activeWithModeForcingAnUnfrettableString == 0,
           "string-per-channel mode reassigned a note instead of dropping it");
    expect(snapshot.ownNoteLandedOnItsOwnString,
           "string-per-channel mode did not put channel 1's note on string 0");
    expect(!snapshot.leakedOntoAnotherString,
           "string-per-channel mode's note leaked onto another string");
    expect(snapshot.activeAfterModeOff == 1,
           "turning string-per-channel mode back off did not restore the "
           "allocator");
}

// What the wheel does when it is not zero, against what the sources measured.
// The gesture those sources describe is a fundamental-frequency modulation
// (Erkut: the string is repeatedly stretched to fluctuate the fundamental),
// so this measures a frequency: each block's loop is resolved for the pitch
// it is sounding, and the same note rendered with the wheel down is resolved
// beside it, so what is compared is the vibrato itself and not the note's own
// tuning residual or its attack glide.
void testTheVibratoWheelStaysInsideItsPublishedBounds()
{
    const auto trace = [] (float wheel, double rate, int block, int midiNote)
    {
        return acustra::AcustraEngineTestAccess::vibratoLoopTrace(
            wheel, rate, 3.0, block, midiNote);
    };
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const int block = 64;
        const double perBlock = block / rate;
        const auto centsTrace = [&] (float wheel, int midiNote)
        {
            const auto bent = trace(wheel, rate, block, midiNote);
            const auto still = trace(0.0f, rate, block, midiNote);
            const double nominalOmega = 2.0 * std::numbers::pi * 440.0
                * std::exp2((midiNote - 69.0) / 12.0) / rate;
            std::vector<double> cents;
            const auto blocks = std::min(bent.size(), still.size());
            for (std::size_t index = 0; index < blocks; ++index)
                cents.push_back(1200.0 * std::log2(
                    loopResonance(bent[index], 1, nominalOmega, 60.0)
                    / loopResonance(still[index], 1, nominalOmega, 60.0)));
            return cents;
        };
        for (const float wheel : { 0.35f, 1.0f })
        {
            const auto cents = centsTrace(wheel, 52);
            expect(cents.size() > 1000, "the vibrato trace was too short");
            double deepest = 0.0;
            double lowest = 1.0e9;
            double shallowest = 0.0;
            for (std::size_t index = 0; index < cents.size(); ++index)
            {
                deepest = std::max(deepest, cents[index]);
                lowest = std::min(lowest, cents[index]);
                if (static_cast<double>(index) * perBlock < 0.2)
                    shallowest = std::max(shallowest, cents[index]);
            }
            // Erkut et al. 2000 Sec. 3.3: the lowest frequency during a
            // vibrato is the nominal fundamental of the tone without it, so
            // the sounding pitch never goes below the note's own. What the
            // tolerance covers is the CACHED DISPERSION DESIGN, not any
            // string-model surrogate: the allpass is re-solved only once the
            // wheel has moved B by 0.2% (see the cache guard in
            // updateDispersion), so between re-solves the loop is tuned for
            // a slightly stale B. Dropping that guard so the design re-solves
            // on every change takes this trace's worst dip from -0.0386 to
            // -0.000153 cents, a factor of 250, and leaves the depth below
            // alone. 0.1 is therefore about 2.6x the worst the shipping
            // build actually dips, which the printed `lowest` reports.
            expect(lowest >= -0.1,
                   "the vibrato took the sounding pitch "
                       + std::to_string(-lowest)
                       + " cents below the note's own");
            // The wheel's full-scale depth is the authored 20 cents, and
            // what the string sounds is that interval plus the loop's own
            // tuning residual, which is not quite the same at the top of the
            // excursion as at the bottom: 0.50% of the interval at 44.1 kHz,
            // 0.46% at 48 and 0.23% at 96, as the printed depths show. Unlike
            // the dip above, this is unchanged when the dispersion design is
            // re-solved on every change instead of every 0.2% of B, so it is
            // the tuning solve and not the stale design. One percent is the
            // bound.
            expect(deepest > 1.0 && deepest <= 20.2,
                   "the wheel's vibrato reached " + std::to_string(deepest)
                       + " cents of pitch");
            // Its 0.5 s transient (Erkut's tt) means the first fifth of a
            // second cannot already be at depth.
            expect(shallowest < 0.5 * deepest,
                   "the vibrato reached " + std::to_string(shallowest)
                       + " of its " + std::to_string(deepest)
                       + " cents inside the first 0.2 s");
            // Rate, from the half-depth crossings after the transient.
            // Erkut's 1.4 Hz slow and 4.9 Hz fast are the wheel's endpoints.
            std::vector<double> crossings;
            for (std::size_t index = 1; index < cents.size(); ++index)
                if (static_cast<double>(index) * perBlock > 1.0
                    && cents[index - 1] < 0.5 * deepest
                    && cents[index] >= 0.5 * deepest)
                    crossings.push_back(
                        static_cast<double>(index) * perBlock);
            expect(crossings.size() >= 2,
                   "the vibrato did not repeat after its transient");
            const double period = (crossings.back() - crossings.front())
                / static_cast<double>(crossings.size() - 1);
            const double measured = 1.0 / period;
            expect(measured > 1.35 && measured < 5.0,
                   "the vibrato ran at " + std::to_string(measured)
                       + " Hz, outside the 1.4-4.9 Hz the sources measured");
            std::cout << "Acustra vibrato wheel " << wheel << " at "
                      << static_cast<int>(rate) << " Hz: " << measured
                      << " Hz, pitch depth " << deepest << " cents, lowest "
                      << lowest << "\n";
        }
        // An open string has no finger stopping it, so it has no vibrato
        // (Laurson et al. 2001: max-depth is zero at fret zero). Nothing
        // about its loop moves, so the two renders are the same loop block
        // for block.
        const auto open = trace(1.0f, rate, block, 40);
        const auto openStill = trace(0.0f, rate, block, 40);
        expect(!open.empty() && open.size() == openStill.size(),
               "the open-string vibrato trace was empty");
        bool moved = false;
        for (std::size_t index = 0; index < open.size(); ++index)
            moved = moved || open[index].delay != openStill[index].delay
                || open[index].inharmonicity
                       != openStill[index].inharmonicity;
        expect(!moved, "an open string was given a vibrato");
    }
}

// A member bend is one finger on one string.
void testAMemberBendRetunesOnlyItsOwnString()
{
    const auto isolation
        = acustra::AcustraEngineTestAccess::memberBendIsolation();
    expect(isolation[1] < isolation[0] - 1.0
               && isolation[3] > isolation[2] * 1.2,
           "a member bend did not retune and re-tension its own string");
    expect(isolation[4] == isolation[5] && isolation[6] == isolation[7],
           "a member bend reached a string on another member channel");
}

void testBlockPartitionIsDeterministic()
{
    const auto a = render({}, 47, 0.73f, 0.5, 1);
    const auto b = render({}, 47, 0.73f, 0.5, 257);
    expect(a.left == b.left && a.right == b.right,
           "host block partition changed deterministic engine output");
}

void testSampleRatesAndAutomationStayBounded()
{
    // Every control a host can automate moves, the construction's own
    // (Model, Tuning, Capture) among them, at every rate the engine
    // models; the Piezo output is requested and held to the same bounds.
    constexpr double rates[] { 8000.0, 44100.0, 48000.0, 88200.0,
                               96000.0, 192000.0, 384000.0 };
    for (const double rate : rates)
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        auto maximumDisplacement = acustra::fittedPhysicalCalibration;
        maximumDisplacement.steelDisplacementScaleMetres = 0.04f;
        engine.setPhysicalCalibration(maximumDisplacement);
        engine.prepare(rate, 73);
        acustra::EngineParameters parameters;
        std::vector<float> left(73);
        std::vector<float> right(73);
        std::vector<float> piezo(73);
        acustra::AcustraEngine::OutputBuses buses;
        buses.piezo = piezo.data();
        for (int step = 0; step < 180; ++step)
        {
            parameters.guitarModel = static_cast<acustra::GuitarModel>((step / 29) % 2);
            parameters.tuning = static_cast<acustra::Tuning>((step / 13) % 5);
            constexpr acustra::CaptureType captures[] { acustra::CaptureType::StereoMic,
                acustra::CaptureType::MonoMic, acustra::CaptureType::Piezo };
            parameters.capture = captures[(step / 19) % 3];
            parameters.pluckPosition = static_cast<float>((step * 43) % 101) / 100.0f;
            parameters.touch = static_cast<float>((step * 53) % 101) / 100.0f;
            parameters.shape = static_cast<acustra::BodyShape>((step / 17) % 4);
            parameters.bodyMaterial = static_cast<acustra::BodyMaterial>((step / 23) % 3);
            parameters.picking = static_cast<acustra::PickingTechnique>((step / 11) % 3);
            parameters.stringAge = static_cast<float>((step * 37) % 101) / 100.0f;
            parameters.bodyAmount = static_cast<float>((step * 19) % 101) / 100.0f;
            parameters.stereoWidth = static_cast<float>((step * 29) % 101) / 100.0f;
            engine.setParameters(parameters);
            if (step % 31 == 0)
            {
                for (const int note : { 40, 45, 50, 55, 59, 64 })
                    engine.noteOn(note, 1.0f);
            }
            if (step % 47 == 0)
                engine.setPitchBend(step % 94 == 0 ? 2.0f : -2.0f);
            engine.process(left.data(), right.data(), buses, 73);
            for (int sample = 0; sample < 73; ++sample)
            {
                const float l = left[static_cast<std::size_t>(sample)];
                const float r = right[static_cast<std::size_t>(sample)];
                const float p = piezo[static_cast<std::size_t>(sample)];
                expect(std::isfinite(l) && std::isfinite(r) && std::isfinite(p),
                       "automation produced non-finite output at "
                           + std::to_string(rate) + " Hz");
                expect(std::abs(l) < 4.0f && std::abs(r) < 4.0f && std::abs(p) < 4.0f,
                       "automation escaped the bounded output knee at "
                           + std::to_string(rate) + " Hz");
            }
            expect(engine.getActiveVoiceCount() <= acustra::AcustraEngine::stringCount,
                   "allocator exceeded six physical strings");
        }
    }
}

void testHostileParametersAreSanitised()
{
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.prepare(sampleRate, blockSize);
    acustra::EngineParameters parameters;
    parameters.shape = static_cast<acustra::BodyShape>(99);
    parameters.bodyMaterial = static_cast<acustra::BodyMaterial>(-4);
    parameters.tuning = static_cast<acustra::Tuning>(88);
    parameters.picking = static_cast<acustra::PickingTechnique>(-1);
    parameters.stringAge = std::numeric_limits<float>::quiet_NaN();
    parameters.pluckPosition = std::numeric_limits<float>::infinity();
    parameters.touch = -std::numeric_limits<float>::infinity();
    parameters.bodyAmount = 1000.0f;
    parameters.outputGain = 1000.0f;
    engine.setParameters(parameters);
    engine.noteOn(40, 1.0f);
    std::vector<float> left(blockSize);
    std::vector<float> right(blockSize);
    engine.process(left.data(), right.data(), blockSize);
    expect(std::all_of(left.begin(), left.end(), [] (float value)
        { return std::isfinite(value) && std::abs(value) <= 1.0f; }),
        "hostile parameters escaped sanitisation");
}

void testHostilePhysicalCalibrationIsSanitised()
{
    const auto uniformMaterial = [] (float value)
    {
        return acustra::MaterialCalibration {
            value, value, value, value, value, value, value
        };
    };
    const auto values = [] (const acustra::MaterialCalibration& material)
    {
        return std::array {
            material.stiffnessScale, material.fundamentalT60Scale,
            material.frequencyLossScale, material.apertureScale,
            material.transientScale, material.pluckDistanceScale,
            material.velocityBrightnessDepth
        };
    };
    const acustra::PhysicalCalibration lowSource {
        -100.0f, -100.0f, -100.0f, -100.0f, -100.0f,
        uniformMaterial(-100.0f), -100.0f, -100.0f,
        -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f,
        -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f
    };
    const acustra::PhysicalCalibration highSource {
        100.0f, 100.0f, 100.0f, 100.0f, 100.0f,
        uniformMaterial(100.0f), 100.0f, 100.0f,
        100.0f, 100.0f, 100.0f, 100.0f, 100000.0f, 100.0f,
        100.0f, 100000.0f, 100.0f, 100.0f, 100.0f, 100.0f
    };
    const auto sanitised = [] (acustra::PhysicalCalibration source)
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        engine.setPhysicalCalibration(source);
        return acustra::AcustraEngineTestAccess::calibration(engine);
    };
    const auto low = sanitised(lowSource);
    const auto high = sanitised(highSource);
    expect(std::array { low.bodyFrequencyScale, low.bodyQScale,
                        low.bridgeMobilityScale, low.residueTiltDbPerOctave,
                        low.directGain, low.apertureRegisterExponent,
                        low.lowBodyModeGain,
                        low.steelDisplacementScaleMetres,
                        low.steelFretT60Slope, low.highLossCutoffScale,
                        low.bridgeConductanceFloor,
                        low.bridgeConductanceCornerHz,
                        low.bridgeTailLengthMetres,
                        low.polarisationEndCorrectionMetres,
                        low.pickReleaseVelocityShare,
                        low.pickReleaseVelocityExponent,
                        low.pickTransientGain }
               == std::array { 0.96f, 0.05f, 0.25f, -6.0f, 0.0f, -1.0f,
                               0.25f, 0.0f, -0.06f, 0.5f, 0.0f, 100.0f,
                               0.00325f, 0.0f, 0.0f, 0.0f, 0.0f },
           "low physical calibration bounds were not enforced");
    expect(std::array { high.bodyFrequencyScale, high.bodyQScale,
                        high.bridgeMobilityScale, high.residueTiltDbPerOctave,
                        high.directGain, high.apertureRegisterExponent,
                        high.lowBodyModeGain,
                        high.steelDisplacementScaleMetres,
                        high.steelFretT60Slope, high.highLossCutoffScale,
                        high.bridgeConductanceFloor,
                        high.bridgeConductanceCornerHz,
                        high.bridgeTailLengthMetres,
                        high.polarisationEndCorrectionMetres,
                        high.pickReleaseVelocityShare,
                        high.pickReleaseVelocityExponent,
                        high.pickTransientGain }
               == std::array { 1.04f, 1.8f, 4.0f, 6.0f, 0.12f, 1.0f,
                               32.0f, 0.04f, 0.05f, 4.0f, 0.02f, 8000.0f,
                               0.060f, 0.82e-3f, 2.0f, 4.0f, 8.0f },
           "high physical calibration bounds were not enforced");
    const std::array materialLow {
        0.25f, 0.4f, 0.35f, 0.35f, 0.0f, 0.7f, 0.0f
    };
    const std::array materialHigh {
        4.0f, 2.0f, 3.0f, 2.5f, 3.0f, 3.0f, 1.2f
    };
    expect(values(low.steel) == materialLow,
           "low material calibration bounds were not enforced");
    expect(values(high.steel) == materialHigh,
           "high material calibration bounds were not enforced");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const acustra::PhysicalCalibration poisoned {
        nan, nan, nan, nan, nan, uniformMaterial(nan),
        nan, nan, nan, nan, nan, nan, nan, nan, nan, nan, nan
    };
    const auto fallback = sanitised(poisoned);
    expect(std::array { fallback.bodyFrequencyScale, fallback.bodyQScale,
                        fallback.bridgeMobilityScale,
                        fallback.residueTiltDbPerOctave, fallback.directGain,
                        fallback.apertureRegisterExponent,
                        fallback.lowBodyModeGain,
                        fallback.steelDisplacementScaleMetres,
                        fallback.steelFretT60Slope,
                        fallback.highLossCutoffScale,
                        fallback.bridgeConductanceFloor,
                        fallback.bridgeConductanceCornerHz,
                        fallback.polarisationEndCorrectionMetres }
               == std::array {
                    acustra::fittedPhysicalCalibration.bodyFrequencyScale,
                    acustra::fittedPhysicalCalibration.bodyQScale,
                    acustra::fittedPhysicalCalibration.bridgeMobilityScale,
                    acustra::fittedPhysicalCalibration.residueTiltDbPerOctave,
                    acustra::fittedPhysicalCalibration.directGain,
                    acustra::fittedPhysicalCalibration.apertureRegisterExponent,
                    acustra::fittedPhysicalCalibration.lowBodyModeGain,
                    acustra::fittedPhysicalCalibration.steelDisplacementScaleMetres,
                    acustra::fittedPhysicalCalibration.steelFretT60Slope,
                    acustra::fittedPhysicalCalibration.highLossCutoffScale,
                    acustra::fittedPhysicalCalibration.bridgeConductanceFloor,
                    acustra::fittedPhysicalCalibration.bridgeConductanceCornerHz,
                    acustra::fittedPhysicalCalibration.polarisationEndCorrectionMetres }
               && values(fallback.steel)
                    == values(acustra::fittedPhysicalCalibration.steel),
           "non-finite physical calibration did not use fitted defaults");

    auto resetProbeOwner = std::make_unique<acustra::AcustraEngine>();
    auto& resetProbe = *resetProbeOwner;
    resetProbe.prepare(sampleRate, blockSize);
    resetProbe.noteOn(52, 0.8f);
    expect(resetProbe.getActiveVoiceCount() == 1,
           "physical-calibration reset probe did not allocate a voice");
    resetProbe.setPhysicalCalibration(high);
    expect(resetProbe.getActiveVoiceCount() == 0,
           "prepared physical-calibration change did not reset the engine");

    for (const auto& calibration : { low, high })
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        engine.setPhysicalCalibration(calibration);
        acustra::EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(sampleRate, blockSize);
        engine.noteOn(52, 1.0f);
        std::vector<float> left(blockSize);
        std::vector<float> right(blockSize);
        double maximum = 0.0;
        for (int block = 0; block < 100; ++block)
        {
            engine.process(left.data(), right.data(), blockSize);
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto index = static_cast<std::size_t>(sample);
                expect(std::isfinite(left[index]) && std::isfinite(right[index]),
                       "bounded physical calibration produced non-finite audio");
                maximum = std::max(maximum, static_cast<double>(std::max(
                    std::abs(left[index]), std::abs(right[index]))));
            }
            expect(std::isfinite(engine.getLastBridgeVelocity())
                       && std::isfinite(engine.getLastBridgeReactionForce())
                       && std::isfinite(engine.getLastBridgeBodyForce())
                       && std::isfinite(engine.getLastBridgeTailForce()),
                   "bounded physical calibration poisoned bridge telemetry");
        }
        expect(maximum > 1.0e-7 && maximum <= 1.0,
               "bounded physical calibration was silent or escaped headroom");
    }
}

void testBodyAndBridgeCalibrationChangePhysicalDescriptors()
{
    auto low = acustra::fittedPhysicalCalibration;
    low.bodyFrequencyScale = 0.96f;
    low.bodyQScale = 0.05f;
    low.bridgeMobilityScale = 0.25f;
    auto high = acustra::fittedPhysicalCalibration;
    high.bodyFrequencyScale = 1.04f;
    high.bodyQScale = 1.8f;
    high.bridgeMobilityScale = 4.0f;
    const auto lowBody = acustra::AcustraEngineTestAccess::configuredBody(low, 2);
    const auto highBody = acustra::AcustraEngineTestAccess::configuredBody(high, 2);
    expect(highBody.frequency / lowBody.frequency > 1.08,
           "body frequency calibration did not move the modal pole");
    expect(highBody.q / lowBody.q > 3.2,
           "body Q calibration did not move modal decay");

    auto downward = acustra::fittedPhysicalCalibration;
    downward.residueTiltDbPerOctave = -6.0f;
    auto upward = acustra::fittedPhysicalCalibration;
    upward.residueTiltDbPerOctave = 6.0f;
    const double downwardSlope
        = acustra::AcustraEngineTestAccess::configuredBody(downward, 36).residue
        / acustra::AcustraEngineTestAccess::configuredBody(downward, 2).residue;
    const double upwardSlope
        = acustra::AcustraEngineTestAccess::configuredBody(upward, 36).residue
        / acustra::AcustraEngineTestAccess::configuredBody(upward, 2).residue;
    expect(upwardSlope > 100.0 * downwardSlope,
           "residue tilt did not rotate the body spectrum around 1 kHz");

    auto quietLowModes = acustra::fittedPhysicalCalibration;
    quietLowModes.lowBodyModeGain = 0.25f;
    auto strongLowModes = acustra::fittedPhysicalCalibration;
    strongLowModes.lowBodyModeGain = 32.0f;
    const double lowModeRatio
        = acustra::AcustraEngineTestAccess::configuredBody(
            strongLowModes, 0).residue
        / acustra::AcustraEngineTestAccess::configuredBody(
            quietLowModes, 0).residue;
    const double midModeRatio
        = acustra::AcustraEngineTestAccess::configuredBody(
            strongLowModes, 2).residue
        / acustra::AcustraEngineTestAccess::configuredBody(
            quietLowModes, 2).residue;
    expect(std::abs(lowModeRatio - 128.0) < 1.0e-3
               && std::abs(midModeRatio - 1.0) < 1.0e-6,
           "low-body calibration changed the wrong measured modes");

    // The plate conductance floor extrapolates past the measured band and
    // carries its own level, so the measured weights must be checked with it
    // switched off; the floor must then add conductance on top of them.
    // Steel's own bridge takes the radiation's poles, so the body's frequency
    // and Q calibration moves its modes too; hold those equal so the ratio
    // reads the mobility scale alone.
    auto lowMeasured = low;
    lowMeasured.bridgeConductanceFloor = 0.0f;
    auto highMeasured = high;
    highMeasured.bridgeConductanceFloor = 0.0f;
    highMeasured.bodyFrequencyScale = lowMeasured.bodyFrequencyScale;
    highMeasured.bodyQScale = lowMeasured.bodyQScale;
    const double mobilityRatio
        = acustra::AcustraEngineTestAccess::bridgeAdmittance(highMeasured)
        / acustra::AcustraEngineTestAccess::bridgeAdmittance(lowMeasured);
    expect(std::abs(mobilityRatio - 16.0) < 1.0e-3,
           "bridge calibration did not scale every positive modal weight");
    expect(acustra::AcustraEngineTestAccess::bridgeAdmittance(low)
               > acustra::AcustraEngineTestAccess::bridgeAdmittance(lowMeasured),
           "the plate conductance floor did not add junction conductance");
    expect(std::abs(acustra::AcustraEngineTestAccess::playedDelay(high)
                  - acustra::AcustraEngineTestAccess::playedDelay(low)) > 0.05,
           "bridge mobility did not reach speaking-string phase delay");
}

// A Finger or Thumb release (initialisePluck) runs the contact's line
// through the stroke's slip, y[s] = (1 - b) x[s] + b y[s - 1] over the
// period, and the full-velocity slip's periodic inverse, then rescales the
// line to the spread about its mean it had and re-zeroes the bridge sample.
// On DFT bin n of an N-sample line that is this ratio times one real scale;
// the re-zeroing moves only DC. With no reference pole the line is the
// contact alone.
std::complex<double> releaseSlipRatio(double slipPole, double referencePole,
                                      int harmonic, int length)
{
    if (!(referencePole > 0.0))
        return 1.0;
    const auto delay = std::polar(1.0, -2.0 * std::numbers::pi * harmonic / length);
    return (1.0 - slipPole) / (1.0 - slipPole * delay)
        * (1.0 - referencePole * delay) / (1.0 - referencePole);
}

// The rescale that keeps the released line's spread: by Parseval the spread
// about the mean is the sum of |X_n|^2 over the bins other than DC.
double releaseSlipScale(double slipPole, double referencePole,
                        const std::vector<std::complex<double>>& bins)
{
    if (!(referencePole > 0.0))
        return 1.0;
    double before = 0.0, after = 0.0;
    const int length = static_cast<int>(bins.size());
    for (int n = 1; n < length; ++n)
    {
        const auto& bin = bins[static_cast<std::size_t>(n)];
        before += std::norm(bin);
        after += std::norm(bin * releaseSlipRatio(slipPole, referencePole, n, length));
    }
    return after > 0.0 ? std::sqrt(before / after) : 1.0;
}

void testReleasedContactPreservesTheLinearFilterSpectrum()
{
    // Independently integrate the asymmetric triangle's Fourier series.
    // Sampling aliases all n+kN coefficients into DFT bin n; the continuous
    // Gaussian convolution multiplies each by exp(-2*pi^2*(n+kN)^2*a^2). Subtracting
    // the endpoint changes only DC. A subsequent zero clamp violates this.
    // The probe's Thumb at velocity 0.6 is then released through its slip as
    // a ratio to the full-velocity slip (releaseSlipRatio), a linear filter
    // too: each bin is multiplied by the ratio's response and one scale.
    constexpr int aliases = 64;
    double worstError = 0.0;
    int slipped = 0;
    for (const int rate : { 44100, 48000, 96000 })
        for (const int string : { 0, 5 })
        {
            const auto state = acustra::AcustraEngineTestAccess::releasedContact(
                rate, string);
            const int length = static_cast<int>(state.history.size());
            const double p = state.position, a = state.aperture;
            expect(state.gain > 0.0 && a > 0.0 && a < 0.125,
                   "released-contact Fourier probe left its resolved domain");
            // A soft stroke is released more slowly than a full one, so its
            // slip's pole is the larger.
            expect(state.referencePole > 0.0 && state.slipPole > state.referencePole,
                   "a velocity 0.6 Thumb was not released through its slower slip");
            slipped += state.referencePole > 0.0 ? 1 : 0;
            const auto contact = [&] (int harmonic)
            {
                std::complex<double> expected {};
                for (int alias = -aliases; alias <= aliases; ++alias)
                {
                    const double m = harmonic + alias * length;
                    if (m == 0.0)
                        continue;
                    const auto triangle = (std::polar(1.0, -2.0 * std::numbers::pi * m * p)
                        - 1.0) / (4.0 * std::numbers::pi * std::numbers::pi
                                  * m * m * p * (1.0 - p));
                    const double kernel = std::exp(-2.0 * std::numbers::pi
                        * std::numbers::pi * m * m * a * a);
                    expected += triangle * kernel;
                }
                return expected;
            };
            std::vector<std::complex<double>> bins(static_cast<std::size_t>(length));
            for (int n = 1; n < length; ++n)
                bins[static_cast<std::size_t>(n)] = contact(n);
            const double scale = releaseSlipScale(state.slipPole, state.referencePole, bins);
            // |T_m| <= 1/(2*pi^2*p*(1-p)*m^2). For n<N/2 the
            // omitted +/- alias tails are bounded by this integral; the
            // slip ratio's gain, at most (1 + u)/(1 - u) times the slip's
            // unit gain, and the scale carry it.
            const double ratioBound = scale * (1.0 + state.referencePole)
                / (1.0 - state.referencePole);
            const double tailBound = ratioBound / (std::numbers::pi * std::numbers::pi
                * p * (1.0 - p) * length * length * (aliases - 0.5));
            // Float phase rounding is amplified by a triangle slope of
            // at most 1/min(p,1-p). Corner interpolation (unit error
            // <6.20e-11) and image sums use double; final float storage
            // and amplitude recovery also fit within this conservative
            // bound on the probed a<0.125 domain. DFT averaging cannot
            // amplify the absolute time-domain error. The slip's float
            // storage between its two passes is amplified by the inverse's
            // gain, bounded as above.
            const double roundingBound = 16.0 * std::numeric_limits<float>::epsilon()
                / std::min(p, 1.0 - p) * (1.0 + ratioBound);
            for (const int harmonic : { 1, 2, 3, 5, 8,
                 static_cast<int>(std::round(0.5 / a)),
                 static_cast<int>(std::round(1.0 / a)) })
            {
                if (harmonic <= 0 || 2 * harmonic >= length)
                    continue;
                const auto expected = bins[static_cast<std::size_t>(harmonic)] * scale
                    * releaseSlipRatio(state.slipPole, state.referencePole, harmonic, length);
                std::complex<double> observed {};
                for (int sample = 0; sample < length; ++sample)
                    observed += state.history[static_cast<std::size_t>(sample)]
                        / (state.gain * length) * std::polar(1.0,
                            -2.0 * std::numbers::pi * harmonic * sample / length);
                const double error = std::abs(observed - expected);
                worstError = std::max(worstError, error);
                expect(error <= tailBound + roundingBound,
                       "released-contact linear spectrum differs by " + std::to_string(error)
                       + " at rate " + std::to_string(rate) + " string "
                       + std::to_string(string) + " H" + std::to_string(harmonic));
            }
        }
    expect(slipped == 6, "the released-contact probe did not reach the slip");
    std::cout << "Acustra released-contact maximum complex coefficient error: "
              << worstError << '\n';
}

void testBroadContactWrapsAndReachesItsUniformLimit()
{
    using Access = acustra::AcustraEngineTestAccess;
    // Public calibration, RPN-range bends, MPE timbre and host rates reach
    // these domains without injecting a kernel or manufacturing loop state.
    // Reconstruct the whole signed wave with an independent Fourier series,
    // including both sides of its periodic boundary and both polarisations.
    struct Domain { int rate; float bend, exponent; };
    constexpr std::array domains {
        Domain { 48000, 0.0f, 1.0f }, Domain { 96000, 12.0f, 1.0f },
        Domain { 384000, 24.0f, 1.0f }, Domain { 384000, 96.0f, 1.0f },
        Domain { 8000, -96.0f, -1.0f }
    };
    constexpr int terms = 32;
    // For a>=0.125 the omitted Fourier tail is below 1e-145.
    // The absolute tolerance covers float phase/gain recovery and storage,
    // plus the double corner table's <6.20e-11 unit interpolation error.
    constexpr double tolerance = 2.0e-6;
    double worstError = 0.0;
    double smallestAperture = std::numeric_limits<double>::max();
    double largestAperture = 0.0;
    int uniformCases = 0, signedWrapCases = 0, cases = 0;
    for (const auto& domain : domains)
        for (const float timbre : { 0.0f, 1.0f })
            for (const int polarisation : { 0, 1 })
            {
                Access::ReleasedContactOptions options;
                options.calibration.steel.apertureScale = 2.5f;
                options.calibration.steel.velocityBrightnessDepth = 0.0f;
                options.calibration.apertureRegisterExponent = domain.exponent;
                options.touch = 0.0f;
                options.bend = domain.bend;
                options.timbre = timbre;
                options.fret = 19;
                options.polarisation = polarisation;
                options.picking = acustra::PickingTechnique::Finger;
                const auto state = Access::releasedContact(domain.rate, 5, options);
                const double p = state.position, a = state.aperture;
                const int length = static_cast<int>(state.history.size());
                expect(state.gain > 0.0 && length >= 8 && a >= 0.125,
                       "broad-contact probe missed its intended legal domain");
                smallestAperture = std::min(smallestAperture, a);
                largestAperture = std::max(largestAperture, a);
                const bool uniform = std::exp(-2.0 * std::numbers::pi
                    * std::numbers::pi * a * a) / (6.0 * p * (1.0 - p))
                    <= std::numeric_limits<double>::epsilon();
                std::array<std::complex<double>, terms> coefficients {};
                for (int n = 1; n <= terms; ++n)
                {
                    const double m = n;
                    coefficients[static_cast<std::size_t>(n - 1)]
                        = (std::polar(1.0, -2.0 * std::numbers::pi * m * p) - 1.0)
                        * std::exp(-2.0 * std::numbers::pi * std::numbers::pi * m * m * a * a)
                        / (4.0 * std::numbers::pi * std::numbers::pi * m * m * p * (1.0 - p));
                }
                // The soft Finger stroke (velocity 0.6, Touch 0) is released
                // through its slip as a ratio to the full-velocity slip
                // (releaseSlipRatio): each term takes the ratio's response
                // on its own bin, and the line one scale.
                expect(state.referencePole > 0.0 && state.slipPole > state.referencePole,
                       "a soft broad-contact Finger stroke was not released through its slip");
                std::vector<std::complex<double>> bins(static_cast<std::size_t>(length));
                for (int n = 1; n <= terms; ++n)
                {
                    const auto coefficient = coefficients[static_cast<std::size_t>(n - 1)];
                    bins[static_cast<std::size_t>(n % length)] += coefficient;
                    bins[static_cast<std::size_t>((length - n % length) % length)]
                        += std::conj(coefficient);
                }
                bins[0] = 0.0;
                const double scale = releaseSlipScale(state.slipPole, state.referencePole, bins);
                for (int n = 1; n <= terms; ++n)
                    coefficients[static_cast<std::size_t>(n - 1)] *= scale
                        * releaseSlipRatio(state.slipPole, state.referencePole, n, length);
                bool negativeBeforeWrap = false;
                for (int sample = 0; sample < length; ++sample)
                {
                    double expected = 0.0;
                    const double phase = static_cast<double>(sample) / length;
                    for (int n = 1; n <= terms; ++n)
                        expected += 2.0 * std::real(coefficients[static_cast<std::size_t>(n - 1)]
                            * (std::polar(1.0, 2.0 * std::numbers::pi * n * phase) - 1.0));
                    const double observed = state.history[static_cast<std::size_t>(sample)] / state.gain;
                    const double error = std::abs(observed - expected);
                    worstError = std::max(worstError, error);
                    expect(std::isfinite(observed) && error < tolerance,
                           "broad-contact periodic Fourier reconstruction differs");
                    if (uniform || sample == 0)
                        expect(observed == 0.0,
                               "uniform contact or its subtracted endpoint was nonzero");
                    if (sample > length / 2 && expected < -4.0 * tolerance)
                    {
                        negativeBeforeWrap = true;
                        expect(observed < 0.0, "contact was rectified before its periodic endpoint");
                    }
                }
                uniformCases += uniform ? 1 : 0;
                signedWrapCases += negativeBeforeWrap ? 1 : 0;
                ++cases;
            }
    expect(uniformCases >= 4 && signedWrapCases >= 4,
           "broad-contact domains did not exercise uniform and signed-wrap branches");
    std::cout << "Acustra broad-contact " << cases << " cases, aperture "
              << smallestAperture << ".." << largestAperture << ", uniform "
              << uniformCases << ", signed wrap " << signedWrapCases
              << ", maximum sample error " << worstError << '\n';
}

void testPickingStylesChangeMoreThanGainAtEveryVelocity()
{
    // Fit and remove any scalar amplitude difference between complete attacks.
    // A technique volume control, or saturated Finger/Pick identity, must fail.
    double smallestDifference = 1.0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int midi : { 43, 52, 64, 76 })
            for (const float velocity : { 32.0f / 127.0f, 0.5f, 91.0f / 127.0f, 1.0f })
            {
                acustra::EngineParameters parameters;
                std::array<Audio, 3> attacks;
                for (int technique = 0; technique < 3; ++technique)
                {
                    parameters.picking = static_cast<acustra::PickingTechnique>(technique);
                    attacks[static_cast<std::size_t>(technique)]
                        = renderAtRate(parameters, midi, velocity, 0.120, rate, 64);
                }
                // Past the 0.46 limit of the string the model plucks, a
                // hand meets the string at that limit. From 2026-09-25 a
                // Finger plucks steel 149 mm from the bridge, 0.46 of a
                // 12th-fret string, where the Thumb (1.95 times further)
                // meets it too: there the two differ by contact width and
                // pluck share alone, and are held to half the margin.
                const auto fingerPoint = acustra::AcustraEngineTestAccess::pluck(
                    acustra::fittedPhysicalCalibration, velocity, midi,
                    acustra::PickingTechnique::Finger).pluckPoint;
                const auto thumbPoint = acustra::AcustraEngineTestAccess::pluck(
                    acustra::fittedPhysicalCalibration, velocity, midi,
                    acustra::PickingTechnique::Thumb).pluckPoint;
                const bool handsMeet = std::abs(fingerPoint - thumbPoint) < 0.021;
                for (int first = 0; first < 3; ++first)
                    for (int second = first + 1; second < 3; ++second)
                    {
                        const auto& a = attacks[static_cast<std::size_t>(first)];
                        const auto& b = attacks[static_cast<std::size_t>(second)];
                        const double margin = handsMeet && first == 0 && second == 2
                            ? 0.01 : 0.02;
                        double aa = 0.0, bb = 0.0, ab = 0.0;
                        for (std::size_t sample = 0; sample < a.left.size(); ++sample)
                        {
                            const double x = 0.5 * (a.left[sample] + a.right[sample]);
                            const double y = 0.5 * (b.left[sample] + b.right[sample]);
                            aa += x * x;
                            bb += y * y;
                            ab += x * y;
                        }
                        const double difference = std::sqrt(std::max(0.0,
                            1.0 - ab * ab / std::max(aa * bb, 1.0e-40)));
                        smallestDifference = std::min(smallestDifference, difference);
                        expect(aa > 0.0 && bb > 0.0 && difference > margin,
                               "picking attacks differ only in gain at MIDI " + std::to_string(midi)
                               + " velocity " + std::to_string(velocity));
                    }
            }
    std::cout << "Acustra picking minimum attack difference after gain removal: "
              << smallestDifference << '\n';
}

void testPickingChangesTheContactWithoutRetuningOrReplucking()
{
    using acustra::PickingTechnique;
    using acustra::AcustraEngineTestAccess;
    for (const float velocity : { 0.2f, 0.5f, 0.9f, 1.0f })
    {
        const auto finger = AcustraEngineTestAccess::pluck(
            acustra::fittedPhysicalCalibration, velocity);
        const auto pick = AcustraEngineTestAccess::pluck(
            acustra::fittedPhysicalCalibration, velocity,
            52, PickingTechnique::Pick);
        const auto thumb = AcustraEngineTestAccess::pluck(
            acustra::fittedPhysicalCalibration, velocity,
            52, PickingTechnique::Thumb);
        expect(pick.peakDisplacement > thumb.peakDisplacement,
               "pick/thumb did not reach the released shape");
        // The pick's contact transient follows its own fitted law, an
        // impact growing with the tip's speed (FittedPhysicalData.h), not
        // the Finger burst the thumb shares. A zero gain is the Finger
        // burst law itself.
        const auto& calibration = acustra::fittedPhysicalCalibration;
        const auto& physical = calibration.steel;
        const double expectedPick = calibration.pickTransientGain > 0.0f
            ? calibration.pickTransientGain * 0.24 * 0.017 * physical.transientScale
                * std::pow(velocity,
                           0.5 * calibration.pickReleaseVelocityExponent)
            : finger.noiseEnvelope;
        expect(std::abs(pick.noiseEnvelope - expectedPick)
                   <= 1.0e-5 * std::max(expectedPick, 1.0e-3),
               "the pick's contact transient did not follow its fitted speed law");
        expect(pick.touch == finger.touch && finger.touch == thumb.touch
                   && thumb.noiseEnvelope == finger.noiseEnvelope,
               "picking styles changed the shared touch/noise amplitude law");
        expect(pick.pluckPoint < finger.pluckPoint
                   && finger.pluckPoint < thumb.pluckPoint,
               "panel-based picking styles did not move bridgeward/neckward");
    }
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        acustra::EngineParameters parameters;
        auto referenceOwner = std::make_unique<acustra::AcustraEngine>();
        auto& reference = *referenceOwner;
        auto changedOwner = std::make_unique<acustra::AcustraEngine>();
        auto& changed = *changedOwner;
        reference.setParameters(parameters);
        changed.setParameters(parameters);
        reference.prepare(rate, 64);
        changed.prepare(rate, 64);
        reference.noteOn(52, 0.6f);
        changed.noteOn(52, 0.6f);
        // Each Picking plays at its own output level (the construction
        // loudness table, ConstructionLoudnessData.h), which glides; the
        // string itself is untouched, so the ringing note changes by one
        // gain, the same in both channels, within the three levels.
        float lowest = 1.0f, highest = 1.0f;
        for (int picking = 0; picking < 3; ++picking)
        {
            const auto cell = [&] (int technique)
            {
                // Original model, Dreadnought, Spruce
                return static_cast<std::size_t>(((0 * 4 + 2) * 3 + 0) * 3
                                                + technique);
            };
            const float ratio = acustra::detail::constructionMicReference[cell(picking)]
                / acustra::detail::constructionMicReference[cell(0)];
            lowest = std::min(lowest, ratio);
            highest = std::max(highest, ratio);
        }
        std::array<float, 64> a {}, b {}, ar {}, br {};
        bool onlyLevel = true;
        for (int block = 0; block < 200; ++block)
        {
            // Changing the picking tool cannot alter a released string.
            parameters.picking = static_cast<PickingTechnique>(block % 3);
            changed.setParameters(parameters);
            reference.process(a.data(), ar.data(), 64);
            changed.process(b.data(), br.data(), 64);
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                // A sample near zero carries no ratio; it must stay near zero.
                if (std::abs(a[i]) < 1.0e-6f || std::abs(ar[i]) < 1.0e-6f)
                {
                    onlyLevel = onlyLevel
                        && std::abs(b[i]) <= std::max(std::abs(a[i]), 1.0e-6f) * highest * 1.01f
                        && std::abs(br[i]) <= std::max(std::abs(ar[i]), 1.0e-6f) * highest * 1.01f;
                    continue;
                }
                const double left = double(b[i]) / a[i];
                const double right = double(br[i]) / ar[i];
                onlyLevel = onlyLevel && std::abs(left - right) <= 2.0e-5 * left
                    && left >= lowest * (1.0 - 1.0e-5) && left <= highest * (1.0 + 1.0e-5);
            }
        }
        expect(onlyLevel, "picking automation changed an already ringing note");
        parameters.picking = PickingTechnique::Pick;
        const auto picked = renderAtRate(parameters, 52, 0.6f, 0.5, rate, 64);
        parameters.picking = PickingTechnique::Thumb;
        const auto thumbed = renderAtRate(parameters, 52, 0.6f, 0.5, rate, 64);
        expect(normalisedDifference(picked, thumbed) > 0.01,
               "pick/thumb excitation did not reach audible output");

    }
}

void testMaterialCalibrationChangesStringAndPluckDescriptors()
{
    auto lowStiffness = acustra::fittedPhysicalCalibration;
    lowStiffness.steel.stiffnessScale = 0.25f;
    auto highStiffness = acustra::fittedPhysicalCalibration;
    highStiffness.steel.stiffnessScale = 4.0f;
    const auto compliant = acustra::AcustraEngineTestAccess::configuredLoop(
        40, sampleRate, lowStiffness);
    const auto stiff = acustra::AcustraEngineTestAccess::configuredLoop(
        40, sampleRate, highStiffness);
    expect(stiff.inharmonicity > 15.9 * compliant.inharmonicity,
           "stiffness calibration did not scale string inharmonicity");

    auto shortT60 = acustra::fittedPhysicalCalibration;
    shortT60.steel.fundamentalT60Scale = 0.4f;
    auto longT60 = acustra::fittedPhysicalCalibration;
    longT60.steel.fundamentalT60Scale = 2.0f;
    const auto shortLoop = acustra::AcustraEngineTestAccess::configuredLoop(
        40, sampleRate, shortT60);
    const auto longLoop = acustra::AcustraEngineTestAccess::configuredLoop(
        40, sampleRate, longT60);
    expect(longLoop.loopGain > shortLoop.loopGain + 0.02,
           "fundamental T60 calibration did not change loop decay");

    auto lowLoss = acustra::fittedPhysicalCalibration;
    lowLoss.steel.frequencyLossScale = 0.35f;
    auto highLoss = acustra::fittedPhysicalCalibration;
    highLoss.steel.frequencyLossScale = 3.0f;
    const auto clear = acustra::AcustraEngineTestAccess::configuredLoop(
        40, sampleRate, lowLoss);
    const auto lossy = acustra::AcustraEngineTestAccess::configuredLoop(
        40, sampleRate, highLoss);
    expect(lossy.broadMix > 8.5 * clear.broadMix
               && lossy.highMix > 8.5 * clear.highMix,
           "frequency-loss calibration missed a loss branch");
    const auto preparedLoss
        = acustra::AcustraEngineTestAccess::changePreparedLoss(
            lowLoss, highLoss);
    const double dispersionChange
        = std::abs(preparedLoss.afterA1 - preparedLoss.beforeA1)
        + std::abs(preparedLoss.afterA2 - preparedLoss.beforeA2);
    expect(std::abs(preparedLoss.beforeScale - 0.35) < 1.0e-6
               && std::abs(preparedLoss.afterScale - 3.0) < 1.0e-6
               && dispersionChange > 1.0e-7,
           "prepared loss calibration reused a stale dispersion design: "
               + std::to_string(preparedLoss.beforeScale) + " -> "
               + std::to_string(preparedLoss.afterScale) + ", delta "
               + std::to_string(dispersionChange));

    auto nearBridge = acustra::fittedPhysicalCalibration;
    nearBridge.steel.pluckDistanceScale = 0.7f;
    auto towardNeck = acustra::fittedPhysicalCalibration;
    towardNeck.steel.pluckDistanceScale = 1.3f;
    const auto nearPluck = acustra::AcustraEngineTestAccess::pluck(
        nearBridge, 0.8f);
    const auto farPluck = acustra::AcustraEngineTestAccess::pluck(
        towardNeck, 0.8f);
    expect(farPluck.peakPosition > nearPluck.peakPosition + 0.05,
           "pluck-distance calibration did not move the initial condition");

    auto narrowAperture = acustra::fittedPhysicalCalibration;
    narrowAperture.steel.apertureScale = 0.35f;
    auto broadAperture = acustra::fittedPhysicalCalibration;
    broadAperture.steel.apertureScale = 2.5f;
    const auto narrow = acustra::AcustraEngineTestAccess::pluck(
        narrowAperture, 0.8f);
    const auto broad = acustra::AcustraEngineTestAccess::pluck(
        broadAperture, 0.8f);
    // The contact's width is a Gaussian smoothing of the written line, so it
    // is read where it acts, on the line's upper partials against its lower:
    // a Finger stroke below full velocity is also released through its slip
    // (as a ratio to the full-velocity one) and rescaled to its spread, the
    // same linear filter for both widths, which leaves their peaks within
    // 7% of each other but not the smoothing's own tilt.
    const auto tilt = [] (const std::vector<double>& line)
    {
        const auto partialDb = [&line] (int harmonic)
        {
            std::complex<double> sum {};
            for (std::size_t sample = 0; sample < line.size(); ++sample)
                sum += line[sample] * std::polar(1.0, -2.0 * std::numbers::pi
                    * harmonic * static_cast<double>(sample)
                    / static_cast<double>(line.size()));
            return 20.0 * std::log10(std::max(std::abs(sum), 1.0e-30));
        };
        double low = 0.0, high = 0.0;
        for (int harmonic = 1; harmonic <= 4; ++harmonic)
            low += partialDb(harmonic) / 4.0;
        for (int harmonic = 9; harmonic <= 16; ++harmonic)
            high += partialDb(harmonic) / 8.0;
        return high - low;
    };
    const double narrowTilt = tilt(acustra::AcustraEngineTestAccess::pluckedLine(
        narrowAperture, acustra::PickingTechnique::Finger, 52, 0.8f));
    const double broadTilt = tilt(acustra::AcustraEngineTestAccess::pluckedLine(
        broadAperture, acustra::PickingTechnique::Finger, 52, 0.8f));
    expect(broadTilt < narrowTilt - 6.0,
           "aperture calibration did not smooth the initial condition");

    auto noTransient = acustra::fittedPhysicalCalibration;
    noTransient.steel.transientScale = 0.0f;
    auto strongTransient = acustra::fittedPhysicalCalibration;
    strongTransient.steel.transientScale = 3.0f;
    const auto quietAttack = acustra::AcustraEngineTestAccess::pluck(
        noTransient, 0.8f);
    const auto strongAttack = acustra::AcustraEngineTestAccess::pluck(
        strongTransient, 0.8f);
    expect(quietAttack.noiseEnvelope == 0.0 && strongAttack.noiseEnvelope > 0.0,
           "transient calibration missed its noise branch");

    auto responsive = acustra::fittedPhysicalCalibration;
    responsive.steel.velocityBrightnessDepth = 1.2f;
    const auto responsiveLow = acustra::AcustraEngineTestAccess::pluck(
        responsive, 0.2f);
    const auto responsiveHigh = acustra::AcustraEngineTestAccess::pluck(
        responsive, 0.8f);
    auto fixedResponse = acustra::fittedPhysicalCalibration;
    fixedResponse.steel.velocityBrightnessDepth = 0.0f;
    const auto fixedLow = acustra::AcustraEngineTestAccess::pluck(
        fixedResponse, 0.2f);
    const auto fixedHigh = acustra::AcustraEngineTestAccess::pluck(
        fixedResponse, 0.8f);
    expect(responsiveHigh.touch - responsiveLow.touch > 0.70,
           "velocity brightness did not reach effective pluck touch");
    expect(responsiveLow.peakDisplacement / responsiveHigh.peakDisplacement
               > 1.8 * fixedLow.peakDisplacement
                    / fixedHigh.peakDisplacement,
           "velocity response did not change the pluck-amplitude exponent");

    acustra::EngineParameters quiet;
    quiet.outputGain = 0.10f;
    quiet.bodyAmount = 0.0f;
    auto noDirect = acustra::fittedPhysicalCalibration;
    noDirect.directGain = 0.0f;
    auto strongDirect = acustra::fittedPhysicalCalibration;
    strongDirect.directGain = 0.12f;
    const auto bodyOnly = renderCalibrated(
        quiet, noDirect, 52, 0.8f, 0.18);
    const auto withDirect = renderCalibrated(
        quiet, strongDirect, 52, 0.8f, 0.18);
    expect(normalisedDifference(bodyOnly, withDirect) > 1.0e-4,
           "direct-gain calibration did not change bridge-local output");
}

void testHighLossCutoffScaleChangesOnlyUpperLoss()
{
    auto lowCutoff = acustra::fittedPhysicalCalibration;
    lowCutoff.highLossCutoffScale = 0.5f;
    auto highCutoff = acustra::fittedPhysicalCalibration;
    highCutoff.highLossCutoffScale = 4.0f;

    const auto roundTripMagnitude = [] (
        const acustra::AcustraEngineTestAccess::StringLoopSnapshot& loop,
        double omega)
    {
        const auto mixedPoleMagnitude = [omega] (double coefficient, double mix)
        {
            const double denominatorReal
                = 1.0 - coefficient * std::cos(omega);
            const double denominatorImaginary
                = coefficient * std::sin(omega);
            const double norm = denominatorReal * denominatorReal
                              + denominatorImaginary * denominatorImaginary;
            const double lowReal
                = (1.0 - coefficient) * denominatorReal / norm;
            const double lowImaginary
                = -(1.0 - coefficient) * denominatorImaginary / norm;
            return std::hypot((1.0 - mix) + mix * lowReal,
                              mix * lowImaginary);
        };
        const double bending = loop.bendingGain / std::hypot(
            1.0 + loop.bendingA1 * std::cos(omega)
                + loop.bendingA2 * std::cos(2.0 * omega),
            loop.bendingA1 * std::sin(omega)
                + loop.bendingA2 * std::sin(2.0 * omega));
        return loop.loopGain
            * mixedPoleMagnitude(loop.broadCoefficient, loop.broadMix)
            * mixedPoleMagnitude(loop.highCoefficient, loop.highMix)
            * bending;
    };

    constexpr int midiNote = 40;
    const double fundamental = 440.0
        * std::exp2((static_cast<double>(midiNote) - 69.0) / 12.0);
    const double fundamentalOmega
        = 2.0 * std::numbers::pi * fundamental / sampleRate;
    const double upperOmega = 2.0 * std::numbers::pi * 8000.0 / sampleRate;
    const auto low = acustra::AcustraEngineTestAccess::configuredLoop(
        midiNote, sampleRate, lowCutoff);
    const auto high = acustra::AcustraEngineTestAccess::configuredLoop(
        midiNote, sampleRate, highCutoff);

    // Relative, since the strings' bending loss (2026-09-28) multiplies
    // both round trips by the same factor, which the cutoff does not
    // touch: at 8 kHz on the E2 0.162, so the round trip's
    // 0.934 -> 0.979 became 0.152 -> 0.159, the same 4.8%.
    expect(roundTripMagnitude(high, upperOmega)
               > 1.01 * roundTripMagnitude(low, upperOmega),
           "high-loss cutoff did not reduce upper-string loss");
    const double fundamentalChangeDb = 20.0 * std::log10(
        roundTripMagnitude(high, fundamentalOmega)
        / roundTripMagnitude(low, fundamentalOmega));
    expect(std::abs(fundamentalChangeDb) < 1.0e-3,
           "high-loss cutoff moved requested fundamental decay");
    const double pitchChangeCents = 1200.0 * std::log2(
        loopResonance(high, 1, fundamentalOmega)
        / loopResonance(low, 1, fundamentalOmega));
    expect(std::abs(pitchChangeCents) < 0.01,
           "high-loss cutoff moved requested fundamental pitch");
}

// The string's own bending loss (bendingLossSection in AcustraEngine.cpp).
// With every factor at zero no section enters the loop. With them on, the
// section each loop carries adds, per round trip of the fundamental period,
// Valette's and Woodhouse's loss pi (f_n / f0) eta B n^2 / (1 + B n^2) to
// within 12% wherever that loss adds 20 to 160 dB/s below 0.3 of the host
// rate - the band the section is designed to follow - at 44.1, 48 and 96 kHz
// alike; both polarisations
// carry the same section, since the loss is the string's; and the
// fundamental keeps the decay the loop gain asks for and the pitch the
// tuning asks for.
void testBendingLossFollowsItsLaw()
{
    using Access = acustra::AcustraEngineTestAccess;
    auto off = acustra::fittedPhysicalCalibration;
    off.steelWoundBendingLoss = off.steelPlainBendingLoss = 0.0f;
    auto on = acustra::fittedPhysicalCalibration;
    on.steelWoundBendingLoss = 0.1f;
    on.steelPlainBendingLoss = 0.006f;

    struct Case
    {
        int midiNote;
        double factor;
        const char* name;
    };
    const Case cases[] {
        { 40, 0.1, "steel E2" },
        { 51, 0.1, "steel D#3" },
        { 64, 0.006, "steel E4" },
        { 72, 0.006, "steel C5" },
    };
    const auto sectionLoss = [] (double gain, double a1, double a2, double omega)
    {
        return -std::log(gain / std::hypot(
            1.0 + a1 * std::cos(omega) + a2 * std::cos(2.0 * omega),
            a1 * std::sin(omega) + a2 * std::sin(2.0 * omega)));
    };
    const auto magnitude = [&] (
        const acustra::AcustraEngineTestAccess::StringLoopSnapshot& loop,
        double omega)
    {
        const double lossOmega = loop.sampleRate == 48000.0 ? omega
            : 2.0 * std::atan((loop.sampleRate / 48000.0) * std::tan(0.5 * omega));
        const auto mixedPoleMagnitude = [lossOmega] (double coefficient, double mix)
        {
            const std::complex<double> low = (1.0 - coefficient)
                / (1.0 - coefficient * std::polar(1.0, -lossOmega));
            return std::abs((1.0 - mix) + mix * low);
        };
        return loop.loopGain
            * mixedPoleMagnitude(loop.broadCoefficient, loop.broadMix)
            * mixedPoleMagnitude(loop.highCoefficient, loop.highMix)
            * std::exp(-sectionLoss(loop.bendingGain, loop.bendingA1,
                                    loop.bendingA2, omega));
    };
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const auto& item : cases)
        {
            const std::string name = std::string(item.name) + " at "
                + std::to_string(static_cast<int>(rate)) + " Hz";
            const auto clear = Access::configuredLoop(
                item.midiNote, rate, off);
            const auto lossy = Access::configuredLoop(
                item.midiNote, rate, on);
            expect(clear.bendingA1 == 0.0 && clear.bendingA2 == 0.0
                       && clear.bendingGain == 1.0,
                   name + ": a zero bending-loss factor left a section");
            const auto planes = Access::bendingSections(
                item.midiNote, rate, on);
            expect(planes.normalActive && planes.parallelActive
                       && planes.normal == planes.parallel,
                   name + ": the polarisations lose the string's bending "
                          "differently");

            const double fundamental = 440.0 * std::exp2(
                (static_cast<double>(item.midiNote) - 69.0) / 12.0);
            const double inharmonicity = lossy.inharmonicity;
            int checked = 0;
            double worst = 0.0;
            for (int partial = 1;; ++partial)
            {
                const double n = static_cast<double>(partial);
                const double stretched = n * std::sqrt(
                    (1.0 + inharmonicity * n * n) / (1.0 + inharmonicity));
                const double frequency = stretched * fundamental;
                if (frequency > 0.3 * rate)
                    break;
                const double bending = inharmonicity * n * n;
                const double law = std::numbers::pi * stretched * item.factor
                    * bending / (1.0 + bending);
                const double lawRate = 20.0 * std::log10(std::exp(1.0))
                    * law * fundamental;
                if (lawRate < 20.0 || lawRate > 160.0)
                    continue;
                const double omega = 2.0 * std::numbers::pi * frequency / rate;
                const double actual = sectionLoss(lossy.bendingGain,
                    lossy.bendingA1, lossy.bendingA2, omega);
                worst = std::max(worst, std::abs(actual / law - 1.0));
                ++checked;
            }
            expect(checked >= 2,
                   name + ": no partial fell in the section's design band");
            expect(worst < 0.12,
                   name + ": the section missed the bending-loss law by "
                       + std::to_string(100.0 * worst) + "%");

            const double fundamentalOmega
                = 2.0 * std::numbers::pi * fundamental / rate;
            const double decayChangeDb = 20.0 * std::log10(
                magnitude(lossy, fundamentalOmega)
                / magnitude(clear, fundamentalOmega));
            expect(std::abs(decayChangeDb) < 1.0e-3,
                   name + ": the bending loss moved the fundamental's decay by "
                       + std::to_string(decayChangeDb) + " dB per pass");
            const double pitchChangeCents = 1200.0 * std::log2(
                loopResonance(lossy, 1, fundamentalOmega)
                / loopResonance(clear, 1, fundamentalOmega));
            expect(std::abs(pitchChangeCents) < 0.01,
                   name + ": the bending loss retuned the fundamental by "
                       + std::to_string(pitchChangeCents) + " cents");
        }
}

// The contact's noise (AcustraEngine::renderContactNoise). With its string
// and click levels at zero nothing else it reads is heard. The click reaches
// the microphones without touching the string or the body, so the difference
// it makes is the click alone: it is linear in its level, it ends when its
// twelve time constants and the filter's ring-down have run, its level at
// 44.1 and 96 kHz is the 48 kHz one, it grows with the stroke's velocity, its
// spectrum moves up with it, and a thumb's sits under a finger's. The
// string-borne force is heard through the string and stays finite.
void testContactNoiseFollowsItsLaw()
{
    auto off = acustra::fittedPhysicalCalibration;
    off.contactNoiseFinger = off.contactNoisePick = 0.0f;
    off.contactClickFinger = off.contactClickPick = 0.0f;
    auto offOther = off;
    offOther.contactNoiseVelocityExponent = 2.5f;
    offOther.contactNoiseCornerHz = 900.0f;
    offOther.pickContactNoiseCornerHz = 900.0f;
    offOther.contactNoiseDecaySeconds = 0.002f;
    const acustra::EngineParameters steel;
    const auto quiet = renderCalibrated(steel, off, 57, 0.85f, 1.0);
    const auto other = renderCalibrated(steel, offOther, 57, 0.85f, 1.0);
    expect(quiet.left == other.left && quiet.right == other.right,
           "the contact noise's shape was heard at zero levels");

    const auto difference = [] (const Audio& a, const Audio& b)
    {
        Audio result { std::vector<float>(a.left.size()),
                       std::vector<float>(a.right.size()) };
        for (std::size_t i = 0; i < a.left.size(); ++i)
        {
            result.left[i] = a.left[i] - b.left[i];
            result.right[i] = a.right[i] - b.right[i];
        }
        return result;
    };
    const auto rms = [] (const Audio& audio, double rate, double begin, double end)
    {
        const auto first = static_cast<std::size_t>(begin * rate);
        const auto last = std::min(audio.left.size(),
                                   static_cast<std::size_t>(end * rate));
        double sum = 0.0;
        for (std::size_t i = first; i < last; ++i)
            sum += 0.5 * (double(audio.left[i]) * audio.left[i]
                          + double(audio.right[i]) * audio.right[i]);
        return last > first ? std::sqrt(sum / double(last - first)) : 0.0;
    };
    const auto centroid = [] (const Audio& audio, double rate)
    {
        // Power-weighted mean frequency of the first 60 ms, by DFT bins.
        const int count = static_cast<int>(0.060 * rate);
        double weighted = 0.0;
        double total = 0.0;
        for (int bin = 1; bin < count / 2; bin += 2)
        {
            const double angle = -2.0 * std::numbers::pi * bin / count;
            double real = 0.0;
            double imaginary = 0.0;
            for (int n = 0; n < count; ++n)
            {
                const double value = 0.5 * (audio.left[static_cast<std::size_t>(n)]
                    + audio.right[static_cast<std::size_t>(n)]);
                real += value * std::cos(angle * n);
                imaginary += value * std::sin(angle * n);
            }
            const double power = real * real + imaginary * imaginary;
            weighted += power * bin * rate / count;
            total += power;
        }
        return total > 0.0 ? weighted / total : 0.0;
    };

    auto click = off;
    click.contactClickFinger = 4.0f;
    auto clickTwice = off;
    clickTwice.contactClickFinger = 8.0f;
    const auto withClick = renderCalibrated(steel, click, 57, 0.85f, 1.0);
    const auto withTwice = renderCalibrated(steel, clickTwice, 57, 0.85f, 1.0);
    const auto once = difference(withClick, quiet);
    const auto twice = difference(withTwice, quiet);
    const double onceRms = rms(once, sampleRate, 0.0, 0.1);
    const double twiceRms = rms(twice, sampleRate, 0.0, 0.1);
    expect(onceRms > 0.0, "a finger's click was not heard");
    expect(std::abs(twiceRms / std::max(onceRms, 1.0e-30) - 2.0) < 1.0e-3,
           "the click was not linear in its level");
    // 12 x 20.7 ms and the filters' ring-down: silent by 0.5 s, exactly.
    bool silent = true;
    for (std::size_t i = static_cast<std::size_t>(0.5 * sampleRate);
         i < once.left.size(); ++i)
        silent = silent && once.left[i] == 0.0f && once.right[i] == 0.0f;
    expect(silent, "the click went on after its noise had ended");

    for (const double rate : { 44100.0, 96000.0 })
    {
        const auto base = renderAtRate(steel, 57, 0.85f, 0.2, rate, blockSize,
                                       true, off);
        const auto clicked = renderAtRate(steel, 57, 0.85f, 0.2, rate, blockSize,
                                          true, click);
        const auto clickAtRate = difference(clicked, base);
        const auto base48 = renderAtRate(steel, 57, 0.85f, 0.2, sampleRate,
                                         blockSize, true, off);
        const auto clicked48 = renderAtRate(steel, 57, 0.85f, 0.2, sampleRate,
                                            blockSize, true, click);
        const double change = 20.0 * std::log10(
            rms(clickAtRate, rate, 0.0, 0.1)
            / rms(difference(clicked48, base48), sampleRate, 0.0, 0.1));
        expect(std::abs(change) < 1.5,
               "the click's level moved " + std::to_string(change)
                   + " dB at " + std::to_string(static_cast<int>(rate)) + " Hz");
    }

    const auto clickAt = [&] (float velocity, acustra::PickingTechnique tool)
    {
        auto parameters = steel;
        parameters.picking = tool;
        auto calibration = click;
        calibration.contactClickPick = 4.0f;
        return difference(renderCalibrated(parameters, calibration, 57,
                                           velocity, 0.2),
                          renderCalibrated(parameters, off, 57, velocity, 0.2));
    };
    const auto soft = clickAt(0.25f, acustra::PickingTechnique::Finger);
    const auto loud = clickAt(0.9f, acustra::PickingTechnique::Finger);
    const auto thumb = clickAt(0.9f, acustra::PickingTechnique::Thumb);
    expect(rms(loud, sampleRate, 0.0, 0.1) > 2.0 * rms(soft, sampleRate, 0.0, 0.1),
           "a harder stroke did not click louder");
    expect(centroid(loud, sampleRate) > 1.5 * centroid(soft, sampleRate),
           "a harder stroke's click did not move up in frequency");
    expect(centroid(thumb, sampleRate) < 0.8 * centroid(loud, sampleRate),
           "a thumb's click was not darker than a finger's");

    auto string = off;
    string.contactNoiseFinger = 0.05f;
    const auto withNoise = renderCalibrated(steel, string, 57, 0.85f, 1.0);
    expect(withNoise.left != quiet.left, "the string-borne noise was not heard");
    for (std::size_t i = 0; i < withNoise.left.size(); ++i)
        expect(std::isfinite(withNoise.left[i]) && std::isfinite(withNoise.right[i]),
               "a string-borne contact noise render was not finite");
    const double early = tailBandRms(withNoise, sampleRate, 0.0, 0.04, 2000.0, 12000.0);
    const double earlyOff = tailBandRms(quiet, sampleRate, 0.0, 0.04, 2000.0, 12000.0);
    expect(early > earlyOff, "the string-borne noise added no early upper band");

    // A repluck while the noise is still in flight hands it to the retained
    // tail, as the contact transport's waves are, and nothing blows up.
    {
        auto both = string;
        both.contactClickPick = 8.0f;
        both.contactNoisePick = 0.2f;
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        auto parameters = steel;
        parameters.picking = acustra::PickingTechnique::Pick;
        engine.setParameters(parameters);
        engine.setPhysicalCalibration(both);
        engine.prepare(sampleRate, blockSize);
        std::vector<float> left(static_cast<std::size_t>(blockSize));
        std::vector<float> right(static_cast<std::size_t>(blockSize));
        bool finite = true;
        double peak = 0.0;
        for (int block = 0; block < 400; ++block)
        {
            if (block % 7 == 0)
                engine.noteOn(52, 0.3f + 0.1f * static_cast<float>(block % 5));
            engine.process(left.data(), right.data(), blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                finite = finite && std::isfinite(left[static_cast<std::size_t>(i)])
                    && std::isfinite(right[static_cast<std::size_t>(i)]);
                peak = std::max(peak, static_cast<double>(std::abs(left[static_cast<std::size_t>(i)])));
            }
        }
        expect(finite && peak < 1.0,
               "replucking through a contact noise in flight was not finite and bounded");
    }
}

void testPlateConductanceFloorDampsOnlyTheUpperBand()
{
    // The plate conductance floor restores the flat conductance a real
    // soundboard keeps above its modal-overlap frequency, which the finite
    // measured modal fit loses. It must shorten the upper-band tail without
    // touching the low partials the measured modes already dominate, and it
    // must be exactly inert when its plateau is zero.
    auto off = acustra::fittedPhysicalCalibration;
    off.bridgeConductanceFloor = 0.0f;
    off.bridgeConductanceCornerHz = 2400.0f;
    auto offOtherCorner = off;
    offOtherCorner.bridgeConductanceCornerHz = 200.0f;
    auto on = off;
    on.bridgeConductanceFloor = 0.004f;

    acustra::EngineParameters parameters;
    const auto quiet = renderCalibrated(parameters, off, 52, 0.85f, 3.0);
    const auto other = renderCalibrated(parameters, offOtherCorner, 52, 0.85f, 3.0);
    const auto damped = renderCalibrated(parameters, on, 52, 0.85f, 3.0);

    expect(quiet.left == other.left && quiet.right == other.right,
           "the conductance corner was not inert at a zero plateau");

    const double upperOff = tailBandRms(quiet, sampleRate, 1.2, 2.6, 2000.0, 4000.0);
    const double upperOn = tailBandRms(damped, sampleRate, 1.2, 2.6, 2000.0, 4000.0);
    const double lowerOff = tailBandRms(quiet, sampleRate, 1.2, 2.6, 80.0, 200.0);
    const double lowerOn = tailBandRms(damped, sampleRate, 1.2, 2.6, 80.0, 200.0);
    expect(upperOff > 0.0 && upperOn > 0.0 && lowerOff > 0.0 && lowerOn > 0.0,
           "a plate-conductance render produced no measurable band energy");

    const double upperChangeDb = 20.0 * std::log10(upperOn / upperOff);
    const double lowerChangeDb = 20.0 * std::log10(lowerOn / lowerOff);
    expect(upperChangeDb < -2.0,
           "the plate conductance floor did not shorten the upper-band tail");
    expect(std::abs(lowerChangeDb) < 1.5,
           "the plate conductance floor changed the low-partial tail");
    for (const auto& audio : { quiet, damped })
        for (std::size_t index = 0; index < audio.left.size(); ++index)
            expect(std::isfinite(audio.left[index])
                       && std::isfinite(audio.right[index]),
                   "a plate-conductance render was not finite");
}

void testStolenStringKeepsRingingUnderHandDamping()
{
    // A chord change takes every string while it is still vibrating. The old
    // vibration must be damped by the hand, not deleted.
    const auto snapshot
        = acustra::AcustraEngineTestAccess::stealStringTail(sampleRate);
    expect(snapshot.heldBeforeSteal > 1.0e-4,
           "the first chord had not stored measurable string energy");
    expect(snapshot.tailActive,
           "taking a sounding string for another note started no tail");
    expect(snapshot.keptInTail > 0.5 * snapshot.heldBeforeSteal,
           "the tail kept less than half of the string's stored energy");
    expect(snapshot.keptInTail <= snapshot.heldBeforeSteal * 1.000001,
           "the tail held more energy than the string it came from");
    // The picking hand's 10 ms contact drives the loop down fast (about
    // -10 dB at 10 ms, -21 dB at 20 ms, flooring near -33 dB) but not
    // instantaneously, so half a second later the tail must be far down.
    expect(snapshot.tailEnergyAfterDecay < 0.01 * snapshot.keptInTail,
           "the stolen tail did not decay under the hand damping");
    // The plane parallel to the top holds most of a pluck, and the hand
    // lands on it too: it is carried and damped exactly as the normal one.
    expect(snapshot.heldParallelBeforeSteal > snapshot.heldBeforeSteal,
           "the first chord did not store most of its energy parallel to the top");
    expect(snapshot.keptInParallelTail > 0.5 * snapshot.heldParallelBeforeSteal
               && snapshot.keptInParallelTail
                   <= snapshot.heldParallelBeforeSteal * 1.000001,
           "the tail did not carry the parallel plane's stored energy");
    expect(snapshot.parallelTailEnergyAfterDecay
               < 0.01 * snapshot.keptInParallelTail,
           "the stolen parallel tail did not decay under the hand damping");
    // Replucking the same note is the hand landing on the string too: what
    // it held goes on in the tail under the hand while the pluck is released
    // from rest.
    expect(snapshot.tailActiveAfterRepluck,
           "replucking a sounding note did not carry it into the tail");

    // Repeated chord changes restart a tail on a string whose previous tail is
    // still sounding, which discards the older one. Run that hard: forty
    // changes, some inside one tail's 10 ms, at the sample-rate extremes.
    for (const double rate : { 44100.0, 384000.0 })
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        acustra::EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(rate, blockSize);
        std::vector<float> left(static_cast<std::size_t>(blockSize));
        std::vector<float> right(static_cast<std::size_t>(blockSize));
        const std::array<std::array<int, 6>, 3> chords {{
            {{ 40, 47, 52, 56, 59, 64 }},
            {{ 43, 50, 55, 58, 62, 67 }},
            {{ 45, 52, 57, 60, 64, 69 }} }};
        double maximum = 0.0;
        int change = 0;
        const int step = static_cast<int>(0.10 * rate);
        for (int i = 0; i < static_cast<int>(4.0 * rate); i += blockSize)
        {
            if (i / step > change - 1 && change < 40)
            {
                for (const int note : chords[static_cast<std::size_t>(
                         change % chords.size())])
                    engine.noteOn(note, 0.95f);
                ++change;
            }
            engine.process(left.data(), right.data(), blockSize);
            for (int k = 0; k < blockSize; ++k)
            {
                const auto index = static_cast<std::size_t>(k);
                expect(std::isfinite(left[index]) && std::isfinite(right[index]),
                       "repeated chord changes produced non-finite audio");
                maximum = std::max(maximum, static_cast<double>(std::max(
                    std::abs(left[index]), std::abs(right[index]))));
            }
        }
        expect(maximum > 1.0e-6 && maximum <= 1.0,
               "repeated chord changes were silent or escaped headroom");
        engine.allSoundOff();
        for (int i = 0; i < static_cast<int>(0.5 * rate); i += blockSize)
        {
            engine.process(left.data(), right.data(), blockSize);
            for (int k = 0; k < blockSize; ++k)
                expect(std::abs(left[static_cast<std::size_t>(k)]) < 1.0e-4f
                       && std::abs(right[static_cast<std::size_t>(k)]) < 1.0e-4f,
                       "a tail survived All Sound Off");
        }
    }

    // Two chords in sequence must stay finite and bounded at every rate.
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        acustra::EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(rate, blockSize);
        std::vector<float> left(static_cast<std::size_t>(blockSize));
        std::vector<float> right(static_cast<std::size_t>(blockSize));
        const std::array<int, 6> first { 40, 47, 52, 56, 59, 64 };
        const std::array<int, 6> second { 43, 50, 55, 58, 62, 67 };
        for (const int note : first) engine.noteOn(note, 0.9f);
        double maximum = 0.0;
        for (int i = 0; i < static_cast<int>(2.0 * rate); i += blockSize)
        {
            if (i >= static_cast<int>(0.4 * rate)
                && i < static_cast<int>(0.4 * rate) + blockSize)
                for (const int note : second) engine.noteOn(note, 0.9f);
            engine.process(left.data(), right.data(), blockSize);
            for (int k = 0; k < blockSize; ++k)
            {
                const auto index = static_cast<std::size_t>(k);
                expect(std::isfinite(left[index]) && std::isfinite(right[index]),
                       "a chord change produced non-finite audio");
                maximum = std::max(maximum, static_cast<double>(std::max(
                    std::abs(left[index]), std::abs(right[index]))));
            }
        }
        expect(maximum > 1.0e-6 && maximum <= 1.0,
               "a chord change was silent or escaped headroom");
    }
}

void testBridgeHandPressureShortensAndDarkens()
{
    // Zero pressure must be an exact no-op, and rising pressure must shorten
    // the note and darken it, monotonically, without escaping headroom.
    const auto render = [] (float pressure, int midiNote, bool applyPressure = true)
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        acustra::EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(sampleRate, blockSize);
        if (applyPressure)
            engine.setPalmMutePressure(pressure);
        const int settle = static_cast<int>(0.10 * sampleRate);
        Audio scratch { std::vector<float>(static_cast<std::size_t>(blockSize)),
                        std::vector<float>(static_cast<std::size_t>(blockSize)) };
        for (int i = 0; i < settle; i += blockSize)
            engine.process(scratch.left.data(), scratch.right.data(), blockSize);
        engine.noteOn(midiNote, 0.85f);
        const int samples = static_cast<int>(2.0 * sampleRate);
        Audio out { std::vector<float>(static_cast<std::size_t>(samples)),
                    std::vector<float>(static_cast<std::size_t>(samples)) };
        for (int offset = 0; offset < samples; offset += blockSize)
        {
            const int count = std::min(blockSize, samples - offset);
            engine.process(out.left.data() + offset, out.right.data() + offset,
                           count);
        }
        return out;
    };
    const auto windowRms = [] (const Audio& a, double from, double to)
    {
        const auto i0 = static_cast<std::size_t>(from * sampleRate);
        const auto i1 = std::min(static_cast<std::size_t>(to * sampleRate),
                                 a.left.size());
        double energy = 0.0;
        for (std::size_t i = i0; i < i1; ++i)
        {
            const double mono = 0.5 * (a.left[i] + a.right[i]);
            energy += mono * mono;
        }
        return std::sqrt(energy / std::max<std::size_t>(1, i1 - i0));
    };

    const auto open = render(0.0f, 52);
    const auto untouched = render(0.0f, 52, false);
    expect(open.left == untouched.left && open.right == untouched.right,
           "zero bridge-hand pressure was not an exact no-op");

    double previousTail = windowRms(open, 1.0, 2.0);
    double previousBright = tailBandRms(open, sampleRate, 0.0, 0.2,
                                        2000.0, 6000.0);
    const double openAttack = windowRms(open, 0.0, 0.05);
    for (const float pressure : { 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const auto muted = render(pressure, 52);
        for (std::size_t i = 0; i < muted.left.size(); ++i)
            expect(std::isfinite(muted.left[i]) && std::isfinite(muted.right[i])
                       && std::abs(muted.left[i]) <= 1.0f
                       && std::abs(muted.right[i]) <= 1.0f,
                   "a muted render left headroom or went non-finite");
        const double tail = windowRms(muted, 1.0, 2.0);
        // Once the tail is at the arithmetic floor (5e-11 at 0.75), more
        // pressure has nothing left to shorten.
        expect(tail < previousTail || tail < 1.0e-9,
               "more bridge-hand pressure did not shorten the note further");
        const double bright = tailBandRms(muted, sampleRate, 0.0, 0.2,
                                          2000.0, 6000.0);
        expect(bright < previousBright,
               "more bridge-hand pressure did not darken the note further");
        expect(windowRms(muted, 0.0, 0.05) < openAttack * 1.001,
               "bridge-hand pressure made the attack louder than open");
        previousTail = tail;
        previousBright = bright;
    }
}

void testNaturalHarmonicsReachAboveTheFretboard()
{
    // Above the twentieth fret the guitar still reaches, through the natural
    // harmonics of its open strings, and the requested pitch alone decides
    // whether one exists. Below the lowest open string it does not reach.
    const auto play = [] (int midiNote)
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        acustra::EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(sampleRate, blockSize);
        engine.noteOn(midiNote, 0.9f);
        const int samples = static_cast<int>(2.0 * sampleRate);
        Audio out { std::vector<float>(static_cast<std::size_t>(samples)),
                    std::vector<float>(static_cast<std::size_t>(samples)) };
        for (int offset = 0; offset < samples; offset += blockSize)
            engine.process(out.left.data() + offset, out.right.data() + offset,
                           std::min(blockSize, samples - offset));
        return out;
    };
    const auto rms = [] (const Audio& a)
    {
        double energy = 0.0;
        for (std::size_t i = 0; i < a.left.size(); ++i)
        {
            const double mono = 0.5 * (a.left[i] + a.right[i]);
            energy += mono * mono;
        }
        return std::sqrt(energy / std::max<std::size_t>(1, a.left.size()));
    };

    const double stopped = rms(play(84));
    expect(stopped > 1.0e-4, "the highest fretted note was silent");
    // E6 is the fourth harmonic of the open high E, and B6 the sixth of the
    // open B. Both are exact multiples, so both must sound.
    for (const int midiNote : { 88, 90 })
    {
        const auto harmonic = play(midiNote);
        const double level = rms(harmonic);
        expect(level > 1.0e-5, "a natural harmonic above the fretboard was silent");
        // A touched node keeps only the modes the pluck already put there, so
        // a harmonic is quieter than a stopped note rather than louder.
        expect(level < stopped, "a natural harmonic was louder than a stopped note");
        for (std::size_t i = 0; i < harmonic.left.size(); ++i)
            expect(std::isfinite(harmonic.left[i])
                       && std::isfinite(harmonic.right[i])
                       && std::abs(harmonic.left[i]) <= 1.0f,
                   "a natural harmonic left headroom or went non-finite");
    }
    // No open string has a harmonic within 25 cents of these, and nothing on a
    // guitar lies below the lowest open string, so all stay silent.
    for (const int midiNote : { 85, 89, 93, 39, 20 })
        expect(rms(play(midiNote)) == 0.0,
               "a pitch the guitar cannot produce was sounded");

    // The lowest usable node wins, and it lands on the pitch it should. The
    // sounding partial is a few cents sharp because it is a real partial of a
    // stiff string, which is what a guitar does too.
    const auto e6 = play(88);
    double best = 0.0;
    double bestFrequency = 0.0;
    for (double frequency = 1250.0; frequency < 1390.0; frequency += 0.25)
    {
        double real = 0.0;
        double imaginary = 0.0;
        const int count = static_cast<int>(1.0 * sampleRate);
        for (int i = 0; i < count; ++i)
        {
            const double t = i / sampleRate;
            const double mono = 0.5 * (e6.left[static_cast<std::size_t>(i)]
                                     + e6.right[static_cast<std::size_t>(i)]);
            real += mono * std::cos(2.0 * std::numbers::pi * frequency * t);
            imaginary += mono * std::sin(2.0 * std::numbers::pi * frequency * t);
        }
        const double magnitude = std::hypot(real, imaginary);
        if (magnitude > best) { best = magnitude; bestFrequency = frequency; }
    }
    const double wanted = 440.0 * std::exp2((88.0 - 69.0) / 12.0);
    const double cents = 1200.0 * std::log2(bestFrequency / wanted);
    expect(std::abs(cents) < 20.0,
           "the fourth harmonic of the open high E was not near E6");
}

// A natural harmonic is its open string touched at a node: the finger damps
// every mode the node does not share, the pluck's own release noise with them,
// so the harmonic - or its octave, which shares the node - is what sounds. The
// release burst used to reach the bridge unfiltered, and on D#6, E6 and E7 an
// unrelated partial of the open string was the loudest in the output.
// It is read at the saddle (Capture Piezo), where the string's own force
// arrives flat across this band: through the microphones the body's
// radiation weights the partials, and the Dreadnought's 8.3 kHz radiation
// peak, measured +12 dB over the piezo's balance and 1.2 dB short of the
// octave before the radiation was continued above its fitted band, lifts the
// open string's 25th partial there (14 dB under the harmonic at the saddle)
// 0.2 dB over E6's octave (Docs/decisions.md, 2026-09-30). That is the
// body's colour, not a mode the finger failed to damp.
void testANaturalHarmonicSoundsItsOwnPitch()
{
    const auto spectrumPeak = [] (const Audio& audio, double begin, double end)
    {
        const auto first = static_cast<std::size_t>(begin * sampleRate);
        const auto count = static_cast<std::size_t>((end - begin) * sampleRate);
        std::size_t size = 1;
        while (size < count)
            size <<= 1;
        std::vector<std::complex<double>> bins(size);
        for (std::size_t i = 0; i < count; ++i)
        {
            const double window = 0.5 - 0.5 * std::cos(
                2.0 * std::numbers::pi * static_cast<double>(i)
                / static_cast<double>(count - 1));
            bins[i] = window * 0.5 * (audio.left[first + i] + audio.right[first + i]);
        }
        // Iterative radix-2 FFT.
        for (std::size_t i = 1, j = 0; i < size; ++i)
        {
            std::size_t bit = size >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap(bins[i], bins[j]);
        }
        for (std::size_t length = 2; length <= size; length <<= 1)
        {
            const double angle = -2.0 * std::numbers::pi / static_cast<double>(length);
            const std::complex<double> step(std::cos(angle), std::sin(angle));
            for (std::size_t start = 0; start < size; start += length)
            {
                std::complex<double> twiddle(1.0, 0.0);
                for (std::size_t k = 0; k < length / 2; ++k)
                {
                    const auto even = bins[start + k];
                    const auto odd = bins[start + k + length / 2] * twiddle;
                    bins[start + k] = even + odd;
                    bins[start + k + length / 2] = even - odd;
                    twiddle *= step;
                }
            }
        }
        const double binHz = sampleRate / static_cast<double>(size);
        std::size_t best = 0;
        for (auto bin = static_cast<std::size_t>(80.0 / binHz);
             bin < static_cast<std::size_t>(16000.0 / binHz); ++bin)
            if (std::abs(bins[bin]) > std::abs(bins[best]))
                best = bin;
        return static_cast<double>(best) * binHz;
    };
    for (const int midiNote : { 87, 88, 91, 95, 100 })
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        acustra::EngineParameters parameters;
        parameters.capture = acustra::CaptureType::Piezo;
        engine.setParameters(parameters);
        engine.prepare(sampleRate, blockSize);
        engine.noteOn(midiNote, 0.7f);
        const int samples = static_cast<int>(0.7 * sampleRate);
        Audio out { std::vector<float>(static_cast<std::size_t>(samples)),
                    std::vector<float>(static_cast<std::size_t>(samples)) };
        for (int offset = 0; offset < samples; offset += blockSize)
            engine.process(out.left.data() + offset, out.right.data() + offset,
                           std::min(blockSize, samples - offset));
        const double peak = spectrumPeak(out, 0.05, 0.65);
        const double wanted = 440.0 * std::exp2((midiNote - 69.0) / 12.0);
        const double cents = 1200.0 * std::log2(peak / wanted);
        const double octaveCents = cents - 1200.0;
        expect(std::abs(cents) < 40.0 || std::abs(octaveCents) < 40.0,
               "natural harmonic MIDI " + std::to_string(midiNote)
                   + "'s loudest partial was at " + std::to_string(peak)
                   + " Hz, not its pitch or octave");
    }
}

void testHeldStringsDoNotLengthenANoteDecay()
{
    // Every string is anchored behind the saddle at all times, so the spring
    // the junction sees is a constant of the instrument. Summing it over the
    // played strings alone made the port stiffen with each voice held, and a
    // note inside a chord then rang 2.15 times longer than the same note
    // alone - including beside a note too quiet to hear, which is the proof
    // that it was the aggregation and not energy arriving from the neighbour.
    constexpr int subject = 43;
    const auto decayRate = [] (const Audio& audio)
    {
        const double early = tailBandRms(audio, sampleRate, 1.0, 2.0, 88.0, 108.0);
        const double late = tailBandRms(audio, sampleRate, 2.5, 3.5, 88.0, 108.0);
        expect(early > 1.0e-7 && late > 1.0e-9,
               "a held-string decay render had no measurable fundamental");
        return 20.0 * std::log10(early / late) / 1.5;
    };
    const auto renderWith = [] (const std::vector<int>& companions,
                                float companionVelocity)
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        acustra::EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(sampleRate, blockSize);
        // The idle-string path is one-way radiation, not the junction; mute
        // it so this measures the bridge port and nothing else.
        engine.setSympatheticStringsEnabled(false);
        for (const int note : companions)
            engine.noteOn(note, companionVelocity);
        engine.noteOn(subject, 0.85f);
        const int samples = static_cast<int>(4.0 * sampleRate);
        Audio result { std::vector<float>(static_cast<std::size_t>(samples)),
                       std::vector<float>(static_cast<std::size_t>(samples)) };
        for (int offset = 0; offset < samples; offset += blockSize)
            engine.process(result.left.data() + offset,
                           result.right.data() + offset,
                           std::min(blockSize, samples - offset));
        return result;
    };

    const double alone = decayRate(renderWith({}, 0.0f));
    expect(alone > 1.0, "the subject note did not decay on its own");
    struct Case { const char* name; std::vector<int> companions; float velocity; };
    // The companions all sound above the 88-108 Hz band this measures, so
    // only the subject's own fundamental is in it.
    const std::vector<Case> cases {
        { "one inaudible neighbour", { 50 }, 0.001f },
        { "one loud neighbour", { 50 }, 0.85f },
        { "a full six-string chord", { 47, 50, 55, 59, 64 }, 0.85f },
    };
    for (const auto& item : cases)
    {
        const double rate = decayRate(renderWith(item.companions, item.velocity));
        const double ratio = alone / rate;
        expect(ratio > 0.80 && ratio < 1.25,
               std::string("holding ") + item.name
                   + " changed the note's own decay");
    }
}


void testANoteOverASoundingInstrumentDoesNotClick()
{
    // Starting a note while the instrument is still ringing used to put the
    // whole released shape into the junction's wave variables in one sample.
    // The finite differences that turn those displacement waves into bridge
    // velocity read that as motion, so every note-on after the first arrived
    // as an impulse about ten times the note it belonged to. A pluck is a
    // release from rest: the shape was standing on the string before the
    // finger let go, and nothing about it is a bridge velocity.
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        acustra::EngineParameters parameters;
        // Measure below the safety limiter, which would otherwise flatten a
        // transient into something that looks like the note it buried.
        parameters.outputGain = 0.04f;
        const int block = 64;
        const auto peakAfter = [&] (const std::vector<int>& held,
                                    bool release, int note)
        {
            auto engineOwner = std::make_unique<acustra::AcustraEngine>();
            auto& engine = *engineOwner;
            engine.setParameters(parameters);
            engine.prepare(rate, block);
            std::vector<float> left(static_cast<std::size_t>(block));
            std::vector<float> right(static_cast<std::size_t>(block));
            for (const int other : held)
                engine.noteOn(other, 0.85f);
            for (int i = 0; i < static_cast<int>(1.5 * rate); i += block)
                engine.process(left.data(), right.data(), block);
            if (release)
            {
                for (const int other : held)
                    engine.noteOff(other);
                for (int i = 0; i < static_cast<int>(0.05 * rate); i += block)
                    engine.process(left.data(), right.data(), block);
            }
            engine.noteOn(note, 0.85f);
            double maximum = 0.0;
            for (int i = 0; i < static_cast<int>(0.06 * rate); i += block)
            {
                engine.process(left.data(), right.data(), block);
                for (int k = 0; k < block; ++k)
                {
                    const auto index = static_cast<std::size_t>(k);
                    expect(std::isfinite(left[index])
                               && std::isfinite(right[index]),
                           "a note over a sounding instrument was not finite");
                    maximum = std::max(maximum, static_cast<double>(std::max(
                        std::abs(left[index]), std::abs(right[index]))));
                }
            }
            return maximum;
        };

        const double fresh = peakAfter({}, false, 43);
        expect(fresh > 1.0e-5, "the reference note was silent");
        struct Case { const char* name; std::vector<int> held; bool release; };
        const std::vector<Case> cases {
            // A free string under a held neighbour.
            { "beside a held neighbour", { 45 }, false },
            // The same string, taken from the note already on it.
            { "taking a sounding string", { 40 }, false },
            // A whole chord, every string occupied.
            { "over a six-string chord", { 40, 47, 52, 56, 59, 64 }, false },
            // And after the hand has left, while the strings ring on.
            { "over a released chord", { 40, 47, 52, 56, 59, 64 }, true },
        };
        for (const auto& item : cases)
        {
            const double peak = peakAfter(item.held, item.release, 43);
            expect(peak < 2.0 * fresh,
                   std::string("a note started ") + item.name
                       + " peaked far above the same note on a silent engine");
        }
    }
}


Audio continueConstructionProbe(acustra::AcustraEngine& engine, int samples)
{
    Audio result { std::vector<float>(static_cast<std::size_t>(samples)),
                   std::vector<float>(static_cast<std::size_t>(samples)) };
    for (int offset = 0; offset < samples; offset += 64)
        engine.process(result.left.data() + offset, result.right.data() + offset,
                       std::min(64, samples - offset));
    return result;
}

// A construction's cell in ConstructionLoudnessData.h, as the engine finds it
// (constructionLoudnessCell).
std::size_t loudnessCell(const acustra::EngineParameters& p)
{
    int cell = static_cast<int>(p.guitarModel);
    cell = cell * 4 + static_cast<int>(p.shape);
    cell = cell * 3 + static_cast<int>(p.bodyMaterial);
    cell = cell * 3 + static_cast<int>(p.picking);
    return static_cast<std::size_t>(cell);
}

// Whether `actual` is `expected` times one level per sample, the same in both
// channels and moving monotonically from 1 toward `to`: a construction's
// output level (ConstructionLoudnessData.h) glided while the waveform stayed
// exactly what it was.
bool differsOnlyInLevel(const Audio& actual, const Audio& expected, double to)
{
    const double low = std::min(1.0, to), high = std::max(1.0, to);
    const double direction = to >= 1.0 ? 1.0 : -1.0;
    double previous = 1.0;
    for (std::size_t i = 0; i < expected.left.size(); ++i)
    {
        const double left = expected.left[i], right = expected.right[i];
        if (std::abs(left) < 1.0e-7 || std::abs(right) < 1.0e-7)
        {
            // Too small to carry a ratio: it may only be as small, scaled.
            if (std::abs(actual.left[i]) > high * std::max(std::abs(left), 1.0e-7) * 1.001
                || std::abs(actual.right[i]) > high * std::max(std::abs(right), 1.0e-7) * 1.001)
                return false;
            continue;
        }
        const double gain = actual.left[i] / left;
        if (std::abs(gain - actual.right[i] / right) > 1.0e-5 * high
            || gain < low * (1.0 - 1.0e-5) || gain > high * (1.0 + 1.0e-5)
            || direction * (gain - previous) < -1.0e-5 * high)
            return false;
        previous = gain;
    }
    return true;
}

void testBodyChangesPreserveTheSoundingStrings()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (const auto capture : { acustra::CaptureType::SaddlePiezo,
                               acustra::CaptureType::Magnetic })
        for (const bool repluck : { false, true })
            for (const bool wood : { false, true })
            {
                acustra::EngineParameters parameters;
                parameters.outputGain = 0.04f;
                parameters.capture = capture;
                auto heldOwner = std::make_unique<acustra::AcustraEngine>();
                auto& held = *heldOwner;
                auto changedOwner = std::make_unique<acustra::AcustraEngine>();
                auto& changed = *changedOwner;
                for (auto* engine : { &held, &changed })
                {
                    engine->setParameters(parameters);
                    engine->prepare(sampleRate, 64);
                    engine->noteOn(52, 0.8f);
                    continueConstructionProbe(*engine, 48000);
                    if (repluck)
                        engine->noteOn(52, 0.8f);
                    continueConstructionProbe(*engine, 64);
                }
                expect(Access::retainedTailCount(changed) == (repluck ? 1 : 0),
                       "the body-change probe did not establish its retained tail");
                if (wood)
                    parameters.bodyMaterial = acustra::BodyMaterial::Maple;
                else
                    parameters.shape = acustra::BodyShape::Parlor;
                changed.setParameters(parameters);
                expect(Access::retainedTailCount(changed)
                           == Access::retainedTailCount(held),
                       "a body change deleted a connected string tail");
                const auto expected = continueConstructionProbe(held, 480);
                const auto actual = continueConstructionProbe(changed, 480);
                // The bridge takes its radiation's poles, so Wood changes
                // the bridge loading as Shape does. Both must retain the
                // connected tails.
                expect(actual.left != expected.left || actual.right != expected.right,
                       "a shape or wood change did not reach the pickup's bridge loading");
                expect(std::all_of(actual.left.begin(), actual.left.end(),
                                   [] (float value) { return std::isfinite(value); })
                           && std::all_of(actual.right.begin(), actual.right.end(),
                                          [] (float value) { return std::isfinite(value); }),
                       "a shape or wood change made the pickup's ringing strings non-finite");
            }
}

void testBodyChangesPreserveAnUnfinishedFade()
{
    using Access = acustra::AcustraEngineTestAccess;
    acustra::EngineParameters parameters;
    parameters.outputGain = 0.04f;
    auto referenceOwner = std::make_unique<acustra::AcustraEngine>();
    auto& reference = *referenceOwner;
    auto changedOwner = std::make_unique<acustra::AcustraEngine>();
    auto& changed = *changedOwner;
    // Wood and Shape move the bridge with the radiation, so the strings ring
    // against a rigid termination: this observes radiation-bank scheduling
    // against exactly the same continuously ringing string input. A body
    // request still reconfigures the bridge at once, and the bridge
    // derivatives that drive the body cross that as a step for one sample
    // (processAcrossStep), so wherever one engine takes a request the other
    // takes one at the same sample: a request queued behind a running fade
    // and cancelled by returning to its target, which leaves the body alone.
    for (auto* engine : { &reference, &changed })
    {
        engine->setParameters(parameters);
        engine->prepare(sampleRate, 64);
        engine->setBridgeCouplingEnabled(false);
        engine->noteOn(52, 0.8f);
        continueConstructionProbe(*engine, 48000);
    }
    const auto compare = [&] (int samples, const char* message)
    {
        const auto expected = continueConstructionProbe(reference, samples);
        const auto actual = continueConstructionProbe(changed, samples);
        expect(actual.left == expected.left && actual.right == expected.right, message);
    };

    auto first = parameters;
    first.bodyMaterial = acustra::BodyMaterial::Maple;
    auto second = first;
    second.bodyMaterial = acustra::BodyMaterial::Mahogany;
    reference.setParameters(second);
    changed.setParameters(first);
    changed.setParameters(second);
    compare(64, "same-tick body updates erased the sounding bank instead of replacing the silent target");

    auto latest = second;
    latest.shape = acustra::BodyShape::Jumbo;
    changed.setParameters(parameters); // superseded before another audio sample
    changed.setParameters(latest);
    expect(Access::bodyUpdatePending(changed), "an interrupted body fade was not queued");
    reference.setParameters(latest);
    reference.setParameters(second);
    int remaining = 0;
    Audio expectedFade, actualFade;
    while (Access::bodyFade(reference) < 1.0f && remaining < 2000)
    {
        const auto expected = continueConstructionProbe(reference, 1);
        const auto actual = continueConstructionProbe(changed, 1);
        expectedFade.left.push_back(expected.left[0]);
        expectedFade.right.push_back(expected.right[0]);
        actualFade.left.push_back(actual.left[0]);
        actualFade.right.push_back(actual.right[0]);
        ++remaining;
    }
    // The queued wood's level (ConstructionLoudnessData.h) glides in at once;
    // the waveform itself must be the sounding bank's, untouched.
    expect(differsOnlyInLevel(actualFade, expectedFade,
               double(acustra::detail::constructionMicReference[loudnessCell(latest)])
                   / acustra::detail::constructionMicReference[loudnessCell(second)]),
           "a queued body change interrupted the existing microphone waveform");
    expect(remaining > 0 && remaining < 2000,
           "a pending body update exceeded the existing 40 ms fade");
    expect(!Access::bodyUpdatePending(changed) && Access::bodyFade(changed) == 0.0f,
           "the latest body request did not start at completion of the previous fade");
    // Applying only the final request at this exact boundary must produce
    // the same actual microphone waveform as the queued sequence. Each
    // construction's output level glides from the request (tested above), so
    // the reference takes the level the queued engine has reached.
    changed.setParameters(first);
    changed.setParameters(latest);
    reference.setParameters(latest);
    Access::copyOutputLevels(changed, reference);
    compare(2000, "the queued body fade did not reach the final requested construction");
    expect(Access::bodyFade(changed) == 1.0f,
           "the queued body's own 40 ms fade did not finish");

    reference.setParameters(first);
    changed.setParameters(first);
    Access::copyOutputLevels(changed, reference);
    compare(64, "the next body fade started inconsistently");
    const float fadeBeforeCancel = Access::bodyFade(changed);
    changed.setParameters(second);
    changed.setParameters(first); // returning to the active target cancels the queue
    expect(!Access::bodyUpdatePending(changed),
           "returning to the active body left an obsolete queued construction");
    expect(fadeBeforeCancel > 0.0f
               && Access::bodyFade(changed) == fadeBeforeCancel,
           "a cancelled body request restarted an unnecessary fade");
    // Whatever was queued and cancelled leaves no trace of itself.
    reference.setParameters(latest);
    reference.setParameters(first);
    compare(4000, "a cancelled body request left a trace of what it asked for");

    changed.setParameters(second);
    continueConstructionProbe(changed, 64);
    changed.setParameters(latest);
    expect(Access::bodyUpdatePending(changed), "the reset probe had no pending body update");
    changed.reset();
    reference.setParameters(latest);
    reference.reset();
    expect(!Access::bodyUpdatePending(changed) && Access::bodyFade(changed) == 1.0f,
           "reset retained an obsolete body fade or pending request");
    for (int mode = 0; mode < Access::bodyModeCapacity; ++mode)
        expect(Access::bodyResidueOf(changed, mode) == Access::bodyResidueOf(reference, mode),
               "reset did not configure the latest requested body bank");
}

// String Age is a continuous, automatable control: each change redesigns
// every string's loss, and a host sends a new value every block. The tail a
// re-pluck left on its string rings out under the hand as it did before the
// change; deleting it made every age step over a re-struck note or chord a
// click (a 0.001 nudge moved the output by up to 2.4 times the note's own
// peak, and strums under an age ramp stood 12-20 dB over the same strums
// without it above 4 kHz).
void testStringAgeKeepsARepluckedTail()
{
    const int block = 16;
    for (const int delay : { 1, 16, 64, 256 })
    {
        // nudge: 0 never, 1 after the re-pluck (the case under test), 2 just
        // before it, where no tail is left under the old age: the change the
        // step itself makes to the sound.
        const auto render = [&] (int nudge, int& tailsAfter)
        {
            auto engineOwner = std::make_unique<acustra::AcustraEngine>();
            auto& engine = *engineOwner;
            acustra::EngineParameters parameters;
            engine.setParameters(parameters);
            engine.prepare(sampleRate, block);
            std::vector<float> left(static_cast<std::size_t>(block));
            std::vector<float> right(static_cast<std::size_t>(block));
            const auto run = [&] (int samples, std::vector<float>* out)
            {
                for (int done = 0; done < samples; done += block)
                {
                    engine.process(left.data(), right.data(), block);
                    if (out != nullptr)
                        out->insert(out->end(), left.begin(), left.end());
                }
            };
            engine.noteOn(52, 0.8f);
            run(static_cast<int>(0.25 * sampleRate), nullptr);
            if (nudge == 2)
            {
                parameters.stringAge += 0.001f;
                engine.setParameters(parameters);
            }
            engine.noteOn(52, 0.8f);
            std::vector<float> output;
            run(delay, &output);
            expect(acustra::AcustraEngineTestAccess::retainedTailCount(engine) > 0,
                   "a re-pluck of a sounding string left no tail to test");
            if (nudge == 1)
            {
                parameters.stringAge += 0.001f;
                engine.setParameters(parameters);
            }
            run(static_cast<int>(0.1 * sampleRate), &output);
            tailsAfter = acustra::AcustraEngineTestAccess::retainedTailCount(engine);
            return output;
        };
        int steadyTails = 0, nudgedTails = 0, agedTails = 0;
        const auto steady = render(0, steadyTails);
        const auto nudged = render(1, nudgedTails);
        const auto aged = render(2, agedTails);
        // The step lands `delay` samples into the output.
        const std::size_t clickEnd = std::min(steady.size(),
            static_cast<std::size_t>(delay + static_cast<int>(0.005 * sampleRate)));
        double peak = 0.0, difference = 0.0, ageing = 0.0, click = 0.0, agedClick = 0.0;
        for (std::size_t index = 0; index < steady.size(); ++index)
        {
            peak = std::max(peak, std::abs(static_cast<double>(steady[index])));
            const double moved = std::abs(static_cast<double>(nudged[index]) - steady[index]);
            difference = std::max(difference, moved);
            const double aging = std::abs(static_cast<double>(aged[index]) - steady[index]);
            if (index < clickEnd)
            {
                click = std::max(click, moved);
                agedClick = std::max(agedClick, aging);
            }
            ageing = std::max(ageing, aging);
        }
        expect(nudgedTails == steadyTails,
               "a String Age step " + std::to_string(delay)
                   + " samples after a re-pluck removed its tail");
        // A deleted tail moved the output by up to 2.4 times the note's own
        // peak at once. The step may change the sound as the same step made
        // just before the re-pluck does, and by no more than 1e-4 of the peak
        // beyond it: over the 5 ms after it, where a deleted tail or a
        // switched filter state would show, and over the whole 100 ms. Since
        // String Age reaches the wound strings' bending loss (2026-09-30) this
        // E3 on the D string ages about eight times as fast per step as it
        // did, and the step's own change grew to 2.4e-4 of the peak, where the
        // test once held the whole difference to 1e-4. Since 2026-10-01 the
        // microphones' capture voicing (CaptureVoicingData.h) lifts 1-1.6 kHz,
        // where a loss step's change lives, by up to 7 dB against the note's
        // low partials that set its peak: the step's own change reads 1.5-1.7
        // times what it did (2.4e-4 -> 3.8e-4), and this tolerance twice its
        // old 1e-4, which leaves the 256-sample case the 7% headroom it had.
        constexpr double tolerance = 2.0e-4;
        expect(ageing > 0.0, "a String Age step did not reach the sound");
        expect(click <= agedClick + tolerance * peak,
               "a 0.001 String Age step " + std::to_string(delay)
                   + " samples after a re-pluck moved the output by "
                   + std::to_string(click / std::max(peak, 1.0e-12))
                   + " of its peak within 5 ms, where the step before the re-pluck moves it by "
                   + std::to_string(agedClick / std::max(peak, 1.0e-12)));
        expect(difference <= ageing + tolerance * peak,
               "a 0.001 String Age step " + std::to_string(delay)
                   + " samples after a re-pluck moved the output by "
                   + std::to_string(difference / std::max(peak, 1.0e-12))
                   + " of its peak, where the step before the re-pluck moves it by "
                   + std::to_string(ageing / std::max(peak, 1.0e-12)));
        std::cout << "Acustra String Age step " << delay << " samples after a re-pluck: "
                  << click / std::max(peak, 1.0e-12) << " of the peak within 5 ms (before: " << agedClick / std::max(peak, 1.0e-12) << "), "
                  << difference / std::max(peak, 1.0e-12) << " over 100 ms, the step "
                  << "before the re-pluck " << ageing / std::max(peak, 1.0e-12) << '\n';
    }
}

void testSwitchingTuningOrModelUnderAChordDoesNotClick()
{
    // Changing the tuning changes every string's impedance at once, so the
    // junction's wave variables step with the port. The strings were
    // retuned; the bridge did not move.
    acustra::EngineParameters steel;
    steel.outputGain = 0.04f;
    const int block = 64;
    const auto stepPeak = [&] (acustra::EngineParameters after)
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        engine.setParameters(steel);
        engine.prepare(sampleRate, block);
        std::vector<float> left(static_cast<std::size_t>(block));
        std::vector<float> right(static_cast<std::size_t>(block));
        const auto sweep = [&] (double seconds)
        {
            double maximum = 0.0;
            for (int i = 0; i < static_cast<int>(seconds * sampleRate);
                 i += block)
            {
                engine.process(left.data(), right.data(), block);
                for (int k = 0; k < block; ++k)
                {
                    const auto index = static_cast<std::size_t>(k);
                    expect(std::isfinite(left[index])
                               && std::isfinite(right[index]),
                           "a construction switch produced non-finite audio");
                    maximum = std::max(maximum, static_cast<double>(std::max(
                        std::abs(left[index]), std::abs(right[index]))));
                }
            }
            return maximum;
        };
        for (const int note : { 40, 47, 52, 56, 59, 64 })
            engine.noteOn(note, 0.85f);
        sweep(1.2);
        const double before = sweep(0.05);
        after.outputGain = 0.04f;
        engine.setParameters(after);
        return std::pair { before, sweep(0.05) };
    };

    {
        acustra::EngineParameters dadgad;
        dadgad.tuning = acustra::Tuning::Dadgad;
        const auto [before, after] = stepPeak(dadgad);
        expect(before > 1.0e-6, "the chord under the switch was silent");
        expect(after < 2.0 * before,
               "switching the tuning under a ringing chord produced a transient");
    }

    // Every direction, measured against the louder of the chord before the
    // switch and the same chord played on the new construction from the
    // start, at the same moment - a switch to a louder construction may be
    // louder, but not more than that. A Model switch hands the chord's
    // stored energy to another guitar's bridge and body, and the new body's
    // modes start from rest under it: the Bellido to the Original about twice
    // (1.998 before the pluck shape was laid over the loop's period on
    // 2026-09-30, 2.030 after; 2.12 once the finger's release slip and the
    // radiation above the measured band joined it the same day, as the
    // Original to the Bellido fell from 3.24 to 3.05; the swell is the
    // chord's stored energy, so its spectrum moves it), bounded at 2.2. The Original to the Bellido is more, since
    // every construction plays at one loudness
    // (ConstructionLoudnessData.h): the Original's chord, drained less by
    // its stiffer top, pours through the Bellido's mobile one at 4.1 times
    // the Bellido's own chord at that moment (4.5 before the levels), and
    // the Bellido no longer sits 5.8 dB under the Original to hide it
    // behind the louder chord: 3.3 times, bounded at 3.5.
    const auto against = [&] (acustra::EngineParameters from, acustra::EngineParameters to)
    {
        from.outputGain = to.outputGain = 0.04f;
        auto freshOwner = std::make_unique<acustra::AcustraEngine>();
        auto& fresh = *freshOwner;
        fresh.setParameters(to);
        fresh.prepare(sampleRate, block);
        std::vector<float> left(static_cast<std::size_t>(block)), right(left);
        for (const int note : { 40, 47, 52, 56, 59, 64 })
            fresh.noteOn(note, 0.85f);
        double reference = 0.0;
        for (int i = 0; i < static_cast<int>(1.25 * sampleRate); i += block)
        {
            fresh.process(left.data(), right.data(), block);
            if (i >= static_cast<int>(1.2 * sampleRate))
                for (int k = 0; k < block; ++k)
                    reference = std::max(reference, static_cast<double>(std::max(
                        std::abs(left[static_cast<std::size_t>(k)]),
                        std::abs(right[static_cast<std::size_t>(k)]))));
        }
        const auto saved = steel;
        steel = from;
        const auto [before, after] = stepPeak(to);
        steel = saved;
        return after / std::max(before, reference);
    };
    const auto make = [] (acustra::GuitarModel model)
    {
        acustra::EngineParameters p;
        p.guitarModel = model;
        return p;
    };
    using G = acustra::GuitarModel;
    struct Switch { const char* name; acustra::EngineParameters from, to; double bound; };
    for (const auto& item : {
             Switch { "Bellido to Original", make(G::Bellido1978), make(G::Original), 2.2 },
             Switch { "Original to Bellido", make(G::Original), make(G::Bellido1978), 3.5 } })
    {
        const double ratio = against(item.from, item.to);
        std::cout << "Acustra construction switch under a chord, " << item.name
                  << ": " << ratio << " x the louder steady chord\n";
        expect(ratio < item.bound,
               std::string("switching ") + item.name + " under a ringing chord swelled to "
                   + std::to_string(ratio) + " times the louder steady chord");
    }
}



void testLongitudinalModesGrowWithVelocity()
{
    // Transverse motion stretches the string, and the tension it adds is a
    // longitudinal wave at the string's own axial resonances. Its drive is a
    // squared slope, so what it puts into the band is the products of the
    // transverse partials and it must grow faster than the note that made it.
    const auto bandEnergy = [] (const Audio& audio, double low, double high)
    {
        return tailBandRms(audio, sampleRate, 0.0, 0.12, low, high);
    };
    expect(acustra::fittedPhysicalCalibration.longitudinalGain == 0.0f,
           "the shipping build reintroduced the drip-like axial onset");
    // The mechanism is measured where it was built: a Finger 74 mm from the
    // bridge (steel.pluckDistanceScale 0.888, the fitted archtop value). At
    // the 149 mm chosen by ear on 2026-09-25 the shallower triangle's squared
    // slope grows E3's axial band only 1.1 dB faster than the note (E2 4.8),
    // and the path ships switched off, so that is not what is under test.
    auto silent = acustra::fittedPhysicalCalibration;
    silent.steel.pluckDistanceScale = 0.88819512f;
    auto sounding = silent;
    sounding.longitudinalGain = 0.025f;
    const auto axial = acustra::AcustraEngineTestAccess::
        longitudinalFrequencies(40);
    expect(std::abs(axial[1] / axial[0] - 3.0) < 1.0e-4,
           "the next odd longitudinal mode is not three times the first");

    const acustra::EngineParameters steel;
    double quietGrowth = 0.0;
    double loudGrowth = 0.0;
    for (const int midiNote : { 40, 52 })
    {
        double growth[2] = { 0.0, 0.0 };
        int index = 0;
        for (const float velocity : { 0.25f, 0.95f })
        {
            const auto off = renderCalibrated(steel, silent, midiNote,
                                              velocity, 0.5);
            const auto on = renderCalibrated(steel, sounding, midiNote,
                                             velocity, 0.5);
            const double before = bandEnergy(off, 1500.0, 4000.0);
            const double after = bandEnergy(on, 1500.0, 4000.0);
            expect(before > 0.0 && after > before,
                   "the longitudinal path added no axial-band energy");
            growth[index++] = 20.0 * std::log10(after / before);
            for (std::size_t sample = 0; sample < on.left.size(); ++sample)
                expect(std::isfinite(on.left[sample])
                           && std::isfinite(on.right[sample]),
                       "a longitudinal render was not finite");
        }
        expect(growth[1] > growth[0] + 3.0,
               "the longitudinal band did not grow faster than the note");
        quietGrowth = std::max(quietGrowth, growth[0]);
        loudGrowth = std::max(loudGrowth, growth[1]);
    }
    std::cout << "Acustra longitudinal band growth: quiet=" << quietGrowth
              << " dB, loud=" << loudGrowth << " dB\n";

    // Zero is an exact no-op.
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.setPhysicalCalibration(silent);
    engine.prepare(sampleRate, blockSize);
    engine.noteOn(52, 0.9f);
    std::vector<float> left(static_cast<std::size_t>(blockSize));
    std::vector<float> right(static_cast<std::size_t>(blockSize));
    double silentForce = 0.0;
    for (int block = 0; block < 96; ++block)
    {
        engine.process(left.data(), right.data(), blockSize);
        silentForce = std::max(silentForce, static_cast<double>(
            std::abs(engine.getLastLongitudinalForce())));
    }
    expect(silentForce == 0.0,
           "a zero longitudinal gain still produced a force");
}

void testTodaysMechanismsSurviveEachOther()
{
    // The plate conductance floor, the stolen-string tail, bridge-hand
    // pressure and natural harmonics all landed together. Drive them against
    // one another: muted chord changes that steal ringing strings, harmonics
    // taken and retaken, a tuning switch underneath, pitch bend
    // across it, and a panic at the end.
    for (const double rate : { 44100.0, 48000.0, 192000.0 })
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        acustra::EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(rate, blockSize);
        std::vector<float> left(static_cast<std::size_t>(blockSize));
        std::vector<float> right(static_cast<std::size_t>(blockSize));
        const std::array<std::array<int, 6>, 2> chords {{
            {{ 40, 47, 52, 56, 59, 64 }}, {{ 43, 50, 55, 58, 62, 67 }} }};
        const std::array<int, 3> harmonics { 88, 90, 95 };
        double maximum = 0.0;
        int step = 0;
        const int stride = static_cast<int>(0.08 * rate);
        for (int i = 0; i < static_cast<int>(6.0 * rate); i += blockSize)
        {
            if (i / stride > step - 1 && step < 60)
            {
                switch (step % 6)
                {
                    case 0:
                        for (const int note : chords[0]) engine.noteOn(note, 0.9f);
                        break;
                    case 1:
                        engine.setPalmMutePressure(0.85f);
                        engine.noteOn(harmonics[static_cast<std::size_t>(
                            step / 6 % harmonics.size())], 0.7f);
                        break;
                    case 2:
                        for (const int note : chords[1]) engine.noteOn(note, 0.9f);
                        break;
                    case 3:
                        engine.setPitchBend(2.0f);
                        engine.setPalmMutePressure(0.0f);
                        // Notes over held strings, released out of order
                        // while the strings are still sounding.
                        engine.noteOn(45, 0.8f);
                        engine.noteOn(48, 0.8f);
                        engine.noteOn(52, 0.8f);
                        engine.noteOff(48);
                        engine.noteOff(52);
                        break;
                    case 4:
                        parameters.tuning = (step / 6) % 2 == 0
                            ? acustra::Tuning::Dadgad : acustra::Tuning::Standard;
                        engine.setParameters(parameters);
                        break;
                    default:
                        engine.setPitchBend(0.0f);
                        for (const int note : chords[0]) engine.noteOff(note);
                        break;
                }
                ++step;
            }
            engine.process(left.data(), right.data(), blockSize);
            for (int k = 0; k < blockSize; ++k)
            {
                const auto index = static_cast<std::size_t>(k);
                expect(std::isfinite(left[index]) && std::isfinite(right[index]),
                       "the combined mechanisms produced non-finite audio");
                maximum = std::max(maximum, static_cast<double>(std::max(
                    std::abs(left[index]), std::abs(right[index]))));
            }
        }
        expect(maximum > 1.0e-6 && maximum <= 1.0,
               "the combined mechanisms were silent or escaped headroom");

        engine.allSoundOff();
        for (int i = 0; i < static_cast<int>(1.0 * rate); i += blockSize)
        {
            engine.process(left.data(), right.data(), blockSize);
            for (int k = 0; k < blockSize; ++k)
                expect(std::abs(left[static_cast<std::size_t>(k)]) < 1.0e-4f
                       && std::abs(right[static_cast<std::size_t>(k)]) < 1.0e-4f,
                       "All Sound Off left the combined mechanisms sounding");
        }
        expect(engine.getActiveVoiceCount() == 0,
               "All Sound Off left a voice held after the combined run");
    }
}

void testNoteAfterSilenceDoesNotClick()
{
    // While no string is sounding the bridge junction has no drive, but it must
    // keep ringing down rather than freeze: paused modes store their energy
    // until the next note reactivates the port, and it arrives as a click.
    const auto peakOfNoteAfter = [] (double quietSeconds, bool playChordFirst)
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        acustra::EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(sampleRate, blockSize);
        std::vector<float> left(static_cast<std::size_t>(blockSize));
        std::vector<float> right(static_cast<std::size_t>(blockSize));
        const auto run = [&] (double seconds)
        {
            for (int i = 0; i < static_cast<int>(seconds * sampleRate);
                 i += blockSize)
                engine.process(left.data(), right.data(), blockSize);
        };
        if (playChordFirst)
        {
            for (const int note : { 40, 47, 52, 56, 59, 64 })
                engine.noteOn(note, 0.72f);
            run(2.0);
            for (const int note : { 40, 47, 52, 56, 59, 64 })
                engine.noteOff(note);
        }
        run(quietSeconds);
        engine.noteOn(43, 0.62f);
        double peak = 0.0;
        for (int i = 0; i < static_cast<int>(0.3 * sampleRate); i += blockSize)
        {
            engine.process(left.data(), right.data(), blockSize);
            for (int k = 0; k < blockSize; ++k)
                peak = std::max(peak, static_cast<double>(std::max(
                    std::abs(left[static_cast<std::size_t>(k)]),
                    std::abs(right[static_cast<std::size_t>(k)]))));
        }
        return peak;
    };

    // Nothing may erupt out of a silent decay either. The junction's modes
    // outlast the strings, so the moment the last string goes quiet used to
    // switch the port out from under them.
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        acustra::EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(sampleRate, blockSize);
        std::vector<float> left(static_cast<std::size_t>(blockSize));
        std::vector<float> right(static_cast<std::size_t>(blockSize));
        const auto run = [&] (double seconds, double* peak)
        {
            for (int i = 0; i < static_cast<int>(seconds * sampleRate);
                 i += blockSize)
            {
                engine.process(left.data(), right.data(), blockSize);
                if (peak == nullptr)
                    continue;
                for (int k = 0; k < blockSize; ++k)
                    *peak = std::max(*peak, static_cast<double>(std::max(
                        std::abs(left[static_cast<std::size_t>(k)]),
                        std::abs(right[static_cast<std::size_t>(k)]))));
            }
        };
        for (const int note : { 40, 47, 52, 56, 59, 64 })
            engine.noteOn(note, 0.62f);
        run(1.3, nullptr);
        for (const int note : { 43, 50, 55, 58, 62, 67 })
            engine.noteOn(note, 0.62f);
        run(1.6, nullptr);
        for (const int note : { 43, 50, 55, 58, 62, 67 })
            engine.noteOff(note);
        run(0.4, nullptr);
        double quietPeak = 0.0;
        run(3.0, &quietPeak);
        expect(quietPeak < 0.05,
               "a transient erupted from a decaying chord with no events");
    }

    const double fresh = peakOfNoteAfter(0.0, false);
    expect(fresh > 1.0e-4, "the reference note was silent");
    for (const double quiet : { 2.0, 4.0, 8.0 })
    {
        const double afterSilence = peakOfNoteAfter(quiet, true);
        expect(afterSilence < 2.0 * fresh,
               "a note after a silent gap peaked far above the same note on a "
               "fresh engine");
        expect(afterSilence > 0.25 * fresh,
               "a note after a silent gap was suppressed");
    }
}

void testRepluckLandsTheHandOnTheString()
{
    // The picking hand lands on a sounding string before it plucks it again,
    // so what the string held goes on under the hand while the new pluck is
    // released from rest - the contact a taken string already goes through.
    // Projecting the stored shape onto the modes with a node at the contact,
    // in one sample, clicked into the limiter and let the near-node modes
    // pile up pluck after pluck; and a note repeated after its key came up
    // hopped to another string that could reach it.
    const auto repeated = [] (int note, bool releaseBetween, double rate)
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        acustra::EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(rate, blockSize);
        engine.setParameters(parameters);
        engine.setSympatheticStringsEnabled(false);
        std::vector<float> l(static_cast<std::size_t>(blockSize));
        std::vector<float> r(static_cast<std::size_t>(blockSize));
        std::vector<double> peaks;
        std::vector<double> firstSamples;
        std::vector<double> levels;
        std::vector<int> voices;
        std::vector<double> tails;
        double before = 0.0;
        for (int repeat = 0; repeat < 6; ++repeat)
        {
            engine.noteOn(note, 0.62f);
            double tail = 0.0;
            voices.push_back(engine.getActiveVoiceCount());
            double peakValue = 0.0;
            double firstSample = 0.0;
            double energy = 0.0;
            int counted = 0;
            const int span = static_cast<int>(0.25 * rate);
            for (int i = 0; i < span; i += blockSize)
            {
                const int count = std::min(blockSize, span - i);
                engine.process(l.data(), r.data(), count);
                for (int k = 0; k < count; ++k)
                {
                    const double value = std::max(
                        std::abs(l[static_cast<std::size_t>(k)]),
                        std::abs(r[static_cast<std::size_t>(k)]));
                    if (i == 0 && k < 4)
                        firstSample = std::max(firstSample, value);
                    peakValue = std::max(peakValue, value);
                    if (i + k >= static_cast<int>(0.03 * rate)
                        && i + k < static_cast<int>(0.10 * rate))
                    {
                        energy += value * value;
                        ++counted;
                    }
                    if (i + k >= span - static_cast<int>(0.01 * rate))
                    {
                        before = std::max(before, value);
                        tail = std::max(tail, value);
                    }
                }
            }
            tails.push_back(tail);
            peaks.push_back(peakValue);
            firstSamples.push_back(firstSample);
            levels.push_back(std::sqrt(energy / std::max(counted, 1)));
            if (releaseBetween)
            {
                engine.noteOff(note);
                const int gap = static_cast<int>(0.05 * rate);
                for (int i = 0; i < gap; i += blockSize)
                    engine.process(l.data(), r.data(),
                                   std::min(blockSize, gap - i));
            }
        }
        return std::tuple { peaks, firstSamples, levels, voices, before, tails };
    };

    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const std::string at = " at " + std::to_string(static_cast<int>(rate));
        // Held key, replucked six times at 250 ms: the level neither climbs
        // nor clicks.
        const auto [peaks, firsts, levels, voices, before, tails]
            = repeated(43, false, rate);
        expect(peaks[0] > 1.0e-4, "the first pluck was silent" + at);
        for (std::size_t index = 1; index < peaks.size(); ++index)
        {
            expect(peaks[index] < 1.5 * peaks[0],
                   "repluck " + std::to_string(index) + " peaked "
                   + std::to_string(peaks[index] / peaks[0])
                   + " times the first" + at);
            expect(levels[index] < 1.6 * levels[0],
                   "repluck " + std::to_string(index) + " piled up energy" + at);
            // A click is a step above what was sounding: on steel's own
            // bridge the G2 fundamental still rings at about two thirds of
            // the next pluck's peak after 250 ms (the air-mode twin blooms
            // it), so the first samples are held against the ring they
            // continue as well as against the new peak.
            expect(firsts[index] < std::max(0.5 * peaks[index],
                                            1.05 * tails[index - 1]),
                   "repluck " + std::to_string(index)
                   + " clicked on its first samples" + at);
        }
        expect(peaks.back() < 0.9, "a repluck approached full scale" + at);

        // Released between repeats, a note three strings can reach stays on
        // the string that was sounding it, and one voice is enough.
        const auto [hopPeaks, hopFirsts, hopLevels, hopVoices, hopBefore, hopTails]
            = repeated(64, true, rate);
        for (const int count : hopVoices)
            expect(count == 1, "a repeated E4 hopped to another string" + at);
        for (std::size_t index = 1; index < hopPeaks.size(); ++index)
        {
            expect(hopPeaks[index] < 1.5 * hopPeaks[0]
                       && hopPeaks[index] > 0.5 * hopPeaks[0],
                   "a repeated E4 changed level on repeat" + at);
            expect(hopFirsts[index] < 0.5 * hopPeaks[index],
                   "a repeated E4 clicked on its first samples" + at);
        }

        // Gate: the picking hand's 10 ms contact time leaves the captured
        // tail's energy below 1% of what it started with 60 ms after a
        // repluck.
        const auto [atCapture, after60ms]
            = acustra::AcustraEngineTestAccess::repluckTailEnergyAt60ms(rate);
        expect(atCapture > 0.0, "a repluck captured no tail energy" + at);
        expect(after60ms < 0.01 * atCapture,
               "the repluck tail held more than 1% of its energy 60 ms later"
               + at);
    }

    // A first pluck is untouched: a fresh engine and one that has only ever
    // been silent render the same bits.
    acustra::EngineParameters parameters;
    const auto plain = renderWithInitialParameters(parameters, 43, 0.62f, 0.5);
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.setParameters(parameters);
    engine.prepare(sampleRate, blockSize);
    engine.noteOn(43, 0.62f);
    const int samples = static_cast<int>(0.5 * sampleRate);
    Audio again { std::vector<float>(static_cast<std::size_t>(samples)),
                  std::vector<float>(static_cast<std::size_t>(samples)) };
    for (int offset = 0; offset < samples; offset += blockSize)
        engine.process(again.left.data() + offset, again.right.data() + offset,
                       std::min(blockSize, samples - offset));
    expect(plain.left == again.left && plain.right == again.right,
           "a first pluck is not what a fresh engine renders");
}

void testSteelFretT60SlopeRaisesOnlyFrettedSustain()
{
    auto neutral = acustra::fittedPhysicalCalibration;
    neutral.steelFretT60Slope = 0.0f;
    auto negative = neutral;
    negative.steelFretT60Slope = -0.030f;

    const auto neutralFretted = acustra::AcustraEngineTestAccess::configuredLoop(
        84, sampleRate, neutral);
    const auto negativeFretted = acustra::AcustraEngineTestAccess::configuredLoop(
        84, sampleRate, negative);
    expect(negativeFretted.loopGain > neutralFretted.loopGain,
           "negative steel fret-T60 slope did not raise fretted sustain");

    const auto neutralOpen = acustra::AcustraEngineTestAccess::configuredLoop(
        40, sampleRate, neutral);
    const auto negativeOpen = acustra::AcustraEngineTestAccess::configuredLoop(
        40, sampleRate, negative);
    expect(negativeOpen.loopGain == neutralOpen.loopGain,
           "steel fret-T60 slope changed an open string");
}

void testApertureRegisterExponentChangesOnlyRegisterGeometry()
{
    constexpr float apertureSamples = 3.25f;
    constexpr float apertureScale = 1.125f;
    const float reference
        = acustra::AcustraEngineTestAccess::apertureReferenceDelay();
    for (const float current : { 2.0f * reference, reference,
                                 0.5f * reference })
    {
        const float legacy = apertureSamples * apertureScale
                           / std::max(current, 8.0f);
        const float registered
            = acustra::AcustraEngineTestAccess::registeredAperture(
                apertureSamples, apertureScale, current, 1.0f);
        expect(registered == legacy,
               "exponent one changed the promoted aperture formula");
    }

    const float lowCurrent = 2.0f * reference;
    const float highCurrent = 0.5f * reference;
    const float lowOriginal
        = acustra::AcustraEngineTestAccess::registeredAperture(
            apertureSamples, apertureScale, lowCurrent, 1.0f);
    const float lowFlat
        = acustra::AcustraEngineTestAccess::registeredAperture(
            apertureSamples, apertureScale, lowCurrent, 0.0f);
    const float highOriginal
        = acustra::AcustraEngineTestAccess::registeredAperture(
            apertureSamples, apertureScale, highCurrent, 1.0f);
    const float highFlat
        = acustra::AcustraEngineTestAccess::registeredAperture(
            apertureSamples, apertureScale, highCurrent, 0.0f);
    expect(lowFlat > 1.99f * lowOriginal
               && highFlat < 0.51f * highOriginal,
           "zero register exponent did not darken lows and brighten highs");
    const float lowNegative
        = acustra::AcustraEngineTestAccess::registeredAperture(
            apertureSamples, apertureScale, lowCurrent, -0.5f);
    const float highNegative
        = acustra::AcustraEngineTestAccess::registeredAperture(
            apertureSamples, apertureScale, highCurrent, -0.5f);
    expect(lowNegative > 1.41f * lowFlat
               && highNegative < 0.71f * highFlat,
           "negative register exponent did not continue the fitted direction");

    auto legacy = acustra::fittedPhysicalCalibration;
    legacy.apertureRegisterExponent = 1.0f;
    auto flat = acustra::fittedPhysicalCalibration;
    flat.apertureRegisterExponent = 0.0f;
    const auto originalLow = acustra::AcustraEngineTestAccess::pluck(
        legacy, 0.8f, 40);
    const auto flatLow = acustra::AcustraEngineTestAccess::pluck(
        flat, 0.8f, 40);
    const auto originalHigh = acustra::AcustraEngineTestAccess::pluck(
        legacy, 0.8f, 84);
    const auto flatHigh = acustra::AcustraEngineTestAccess::pluck(
        flat, 0.8f, 84);
    expect(flatLow.peakDisplacement < originalLow.peakDisplacement
               && flatHigh.peakDisplacement > originalHigh.peakDisplacement,
           "register exponent did not reach the note-on aperture geometry");
}

void testOrdinaryOutputIsLinearAndPathologicalOutputIsBounded()
{
    acustra::EngineParameters quiet;
    quiet.outputGain = 0.10f;
    auto louder = quiet;
    louder.outputGain = 0.20f;
    const auto a = renderWithInitialParameters(quiet, 52, 0.90f, 0.35);
    const auto b = renderWithInitialParameters(louder, 52, 0.90f, 0.35);
    expect(peak(b) < 0.89125094,
           "linearity probe unexpectedly reached the safety limiter");
    double differenceEnergy = 0.0;
    double referenceEnergy = 0.0;
    for (std::size_t sample = 0; sample < a.left.size(); ++sample)
    {
        for (const auto pair : { std::pair { a.left[sample], b.left[sample] },
                                 std::pair { a.right[sample], b.right[sample] } })
        {
            const double expected = 2.0 * pair.first;
            const double difference = pair.second - expected;
            differenceEnergy += difference * difference;
            referenceEnergy += expected * expected;
        }
    }
    expect(referenceEnergy > 0.0,
           "linearity probe rendered silence");
    expect(std::sqrt(differenceEnergy / referenceEnergy) < 1.0e-6,
           "ordinary output is not linear below the -1 dBFS safety threshold");

    acustra::EngineParameters hostile;
    hostile.outputGain = 4.0f;
    const auto bounded = renderWithInitialParameters(
        hostile, 52, 1.0f, 0.35);
    expect(peak(bounded) <= 1.0,
           "safety limiter exceeded unit headroom");
}

// A lifted key must only take energy out of the string. The release loss is
// a per-round-trip gain, and applying its full value to the first sample
// after note-off stepped the wave the junction reads by up to a third on a low
// fretted note, which the bridge and body rang on as a thump 4.6 to 8.6 dB
// above the note's own level at that moment. Ramping the loss in over one
// round trip, the unit it is defined in, leaves nothing above the held note.
void testAScheduledPluckIsANoteOnIssuedThen()
{
    // A strum takes its strings at once and releases them one after another.
    // A pluck scheduled D samples ahead must be the note-on issued D samples
    // later, sample for sample, and nothing at all before it.
    for (const int delay : { 1, 97, 480, 2000 })
    {
        auto scheduledOwner = std::make_unique<acustra::AcustraEngine>();
        auto& scheduled = *scheduledOwner;
        auto issuedOwner = std::make_unique<acustra::AcustraEngine>();
        auto& issued = *issuedOwner;
        scheduled.prepare(sampleRate, blockSize);
        issued.prepare(sampleRate, blockSize);
        const int total = delay + static_cast<int>(0.4 * sampleRate);
        std::vector<float> a(static_cast<std::size_t>(total));
        std::vector<float> b(static_cast<std::size_t>(total));
        std::vector<float> scratch(static_cast<std::size_t>(total));
        scheduled.noteOn(52, 0.8f, 1, delay);
        scheduled.process(a.data(), scratch.data(), total);
        issued.process(b.data(), scratch.data(), delay);
        issued.noteOn(52, 0.8f);
        issued.process(b.data() + delay, scratch.data(), total - delay);
        bool silentBefore = true;
        for (int sample = 0; sample < delay; ++sample)
            silentBefore = silentBefore && a[static_cast<std::size_t>(sample)] == 0.0f;
        expect(silentBefore, "a scheduled pluck sounded before its time");
        expect(a == b, "a scheduled pluck differed from the note-on issued then");
    }

    // A repeated stroke must also wait for the pick to reach an already held
    // string. Until then its old wave keeps ringing unchanged; at the release
    // it must become the same audible re-pluck issued at that sample. Vary
    // velocity so resetting the attack or pluck shape early cannot hide.
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const bool strumming : { false, true })
            for (const int delay : { 1, 31, 97, 480, 2000 })
            {
                acustra::EngineParameters parameters;
                auto scheduledOwner = std::make_unique<acustra::AcustraEngine>();
                auto& scheduled = *scheduledOwner;
                auto issuedOwner = std::make_unique<acustra::AcustraEngine>();
                auto& issued = *issuedOwner;
                scheduled.setParameters(parameters);
                issued.setParameters(parameters);
                scheduled.prepare(rate, blockSize);
                issued.prepare(rate, blockSize);
                const int warmup = static_cast<int>(0.05 * rate);
                const int after = static_cast<int>(0.04 * rate);
                const int size = std::max({ warmup, delay, after });
                std::vector<float> a(size), b(size), rightA(size), rightB(size);
                scheduled.noteOn(52, 0.35f);
                issued.noteOn(52, 0.35f);
                scheduled.process(a.data(), rightA.data(), warmup);
                issued.process(b.data(), rightB.data(), warmup);
                scheduled.noteOn(52, 0.85f, 1, delay, strumming);
                scheduled.process(a.data(), rightA.data(), delay);
                issued.process(b.data(), rightB.data(), delay);
                const std::string at = " at " + std::to_string(rate)
                    + " Hz, delay " + std::to_string(delay)
                    + (strumming ? ", strum" : ", note");
                expect(std::equal(a.begin(), a.begin() + delay, b.begin())
                           && std::equal(rightA.begin(), rightA.begin() + delay,
                                         rightB.begin()),
                       "a delayed held re-pluck changed the preceding wave" + at);
                expect(std::any_of(b.begin(), b.begin() + delay,
                                   [] (float value) { return value != 0.0f; }),
                       "the held re-pluck timing reference was silent" + at);
                issued.noteOn(52, 0.85f, 1, 0, strumming);
                scheduled.process(a.data(), rightA.data(), after);
                issued.process(b.data(), rightB.data(), after);
                expect(std::equal(a.begin(), a.begin() + after, b.begin())
                           && std::equal(rightA.begin(), rightA.begin() + after,
                                         rightB.begin()),
                       "a delayed held re-pluck differed from one issued then" + at);
            }

    // The hand leaving before the pick arrives means the string never sounds.
    auto cancelledOwner = std::make_unique<acustra::AcustraEngine>();
    auto& cancelled = *cancelledOwner;
    cancelled.prepare(sampleRate, blockSize);
    std::vector<float> left(static_cast<std::size_t>(sampleRate));
    std::vector<float> right(static_cast<std::size_t>(sampleRate));
    cancelled.noteOn(52, 0.8f, 1, 4800);
    cancelled.process(left.data(), right.data(), 2400);
    cancelled.noteOff(52);
    cancelled.process(left.data(), right.data(), static_cast<int>(sampleRate));
    expect(std::all_of(left.begin(), left.end(), [] (float v) { return v == 0.0f; }),
           "a pluck released before the pick arrived still sounded");
    auto silencedOwner = std::make_unique<acustra::AcustraEngine>();
    auto& silenced = *silencedOwner;
    silenced.prepare(sampleRate, blockSize);
    silenced.noteOn(52, 0.8f, 1, 4800);
    silenced.process(left.data(), right.data(), 2400);
    silenced.allSoundOff();
    silenced.process(left.data(), right.data(), static_cast<int>(sampleRate));
    expect(std::all_of(left.begin(), left.end(), [] (float v) { return v == 0.0f; }),
           "All Sound Off left a scheduled pluck waiting");
}

void testCancelledScheduledAttacksKeepOnlyTheExistingWave()
{
    constexpr int delay = 2400;
    constexpr int after = delay + 1024;
    std::vector<float> left(after), right(after), referenceLeft(after), referenceRight(after);
    const auto compare = [&](acustra::AcustraEngine& engine,
                             acustra::AcustraEngine& reference,
                             const std::string& message)
    {
        engine.process(left.data(), right.data(), after);
        reference.process(referenceLeft.data(), referenceRight.data(), after);
        expect(left == referenceLeft && right == referenceRight, message);
    };
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const std::string at = " at " + std::to_string(rate) + " Hz";
        for (const bool held : { false, true })
            for (const bool sustain : { false, true })
            {
                auto engineOwner = std::make_unique<acustra::AcustraEngine>();
                auto& engine = *engineOwner;
                auto referenceOwner = std::make_unique<acustra::AcustraEngine>();
                auto& reference = *referenceOwner;
                for (auto* instrument : { &engine, &reference })
                {
                    instrument->prepare(rate, blockSize);
                    if (held)
                    {
                        instrument->noteOn(52, 0.35f);
                        instrument->process(left.data(), right.data(), 2400);
                    }
                    instrument->setSustainPedal(sustain);
                }
                engine.noteOn(52, 0.85f, 1, delay);
                if (!held)
                {
                    // The established single-note cancellation is an
                    // independent reference for a not-yet-sounding string.
                    reference.noteOn(52, 0.85f, 1, delay);
                    reference.noteOff(52);
                }
                else
                    reference.allNotesOff();
                engine.allNotesOff();
                compare(engine, reference,
                        "All Notes Off retained a queued attack" + at
                            + (held ? ", held" : ", fresh")
                            + (sustain ? ", pedal down" : ", pedal up"));
                if (held)
                    expect(std::any_of(referenceLeft.begin(), referenceLeft.end(),
                                       [](float sample) { return sample != 0.0f; }),
                           "the cancellation reference lost its existing vibration" + at);
                else
                    expect(std::all_of(left.begin(), left.end(),
                                      [](float sample) { return sample == 0.0f; }),
                           "a cancelled fresh attack sounded" + at);
                if (sustain)
                {
                    engine.setSustainPedal(false);
                    reference.setSustainPedal(false);
                    compare(engine, reference,
                            "pedal release revived a cancelled attack" + at);
                }
            }

    }
}

void testStrumTimingFollowsThePickAcrossTheStrings()
{
    // The k-th string a strum reaches sounds k spacings later at the pick's
    // speed for that velocity: later strings later, harder strums faster,
    // and the whole sweep between the map's two endpoints.
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.prepare(sampleRate, blockSize);
    expect(engine.strumDelaySamples(0, 0.5f) == 0,
           "the first string of a strum did not sound at once");
    for (const float velocity : { 0.1f, 0.5f, 1.0f })
        for (int rank = 1; rank < 6; ++rank)
            expect(engine.strumDelaySamples(rank, velocity)
                       > engine.strumDelaySamples(rank - 1, velocity),
                   "a later string of a strum did not sound later");
    for (int rank = 1; rank < 6; ++rank)
        expect(engine.strumDelaySamples(rank, 1.0f)
                   < engine.strumDelaySamples(rank, 0.2f),
               "a harder strum did not cross the strings faster");
    const double softest = engine.strumDelaySamples(5, 0.0f) / sampleRate;
    const double hardest = engine.strumDelaySamples(5, 1.0f) / sampleRate;
    // 0.51 to 2.46 m/s, GuitarSet's comping tracks' pooled 10-90% traversal
    // speed (Tools/MeasureStrums.py); five 10.8 mm gaps at each endpoint.
    expect(std::abs(softest - 0.10588) < 0.004 && std::abs(hardest - 0.02195) < 0.002,
           "the strum's endpoints moved from the measured map");
}

void testRepeatedStrumsCrossTheStringsLikeRepeatedRealStrums()
{
    // GuitarSet's comping tracks (Tools/MeasureStrums.py), consecutive-rank
    // inter-string intervals ordered along the direction of travel, put
    // 63.3% of 1945 real intervals inside the measured 0.51-2.46 m/s
    // traversal-speed band with 0.4% running the pick backwards
    // (interval <= 0, a later string sounding before an earlier one). This
    // is the actual gate on the strum's timing, not the summed-stroke span
    // the timing spread is built from: it fails if strings are jittered
    // independently even when the span comes out right (independent jitter
    // of about a fifth of the inter-string gap scrambles the order well
    // before it broadens the span by the measured amount).
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    acustra::EngineParameters parameters;
    engine.setParameters(parameters);
    engine.prepare(sampleRate, blockSize);
    const std::array<int, 6> chord { 40, 47, 52, 55, 59, 64 };
    constexpr double spacing = 0.0108; // shipping steel saddle spacing
    constexpr int strokesPerVelocity = 200;
    int inBand = 0, nonMonotonic = 0, total = 0;
    for (const float velocity : { 0.25f, 0.60f, 0.95f })
    {
        for (int r = 0; r < strokesPerVelocity; ++r)
        {
            const auto delays = acustra::AcustraEngineTestAccess::strumSchedule(
                engine, chord, velocity);
            for (std::size_t i = 1; i < delays.size(); ++i)
            {
                ++total;
                const int intervalSamples = delays[i] - delays[i - 1];
                if (intervalSamples <= 0)
                {
                    ++nonMonotonic;
                    continue;
                }
                const double speed = spacing
                    / (static_cast<double>(intervalSamples) / sampleRate);
                if (speed >= 0.51 && speed <= 2.46)
                    ++inBand;
            }
            for (const int note : chord)
                engine.noteOff(note, 1);
            engine.reset(); // frees every voice without waiting out its tail
        }
    }
    expect(nonMonotonic == 0,
           "a strum's own strings crossed order -- the pick reversed mid-stroke");
    const double inBandFraction = static_cast<double>(inBand) / static_cast<double>(total);
    expect(inBandFraction > 0.60,
           "fewer than GuitarSet's own 63.3% of consecutive-rank intervals "
           "landed inside the measured traversal-speed band");
}

void testRepeatedStrumsVaryLikeRepeatedRealStrums()
{
    // GuitarSet's comping tracks (Tools/MeasureStrums.py), pooled over runs
    // of >=3 repeats of one chord and direction on the hex-pickup channels,
    // put a stroke's own span deviation at a 36.5% std relative to its
    // run's own mean span, a repeated string's own level deviation at a
    // 4.47 dB std, and two adjacent real strokes' raw first-250-ms
    // correlation at a mean of -0.02 (min -0.51, max 0.46) rather than
    // phase-locked.
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    acustra::EngineParameters parameters;
    engine.setParameters(parameters);
    engine.prepare(sampleRate, blockSize);
    engine.setParameters(parameters);

    // Per-string level spread: one string, repeatedly plucked as a strum
    // member (so it draws initialisePluck's strumLevelGain), damped between
    // repeats. Unlike the summed six-string chord peak, this is exactly the
    // same-channel quantity GuitarSet's 4.47 dB figure measures.
    constexpr int levelRepeats = 40;
    constexpr int levelWindowSamples = static_cast<int>(0.040 * sampleRate);
    std::vector<float> left(static_cast<std::size_t>(levelWindowSamples));
    std::vector<float> right(left.size());
    std::array<double, levelRepeats> stringPeakDb {};
    for (int r = 0; r < levelRepeats; ++r)
    {
        engine.beginStrum();
        engine.noteOn(52, 0.84f, 1, 0, true);
        double peak = 0.0;
        for (int done = 0; done < levelWindowSamples; )
        {
            const int n = std::min(blockSize, levelWindowSamples - done);
            engine.process(left.data(), right.data(), n);
            for (int s = 0; s < n; ++s)
                peak = std::max(peak, static_cast<double>(
                    std::fabs(left[static_cast<std::size_t>(s)])
                    + std::fabs(right[static_cast<std::size_t>(s)])));
            done += n;
        }
        stringPeakDb[static_cast<std::size_t>(r)] = 20.0 * std::log10(std::max(peak, 1e-9));
        engine.noteOff(52, 1);
        engine.reset();
    }
    const auto meanOf = [] (const auto& values)
    {
        double sum = 0.0;
        for (double v : values) sum += v;
        return sum / static_cast<double>(values.size());
    };
    const auto stdOf = [&] (const auto& values)
    {
        const double mean = meanOf(values);
        double variance = 0.0;
        for (double v : values) variance += (v - mean) * (v - mean);
        return std::sqrt(variance / static_cast<double>(values.size()));
    };
    const double levelStd = stdOf(stringPeakDb);
    // Sampling error on a 40-draw std of the measured 4.47 dB figure is a
    // few tenths of a dB; a 2 dB half-width around it is generous.
    expect(std::abs(levelStd - 4.47) < 2.0,
           "one string's own level spread across repeated strums moved "
           "away from GuitarSet's measured 4.47 dB std");

    // Stroke span and same-direction correlation: eight repeats of one
    // chord, hand-damped between strokes like the prior single-note
    // measurement.
    const std::array<int, 6> chord { 40, 47, 52, 55, 59, 64 };
    const float velocity = 0.84f;
    constexpr int repeats = 8;
    constexpr int strokeSamples = static_cast<int>(0.3 * sampleRate);
    constexpr int quietSamples = static_cast<int>(1.0 * sampleRate);
    left.assign(static_cast<std::size_t>(std::max(strokeSamples, quietSamples)), 0.0f);
    right.assign(left.size(), 0.0f);
    std::array<double, repeats> spanMs {};
    std::vector<std::vector<float>> strokeSum(repeats);

    for (int r = 0; r < repeats; ++r)
    {
        const auto delays = acustra::AcustraEngineTestAccess::strumSchedule(
            engine, chord, velocity);
        const int maxDelay = *std::max_element(delays.begin(), delays.end());
        const int minDelay = *std::min_element(delays.begin(), delays.end());
        spanMs[static_cast<std::size_t>(r)] =
            static_cast<double>(maxDelay - minDelay) / sampleRate * 1000.0;

        std::vector<float> sum(static_cast<std::size_t>(strokeSamples));
        for (int done = 0; done < strokeSamples; )
        {
            const int n = std::min(blockSize, strokeSamples - done);
            engine.process(left.data(), right.data(), n);
            for (int s = 0; s < n; ++s)
                sum[static_cast<std::size_t>(done + s)] = left[static_cast<std::size_t>(s)]
                    + right[static_cast<std::size_t>(s)];
            done += n;
        }
        strokeSum[static_cast<std::size_t>(r)] = std::move(sum);

        for (const int note : chord)
            engine.noteOff(note, 1);
        for (int done = 0; done < quietSamples; )
        {
            const int n = std::min(blockSize, quietSamples - done);
            engine.process(left.data(), right.data(), n);
            done += n;
        }
    }

    const double spanStd = stdOf(spanMs);
    expect(spanStd > 3.0,
           "repeated strums scheduled with no resolvable timing spread");
    expect(spanStd < 40.0,
           "repeated strums' timing spread ran far past GuitarSet's measured span variability");

    // Same-direction stroke correlation, matched to the real-corpus method
    // (Tools/MeasureStrums.py): raw-sample Pearson correlation of the first
    // 250 ms, summed across strings, between adjacent repeats.
    const int corrWindow = std::min(strokeSamples, static_cast<int>(0.250 * sampleRate));
    std::vector<double> correlations;
    for (int r = 0; r + 1 < repeats; ++r)
    {
        const auto& a = strokeSum[static_cast<std::size_t>(r)];
        const auto& b = strokeSum[static_cast<std::size_t>(r + 1)];
        double meanA = 0.0, meanB = 0.0;
        for (int s = 0; s < corrWindow; ++s)
        {
            meanA += a[static_cast<std::size_t>(s)];
            meanB += b[static_cast<std::size_t>(s)];
        }
        meanA /= corrWindow; meanB /= corrWindow;
        double numerator = 0.0, varianceA = 0.0, varianceB = 0.0;
        for (int s = 0; s < corrWindow; ++s)
        {
            const double x = a[static_cast<std::size_t>(s)] - meanA;
            const double y = b[static_cast<std::size_t>(s)] - meanB;
            numerator += x * y;
            varianceA += x * x;
            varianceB += y * y;
        }
        correlations.push_back(
            numerator / std::sqrt(varianceA * varianceB + 1e-30));
    }
    const double meanCorrelation = meanOf(correlations);
    // The shipping engine's own same-direction repeats correlated 0.93 to
    // 0.995 (decisions.md, 2026-09-02): still phase-locked, since a
    // deterministic model with no timing spread repeats its own waveform
    // almost exactly. This bound is that PRIOR ENGINE figure, not
    // GuitarSet's own corpus mean (-0.02, decisions.md records both): a
    // handful of noisy real pairs centred near zero is not a stable target
    // to assert below, so the gate here is only that the engine is no
    // longer phase-locked the way it was.
    expect(meanCorrelation < 0.93,
           "repeated strums stayed as phase-locked as the unvaried engine");
}

void testNoTwoPlucksLandInTheSamePlace()
{
    // Each pluck draws its own point within the take-to-take spread the
    // recordings show, and stays inside it - also where the hand's distance
    // from the bridge reaches the band's mid-string limit (a Finger high on
    // the neck, a Thumb from the low frets up), where the draw used to be
    // clamped away and every pluck landed on exactly the same point.
    struct Case { int note; acustra::PickingTechnique picking; };
    for (const Case pluck : { Case { 52, acustra::PickingTechnique::Finger },
                              Case { 79, acustra::PickingTechnique::Finger },
                              Case { 76, acustra::PickingTechnique::Thumb } })
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        acustra::EngineParameters parameters;
        parameters.picking = pluck.picking;
        engine.setParameters(parameters);
        engine.prepare(sampleRate, blockSize);
        std::vector<float> left(static_cast<std::size_t>(blockSize));
        std::vector<float> right(static_cast<std::size_t>(blockSize));
        std::vector<double> points;
        for (int take = 0; take < 6; ++take)
        {
            engine.noteOn(pluck.note, 0.8f);
            points.push_back(acustra::AcustraEngineTestAccess::lastPluckPoint(engine));
            for (int block = 0; block < 40; ++block)
                engine.process(left.data(), right.data(), blockSize);
            engine.noteOff(pluck.note);
            for (int block = 0; block < 400; ++block)
                engine.process(left.data(), right.data(), blockSize);
        }
        const auto [lowest, highest] = std::minmax_element(points.begin(), points.end());
        const std::string name = "MIDI " + std::to_string(pluck.note);
        expect(*highest - *lowest <= 0.0401 && *lowest > 0.0 && *highest <= 0.46,
               "a pluck of " + name + " landed outside the measured take-to-take spread");
        expect(*highest - *lowest > 1.0e-4,
               "six plucks of " + name + " all landed in the same place");
    }
}

void testNoteOffDoesNotCreateANewAttack()
{
    const auto renderNote = [] (std::initializer_list<int> notes,
                                bool release, double rate)
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        acustra::EngineParameters parameters;
        engine.setParameters(parameters);
        engine.prepare(rate, blockSize);
        engine.setParameters(parameters);
        const int total = static_cast<int>(2.0 * rate);
        const int onAt = static_cast<int>(0.2 * rate);
        const int offAt = static_cast<int>(1.0 * rate);
        Audio result { std::vector<float>(static_cast<std::size_t>(total)),
                       std::vector<float>(static_cast<std::size_t>(total)) };
        int rendered = 0;
        const auto renderTo = [&] (int target)
        {
            while (rendered < target)
            {
                const int count = std::min(blockSize, target - rendered);
                engine.process(result.left.data() + rendered,
                               result.right.data() + rendered, count);
                rendered += count;
            }
        };
        renderTo(onAt);
        int index = 0;
        for (const int note : notes)
        {
            engine.noteOn(note, 0.7f);
            renderTo(onAt + static_cast<int>(0.028 * rate) * ++index);
        }
        renderTo(offAt);
        if (release)
            for (const int note : notes)
                engine.noteOff(note);
        renderTo(total);
        return result;
    };

    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const auto& notes : std::vector<std::vector<int>> {
                 { 43 }, { 48 }, { 40, 47, 52, 56, 59, 64 } })
        {
            std::initializer_list<int> list {};
            std::vector<int> copy = notes;
            const auto released = [&]
            {
                auto dummyOwner = std::make_unique<acustra::AcustraEngine>();
                auto& dummy = *dummyOwner; (void) dummy; (void) list;
                return Audio {};
            };
            (void) released;
            Audio held;
            Audio lifted;
            if (copy.size() == 1)
            {
                held = renderNote({ copy[0] }, false, rate);
                lifted = renderNote({ copy[0] }, true, rate);
            }
            else
            {
                held = renderNote(
                    { copy[0], copy[1], copy[2], copy[3], copy[4], copy[5] },
                    false, rate);
                lifted = renderNote(
                    { copy[0], copy[1], copy[2], copy[3], copy[4], copy[5] },
                    true, rate);
            }
            const int offAt = static_cast<int>(1.0 * rate);
            const int window = static_cast<int>(0.05 * rate);
            const double before = peak(held, offAt - window, offAt);
            const double after = peak(lifted, offAt, offAt + window);
            double removed = 0.0;
            for (int sample = offAt; sample < offAt + window; ++sample)
            {
                const auto i = static_cast<std::size_t>(sample);
                removed = std::max(removed, static_cast<double>(std::abs(
                    0.5 * ((lifted.left[i] - held.left[i])
                         + (lifted.right[i] - held.right[i])))));
            }
            const std::string label = std::string("steel")
                + (copy.size() == 1 ? " note " + std::to_string(copy[0])
                                    : " chord")
                + " at " + std::to_string(static_cast<int>(rate));
            expect(before > 1.0e-4,
                   label + ": the held note is audible before the release");
            // A hand-damped note leaves what it drove ringing: in the
            // two-way junction a G2's third partial keeps the idle D
            // string's second sounding through the anchor's 306 Hz
            // resonance, peaking 35 ms after the note-off at 1.4x the
            // held peak. These peaks are an audible-attack heuristic,
            // not a measure of physical energy or a passivity proof.
            // The held envelope can already be rising after the event,
            // so include its same-time 5 ms peak in the reference. The
            // 1.05 ceiling still catches a release-induced attack; the
            // later 50 ms allowance still covers what keeps ringing.
            const double atRelease = peak(lifted, offAt,
                offAt + static_cast<int>(0.005 * rate));
            const double heldAtRelease = peak(held, offAt,
                offAt + static_cast<int>(0.005 * rate));
            const double reference = std::max(before, heldAtRelease);
            expect(atRelease <= 1.05 * reference,
                   label + ": the release stepped " + std::to_string(atRelease)
                   + " over the held reference " + std::to_string(reference));
            expect(after <= 1.5 * before,
                   label + ": note-off peaks " + std::to_string(after)
                   + " over a held " + std::to_string(before));
            expect(removed <= 2.0 * before,
                   label + ": the release adds " + std::to_string(removed)
                   + " against a held " + std::to_string(before));
        }
}

// The steel guitar's measured banks: the body arrays are sized to the
// largest bank, the steel blend (SteelBodyBlend.h), and the bridge bank must
// stay positive real.
void testTheSteelBanksFillTheirSlotsAndStayPassive()
{
    using Access = acustra::AcustraEngineTestAccess;

    // The surplus slots a bank leaves must be silent, not stale.
    auto steelEngineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& steelEngine = *steelEngineOwner;
    steelEngine.prepare(48000.0, 64);
    expect(Access::bodyModesInUse(steelEngine)
               >= static_cast<int>(acustra::detail::measuredSteelBodyModes.size()),
           "steel does not play g21's whole bank");
    for (int index = Access::bodyModesInUse(steelEngine);
         index < Access::bodyModeCapacity; ++index)
        expect(Access::bodyResidueOf(steelEngine, index) == 0.0,
               "a surplus body slot kept another bank's mode");

    // The bridge bank must stay positive real. With two degrees of freedom
    // that is a matrix condition: a string at lever arm u sees
    // heave + 2u*cross + u^2*rock, so the residue matrix
    // [[heave, cross], [cross, rock]] must be positive semidefinite for every
    // u at once. This guards a hand-edited header, not the fit.
    int rocking = 0;
    for (const auto& mode : acustra::detail::measuredSteelBridgeModes)
    {
        if (mode.rock > 0.0f)
            ++rocking;
        expect(mode.q > 0.0f && mode.frequency >= 60.0f
                   && mode.frequency <= 10000.0f,
               "the steel bridge bank left the fit's range");
        expect(mode.heave >= 0.0f && mode.rock >= 0.0f
                   && mode.cross * mode.cross
                          <= mode.heave * mode.rock * 1.000001f + 1.0e-12f,
               "the steel bridge bank has an indefinite residue matrix");
        expect(mode.heave > 0.0f || mode.rock > 0.0f,
               "the steel bridge bank has an empty mode");
    }
    // A bank with no rocking residue at all is a one-degree-of-freedom
    // bridge, and bridgePhaseDelay's 2x2 adjugate collapses to zero on it
    // and falls back to the scalar port. The shipped bank carries rocking
    // modes (30 of 47), so the shipped tuning compensation is always the
    // two-point one.
    expect(rocking > 0, "the steel bridge bank carries no rocking mode");

    // Every string's own mobility must then be a positive conductance at its
    // own modes, which is what the waveguide termination's passivity rests
    // on, and the two ends must be the two measured driving points: the
    // treble impact at u = +1 and the bass impact at u = -1.
    for (const auto& mode : acustra::detail::measuredSteelBridgeModes)
        for (int string = 0; string < 6; ++string)
        {
            const float arm = 0.5f * (static_cast<float>(string) - 2.5f);
            expect(mode.heave + 2.0f * arm * mode.cross
                       + arm * arm * mode.rock >= -1.0e-9f,
                   "a string's own bridge residue went negative");
        }
}

// A plectrum lets the string go as the string slides round its edge, which
// takes the edge radius over the speed the string's own held force gives it
// (AcustraEngine::plectrumSlipPole), so a soft stroke is released slowly and
// dark and a hard one fast and bright. Finger and Thumb never read the edge.
void testAPlectrumSlipsOffItsEdgeFasterWhenHarder()
{
    using Access = acustra::AcustraEngineTestAccess;
    using acustra::PickingTechnique;
    const auto shipping = acustra::fittedPhysicalCalibration;
    expect(shipping.pickEdgeRadiusMetres > 0.0f,
           "the shipping plectrum has no edge to slide round");
    auto instant = shipping;
    instant.pickEdgeRadiusMetres = 0.0f;
    const auto partialPower = [] (const std::vector<double>& line, int harmonic)
    {
        std::complex<double> sum {};
        for (std::size_t sample = 0; sample < line.size(); ++sample)
            sum += line[sample] * std::polar(1.0, -2.0 * std::numbers::pi
                * harmonic * static_cast<double>(sample)
                / static_cast<double>(line.size()));
        return std::norm(sum);
    };
    const auto balanceDb = [&] (const std::vector<double>& line)
    {
        double low = 0.0, high = 0.0;
        for (int harmonic = 1; harmonic <= 4; ++harmonic)
            low += partialPower(line, harmonic);
        for (int harmonic = 5; harmonic <= 12; ++harmonic)
            high += partialPower(line, harmonic);
        return 10.0 * std::log10(std::max(high, 1.0e-40) / std::max(low, 1.0e-40));
    };
    for (const int midi : { 40, 52, 64, 76 })
    {
        for (const auto picking : { PickingTechnique::Finger, PickingTechnique::Thumb })
        {
            const auto a = Access::pluckedLine(shipping, picking, midi, 0.5f);
            const auto b = Access::pluckedLine(instant, picking, midi, 0.5f);
            expect(a == b, "the plectrum's edge reached a finger or thumb pluck");
        }
        double drop[2] {};
        int index = 0;
        for (const float velocity : { 0.15f, 0.9f })
        {
            const auto slipped = Access::pluckedLine(shipping, PickingTechnique::Pick, midi, velocity);
            const auto sharp = Access::pluckedLine(instant, PickingTechnique::Pick, midi, velocity);
            expect(slipped.size() == sharp.size() && slipped.size() > 8,
                   "the slip changed the line's length");
            double peak = 0.0;
            for (const double value : slipped)
                peak = std::max(peak, std::abs(value));
            // The loop's empty filter states need the line at rest at the
            // bridge, as the instant pluck is.
            expect(std::abs(slipped.front()) <= 1.0e-6 * peak,
                   "the slipped line does not start at rest at the bridge");
            drop[index++] = balanceDb(slipped) - balanceDb(sharp);
            // A first-order low-pass with unit DC gain: the fundamental of a
            // loud stroke on the low strings is barely touched. With a
            // release-velocity share (0.582 v^0.859 since 2026-09-28) the
            // share is re-solved against the slipped displacement's energy,
            // which lifts a loud H1 by 0.002-0.003 dB (without the share it
            // falls 0.007), so the bound allows a hundredth of a decibel.
            if (velocity > 0.5f && midi <= 52)
            {
                const double h1 = 10.0 * std::log10(partialPower(slipped, 1)
                    / partialPower(sharp, 1));
                expect(h1 > -1.0 && h1 <= 0.01,
                       "the slip did more than low-pass a loud fundamental");
            }
        }
        std::cout << "Acustra plectrum slip H5-H12/H1-H4 change at MIDI " << midi
                  << ": soft " << drop[0] << " dB, loud " << drop[1] << " dB\n";
        // Measured at the 0.15 mm edge with no release share: soft
        // -3.3/-3.3/-3.1/-8.4 dB and loud -0.34/-0.34/-0.30/-1.7 dB at MIDI
        // 40/52/64/76. At the 0.116 mm edge and the 0.582 v^0.859 share
        // chosen by ear on 2026-09-28: soft -2.05/-2.05/-1.92/-4.99 and loud
        // +0.06/+0.04/+0.01/-0.21, the share's re-solve against the slipped
        // displacement taking back what the fast loud slip removes (with the
        // share at zero, loud -0.21/-0.21/-0.18/-1.07, soft 2.2-6.3 dB
        // darker). The bound keeps the soft stroke darker than the instant
        // release, the loud one within a tenth of a decibel of it or darker,
        // and the soft at least 1.5 dB darker than the loud.
        expect(drop[0] < 0.0 && drop[1] < 0.1 && drop[0] < drop[1] - 1.5,
               "a soft plectrum stroke was not released darker than a hard one");
    }
}

// A string does not leave a plectrum's tip from rest (FittedPhysicalData.h).
// Under Pick the written line carries, beside the fitted displacement, a
// velocity over the contact width whose energy is the fitted share of the
// displacement's, in the same time frame the calibration was fitted in;
// Finger and Thumb never read the three plectrum values.
void testAPlectrumReleasesWithVelocity()
{
    using Access = acustra::AcustraEngineTestAccess;
    using acustra::PickingTechnique;
    auto plain = acustra::fittedPhysicalCalibration;
    plain.pickReleaseVelocityShare = 0.0f;
    plain.pickReleaseVelocityExponent = 0.0f; // the share applies at every velocity
    plain.pickTransientGain = 0.0f;
    auto shared = plain;
    shared.pickReleaseVelocityShare = 1.0f;
    auto traceOnly = plain;
    traceOnly.pickReleaseVelocityShare = 1.0e-10f;

    // In a lossless line every sample-to-sample difference carries the same
    // energy whichever wave it belongs to, so the share is read straight
    // from the summed squared differences around the loop.
    const auto lineEnergy = [] (const std::vector<double>& line)
    {
        double energy = 0.0;
        for (std::size_t sample = 0; sample < line.size(); ++sample)
        {
            const double step = line[sample]
                - line[(sample + line.size() - 1) % line.size()];
            energy += step * step;
        }
        return energy;
    };
    const auto partialDb = [] (const std::vector<double>& line, int harmonic)
    {
        std::complex<double> sum {};
        for (std::size_t sample = 0; sample < line.size(); ++sample)
            sum += line[sample] * std::polar(1.0, -2.0 * std::numbers::pi
                * harmonic * static_cast<double>(sample)
                / static_cast<double>(line.size()));
        return 20.0 * std::log10(std::max(std::abs(sum), 1.0e-30));
    };
    for (const int midi : { 40, 52, 64 })
    {
        const auto legacy = Access::pluckedLine(plain, PickingTechnique::Pick, midi, 0.7f);
        const auto trace = Access::pluckedLine(traceOnly, PickingTechnique::Pick, midi, 0.7f);
        const auto released = Access::pluckedLine(shared, PickingTechnique::Pick, midi, 0.7f);
        expect(legacy.size() > 8 && legacy.size() == trace.size()
                   && legacy.size() == released.size(),
               "the plectrum write changed the line written at the pluck");
        // The fitted frame: with the velocity switched off the new write is
        // the legacy pluck, sample for sample.
        double peak = 0.0, deviation = 0.0;
        for (std::size_t sample = 0; sample < legacy.size(); ++sample)
        {
            peak = std::max(peak, std::abs(legacy[sample]));
            deviation = std::max(deviation, std::abs(legacy[sample] - trace[sample]));
        }
        expect(deviation < 1.0e-4 * peak,
               "the plectrum's rest frame is not the fitted pluck's frame");
        const double ratio = lineEnergy(released) / lineEnergy(legacy);
        expect(std::abs(ratio - 2.0) < 0.02,
               "a unit release share did not add the displacement's own energy");
        // The velocity's partials fall 6 dB/octave slower than the
        // displacement's, so the top of the band rises against the bottom.
        double lowLegacy = 0.0, lowReleased = 0.0, highLegacy = 0.0, highReleased = 0.0;
        for (int harmonic = 1; harmonic <= 3; ++harmonic)
        {
            lowLegacy += partialDb(legacy, harmonic) / 3.0;
            lowReleased += partialDb(released, harmonic) / 3.0;
        }
        for (int harmonic = 8; harmonic <= 12; ++harmonic)
        {
            highLegacy += partialDb(legacy, harmonic) / 5.0;
            highReleased += partialDb(released, harmonic) / 5.0;
        }
        expect((highReleased - lowReleased) - (highLegacy - lowLegacy) > 1.0,
               "the release velocity did not tilt the line toward its upper partials");
    }

    // Finger and Thumb never read the plectrum's values; Pick does.
    auto loud = plain;
    loud.pickReleaseVelocityShare = 2.0f;
    loud.pickReleaseVelocityExponent = 2.0f;
    loud.pickTransientGain = 8.0f;
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        acustra::EngineParameters parameters;
        for (const auto technique : { PickingTechnique::Finger, PickingTechnique::Thumb })
        {
            parameters.picking = technique;
            const auto before = renderAtRate(parameters, 52, 0.8f, 0.3, rate, 64, true, plain);
            const auto after = renderAtRate(parameters, 52, 0.8f, 0.3, rate, 64, true, loud);
            expect(before.left == after.left && before.right == after.right,
                   "a plectrum value reached a finger or thumb pluck");
        }
        parameters.picking = PickingTechnique::Pick;
        const auto before = renderAtRate(parameters, 52, 0.8f, 0.3, rate, 64, true, plain);
        const auto after = renderAtRate(parameters, 52, 0.8f, 0.3, rate, 64, true, loud);
        expect(normalisedDifference(before, after) > 0.01,
               "the plectrum values did not reach a picked note");
        // Headroom over the whole note; growth read on the ring after the
        // attack (from 50 ms), where a runaway would keep rising. The
        // attack's own peak is the 8x broadband contact burst, which the
        // body radiates up to 18 kHz since its radiation was continued above
        // the fitted band (Docs/decisions.md, 2026-09-30): 3.4-5.1x the
        // plain pluck's there, against 1.8-2.6x when that band was 20-40 dB
        // down, while the ring stays within 1.35x at every rate.
        double peakBefore = 0.0, peakAfter = 0.0, ringBefore = 0.0, ringAfter = 0.0;
        const auto attack = static_cast<std::size_t>(0.05 * rate);
        bool finite = true;
        for (std::size_t sample = 0; sample < after.left.size(); ++sample)
        {
            finite = finite && std::isfinite(after.left[sample]) && std::isfinite(after.right[sample]);
            peakBefore = std::max(peakBefore, static_cast<double>(std::abs(before.left[sample])));
            peakAfter = std::max(peakAfter, static_cast<double>(std::abs(after.left[sample])));
            if (sample >= attack)
            {
                ringBefore = std::max(ringBefore, static_cast<double>(std::abs(before.left[sample])));
                ringAfter = std::max(ringAfter, static_cast<double>(std::abs(after.left[sample])));
            }
        }
        expect(finite && peakAfter < 1.0 && ringAfter < 2.0 * ringBefore,
               "an extreme plectrum setting left headroom or blew up");
    }

    // Brightness grows with dynamics under Pick: the loud-over-soft rise of
    // the upper partials against the lower ones, read on the excitation alone
    // (bridge coupling off), exceeds the Finger law's on the same notes. It
    // is read where the release law was built, the plectrum 30 mm from the
    // bridge (steel.pluckDistanceScale 0.888): at the 60 mm it has followed
    // the Finger to since 2026-09-25 the same share lifts the rise by 0.6 dB
    // against the pick released from rest, and the refitted share is
    // 0.0117, so the effect is not what ships.
    plain.steel.pluckDistanceScale = 0.88819512f;
    auto fitted = plain;
    fitted.pickReleaseVelocityShare = 1.5f;
    fitted.pickReleaseVelocityExponent = 2.0f;
    fitted.pickTransientGain = 2.0f;
    const auto balance = [&] (PickingTechnique technique, int midi, float velocity,
                              const acustra::PhysicalCalibration& calibration)
    {
        acustra::EngineParameters parameters;
        parameters.picking = technique;
        const auto audio = renderAtRate(parameters, midi, velocity, 0.3, 48000.0, 64,
                                        false, calibration);
        const double f0 = 440.0 * std::exp2((midi - 69) / 12.0);
        std::array<double, 12> levels {};
        for (int harmonic = 1; harmonic <= 12; ++harmonic)
        {
            // The steel attack-pitch glide moves a loud note's partials off
            // exact multiples of f0 inside this window, so each level is the
            // strongest line within the scorer's own +/-65 cent search.
            double strongest = 0.0;
            for (int step = -8; step <= 8; ++step)
            {
                const double frequency = harmonic * f0
                    * std::exp2(65.0 * step / (8.0 * 1200.0));
                std::complex<double> sum {};
                for (int sample = 3840; sample < 12000; ++sample)
                    sum += static_cast<double>(audio.left[static_cast<std::size_t>(sample)])
                         * std::polar(1.0, -2.0 * std::numbers::pi * frequency
                                           * sample / 48000.0);
                strongest = std::max(strongest, std::abs(sum));
            }
            levels[static_cast<std::size_t>(harmonic - 1)]
                = 20.0 * std::log10(std::max(strongest, 1.0e-30));
        }
        double low = 0.0, high = 0.0;
        for (int harmonic = 1; harmonic <= 4; ++harmonic)
            low += levels[static_cast<std::size_t>(harmonic - 1)] / 4.0;
        for (int harmonic = 5; harmonic <= 12; ++harmonic)
            high += levels[static_cast<std::size_t>(harmonic - 1)] / 8.0;
        return high - low;
    };
    // The comparison is the same plectrum without its release velocity: the
    // Pick contact's own narrower width and the shared Touch law already
    // give a rise, and the release has to add to it.
    double pickRise = 0.0, restRise = 0.0, fingerRise = 0.0;
    for (const int midi : { 45, 52, 59 })
    {
        pickRise += balance(PickingTechnique::Pick, midi, 1.0f, fitted)
                  - balance(PickingTechnique::Pick, midi, 0.2f, fitted);
        restRise += balance(PickingTechnique::Pick, midi, 1.0f, plain)
                  - balance(PickingTechnique::Pick, midi, 0.2f, plain);
        fingerRise += balance(PickingTechnique::Finger, midi, 1.0f, fitted)
                    - balance(PickingTechnique::Finger, midi, 0.2f, fitted);
    }
    std::cout << "Acustra plectrum loud-over-soft upper-partial rise: pick "
              << pickRise / 3.0 << " dB, from rest " << restRise / 3.0
              << " dB, finger " << fingerRise / 3.0 << " dB\n";
    expect(pickRise > restRise + 2.0,
           "a picked note did not brighten with dynamics beyond its release from rest");
}

// The Pick release's share was chosen by ear at 48 kHz, and its hump is a
// continuous wave the host grid only samples, so a picked note's partials
// must not depend on the rate. Solved on the host grid, the summed squared
// differences it reads scale differently with the grid for the rest and the
// velocity, and the hump's 0-6 kHz partials against the rest's fell by up
// to 4.8 dB at 96 kHz and 7.6 dB at 192 kHz, and rose by up to 0.95 dB at
// 44.1 kHz; solving it on the note's 48 kHz period at 48 kHz's slip keeps
// them within 0.1 dB (writePickRelease). Read on the written line, the
// hump alone (the line less the same pluck with a vanishing share) against
// the rest.
void testAPickReleaseKeepsItsHumpAcrossRates()
{
    using Access = acustra::AcustraEngineTestAccess;
    using acustra::PickingTechnique;
    const auto shipping = acustra::fittedPhysicalCalibration;
    auto trace = shipping;
    trace.pickReleaseVelocityShare = 1.0e-10f;
    const auto humpDb = [&] (double rate, int midi, float velocity, float touch)
    {
        const auto line = Access::pluckedLine(shipping, PickingTechnique::Pick,
                                              midi, velocity, rate, touch);
        const auto rest = Access::pluckedLine(trace, PickingTechnique::Pick,
                                              midi, velocity, rate, touch);
        expect(line.size() > 8 && line.size() == rest.size(),
               "a picked line changed length with its release share");
        const double f0 = 440.0 * std::exp2((midi - 69) / 12.0);
        const int top = std::max(1, static_cast<int>(6000.0 / f0));
        double humpEnergy = 0.0, restEnergy = 0.0;
        for (int harmonic = 1; harmonic <= top; ++harmonic)
        {
            std::complex<double> hump {}, still {};
            for (std::size_t sample = 0; sample < line.size(); ++sample)
            {
                const auto turn = std::polar(1.0, -2.0 * std::numbers::pi * harmonic
                    * static_cast<double>(sample) / static_cast<double>(line.size()));
                hump += (line[sample] - rest[sample]) * turn;
                still += rest[sample] * turn;
            }
            humpEnergy += std::norm(hump);
            restEnergy += std::norm(still);
        }
        return 10.0 * std::log10(std::max(humpEnergy, 1.0e-300)
                                 / std::max(restEnergy, 1.0e-300));
    };
    double worst = 0.0;
    for (const int midi : { 45, 59, 69, 76 })
        for (const float velocity : { 0.3f, 1.0f })
            for (const float touch : { 0.2f, 0.9f })
            {
                const double reference = humpDb(48000.0, midi, velocity, touch);
                expect(reference > -40.0 && reference < 0.0,
                       "the picked release carried no hump at 48 kHz");
                for (const double rate : { 44100.0, 96000.0, 192000.0 })
                {
                    const double change = humpDb(rate, midi, velocity, touch) - reference;
                    worst = std::max(worst, std::abs(change));
                    expect(std::abs(change) < 0.2,
                           "a picked note's release hump moved with the sample rate");
                }
            }
    std::cout << "Acustra pick release hump across 44.1-192 kHz: worst "
              << worst << " dB from 48 kHz\n";
}

// A Shape is the measured body's A0 and T1 re-coupled through Christensen and
// Vistisen's two-oscillator model for a published box, with the plate modes
// above T1 on the equal-thickness plate law. The anchor - the wide
// Dreadnought chosen by ear - must not move; the other shapes must land where
// an independent evaluation of the same model puts them.
void testBodyShapesFollowTheCoupledTopAndCavity()
{
    using Access = acustra::AcustraEngineTestAccess;
    using acustra::BodyShape;
    const auto calibration = acustra::fittedPhysicalCalibration;
    const auto body = [&] (BodyShape shape, int index)
    {
        return Access::configuredBody(calibration, index, shape);
    };

    // The anchor is the Dreadnought: the flamenca's bank, resolved over
    // 250 ms, under the wide transform, A0 at 90.82 * 98/107 * (1 + 0.018)
    // and T1, the bank's second mode, at 178.53 * 0.900 * (1 - 0.018 / sqrt(2)).
    const auto steelDread0 = body(BodyShape::Dreadnought, 0);
    const auto steelDread1 = body(BodyShape::Dreadnought, 1);
    expect(std::abs(steelDread0.frequency - 90.8203125 * (98.0 / 107.0) * 1.018) < 0.01,
           "the steel Dreadnought anchor moved its A0");
    expect(std::abs(steelDread1.frequency
                    - 178.532211 * 0.900 * (1.0 - 0.018 / std::sqrt(2.0))) < 0.01,
           "the steel Dreadnought anchor moved its T1");

    // Independently evaluated (Tools-free, in double precision from the same
    // published boxes) the coupled pair puts the steel shapes here; the
    // engine's single-precision path must agree within a fraction of a hertz.
    struct Expected { BodyShape shape; double a0, t1; };
    for (const auto& row : { Expected { BodyShape::Parlor, 111.34, 183.71 },
                             Expected { BodyShape::Auditorium, 95.72, 171.18 },
                             Expected { BodyShape::Jumbo, 76.09, 145.57 } })
    {
        const auto a0 = body(row.shape, 0);
        const auto t1 = body(row.shape, 1);
        expect(std::abs(a0.frequency - row.a0) < 0.5,
               "a steel shape's A0 is not where the coupled model puts it");
        expect(std::abs(t1.frequency - row.t1) < 0.5,
               "a steel shape's T1 is not where the coupled model puts it");
    }

    // A smaller box raises both modes and a larger one lowers them, and the
    // same ordering holds for the plate modes above T1.
    const int t1Index = 1;
    const int plateIndex = 9;
    double previousA0 = 1.0e9, previousT1 = 1.0e9, previousPlate = 1.0e9;
    for (const auto shape : { BodyShape::Parlor, BodyShape::Auditorium,
                              BodyShape::Dreadnought, BodyShape::Jumbo })
    {
        const double a0 = body(shape, 0).frequency;
        const double t1 = body(shape, t1Index).frequency;
        const double plate = body(shape, plateIndex).frequency;
        expect(a0 < previousA0 && t1 < previousT1 && plate < previousPlate,
               "a larger box did not lower A0, T1 and the plate modes");
        previousA0 = a0;
        previousT1 = t1;
        previousPlate = plate;
    }
    // The measured Q is retained by every shape.
    expect(std::abs(body(BodyShape::Parlor, t1Index).q
                    - body(BodyShape::Jumbo, t1Index).q) < 0.05,
           "a shape changed a body mode's measured Q");

    // The small box radiates its A0 more strongly per unit force (the piston
    // is smaller, so the same force is more cavity pressure) while its plate
    // modes radiate from less area; the large box the other way round. The
    // capture voicing (CaptureVoicingData.h) reads each mode at its own
    // frequency, which Shape moves, so the box's own radiation is compared
    // with the microphones' voicing divided back out.
    const auto radiated = [] (const auto& mode)
    {
        return mode.residue / Access::captureVoicing(mode.frequency);
    };
    const auto steelParlor0 = body(BodyShape::Parlor, 0);
    const auto steelJumbo0 = body(BodyShape::Jumbo, 0);
    const auto steelParlor9 = body(BodyShape::Parlor, 9);
    const auto steelDread9 = body(BodyShape::Dreadnought, 9);
    const auto steelJumbo9 = body(BodyShape::Jumbo, 9);
    expect(radiated(steelParlor0) > radiated(steelDread0)
               && radiated(steelJumbo0) < radiated(steelDread0),
           "A0 radiation did not follow the coupled model's residues");
    expect(radiated(steelParlor9) < radiated(steelDread9)
               && radiated(steelJumbo9) > radiated(steelDread9),
           "plate radiation did not scale with the plate area");
}

// Woodhouse (Acta Acustica 90 (2004) 945-965, Sec. 4.3) measures the two
// polarisations of a plucked string as a doublet split not by the body -- the
// measured 2x2 admittance matrix splits it by about 0.1 Hz -- but by an end
// correction at the terminations, the parallel polarisation running about
// 0.8 mm longer on 650 mm and so lower. What is pinned here is that the
// mechanism carries it as a LENGTH: the same 0.8 mm splits a stopped string
// wider than an open one, and it never moves the normal loop, which is the
// one the tuning is built on. The instrument ships it at zero, chosen by ear
// (Docs/decisions.md, 2026-09-24), so the shipped pair is exactly in tune.
void testTheNormalPolarisationIsTheHigherMemberByALength()
{
    using acustra::AcustraEngineTestAccess;
    const auto cents = [] (std::array<double, 2> delays)
    {
        return 1200.0 * std::log2(delays[1] / delays[0]);
    };
    auto measured = acustra::fittedPhysicalCalibration;
    measured.polarisationEndCorrectionMetres = 0.0008f;
    const auto shipped = measured.polarisationEndCorrectionMetres;
    const double openB = cents(AcustraEngineTestAccess::polarisationDelays(
        59, measured));
    const double openE = cents(AcustraEngineTestAccess::polarisationDelays(
        40, measured));
    const double stoppedB = cents(
        AcustraEngineTestAccess::polarisationDelays(71, measured));
    const double expected
        = 1200.0 * std::log2(1.0 + static_cast<double>(shipped) / 0.648);
    expect(openB > 0.0 && openE > 0.0,
           "the parallel polarisation is not the lower member of the pair");
    // The tolerance is float rounding of one product, not slack in the law.
    expect(std::abs(openB - expected) < 1.0e-3
               && std::abs(openE - expected) < 1.0e-3,
           "the open-string split is not the measured end correction over "
           "the scale length");
    expect(stoppedB > openB * 1.2,
           "a stopped string did not split wider than an open one, so the "
           "correction is not being carried as a length");

    const auto none = AcustraEngineTestAccess::polarisationDelays(59);
    expect(acustra::fittedPhysicalCalibration.polarisationEndCorrectionMetres
                   == 0.0f
               && none[0] == none[1],
           "the shipped zero end correction did not leave the two "
           "polarisations exactly in tune");
    auto corrected = AcustraEngineTestAccess::polarisationDelays(
        59, measured);
    expect(corrected[0] == none[0],
           "the end correction moved the normal loop, which carries the "
           "tuning");

    // The bound is one string diameter, the 0.82 mm nylon B string the
    // 0.8 mm was measured on: an out-of-range request is clamped, not taken.
    auto huge = acustra::fittedPhysicalCalibration;
    huge.polarisationEndCorrectionMetres = 0.01f;
    const double clamped = cents(AcustraEngineTestAccess::polarisationDelays(
        59, huge));
    expect(std::abs(clamped
                    - 1200.0 * std::log2(1.0 + 0.82e-3 / 0.648)) < 1.0e-3,
           "the end correction was not bounded at one string diameter");

    std::cout << "Acustra polarisation split: open B " << openB
              << " cents, open E " << openE
              << " cents, m71 " << stoppedB
              << " cents, normal loop unmoved\n";
}

// The polarisation parallel to the soundboard pushes the saddle crown
// sideways at its height over the top, which is a moment about the string's
// own axis: on a bridge whose rocking and moment radiation were measured it
// reaches the microphones and forms, with the normal polarisation, the doublet
// every guitar partial beats with. Where the measurement is scalar nothing
// can carry it and the plane would stay silent; every shipping bank is not.
void testTheParallelPolarisationRadiatesThroughTheRockingSaddle()
{
    using acustra::AcustraEngineTestAccess;
    using acustra::GuitarModel;
    const auto renderNote = [] (acustra::EngineParameters parameters,
                                int midiNote, bool silenceParallel)
    {
        auto engineOwner = std::make_unique<acustra::AcustraEngine>();
        auto& engine = *engineOwner;
        engine.setParameters(parameters);
        engine.prepare(sampleRate, blockSize);
        engine.noteOn(midiNote, 0.75f);
        if (silenceParallel)
            AcustraEngineTestAccess::silenceParallelPolarisation(engine);
        const int samples = static_cast<int>(2.5 * sampleRate);
        Audio audio { std::vector<float>(static_cast<std::size_t>(samples)),
                      std::vector<float>(static_cast<std::size_t>(samples)) };
        for (int offset = 0; offset < samples; offset += blockSize)
            engine.process(audio.left.data() + offset,
                           audio.right.data() + offset,
                           std::min(blockSize, samples - offset));
        return std::pair { audio,
                           AcustraEngineTestAccess::saddleHeightRatio(engine) };
    };
    const auto energy = [] (const std::vector<float>& signal, std::size_t from)
    {
        double sum = 0.0;
        for (std::size_t index = from; index < signal.size(); ++index)
            sum += static_cast<double>(signal[index]) * signal[index];
        return sum;
    };
    struct Case
    {
        GuitarModel model;
        bool radiates;
        const char* name;
    };
    for (const Case test : {
             Case { GuitarModel::Original, true, "Original" },
             Case { GuitarModel::Bellido1978, true, "Bellido" } })
    {
        acustra::EngineParameters parameters;
        parameters.guitarModel = test.model;
        for (const int midiNote : { 45, 57, 64 })
        {
            const auto [both, eta] = renderNote(parameters, midiNote, false);
            const auto [normalOnly, unused] = renderNote(parameters, midiNote, true);
            std::vector<float> parallel(both.left.size());
            for (std::size_t index = 0; index < parallel.size(); ++index)
                parallel[index] = both.left[index] - normalOnly.left[index];
            const auto from = static_cast<std::size_t>(0.1 * sampleRate);
            const double share = energy(parallel, from)
                / std::max(energy(both.left, from), 1.0e-30);
            if (test.radiates)
            {
                // The published crown heights over the 23.2 mm half-spacing.
                expect(eta > 0.34f && eta < 0.45f,
                       std::string(test.name) + " did not project the crown's "
                       "published height onto the rocking");
                // Parallel plucks are markedly quieter (Woodhouse 2004), but a
                // pluck puts most of its energy in that plane and its share of
                // what is heard grows as the normal plane, which the bridge
                // loads harder, decays away from it; the two planes also
                // interfere, so the difference can exceed the whole.
                expect(share > 1.0e-4 && std::isfinite(share),
                       std::string(test.name) + " MIDI "
                           + std::to_string(midiNote)
                           + ": the parallel plane's share of the sound was "
                           + std::to_string(share));
            }
            else
            {
                expect(eta == 0.0f && share == 0.0,
                       std::string(test.name)
                           + ": a scalar measurement carried the parallel "
                             "plane to the microphones");
            }
            if (midiNote == 57)
                std::cout << "Acustra parallel polarisation " << test.name
                          << " A3 share of the sound after 0.1 s: "
                          << 10.0 * std::log10(std::max(share, 1.0e-30))
                          << " dB\n";
        }
    }
}

void testPerformance()
{
    auto engineOwner = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *engineOwner;
    engine.prepare(sampleRate, 64);
    for (const int note : { 40, 45, 50, 55, 59, 64 })
        engine.noteOn(note, 0.9f);
    std::vector<float> left(64);
    std::vector<float> right(64);
    constexpr int seconds = 4;
    const auto start = std::chrono::steady_clock::now();
    for (int sample = 0; sample < static_cast<int>(sampleRate) * seconds;
         sample += 64)
        engine.process(left.data(), right.data(), 64);
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    const double realtimeRatio = elapsed / seconds;
    std::cout << "Acustra six-string realtime ratio: " << realtimeRatio << '\n';
    expect(realtimeRatio < 0.25,
           "six-string engine exceeded the 0.25x realtime CPU gate");
}
// The idle flush lands on the same sample whatever blocks the audio is
// rendered in. It used to be counted in whole process() calls, so with
// large blocks it came later, and a note played in between started from a
// different state than with small blocks.
void testIdleFlushIsIndependentOfBlockSize()
{
    const auto render = [] (int blockSize, long secondAt, long length, long* flushedAt)
    {
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->prepare(sampleRate, 4096);
        std::vector<float> left(static_cast<std::size_t>(length)),
            right(static_cast<std::size_t>(length));
        const long releaseAt = static_cast<long>(sampleRate) / 10;
        long position = 0;
        engine->noteOn(52, 0.8f);
        while (position < length)
        {
            long end = std::min(position + blockSize, length);
            for (const long event : { releaseAt, secondAt })
                if (event > position && event < end)
                    end = event;
            if (position == releaseAt)
                engine->noteOff(52);
            if (position == secondAt)
                engine->noteOn(57, 0.8f);
            const auto at = static_cast<std::size_t>(position);
            const bool wasFlushed = acustra::AcustraEngineTestAccess::idleFlushed(*engine);
            engine->process(left.data() + at, right.data() + at,
                           static_cast<int>(end - position));
            if (flushedAt != nullptr && *flushedAt < 0 && !wasFlushed && position > releaseAt
                && acustra::AcustraEngineTestAccess::idleFlushed(*engine))
                *flushedAt = end;
            position = end;
        }
        left.insert(left.end(), right.begin(), right.end());
        return left;
    };
    const long length = 30L * static_cast<long>(sampleRate);
    long flushedAt = -1;
    render(1, length, length, &flushedAt);
    expect(flushedAt > 0, "a released note never reached the idle flush");
    if (flushedAt <= 0)
        return;
    const long secondAt = flushedAt + 100;
    const long total = secondAt + static_cast<long>(sampleRate) / 2;
    const auto reference = render(1, secondAt, total, nullptr);
    for (const int blockSize : { 64, 4096 })
        expect(render(blockSize, secondAt, total, nullptr) == reference,
               "a note after the idle flush sounded differently in blocks of "
                   + std::to_string(blockSize));
}

// The bridge hand shortens the top by the 0.62 high-to-fundamental T60
// ratio its comment gives: its extra shelf loss per round trip is
// 0.001^((1/0.62 - 1) R / f) for its 1/T60 rate R. The shelf used exp in
// place of 0.001^ and added 1/6.9 of that.
void testBridgeHandTopLossIsItsT60Ratio()
{
    const auto mix = [] (float pressure)
    {
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->prepare(sampleRate, blockSize);
        engine->setStringPerChannelMode(true);
        engine->setPalmMutePressure(pressure);
        std::vector<float> left(static_cast<std::size_t>(blockSize)),
            right(static_cast<std::size_t>(blockSize));
        for (int block = 0; block < 40; ++block)
            engine->process(left.data(), right.data(), blockSize);
        engine->noteOn(40, 0.8f, 1);
        for (int block = 0; block < 40; ++block)
            engine->process(left.data(), right.data(), blockSize);
        return static_cast<double>(acustra::AcustraEngineTestAccess::highLossMix(*engine, 0));
    };
    const double open = mix(0.0f);
    for (const float pressure : { 0.25f, 0.5f, 1.0f })
    {
        // The hand's mapped T60, 4 s at no pressure to 80 ms at full, and its
        // rate scaled by pressure (AcustraEngine::configureVoice).
        const double handT60 = std::exp(std::log(4.0) + pressure * (std::log(0.080) - std::log(4.0)));
        const double rate = pressure / handT60;
        const double extra = -std::log((1.0 - mix(pressure)) / (1.0 - open)) / std::log(1000.0)
            * 82.4069 / rate;
        std::cout << "Acustra bridge hand " << pressure << ": top's extra rate "
                  << extra << " of the fundamental's (0.62 ratio: " << 1.0 / 0.62 - 1.0 << ")\n";
        expect(std::abs(extra / (1.0 / 0.62 - 1.0) - 1.0) < 0.02,
               "the bridge hand's top loss was not its 0.62 T60 ratio at pressure "
                   + std::to_string(pressure));
    }
}

// A pluck's random draws (where the hand lands, a strummed string's level,
// the release angle) follow only the plucks before it, not how hard or at
// what rate they were played. The release burst drew its noise from the
// same state for as many samples as its envelope lasted, which velocity,
// Touch and the rate set.
void testPluckDrawsIgnoreEarlierBursts()
{
    const auto stateAfter = [] (double rate, float firstVelocity, acustra::PickingTechnique picking)
    {
        auto engine = std::make_unique<acustra::AcustraEngine>();
        acustra::EngineParameters parameters;
        parameters.picking = picking;
        engine->setParameters(parameters);
        engine->prepare(rate, 256);
        std::vector<float> left(256), right(256);
        engine->noteOn(52, firstVelocity);
        for (int block = 0; block < static_cast<int>(0.3 * rate / 256.0); ++block)
            engine->process(left.data(), right.data(), 256);
        engine->noteOn(52, 0.7f);
        engine->process(left.data(), right.data(), 256);
        const int string = engine->heldString(52);
        return string < 0 ? 0u : acustra::AcustraEngineTestAccess::pluckDrawState(*engine, string);
    };
    for (const auto picking : { acustra::PickingTechnique::Finger, acustra::PickingTechnique::Pick })
    {
        const auto reference = stateAfter(48000.0, 0.7f, picking);
        expect(reference != 0u, "the re-struck note found no string");
        expect(stateAfter(48000.0, 0.2f, picking) == reference
                   && stateAfter(48000.0, 1.0f, picking) == reference,
               "an earlier pluck's velocity moved the next pluck's draws");
        expect(stateAfter(96000.0, 0.7f, picking) == reference
                   && stateAfter(44100.0, 0.7f, picking) == reference,
               "the sample rate moved the next pluck's draws");
    }
}

// A second construction change inside the bridge's 20 ms crossfade waits for
// it to end, as the body's does. Rebuilding the bridge under the running
// fade stepped it: up to 83 times the largest sample-to-sample surprise in
// its motion before the switch, 5-9 dB more top for a few milliseconds.
void testASecondConstructionChangeDoesNotStepTheBridge()
{
    using acustra::EngineParameters;
    EngineParameters steel;
    steel.outputGain = 0.2f;
    auto parlor = steel;
    parlor.shape = acustra::BodyShape::Parlor;
    auto bellido = steel;
    bellido.guitarModel = acustra::GuitarModel::Bellido1978;
    auto bellidoParlor = parlor;
    bellidoParlor.guitarModel = acustra::GuitarModel::Bellido1978;
    const int rate = 48000;
    const int ms5 = rate / 200;
    struct Case { const char* name; EngineParameters first; int after; EngineParameters second; };
    for (const auto& change : { Case { "Model then back 15 ms later", bellido, 3 * ms5, steel },
                                Case { "Model then back 5 ms later", bellido, ms5, steel },
                                Case { "Shape then Model 5 ms later", parlor, ms5, bellidoParlor } })
    {
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->setParameters(steel);
        engine->prepare(rate, 256);
        engine->beginStrum();
        const std::array<int, 6> chord { 40, 47, 52, 56, 59, 64 };
        for (std::size_t k = 0; k < chord.size(); ++k)
            engine->noteOn(chord[k], 0.8f, 1, static_cast<int>(k) * 300, true);
        const int start = 24000;
        std::vector<double> x;
        double before = 0.0, worst = 0.0;
        for (int i = 0; i < start + 2 * change.after + 960; ++i)
        {
            if (i == start)
                engine->setParameters(change.first);
            if (i == start + change.after)
                engine->setParameters(change.second);
            float left = 0.0f, right = 0.0f;
            engine->process(&left, &right, 1);
            x.push_back(acustra::AcustraEngineTestAccess::bridgeDisplacement(*engine));
            if (i < 2)
                continue;
            const auto n = x.size() - 1;
            const double surprise = std::abs(x[n] - 2.0 * x[n - 1] + x[n - 2]);
            if (i >= start - rate / 50 && i < start)
                before = std::max(before, surprise);
            else if (i >= start)
                worst = std::max(worst, surprise);
        }
        std::cout << "Acustra bridge, " << change.name << ": worst motion surprise "
                  << worst / before << "x the largest before\n";
        expect(worst < 3.0 * before,
               std::string("the bridge stepped on a second construction change: ") + change.name);
    }
}

// The body radiates at other rates what it radiates at 48 kHz, where its
// residues were fitted. Converted as a held continuous mode it kept the
// 48 kHz hold's droop at the mode instead: 0.36-0.39 dB too much at 9 kHz
// at 96 kHz, 0.18 dB too little at 44.1 kHz.
void testBodyRadiationKeepsItsLevelAcrossRates()
{
    for (const auto model : { acustra::GuitarModel::Original, acustra::GuitarModel::Bellido1978 })
    {
        acustra::EngineParameters parameters;
        parameters.guitarModel = model;
        std::vector<std::unique_ptr<acustra::AcustraEngine>> engines;
        const std::array<double, 3> rates { 48000.0, 44100.0, 96000.0 };
        for (const double rate : rates)
        {
            engines.push_back(std::make_unique<acustra::AcustraEngine>());
            engines.back()->setParameters(parameters);
            engines.back()->prepare(rate, 64);
        }
        double worst = 0.0;
        for (const double f : { 3000.0, 5000.0, 7000.0, 9000.0 })
        {
            // A third-octave comb, so no one mode decides it.
            std::array<double, 3> power {};
            for (std::size_t r = 0; r < rates.size(); ++r)
                for (int k = -3; k <= 3; ++k)
                    power[r] += std::pow(acustra::AcustraEngineTestAccess::bodyResponse(
                        *engines[r], f * std::exp2(k / 36.0)), 2.0);
            for (std::size_t r = 1; r < rates.size(); ++r)
                worst = std::max(worst, std::abs(10.0 * std::log10(power[r] / power[0])));
        }
        std::cout << "Acustra body radiation 3-9 kHz across 44.1/96 kHz, model "
                  << static_cast<int>(model) << ": worst " << worst << " dB from 48 kHz\n";
        expect(worst < 0.12, "the body's radiation moved with the sample rate");
    }
}

// The bridge's plate conductance floor damps a string's upper partials alike
// at every host rate. Prewarped at the host rate its over-damped section
// moved its fitted plateau (5-7% faster bridge decay above 1 kHz at 96 kHz),
// and below a 13 kHz host it was dropped with the modes above 0.45 fs.
void testThePlateFloorDampsAlikeAtEveryRate()
{
    const std::array<double, 6> rates { 48000.0, 44100.0, 96000.0, 192000.0, 12000.0, 11025.0 };
    std::vector<std::unique_ptr<acustra::AcustraEngine>> engines;
    for (const double rate : rates)
    {
        engines.push_back(std::make_unique<acustra::AcustraEngine>());
        engines.back()->prepare(rate, 64);
    }
    const auto open = acustra::AcustraEngine::openNotes(acustra::Tuning::Standard);
    double worstHigh = 0.0, worstLow = 0.0;
    for (const int string : { 0, 2, 5 })
    {
        const double f0 = 440.0 * std::exp2((open[static_cast<std::size_t>(string)] - 69) / 12.0);
        for (const int harmonic : { 3, 5, 8, 12, 20 })
        {
            const double f = harmonic * f0;
            const double reference = acustra::AcustraEngineTestAccess::bridgeDecay(
                *engines[0], f, f0, string);
            for (std::size_t r = 1; r < rates.size(); ++r)
            {
                if (f > 0.4 * rates[r])
                    continue;
                const double relative = std::abs(acustra::AcustraEngineTestAccess::bridgeDecay(
                    *engines[r], f, f0, string) / reference - 1.0);
                (rates[r] >= 44100.0 ? worstHigh : worstLow)
                    = std::max(rates[r] >= 44100.0 ? worstHigh : worstLow, relative);
            }
        }
    }
    std::cout << "Acustra bridge decay against 48 kHz: worst " << 100.0 * worstHigh
              << "% at 44.1-192 kHz, " << 100.0 * worstLow << "% at 11-12 kHz\n";
    expect(worstHigh < 0.025, "the bridge's decay of upper partials moved with the rate");
    expect(worstLow < 0.3, "a low host rate lost the bridge's plate conductance floor");
}

// The release shape's kink lands at the pluck point of the string, not of
// the delay line it is written on: the loop's round trip is 3-13% longer
// than that line, and a kink at p of the line sat that much bridgeward on
// the string (Docs/decisions.md, 2026-09-30).
void testThePluckKinkLandsAtThePluckPoint()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int note : { 40, 52, 64, 76, 88 })
        {
            auto engine = std::make_unique<acustra::AcustraEngine>();
            engine->prepare(rate, 64);
            engine->noteOn(note, 0.6f);
            const int string = engine->heldString(note);
            if (string < 0)
                continue;
            const auto kink = acustra::AcustraEngineTestAccess::kinkOnTheString(*engine, string);
            // The attack's pitch glide moves the period by ~0.1% after the pluck.
            expect(std::abs(kink.first - kink.second) < 2.0e-3,
                   "the pluck's kink sat at " + std::to_string(kink.first)
                       + " of the string, not its pluck point " + std::to_string(kink.second)
                       + ", MIDI " + std::to_string(note) + " at " + std::to_string(rate));
        }
}

// Pluck Position changes the stroke on every string at every fret, for every
// technique: no fret where the control is dead. The hand is held at a
// distance from the bridge, so on a short string (high on the neck) or under
// a Thumb it reaches past the midpoint; the point then bends toward 0.36 of
// the string without reaching it (initialisePluck). The hard 0.46 clamp left
// the control without effect for a Thumb from the 11th fret up and for a
// Finger from the 19th, and a fold about the midpoint with a quarter-string
// floor did the same for a Thumb at the 20th.
void testPluckPositionChangesEveryFret()
{
    using Access = acustra::AcustraEngineTestAccess;
    using acustra::PickingTechnique;
    const auto partialDb = [] (const std::vector<double>& line, int harmonic)
    {
        std::complex<double> sum {};
        for (std::size_t sample = 0; sample < line.size(); ++sample)
            sum += line[sample] * std::polar(1.0, -2.0 * std::numbers::pi
                * harmonic * static_cast<double>(sample)
                / static_cast<double>(line.size()));
        return 20.0 * std::log10(std::max(std::abs(sum), 1.0e-30));
    };
    constexpr std::array positions { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    double leastChange = std::numeric_limits<double>::max();
    double leastMove = std::numeric_limits<double>::max();
    double highestPoint = 0.0;
    for (const auto picking : { PickingTechnique::Finger, PickingTechnique::Pick,
                                PickingTechnique::Thumb })
        for (const int string : { 0, 5 })
            for (int fret = 0; fret <= acustra::AcustraEngine::fretCount; ++fret)
            {
                std::array<std::pair<double, std::vector<double>>, positions.size()> strokes;
                for (std::size_t index = 0; index < positions.size(); ++index)
                    strokes[index] = Access::pluckAtFret(picking, positions[index], string, fret);
                const std::string where = "technique " + std::to_string(static_cast<int>(picking))
                    + ", string " + std::to_string(string) + ", fret " + std::to_string(fret);
                for (std::size_t index = 0; index < positions.size(); ++index)
                {
                    highestPoint = std::max(highestPoint, strokes[index].first);
                    expect(strokes[index].first > 0.0 && strokes[index].first < 0.4,
                           "a stroke met the string off its band, " + where);
                }
                // A clamp puts every stroke past it on exactly one point (the
                // strokes share their take offset); the knee never does, if
                // only by a fraction of a millimetre for a Thumb high on the
                // neck, where the hand is past the string's end.
                for (std::size_t index = 1; index < positions.size(); ++index)
                {
                    const double moved = std::abs(strokes[index].first - strokes[index - 1].first);
                    leastMove = std::min(leastMove, moved);
                    expect(moved > 5.0e-5,
                           "a quarter turn of Pluck Position did not move the pluck point, "
                               + where);
                }
                // And the sound: some partial of the written line moves by a
                // decibel across the control's range.
                const auto& near = strokes.front().second;
                const auto& far = strokes.back().second;
                double change = 0.0;
                for (int harmonic = 1; harmonic <= 10 && 2 * harmonic < static_cast<int>(near.size());
                     ++harmonic)
                    change = std::max(change, std::abs(partialDb(far, harmonic)
                                                       - partialDb(near, harmonic)));
                leastChange = std::min(leastChange, change);
                expect(near.size() == far.size() && change > 1.0,
                       "Pluck Position did not change the stroke's partials, " + where);
            }
    std::cout << "Acustra Pluck Position at every fret: least partial change "
              << leastChange << " dB, least quarter-turn move " << leastMove
              << " of the string, highest pluck point " << highestPoint << '\n';
}

// One note on its own string at 48 kHz, as the strings' A/B probes played
// it: the engine settled over 40 blocks, then one stroke, read in mono.
std::vector<double> renderOnString(acustra::EngineParameters parameters, int midiNote,
                                   float velocity, int string, double seconds)
{
    constexpr int block = 256;
    auto engine = std::make_unique<acustra::AcustraEngine>();
    engine->prepare(sampleRate, block);
    engine->setParameters(parameters);
    engine->setStringPerChannelMode(true);
    std::vector<float> left(block), right(block);
    for (int settle = 0; settle < 40; ++settle)
        engine->process(left.data(), right.data(), block);
    engine->noteOn(midiNote, velocity, string);
    const int samples = static_cast<int>(seconds * sampleRate);
    std::vector<double> mono;
    mono.reserve(static_cast<std::size_t>(samples));
    for (int offset = 0; offset < samples; offset += block)
    {
        const int count = std::min(block, samples - offset);
        engine->process(left.data(), right.data(), count);
        for (int sample = 0; sample < count; ++sample)
            mono.push_back(0.5 * (left[static_cast<std::size_t>(sample)]
                                  + right[static_cast<std::size_t>(sample)]));
    }
    return mono;
}

// The power spectrum of x[begin, begin + count) under a Hann window, zero
// padded to `size` (a power of two), bins 0..size/2.
std::vector<double> hannPowerSpectrum(const std::vector<double>& x, int begin, int count,
                                      std::size_t size)
{
    std::vector<std::complex<double>> data(size);
    for (int sample = 0; sample < count; ++sample)
    {
        const auto index = static_cast<std::size_t>(begin + sample);
        const double window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * sample
                                                    / std::max(count - 1, 1));
        data[static_cast<std::size_t>(sample)] = index < x.size() ? x[index] * window : 0.0;
    }
    for (std::size_t i = 1, j = 0; i < size; ++i)
    {
        std::size_t bit = size >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (std::size_t length = 2; length <= size; length <<= 1)
    {
        const auto step = std::polar(1.0, -2.0 * std::numbers::pi / static_cast<double>(length));
        for (std::size_t start = 0; start < size; start += length)
        {
            std::complex<double> twiddle { 1.0 };
            for (std::size_t k = 0; k < length / 2; ++k)
            {
                const auto even = data[start + k];
                const auto odd = data[start + k + length / 2] * twiddle;
                data[start + k] = even + odd;
                data[start + k + length / 2] = even - odd;
                twiddle *= step;
            }
        }
    }
    std::vector<double> power(size / 2 + 1);
    for (std::size_t bin = 0; bin < power.size(); ++bin)
        power[bin] = std::norm(data[bin]);
    return power;
}

// Where the stroke starts: the first sample whose 1 ms RMS passes a tenth
// of its largest, walked back to where it stood under a hundredth of it.
int strokeOnset(const std::vector<double>& x)
{
    std::vector<double> envelope(x.size());
    double sum = 0.0, largest = 0.0;
    for (std::size_t sample = 0; sample < x.size(); ++sample)
    {
        sum += x[sample] * x[sample];
        if (sample >= 48)
            sum -= x[sample - 48] * x[sample - 48];
        envelope[sample] = std::sqrt(std::max(sum, 0.0) / 48.0);
        largest = std::max(largest, envelope[sample]);
    }
    std::size_t onset = 0;
    while (onset < x.size() && envelope[onset] <= 0.1 * largest)
        ++onset;
    while (onset > 0 && envelope[onset] > 0.01 * largest)
        --onset;
    return static_cast<int>(onset);
}

// The strongest line within 2.5% of each partial of f0, in dB, from a
// power spectrum of `size` bins at 48 kHz; NaN past 20 kHz.
std::vector<double> partialLevels(const std::vector<double>& power, std::size_t size,
                                  double f0, int partials, double reach = 0.025,
                                  double upperReach = 0.025)
{
    std::vector<double> levels;
    const double binHz = sampleRate / static_cast<double>(size);
    for (int n = 1; n <= partials; ++n)
    {
        const double centre = n * f0;
        if (centre > 20000.0)
        {
            levels.push_back(std::numeric_limits<double>::quiet_NaN());
            continue;
        }
        const auto low = static_cast<std::size_t>(std::ceil(centre * (1.0 - reach) / binHz));
        const auto high = std::min(power.size() - 1, static_cast<std::size_t>(
            std::floor((centre * (1.0 + upperReach) + 2.0) / binHz)));
        double strongest = 0.0;
        for (std::size_t bin = low; bin <= high; ++bin)
            strongest = std::max(strongest, power[bin]);
        levels.push_back(10.0 * std::log10(strongest + 1.0e-24));
    }
    return levels;
}

// The sustained H5-H12 over H1-H4 balance of a stroke, 30-330 ms after it.
double upperPartialBalance(const std::vector<double>& x, int midiNote)
{
    const double f0 = 440.0 * std::exp2((midiNote - 69) / 12.0);
    const int begin = strokeOnset(x) + 1440;
    constexpr std::size_t size = 1u << 16;
    const auto levels = partialLevels(hannPowerSpectrum(x, begin, 14400, size), size, f0, 12);
    double low = 0.0, high = 0.0;
    for (int n = 1; n <= 12; ++n)
    {
        const double level = levels[static_cast<std::size_t>(n - 1)];
        if (std::isnan(level))
            continue;
        (n <= 4 ? low : high) += std::pow(10.0, level / 10.0);
    }
    return 10.0 * std::log10(high / std::max(low, 1.0e-30));
}

// A finger-plucked flat-top brightens with dynamics: the recordings' H5-H12
// over H1-H4 balance rises a median 9.0 dB from a soft stroke to a loud one.
// A Finger released from rest rose 1.3-2.9 dB (median 2.1 over these notes);
// released through the plectrum's slip law as a ratio to its full-velocity
// slip (initialisePluck) it rises a median 9.2.
void testAFingerBrightensWithVelocityAsTheRecordingsDo()
{
    constexpr std::array<std::pair<int, int>, 8> notes { {
        { 40, 1 }, { 45, 2 }, { 51, 3 }, { 57, 4 }, { 60, 5 }, { 66, 6 }, { 72, 6 }, { 78, 6 } } };
    acustra::EngineParameters parameters;
    parameters.picking = acustra::PickingTechnique::Finger;
    std::vector<double> rises;
    for (const auto& [midi, string] : notes)
    {
        const double soft = upperPartialBalance(
            renderOnString(parameters, midi, 16.0f / 127.0f, string, 0.4), midi);
        const double loud = upperPartialBalance(
            renderOnString(parameters, midi, 112.0f / 127.0f, string, 0.4), midi);
        rises.push_back(loud - soft);
        expect(loud - soft > 3.0,
               "a loud Finger stroke was not brighter than a soft one on MIDI "
                   + std::to_string(midi));
    }
    std::sort(rises.begin(), rises.end());
    const double median = 0.5 * (rises[3] + rises[4]);
    std::cout << "Acustra Finger loud-over-soft H5-H12/H1-H4 rise: median " << median
              << " dB (recordings 9.0), " << rises.front() << ".." << rises.back() << " dB\n";
    expect(median > 7.0 && median < 11.0,
           "a Finger's velocity brightness rise, a median " + std::to_string(median)
               + " dB, is not within 2 dB of the recordings' 9.0");
}

// A fingertip or thumb's contact burst is the same noise through the same
// corner twice, with no white share (renderExcitation): its 2-12 kHz share
// of the first 15 ms over 60 Hz-16 kHz fell, at velocity 127 (where the
// release is the shape the listener chose), from a mean of -15.5 dB to
// -19.2 for the Finger and -15.5 to -20.2 for the Thumb over these notes,
// toward the Eastman E1D's finger take. The Pick keeps its law, -10.0.
// Since 2026-10-01 the microphones carry the capture voicing
// (CaptureVoicingData.h), whose 1-1.6 kHz lift and 250-500 Hz cut raise this
// share for every Picking alike: on the same build without it they read
// -18.7, -19.5 and -9.1, with it -14.5, -15.5 and -6.7. The bounds moved by
// that lift (4.1 dB for the soft contacts, 2.4 for the Pick).
// The held string's released static force (initialisePluck, 2026-10-01,
// second entry) puts the top's spring-back into the same 15 ms, all of it
// under 2 kHz, and with the voicing refitted after it every share fell, at
// the full release, to -16.0, -15.9 and -10.5; at the 0.8 of it a listener
// chose they read -15.5, -15.6 and -9.6. The Pick's falls furthest (2.9 dB
// against the Finger's 1.0), as its hand holds the string nearer the bridge,
// where the same displacement is a larger force against its note. Against
// the recordings measured the same way (MIDI 40-66, a Hann window over each
// note's first 15 ms) the Eastman E1D's picked take reads -10.5 and its
// finger take -29.1, an 18.6 dB lead: the Pick's own share meets its
// recording, and what keeps its lead short is the soft contacts' known
// brightness (README, Known gaps), not the Pick. Its bound follows its
// recording (-10.5 less 2.5 dB) and the lead keeps 5 dB.
void testSoftContactsCarryLessAttackHiss()
{
    constexpr std::array<std::pair<int, int>, 6> notes { {
        { 40, 1 }, { 45, 2 }, { 51, 3 }, { 57, 4 }, { 60, 5 }, { 66, 6 } } };
    const auto attackShare = [] (const std::vector<double>& x)
    {
        const int onset = strokeOnset(x);
        const int begin = std::max(onset - 96, 0);
        const int count = onset + 720 - begin;
        constexpr std::size_t size = 8192;
        const auto power = hannPowerSpectrum(x, begin, count, size);
        double hiss = 0.0, total = 0.0;
        for (std::size_t bin = 0; bin < power.size(); ++bin)
        {
            const double frequency = bin * sampleRate / static_cast<double>(size);
            if (frequency >= 60.0 && frequency < 16000.0)
                total += power[bin];
            if (frequency >= 2000.0 && frequency < 12000.0)
                hiss += power[bin];
        }
        return 10.0 * std::log10(hiss / std::max(total, 1.0e-30));
    };
    std::array<double, 3> share {};
    for (const auto picking : { acustra::PickingTechnique::Finger,
                                acustra::PickingTechnique::Pick,
                                acustra::PickingTechnique::Thumb })
    {
        acustra::EngineParameters parameters;
        parameters.picking = picking;
        double sum = 0.0;
        for (const auto& [midi, string] : notes)
            sum += attackShare(renderOnString(parameters, midi, 1.0f, string, 0.05));
        share[static_cast<std::size_t>(picking)] = sum / static_cast<double>(notes.size());
    }
    const double finger = share[static_cast<std::size_t>(acustra::PickingTechnique::Finger)];
    const double pick = share[static_cast<std::size_t>(acustra::PickingTechnique::Pick)];
    const double thumb = share[static_cast<std::size_t>(acustra::PickingTechnique::Thumb)];
    std::cout << "Acustra attack 2-12 kHz share of the first 15 ms: Finger " << finger
              << " dB, Thumb " << thumb << " dB, Pick " << pick << " dB\n";
    expect(finger < -12.4, "a Finger's attack kept its hiss: " + std::to_string(finger) + " dB");
    expect(thumb < -12.4, "a Thumb's attack kept its hiss: " + std::to_string(thumb) + " dB");
    expect(pick > -13.0 && pick > finger + 5.0 && pick > thumb + 5.0,
           "the Pick's attack lost the brightness it keeps over the soft contacts");
}

// String Age reaches the wound strings: grime between their windings is
// internal friction in their bending, so an old set's bass loses its upper
// partials faster. On a low E, H5-H8 over 0.1-1.2 s decayed 2.1 dB/s faster
// at age 1 than at the default 0.15 when only the absolute-frequency cutoff
// aged them (fresh strings 0.3 slower); the wound loss now scales with age,
// pivoted on the default (the shipped sound there is unchanged): 14.8 dB/s
// faster at age 1, and fresh strings (age 0) 2.1 dB/s slower.
void testStringAgeReachesTheWoundStrings()
{
    const auto upperDecay = [] (float age)
    {
        acustra::EngineParameters parameters;
        parameters.stringAge = age;
        const auto x = renderOnString(parameters, 40, 0.8f, 1, 1.4);
        const int onset = strokeOnset(x);
        const double f0 = 440.0 * std::exp2((40 - 69) / 12.0);
        constexpr int window = 4800;
        constexpr std::size_t size = 16384;
        std::vector<double> times;
        std::vector<std::array<double, 4>> levels;
        for (int start = onset + 4800; start + window <= onset + 57600
             && start + window <= static_cast<int>(x.size()); start += 480)
        {
            const auto partials = partialLevels(hannPowerSpectrum(x, start, window, size),
                                                size, f0, 8, 0.025, 0.03);
            times.push_back(static_cast<double>(start - onset - 4800) / sampleRate);
            levels.push_back({ partials[4], partials[5], partials[6], partials[7] });
        }
        double decay = 0.0;
        const double count = static_cast<double>(times.size());
        double meanTime = 0.0;
        for (double time : times)
            meanTime += time / count;
        for (std::size_t partial = 0; partial < 4; ++partial)
        {
            double meanLevel = 0.0;
            for (const auto& level : levels)
                meanLevel += level[partial] / count;
            double covariance = 0.0, variance = 0.0;
            for (std::size_t frame = 0; frame < times.size(); ++frame)
            {
                covariance += (times[frame] - meanTime) * (levels[frame][partial] - meanLevel);
                variance += (times[frame] - meanTime) * (times[frame] - meanTime);
            }
            decay -= covariance / variance / 4.0;
        }
        return decay;
    };
    const double fresh = upperDecay(0.0f);
    const double standard = upperDecay(0.15f);
    const double old = upperDecay(1.0f);
    std::cout << "Acustra low E H5-H8 decay: " << fresh << " dB/s fresh, " << standard
              << " at the default age, " << old << " at age 1\n";
    expect(old > standard + 8.0,
           "String Age did not reach the wound strings' upper-partial decay");
    expect(standard > fresh + 1.0,
           "fresh wound strings did not ring longer than the default set");
}

} // namespace

int main()
{
    testThePluckKinkLandsAtThePluckPoint();
    testPluckPositionChangesEveryFret();
    testAFingerBrightensWithVelocityAsTheRecordingsDo();
    testSoftContactsCarryLessAttackHiss();
    testStringAgeReachesTheWoundStrings();
    testIdleFlushIsIndependentOfBlockSize();
    testThePlateFloorDampsAlikeAtEveryRate();
    testBodyRadiationKeepsItsLevelAcrossRates();
    testASecondConstructionChangeDoesNotStepTheBridge();
    testPluckDrawsIgnoreEarlierBursts();
    testBridgeHandTopLossIsItsT60Ratio();
    testDecayEstimatorFollowsPitchGlides();
    testLossFiltersPreserveTheReferenceTransfer();
    testSilenceAndFiniteOutput();
    testUnsupportedSampleRatesClampToTheModelledRange();
    testPrepareRestartsThePerformanceExactly();
    testPlayableRangeFollowsTuning();
    testSteelRetuningPreservesStringMass();
    testAudiblePhysicalDecay();
    testPhysicalPluckOnsetIsBounded();
    testBridgeObservableIsSampleRateNormalised();
    testZeroWidthCollapsesToMono();
    testReleaseEventuallyReturnsTheString();
    testPhysicalVoiceOwnsReleaseLifecycle();
    testMidiChannelOwnershipAndAdditiveBend();
    testSharedBodyExcitesIdleStrings();
    testSympatheticStringsAreAudibleButBounded();
    testPassiveBridgeBranchesBalance();
    testPowerObserversKeepInitialAndRepeatedPluckWork();
    testRetainedTailClosesTheWaveNormBalance();
    testConstructionControlsChangeTheModel();
    testAgeRemovesUpperStringEnergy();
    testAttackPitchIsBoundedAndVelocityResponsive();
    testSteadyPitchIsCompensated();
    testPhysicalSustainSettlesNearRequestedPitch();
    testLoadedE2IsCentredAndNotSplit();
    testSteelDispersionTracksTheStiffStringLaw();
    testDispersionSolveCacheMatchesForcedRecomputation();
    testConfigurationKeysAndObserversLeaveTheOutputUnchanged();
    testDispersionAcrossRatesAndNotes();
    testDispersionAcrossTheFretboard();
    testTheFractionalDelayReadIsLossless();
    testASlewingDelayDoesNotClickAboveFourteenKilohertz();
    testAMemberBendIsATensionBendByGrimes();
    testABendMovesTheTwelfthPartialStretch();
    testABendDoesNotStepTheJunctionPort();
    testAnExtremeBendSaturatesInsideTheBendRange();
    testAWideMemberGlideStaysAsLoudAsASlide();
    testTheVibratoWheelAtZeroIsExact();
    testMpeTimbreSetsPerNotePluckPointOnMemberChannelOnly();
    testMpePressureBiasesVibratoDepthWithinTheWheelsOwnBound();
    testStringPerChannelModeIsOptInAndBypassesTheAllocator();
    testTheVibratoWheelStaysInsideItsPublishedBounds();
    testAMemberBendRetunesOnlyItsOwnString();
    testBlockPartitionIsDeterministic();
    testSampleRatesAndAutomationStayBounded();
    testHostileParametersAreSanitised();
    testHostilePhysicalCalibrationIsSanitised();
    testBodyAndBridgeCalibrationChangePhysicalDescriptors();
    testMaterialCalibrationChangesStringAndPluckDescriptors();
    testReleasedContactPreservesTheLinearFilterSpectrum();
    testBroadContactWrapsAndReachesItsUniformLimit();
    testPickingChangesTheContactWithoutRetuningOrReplucking();
    testPickingStylesChangeMoreThanGainAtEveryVelocity();
    testHighLossCutoffScaleChangesOnlyUpperLoss();
    testBendingLossFollowsItsLaw();
    testPlateConductanceFloorDampsOnlyTheUpperBand();
    testContactNoiseFollowsItsLaw();
    testStolenStringKeepsRingingUnderHandDamping();
    testBridgeHandPressureShortensAndDarkens();
    testNaturalHarmonicsReachAboveTheFretboard();
    testANaturalHarmonicSoundsItsOwnPitch();
    testHeldStringsDoNotLengthenANoteDecay();
    testANoteOverASoundingInstrumentDoesNotClick();
    testBodyChangesPreserveTheSoundingStrings();
    testBodyChangesPreserveAnUnfinishedFade();
    testSwitchingTuningOrModelUnderAChordDoesNotClick();
    testStringAgeKeepsARepluckedTail();
    testLongitudinalModesGrowWithVelocity();
    testTodaysMechanismsSurviveEachOther();
    testNoteAfterSilenceDoesNotClick();
    testRepluckLandsTheHandOnTheString();
    testSteelFretT60SlopeRaisesOnlyFrettedSustain();
    testApertureRegisterExponentChangesOnlyRegisterGeometry();
    testOrdinaryOutputIsLinearAndPathologicalOutputIsBounded();
    testNoteOffDoesNotCreateANewAttack();
    testAScheduledPluckIsANoteOnIssuedThen();
    testCancelledScheduledAttacksKeepOnlyTheExistingWave();
    testStrumTimingFollowsThePickAcrossTheStrings();
    testRepeatedStrumsCrossTheStringsLikeRepeatedRealStrums();
    testRepeatedStrumsVaryLikeRepeatedRealStrums();
    testNoTwoPlucksLandInTheSamePlace();
    testTheSteelBanksFillTheirSlotsAndStayPassive();
    testBodyShapesFollowTheCoupledTopAndCavity();
    testAPlectrumReleasesWithVelocity();
    testAPickReleaseKeepsItsHumpAcrossRates();
    testAPlectrumSlipsOffItsEdgeFasterWhenHarder();
    testTheNormalPolarisationIsTheHigherMemberByALength();
    testTheParallelPolarisationRadiatesThroughTheRockingSaddle();
    testPerformance();
    if (failures == 0)
        std::cout << "All Acustra engine tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
