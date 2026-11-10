// The parameter matrix's phrase (Tests/ParameterMatrix.h, Play) and the
// silence it may ask for after release, shared by the plug-in matrix
// (ParameterMatrixTests.cpp) and the JUCE-free proof of what that silence
// measures (MatrixReleaseTests.cpp).
#pragma once

#include <cmath>

namespace acustra::matrixrelease
{
// The matrix plays a C major triad and asks for silence two seconds after its
// release. On Acustra the strings the chord did not play stay open on the
// bridge, ring in sympathy, and no hand damps them (README, How it works:
// Bridge, sympathetic strings and body), so that bound measures their ring
// and not the release.
// Measured at 48 kHz, block 64, 2.06 s after release (MatrixReleaseTests.cpp
// repeats it on every preset and technique):
//
//   sympathetic strings off   below -143 dBFS: the played strings are silent
//   sympathetic strings on    up to -58.5 dBFS before Output (Jumbo Steel
//                             Pick played with fingers), -62.1 as it ships
//   note-offs lost            -42.9 dBFS or louder before Output
//
// The matrix's shared -60 dBFS floor therefore sat inside the open strings'
// ring. It passed through 1.9.35 only because that preset's Output trim was
// -0.9 dB; 1.9.36 re-trimmed it to -0.1 dB, the engine under fingers stayed
// byte-identical, and six Picking = 0 cases read -59.89 to -59.99 dBFS. The
// floor here is -54 dBFS: 4.5 dB above the loudest ring before Output and
// 11 dB under the quietest stuck chord.
inline constexpr float silenceFloor = 2.0e-3f;

// Ordinary key-up keeps held-string physics through an inclusive 1/32-note
// join window (7.5/BPM seconds; Docs/research/release-join-2026-10-05.md),
// then the damping hand starts. The two-second bound starts at the first
// damped sample, observed through the output latency.
inline double releaseSeconds(double sampleRate, double bpm, int latencySamples)
{
    return 2.0 + (std::ceil(7.5 * sampleRate / bpm) + 1.0 + latencySamples) / sampleRate;
}
}
