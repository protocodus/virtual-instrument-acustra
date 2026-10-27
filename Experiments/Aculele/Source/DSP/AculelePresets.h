#pragma once

#include "AculeleEngine.h"

#include <array>
#include <cmath>

namespace aculele
{
// The factory preset bank: the preset demos (Docs/audio/midi/<NN>-<slug>.mid)
// play it in order.
//
// A preset sets the sound. How it is played stays in the MIDI: the strum
// direction keyswitches (C0 down, C#0 up, D0 automatic) latch the strumming
// finger's pattern, as the demos do.
struct FactoryPreset
{
    const char* name;
    const char* slug;
    BodyShape shape;
    BodyMaterial bodyMaterial;
    StringMaterial strings;
    Tuning tuning;
    PickingTechnique picking;
    CaptureType capture;
    // 0..1, as EngineParameters holds them.
    float stringAge;
    float pluckPosition;
    float touch;
    float bodyAmount;
    float stereoWidth;
    float piezoMix;
    float releaseNoise;
    float room;
    // The Output control, -24..+6 dB: each preset's demo plays at -18 LUFS.
    float outputDecibels;
};

inline constexpr std::array<FactoryPreset, 7> factoryPresets {{
    { "Concert Strum", "concert-strum",
      BodyShape::Concert, BodyMaterial::Mahogany, StringMaterial::Nylon,
      Tuning::Standard, PickingTechnique::Strum, CaptureType::StereoMic,
      0.15f, 0.50f, 0.55f, 0.82f, 0.62f, 0.0f, 0.10f, 0.10f, 0.0f },
    { "Soprano D Tuning", "soprano-d-tuning",
      BodyShape::Soprano, BodyMaterial::Mahogany, StringMaterial::Nylon,
      Tuning::DTuning, PickingTechnique::Strum, CaptureType::StereoMic,
      0.10f, 0.50f, 0.62f, 0.82f, 0.62f, 0.0f, 0.10f, 0.10f, -1.0f },
    { "Tenor Fluorocarbon Fingerstyle", "tenor-fluorocarbon-fingerstyle",
      BodyShape::Tenor, BodyMaterial::Spruce, StringMaterial::Fluorocarbon,
      Tuning::Standard, PickingTechnique::Finger, CaptureType::StereoMic,
      0.10f, 0.40f, 0.55f, 0.86f, 0.62f, 0.0f, 0.10f, 0.10f, -1.8f },
    { "Low G Tenor", "low-g-tenor",
      BodyShape::Tenor, BodyMaterial::Mahogany, StringMaterial::Nylon,
      Tuning::LowG, PickingTechnique::Finger, CaptureType::StereoMic,
      0.20f, 0.38f, 0.50f, 0.86f, 0.62f, 0.0f, 0.10f, 0.12f, -0.8f },
    { "Soprano Thumb", "soprano-thumb",
      BodyShape::Soprano, BodyMaterial::Mahogany, StringMaterial::Nylon,
      Tuning::Standard, PickingTechnique::Thumb, CaptureType::StereoMic,
      0.30f, 0.45f, 0.40f, 0.86f, 0.55f, 0.0f, 0.10f, 0.10f, 4.7f },
    { "Tenor Picked Melody", "tenor-picked-melody",
      BodyShape::Tenor, BodyMaterial::Maple, StringMaterial::Fluorocarbon,
      Tuning::LowG, PickingTechnique::Pick, CaptureType::MonoMic,
      0.10f, 0.30f, 0.60f, 0.82f, 0.62f, 0.0f, 0.10f, 0.10f, -0.8f },
    { "Concert Piezo Stage", "concert-piezo-stage",
      BodyShape::Concert, BodyMaterial::Spruce, StringMaterial::Nylon,
      Tuning::Standard, PickingTechnique::Strum, CaptureType::Piezo,
      0.15f, 0.50f, 0.60f, 0.60f, 0.62f, 0.0f, 0.10f, 0.0f, 3.6f },
}};

inline EngineParameters engineParameters(const FactoryPreset& preset) noexcept
{
    EngineParameters result;
    result.shape = preset.shape;
    result.bodyMaterial = preset.bodyMaterial;
    result.strings = preset.strings;
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
} // namespace aculele
