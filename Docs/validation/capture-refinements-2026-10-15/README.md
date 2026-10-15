# Capture refinements: four experiments, no production change

The current microphone path was tested from `90d2d8c`. Twelve native Performer
scores span all four Spruce Shapes and Finger, Pick and Thumb at the plug-in's
Room .5, Piezo Mix .2 and Release Noise .1. Each score lasts 12 seconds and
includes register-spanning notes, four chords, moving plucks and a sustained
bass/octave group. See `Tools/CaptureRefinementAudit` for the deterministic
score, compilation and analysis commands. Raw float audio is preserved outside
Git at `/workspace/acustra-capture-experiments`.

1. **Relax or reposition the presence contour.** The 2026-10-10 matched-capture
   audit found the previous fit had mistaken stereo phase cancellation for
   missing presence and proposed moving its 1.4 kHz section to 1.6 or 1.8 kHz.
   Three bounded alternatives were measured: reduce the 1 kHz peak by 1.5 dB
   and the 1.4 kHz peak by 2 dB; reduce the 1 kHz peak and move the unchanged
   +6 dB section to 1.8 kHz; or move it while reducing it to +4 dB. These are
   alternatives in one experiment, not three independent improvements.
2. **Clean subsonic pressure.** A 35 Hz second-order Butterworth high-pass on
   microphone pressure lowers the 0–35 Hz energy by a median 3.67 dB after
   matching RMS, but that energy is already 36–39 dB below total. It rotates
   the microphone phase against the piezo and changes peaks by −0.65 to
   +1.08 dB. No useful default improvement is established.
3. **Soften the highest treble.** A −1.5 dB shelf at 4 kHz lowers 4–8 kHz by
   0.71 dB and 8–16 kHz by 1.43 dB at matched RMS. The full-band difference
   is only −34 dB RMS at the median. No current reference evidence identifies
   this region as an excess; extra dulling is not selected.
4. **Test the per-mode brightness tilt's contribution.** Removing the existing
   0.4375 dB/octave modal weighting lowers 8–16 kHz by 2.27 dB at matched RMS
   and raises native phrase level by 0.84 dB median. It reverses an accepted
   brightness choice and does not establish a better residual response. The
   causal pressure-tilt replacement originally considered was not built; this
   diagnostic supplies no reason to replace the accepted tilt.

## Presence reference check and decision

All eight embedded Eastman flat-top references were freshly exported with the
baseline renderer and shipping calibration. A separate native note probe
reproduces all eight baseline model files sample for sample, then changes only
the candidate capture coefficients. The comparison uses per-channel power,
the existing 0.03–1 second onset-relative window, and equal total spectral power
per note. These are previously used notes from one take, not untouched data or
independent evidence across guitars. No one listened during this screen.

| Contour | Pooled 0.8–3.2 kHz error, dB RMS | Median occupied-note error, dB RMS |
|---|---:|---:|
| Current | 4.675 | 7.608 |
| Reduce both boosts | 4.920 | 7.186 |
| Move +6 dB peak to 1.8 kHz | 4.485 | 7.843 |
| Move peak and reduce to +4 dB | 4.835 | 6.900 |

Individual-note comparisons use the same band mask for all variants: reference
and baseline must both have energy within 40 dB of their largest third-octave
band, restricted to 0.8–3.2 kHz. Full-range spectral RMS is unsuitable here:
unoccupied bands dominate it, so that diagnostic was discarded. Pooled spectra
contain all six third-octave centres from 1 to 3.175 kHz.

The old reported 1.26 kHz excess is not present in these current reference
comparisons: the reference is just 0.02 dB below the current model there. The
1.8 kHz shift reduces pooled error 4%, but worsens the median occupied-note
error. At 2 kHz it turns a 3.09 dB model deficit into a 0.98 dB excess; at
3.175 kHz it worsens the excess from 4.67 to 7.93 dB. The 1.587 kHz deficit
falls only from 7.41 to 6.83 dB. Historical evidence therefore gives a useful
hypothesis but does not justify assuming the post-blend engine needs the old
correction.

**Decision:** preserve production capture. All four avenues were explored;
none establishes a sufficiently consistent improvement for global adoption.
The 1.8 kHz shift remains a concrete artistic option, with two optional RMS
matched A/B pairs in `/workspace/acustra-capture-experiments/listening/` and a
separate key. No new UI parameter, production DSP state, physical calibration
or generated table was changed by this work.
