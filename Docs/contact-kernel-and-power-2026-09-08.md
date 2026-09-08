# Continuous pluck contact and complete power observation

The pluck initializer convolves its periodic travelling-wave triangle with a
continuous Gaussian of standard deviation equal to the existing aperture.
That matches the variance of the former five atoms, but removes their repeated
unity-gain high-frequency lobes. The former transfer was
`cos(pi*n*a)^4`; the continuous transfer is `exp(-2*pi^2*n^2*a^2)`.
No displacement, velocity, pluck-position, harmonic-projection or body parameter
is introduced by this numerical-profile change.

A finite contact distributes the excitation over a region; its profile needs
an additional modelling assumption. Gaussian smoothing of finite string kinks
is described in Colin Gough's *Musical Acoustics*, section “Finite Spatial
Variation” (printed pp. 562–563,
[author chapter](https://www.math.ucdavis.edu/~saito/data/auditory/gough_musical-acoustics.pdf)).
The Gaussian here is a numerical profile, not a measured finger/pick shape or
an explicit friction/contact solver. The existing aperture magnitude and
velocity map remain calibration parameters. The later acoustic response still
depends on the strings, bridge and measured body; correcting the spatial
profile alone does not close the known velocity-response gap.

Periodic images cover both sides of the travelling-wave boundary. Signed
endpoint subtraction remains a DC change; there is no clipping. Double
triangle evaluation avoids the earlier candidate's broad-width cancellation
floor. A 641-pair constexpr table evaluates Gaussian corner integrals by cubic
Hermite interpolation, with a unit-corner absolute error bound of 6.20e-11.
Very broad profiles use the bounded uniform limit. There are no runtime
allocations or runtime table construction, and no extra steady-state string
processing for the contact profile.

Regenerate with `python3 Tools/GenerateGaussianAperture.py`; verify the checked-in
artifact with `python3 Tools/GenerateGaussianAperture.py --check`. CTest runs
that verification. The native contact test compares actual initialized waves
with independently integrated triangle coefficients times the continuous
Gaussian transfer, including sampling aliases. A separate dense audit of 28
profiles with 131072 samples each found maximum complex coefficient error
4.16e-9 and maximum resolved gain error 5.84e-6. A 109005-point sweep over
apertures 4.98636e-6 through 27.8536 remained finite and periodic.

## Matched recording evidence

Before the separate nylon decay/aperture calibration, frozen disjoint-pitch
comparisons with real Yamaha CM-40 classical and Walden G551E steel recordings
gave the following descriptor scores (lower is better):

| Instrument / split | Five-atom contact | Continuous contact |
| --- | ---: | ---: |
| Yamaha nylon training | 8.280994 | 8.196967 |
| Yamaha nylon validation | 8.182101 | 8.128073 |
| Walden steel training | 9.133216 | 9.132877 |
| Walden steel validation | 8.360820 | 8.383039 |
| Existing dry corpus training | 6.441610 | 6.417408 |
| Existing dry corpus validation | 6.525011 | 6.467851 |
| Existing flat-top steel holdout | 9.263118 | 9.289070 |

This is small, mixed acoustic evidence: nylon and pooled development scores
improve; the steel validation and flat-top holdout worsen slightly. It is not
a listening preference or proof that a Gaussian matches the actual contact.
Source and protocol: [Guitar Notes Dataset](https://www.kaggle.com/datasets/mohammedalkooheji/guitar-notes-dataset),
frozen author-labelled finger/thumb takes and disjoint pitches; complete
manifests remain in the 2026-09-08 calibration reports.

## Power ledger correction

The continuous profile exposed an existing observer error. The bridge's audio
derivatives suppress the initial displacement-shape step, while the passive
solver has already accepted that state. Multiplying those primed derivatives
omits positive initial work but counts the following elastic return. For the
steel low-E reproduction, the old ledger reached -4.08649e-14 total and
-5.48239e-14 body work at sample 1, although direct zero-state observation of
the actual bridge ports had minimum work exactly zero.

Power observation now has independent zero-state finite-difference histories
for motion and total/body/tail forces in both saddle coordinates. Acoustic
histories, radiation and string motion remain unchanged. The existing
-1e-14 cumulative-work gates remain unchanged. An independent test reconstructs
the actual bridge-port trajectory at 44.1/48/96 kHz through initial and repeated
plucks, with forward floating-point error bounds for fractional interpolation.

Before/after observer rendering is byte-identical for all 5,417,280 float
samples from 36 phrases spanning both materials, three rates, three capture
modes and Finger/Thumb. This separates the bookkeeping correction from the
acoustic contact-profile change. The powers use the existing reference-rate
normalization; they are not calibrated physical joules or a complete
string/hand/body energy ledger.
