# ACUSTRA commercial-release readiness — 2026-10-11

Release Noise now defaults to **10%** in new native and Rack instances and the
eight Rack factory patches. The prior 70% setting remains a valid saved value;
legacy native states missing the parameter still restore zero. The control
range and gain law are unchanged. Native double-click reset follows the new
parameter default. The bare DSP's opt-in zero default is intentional, rather
than a mismatch between user-facing products.

Canonical source is `11eafbefb3a7438f3012533c1ad8e45a065d480a`. Current fetched
main remains `a534d774aa85f31acac6e74e6efebc18221ec748`, contained in this branch.
DSP source and native library hashes are identical to the previous studio
capture validation. The focused native processor/state and tempo/release
checks pass, and VST3, AU and Standalone build for arm64 and x86_64. See the
[default receipt](../validation/release-readiness-2026-10-11/defaults/receipt.json).
Rack 1.2.8f1 validation is recorded separately in the sibling Rack repository.
No source has been merged or published as part of this task.

## Audible priorities

The user's latest listening feedback identifies **remaining resonances and
release noise that is too prominent or stereotyped**, while the rest of the
instrument sounds acceptable. This is useful listening feedback, but it does
not identify the exact installed build or establish a verdict on the new 10%
default. The next sound work should target those two concerns; a broad model
rewrite is not justified by the present evidence.

The 10% control default reduces the added release-noise level. It does not
change the contact's spectral/envelope design or the physical damping sound.
Existing songs with an explicit 70% value remain at 70% until adjusted.

## Release-noise character

The current implementation supports a plausible explanation for the user's
feedback. Fully fretted releases share a nominal 1.4 kHz touch corner, 10 ms
fall and a common envelope family across picking styles. The wound-string
brush has fixed per-string spectral centers, with modest event variation.
Rack receives no key-up velocity and uses nominal firmness/brush speed for
each release. The touch follows current string reaction, while brush level
follows the preceding pluck's retained peak; consequently a long-held note
can become quiet without reducing the brush's amplitude scale.

Successive events already draw different waveforms and coefficients. The
common gesture structure, context and balance deserve attention. The 70% to
10% change reduces the added source scale by a factor of seven (about 16.9 dB)
under identical settings; it leaves that structure and physical damping intact.

The next discriminating experiment should isolate **landing versus brush**
for short and long holds at matched performance/output settings. If the brush
is the objectionable component, separate passive pad/palm landing from active
winding slip, using available local motion/contact state and a conservative
fallback for Rack's missing gesture information. A hand can excite an already
quiet string, so simply replacing attack peak with remaining note level would
not by itself establish realism. Calibrate the balance and event diversity
against isolated recorded releases before changing the shipping character.
These are code-supported hypotheses, not a listening-confirmed root cause.
See the [release-noise audit](../validation/release-readiness-2026-10-11/release-noise/findings.md).

## Remaining model findings

This is a bounded review of the latest model, using existing final evidence,
four fresh current-only force/moment traces and a renewed moving-stop witness.
It establishes no new arithmetic, passive-stability or state-management defect.
The user-reported sound concerns remain open even where a numerical test passes.
The following observations help localize the work; they are not all commercial
release blockers.

| Finding | Current evidence | Recommended action |
| --- | --- | --- |
| Low-note peaks remain | Auditorium A1 is about 1.9 dB above nearby notes in the early-window metric; Jumbo F1 about 1.5–1.7 dB. The capture blend improves harmonic balance without changing these body poles. Parlor B1 is not a local early-window maximum under that metric. | Prioritize the user-reported Parlor B1, Auditorium A1 and Jumbo E1–F1 in sustained notes and phrases, both woods and microphone modes. Measure actual mic force/moment contributions and decay. Refine capture geometry/participation if repeatable boom remains; preserve body/Piezo and avoid blanket EQ cuts. |
| Narrow Parlor microphone cancellation | Static stereo-sum tests have deep zeros near 254 Hz Spruce and 245 Hz Mahogany. The four fresh played-string probes do not reproduce the extreme static losses: actual complex force/moment ratios differ from the stress input. Earlier final audio's worst focused H3 loss was 1.94 dB, on a weak partial. | Check sustained harmonics and slow bends through those frequencies in stereo, summed mono and the dedicated Mono capture. If audible holes recur, repair observation phase/participation using pressure evidence. Preserve body/Piezo while studying this. |
| Studio projection is not a measured mic placement | Thirty percent of the approved blend uses low-mode spatial priors inferred from sparse close measurements. The new 30/45-cm field has no independent measurement validation. | Obtain phase-preserving pressure responses at documented studio positions on the same measured guitar, or keep product claims explicit about the modeled projection. Useful future capture work; numerical precision alone does not prove acoustic realism. |
| Slide wave transport is approximate | The current isolated witness again changes returning waves after two samples, before a low-E fret motion could propagate to the bridge (about 5.4 ms in the cited case). This is an isolated physical inconsistency, not a measured whole-instrument audibility result. | Evaluate audible slides first. A physical correction requires coherent moving-boundary history for transport, local contact, re-plucks and tails. A simple delayed-read/output patch is insufficient. |
| Mechanical fit retains calibration debt | The own-bank relative complex transfer error is 0.279943 against its historical 0.24 target; its 1.431-dB median-magnitude result passes the 1.6-dB gate. The joint bank passes both. This is inherited and does not identify a specific bad note. | Localize the residual by frequency and excitation before attempting a passive residue refit at shared poles. This would change mechanics/Piezo and deserves a separate explicitly scoped evaluation. It cannot be repaired in the microphone alone. |

