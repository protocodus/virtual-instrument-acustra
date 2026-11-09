# Palm tuning and callback overruns, 1.9.37 — 2026-11-09

The source is 1.9.37, compared with 1.9.36 (`3452312`). Measurements are at
48 kHz in 256-frame blocks on one development machine (gcc 13.3, 4-thread
Xeon at 2.1 GHz). They are not Reason or DAW CPU figures.

## The defect

The coupled polarisation pair is tuned by a damped-pole solve
(`coupledPolarisationDetune`), cached on its exact inputs. One input is
`tuningPeriodGain`, which read the palm's smoothed loss. While the palm
settled after a CC2 change, about 0.2 s, every control period changed that
gain, so all six strings re-solved every control period. A single Pick note
cost 6.5-9.5 ms a block for the 39 blocks of a palm move from none to full,
against 0.3 ms held. That is more than the 5.33 ms a block lasts, so Palm
Muted Steel Pick's factory demo, which moves CC2 four times, had blocks of
up to 2.7 times their period. The 1.9.35 CPU audit had found the cause and
left it open
([cpu-optimization-2026-11-09](cpu-optimization-2026-11-09.md)).

## Tried and not kept: tuning on the palm at contact

The pair is already tuned at the finger's stop at contact, so the first
candidate tuned it on the palm's loss at contact too. It removed the
overruns: the Palm Muted demo's median real-time factor fell from 0.180 to
0.101, and its worst block from 2.7 to 0.6 times its period. Every render
without a moving palm was byte-identical, and the `unit` and `audio` tiers
passed.

It was reverted because of the pitch it leaves. Between no palm and full
palm, the played string's coupled detune moves by a median of 0.42 cents
over MIDI 40-86, and by 9.3 cents at MIDI 42. Tuned at contact, a note
struck under the palm would have rung up to 9 cents out of tune once the
palm lifted. No test caught it, because no test lifted a palm off a
ringing note.

## The change

`configureVoice` tunes the pair on the palm's target (`targetPalmMute_`)
instead of its smoothed value. A CC2 change solves each string once, at the
new target, and the solve is cached for the rest of the palm's move. Once
the palm settles, the target and the smoothed value are the same float, so
the pair is tuned exactly as in 1.9.36. The loop's gain and its loss
filters still follow the smoothed palm. While the palm moves, the pair's
detune is already the settled one, at most 9.3 cents away (the same full
swing at MIDI 42) and usually under half a cent, for about 0.2 s.

## Contracts

- New, `Acustra.PalmMutePitch`: strike a note at one palm pressure, move
  the palm (1 to 0, 0 to 0.6 and 1 to 0.3) and let it settle. The played
  string's coupled detune must equal that of a note struck where the palm
  came to rest, on eight notes from MIDI 40 to 76. 1.9.37 matches exactly
  (0 cents). Tuning at contact differs by up to 9.3 cents on this check.
- New, `Acustra.PalmMutePitch`: the pair now reads the palm's target as soon
  as CC2 sets it, between control periods, so a phrase that moves the palm
  mid-note, once in the same sample as a note-on, must render the same bits
  at block sizes 1, 17, 64, 256, 333 and 512. It does, as 1.9.36 does.
- Seven of the eight factory demos and both presets of the 1.9.36 A/B set
  render byte-identical to 1.9.36. Only the Palm Muted demo changes, because
  only it moves CC2.
- The `unit` and `audio` tiers (125 tests) and the `perf` tier pass.
  `Acustra.EnginePerformance` measures a six-string real-time ratio of 0.115.

## CPU

A single Pick note while the palm moves from none to full: 0.38-0.42 ms a
block on average, against 6.5-9.5 ms in 1.9.36, over MIDI 40-84.

`Acustra.CpuBudget`, two alternating rounds of three repeats each, median
real-time factor and the range of the worst block's share of its period:

| Preset | 1.9.36 | 1.9.37 | Worst block, 1.9.36 | Worst block, 1.9.37 |
| --- | ---: | ---: | ---: | ---: |
| Palm Muted Steel Pick | 0.159 | 0.104 | 1.64-1.98 | 0.35-0.85 |

The other seven demos compute identical samples in both versions. Their
medians moved by -9% to +29% between rounds, which is this machine's noise.

## Not addressed

A slide moves `speakingFret`, and the fret's T60 slope enters the same
tuning gain, so a wheel slide was measured too. Sliding a ringing note two
semitones over 40 blocks with `setPitchBend` costs 0.40-0.43 ms a block on
average (worst 0.86 ms) against 0.25-0.29 ms held, at MIDI 40, 52, 64 and
76. That is well inside the period, so it was left alone. The 24-event
Gather burst at 96 kHz/64 frames is the other open deadline limit in the
1.9.35 audit.
