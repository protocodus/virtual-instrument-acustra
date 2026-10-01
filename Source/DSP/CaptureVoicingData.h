// Acustra: the Original guitar's microphones as a recording hears them.
//
// Written by Tools/FitCaptureVoicing.py --write-header; edit the tool's
// inputs, not this file.
//
// The Original's radiation bank is g21, a nylon-strung flamenca measured by
// two microphones close over its bridge and upper bout, morphed to the
// steel-string Shapes. Against real steel-string flat-tops recorded as
// players record them - the Eastman E1D dreadnought (CC0, picked and
// finger-plucked, a coincident pair), Jeff Learman's Martin HD28 (CC0), the
// reference bank's finger-plucked flat-top, and GuitarSet's six players (a
// U87 about 30 cm from the 18th fret, CC BY 4.0) - those close microphones
// hear too little of the 90-180 Hz octave, too much of 250-630 Hz and too
// little of 0.8-1.6 kHz, every source agreeing on all three. This is that
// difference as one smooth gain, fitted to the consensus of the sources
// (each weighted equally, the Eastman's two takes as one source) in third
// octaves over each note's first second; above 3 kHz the sources disagree
// and the fit stays near flat. It multiplies every radiation mode's
// residues at the mode's own frequency (AcustraEngine::configureBody), so it
// costs nothing per sample, moves with Shape and Wood as the modes do, and
// reaches only the microphones: the piezo reads the saddle force, which no
// microphone position changes. The named Bellido keeps its own measured
// microphones.
//
// Each section is the analog prototype of an RBJ cookbook filter, read as a
// magnitude at the mode's frequency; levelDb keeps the default
// construction's loudness where it was (Tools/CalibrateConstructionLoudness.py
// measures it).

#pragma once

namespace acustra::detail
{

enum class CaptureVoicingKind
{
    LowShelf,
    Peak,
    HighShelf
};

struct CaptureVoicingSection
{
    CaptureVoicingKind kind;
    float frequencyHz;
    float gainDb;
    float q;
};

inline constexpr CaptureVoicingSection captureVoicingSections[] {
    { CaptureVoicingKind::LowShelf, 120.0f, 0.58f, 0.7f },
    { CaptureVoicingKind::Peak, 125.0f, 2.75f, 1.2f },
    { CaptureVoicingKind::Peak, 250.0f, -6.00f, 1.2f },
    { CaptureVoicingKind::Peak, 500.0f, -6.00f, 1.2f },
    { CaptureVoicingKind::Peak, 1000.0f, 3.28f, 1.2f },
    { CaptureVoicingKind::Peak, 1400.0f, 6.00f, 1.2f },
};

inline constexpr float captureVoicingLevelDb = 3.82f;

} // namespace acustra::detail