The prior paired broad-Q pole-convention mismatch is **resolved**, not another
open issue. Paired bridge/radiation roots agree; unpaired modes deliberately
remain independent. See the [complete findings](../validation/release-readiness-2026-10-11/physics/findings.md)
and [current-source provenance](../validation/release-readiness-2026-10-11/physics/provenance.json).
The transfer tests are not exhaustive playing coverage, and no new listener
preference is claimed.

## Recommended release order

1. **Finish resonance and release-noise listening first; freeze other features.**
   Use the current 10% default, then compare noise-off and the former 70% at
   identical performance and output settings. Separate a harsh/noisy key-up
   from the string's damping transient. Compare short and long holds, repeated
   notes, wound/plain strings, open/fretted notes and phrase transitions.
   For resonance, use the reported low notes with neighboring semitones,
   sustained decays and real phrases, followed by Parlor partial/bend crossings
   in stereo and mono. Preserve the accepted overall timbre. A small group of
   guitarists, keyboard players and recording engineers should audition the
   final candidate blind and level-matched, with unfamiliar phrases and
   suitable acoustic recordings not used to choose the capture blend.
   Fix reproduced audible defects before the commercial candidate is frozen;
   do not chase a flat response or add unsupported techniques.
2. **Complete real Reason session acceptance.** The SDK's local checklist
   calls for the latest Testing and Deployment hosts. Exercise all eight named
   patches, initial/reset defaults, transport stop/play, automation and Remote,
   sustain/panic, master tuning, CV/RPG-8, mono/stereo/Piezo cables, deferred
   quality changes, and save/close/reopen. CLI creation/conversion is useful
   but does not establish these operations. Verify the exact cloud-built
   candidate in retail Reason before public release. SDK 5 requires Reason 14
   or later. See the [official SDK 5 readme](https://developer.reasonstudios.com/documentation/rack-extension-sdk/5.0.0/jukebox-readme).
3. **Set an honest performance envelope.** Test representative multi-instance
   songs on intended minimum hardware at low latency. Separate normal 1x live
   use from costly high-rate/oversampled configurations. Current native/SDK
   timing does not prove host deadlines: prior 48-kHz SDK trials had wall-time
   outliers, and the documented 96-kHz/32-frame native re-pick limit remains.
   Publish measured requirements rather than an unqualified low-CPU claim.
4. **Make the customer-facing package coherent.** Keep the corrected Shop copy,
   current panel/default previews and a concise manual together. Explain the
   six-string allocation, playing controls, separate Piezo route and deferred
   quality change. Describe shape/wood variants and studio projections as
   models derived from measurements, not measurements of every advertised
   guitar/position. Preserve required measurement attribution and adaptation
   notices. Rack does not embed JUCE; desktop licensing is a separate release
   check if VST3/AU are distributed. See [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)
   and [JUCE's licensing information](https://juce.com/get-juce/).
5. **Release one frozen candidate through the official process.** Device
   acceptance and Shop approval are distinct. Keep a clear source/package
   version, regression evidence, known limitations and a tested rollback/update
   plan. Do not equate a successful u45 build or Recon CLI exit with commercial
   acceptance. See the [official publishing workflow](https://developer.reasonstudios.com/learning-and-support/how-to-publish).

My release recommendation is focused listening/beta now, with commercial
release following resolution of the two audible concerns and real-session
acceptance. A complete physical moving-fret redesign is substantial future work unless listening demonstrates
that its current approximation is a release-blocking sound problem.
