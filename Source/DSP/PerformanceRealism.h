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
    // Each string of a swept stroke sounds at its own moment about the
    // sweep, as a pick meets real strings (AcustraEngine::strumScatterSamples).
    bool strumScatter { true };
    // Under Pick two notes on one sample are a stroke across their two
    // strings, swept as a strum is (Performer::flushNoteGroup).
    bool twoStringStrokes { true };
    // A stroke's span grows less than in proportion to the strings it
    // crosses, so a small stroke crosses each string more slowly
    // (AcustraEngine::strumDelaySamples).
    bool strokeSpanLaw { true };
    // A stroke strikes the strings it enters and leaves on more lightly than
    // those between them, and an upstroke tilts its strings by the stroke's
    // own measure (AcustraEngine::strumEdgeGain, initialisePluck).
    bool strokeEdges { true };
    // Restrain the independent pick-contact roughness while retaining the
    // calibrated displacement/velocity release (AcustraEngine::initialisePluck).
    // An authored sound refinement; this is not a measured pick-noise level.
    bool restrainedPickRoughness { true };
    // Keep the room's low end near the stereo centre without changing its
    // mono return, timing or dry microphones. An authored mix refinement.
    bool roomBassCoherence { true };
};

} // namespace acustra
