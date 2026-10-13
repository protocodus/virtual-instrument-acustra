#pragma once

namespace acustra
{

// Offline comparison switches, not performance controls or saved plug-in
// parameters. Production uses the defaults. Configure before prepare(), or
// while stopped: changing these options clears the instrument's sound state.
// Keeping the mechanisms independent makes their audible/CPU contribution
// testable without maintaining separate engine implementations.
struct PerformanceRealism
{
    bool contactRelease { true };
    bool coherentHand { true };
    bool gestureDamping { true };
    // The quieter contacts expose intrinsic-filter retuning discontinuities.
    // Keep their continuity repair independently ablatable as well.
    bool retuneContinuity { true };
    // An upstroke strikes the treble strings harder and the bass strings
    // lighter than a downstroke (AcustraEngine::initialisePluck).
    bool strokeDepth { true };
    // Single picked notes alternate down and up as a plectrum does
    // (Performer::flushNoteGroup).
    bool alternatePicking { true };
    // A released fretted note falls toward the previous fret as the lifting
    // fingertip becomes the string's end (AcustraEngine::beginReleaseFall).
    bool releasePitchFall { true };
    // Under Finger the thumb and fingers pluck a chord of three or four
    // strings together instead of sweeping it
    // (AcustraEngine::fingerChordOffsetSamples).
    bool fingerChords { true };
    // A strum crossing a string inside its span that the chord leaves out
    // gives it a dead stroke under the fretting hand that mutes it
    // (AcustraEngine::muteStroke).
    bool mutedStrings { true };
};

} // namespace acustra
