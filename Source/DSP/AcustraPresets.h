#pragma once

#include "AcustraEngine.h"

#include <array>
#include <cmath>

namespace acustra
{
// The factory preset bank: the plug-in's program list, the preset demos
// (Docs/audio/midi/<NN>-<slug>.mid) and the Rack Extension's factory patches,
// which mirror it value for value (Examples/Acustra/Resources/Public in the
// Rack repository, checked by its patch validator).
//
// A preset sets the sound. How it is played stays in the MIDI: the Rack's
// Bridge Hand and Bend Range knobs are CC2 and the RPN 0 bend range here, so
// "Palm Muted Steel Pick" palm-mutes with CC2 and "Open G Slide-ready"
// slides once a 12-semitone range is set, as their demos do.
struct FactoryPreset
{
    const char* name;
    const char* slug;
    BodyShape shape;
    BodyMaterial bodyMaterial;
    Tuning tuning;
    PickingTechnique picking;
    CaptureType capture;
    // 0..1, as EngineParameters holds them (the plug-in shows percent).
    float stringAge;
    float pluckPosition;
    float touch;
    float bodyAmount;
    float stereoWidth;
    float piezoMix;
    float releaseNoise;
    float room;
    // The Output control, -24..+6 dB.
    float outputDecibels;
};

inline constexpr std::array<FactoryPreset, 8> factoryPresets {{
    { "Dreadnought Steel", "dreadnought-steel",
      BodyShape::Dreadnought, BodyMaterial::Spruce, Tuning::Standard,
      PickingTechnique::Finger, CaptureType::StereoMic,
      0.15f, 0.28f, 0.58f, 0.82f, 0.62f, 0.20f, 0.10f, 0.10f, 0.0f },
    { "Parlor Steel Fingerstyle", "parlor-steel-fingerstyle",
      BodyShape::Parlor, BodyMaterial::Mahogany, Tuning::Standard,
      PickingTechnique::Finger, CaptureType::StereoMic,
      0.25f, 0.22f, 0.46f, 0.86f, 0.55f, 0.0f, 0.10f, 0.10f, -5.5f },
    { "Mahogany Steel Thumb", "mahogany-steel-thumb",
      BodyShape::Auditorium, BodyMaterial::Mahogany, Tuning::Standard,
      PickingTechnique::Thumb, CaptureType::StereoMic,
      0.35f, 0.38f, 0.38f, 0.90f, 0.50f, 0.0f, 0.10f, 0.10f, -4.5f },
    { "Jumbo Steel Pick", "jumbo-steel-pick",
      BodyShape::Jumbo, BodyMaterial::Spruce, Tuning::Standard,
      PickingTechnique::Pick, CaptureType::StereoMic,
      0.08f, 0.18f, 0.72f, 0.78f, 0.72f, 0.0f, 0.10f, 0.10f, -5.0f },
    { "Palm Muted Steel Pick", "palm-muted-steel-pick",
      BodyShape::Dreadnought, BodyMaterial::Mahogany, Tuning::Standard,
      PickingTechnique::Pick, CaptureType::StereoMic,
      0.20f, 0.16f, 0.70f, 0.82f, 0.62f, 0.0f, 0.10f, 0.10f, -3.5f },
    { "DADGAD Mono Mic", "dadgad-mono-mic",
      BodyShape::Dreadnought, BodyMaterial::Mahogany, Tuning::Dadgad,
      PickingTechnique::Finger, CaptureType::MonoMic,
      0.15f, 0.32f, 0.50f, 0.88f, 0.62f, 0.0f, 0.10f, 0.10f, -7.0f },
    { "Open G Slide-ready", "open-g-slide-ready",
      BodyShape::Auditorium, BodyMaterial::Mahogany, Tuning::OpenG,
      PickingTechnique::Finger, CaptureType::StereoMic,
      0.30f, 0.28f, 0.55f, 0.82f, 0.62f, 0.0f, 0.10f, 0.10f, -4.0f },
    { "Piezo Stage Steel", "piezo-stage-steel",
      BodyShape::Dreadnought, BodyMaterial::Spruce, Tuning::Standard,
      PickingTechnique::Pick, CaptureType::Piezo,
      0.15f, 0.24f, 0.62f, 0.60f, 0.62f, 0.0f, 0.10f, 0.10f, -6.0f },
}};

inline EngineParameters engineParameters(const FactoryPreset& preset) noexcept
{
    EngineParameters result;
    result.shape = preset.shape;
    result.bodyMaterial = preset.bodyMaterial;
    result.tuning = preset.tuning;
    result.picking = preset.picking;
    result.capture = preset.capture;
    result.stringAge = preset.stringAge;
    result.pluckPosition = preset.pluckPosition;
    result.touch = preset.touch;
    result.bodyAmount = preset.bodyAmount;
    result.stereoWidth = preset.stereoWidth;
    result.piezoMix = preset.piezoMix;
    result.releaseNoise = preset.releaseNoise;
    result.room = preset.room;
    result.outputGain = std::pow(10.0f, preset.outputDecibels / 20.0f);
    return result;
}
} // namespace acustra
