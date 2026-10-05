#pragma once

#include "RepertoireScores.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace acustra::repertoire
{
// The short notes identify the tremolo using the same role threshold as
// GenerateRepertoireScores.py. Written note lengths are notation, not an
// instruction to lift the fretting finger while the thumb plays its slot.
inline bool isTremoloStroke(const ScoreNote& note) noexcept
{
    return note.heldBeats <= 0.1875;
}

template <std::size_t Count>
std::array<ScoreNote, Count> joinTremoloLine(
    const std::array<ScoreNote, Count>& written)
{
    auto performed = written;
    double endBeat = 0.0;
    for (const auto& note : written)
        endBeat = std::max(endBeat, note.startBeats + note.heldBeats);

    // Keep one owner per written stroke, with its balanced key-up at the
    // next melody attack. Off-before-on ordering in playScore handles ties;
    // there is no render sample of damping between those paired events.
    // All pitches, onsets and velocities stay exactly as written.
    std::size_t next = Count;
    for (std::size_t index = Count; index-- > 0;)
    {
        if (!isTremoloStroke(written[index]))
            continue;
        const double end = next < Count ? written[next].startBeats : endBeat;
        performed[index].heldBeats = end - written[index].startBeats;
        next = index;
    }
    return performed;
}
} // namespace acustra::repertoire
