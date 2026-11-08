# Piezo loading correction — 2026-11-08

Local 1.9.31 candidate. The existing saddle observation and electrical circuit
are retained; the saddle's load now follows the actual normal junction sum
instead of the fixed Standard tuning used at preparation.

The normal-load bound is derived from existing supported model limits:
`2 × sqrt(2) × 2^(100/1200) × 3.732363224 = 11.184452464 N s/m`.
The factor two covers the live and retained branch per string, sqrt(2) is the
existing bend impedance limit, and 100 cents is the host master-tuning bound.
Every alternate tuning has no higher open pitch than Standard on any string.
Zero includes disabled or empty ports. Parallel polarization and pickup
sensitivity weights do not contribute extra normal loading.
The prepared numerical domain rounds this bound outward to 11.185 N s/m
and then to the next float, covering accumulation of twelve float ports.
The actual mechanical loads are not rounded. Endpoint tests exercise all
720 string orders with both branches at their maximum float load.

Sixty-five filter designs are prepared per rate, with an exact Standard
anchor and 32 intervals on either side. Linear interpolation is bounded to
this range and skipped when the float load is unchanged. No fitting,
allocation, logarithm or trigonometry occurs in that update. The direct-form
history and all circuit histories remain intact. Reset clears those histories
and selects the current unbent tuning/master load.

`PiezoCircuitTests` compares 257 load values against the independent analog
force/velocity circuit and separately against a freshly fitted design. At
8/16/44.1/48/96/192/384/768 kHz, worst interpolation error is 0.001250 dB and
0.014418 degrees; DC error is at most 5.42e-5. The 768 kHz bank is tested
independently of the normal plug-in's whole-engine rate clamp, covering
Reason's 192 kHz × 4 mode. Jury/Schur inequalities pass at
every point. The independent analog error across the full load range is
0.58993 dB at 44.1 kHz and 0.37882 dB at 48 kHz; the original stricter
Standard-only response gates continue to pass. The existing low-rate FIR
approximation is retained (its 8 kHz analog error reaches 2.69 dB).

Live DADGAD tuning, +100-cent master tuning and a three-semitone member bend
settle to the independently derived load and match a fresh final setup at
44.1/48/96 kHz. The sensor follows the actual retained-tail junction sum;
coefficient updates preserve every circuit history, and reset removes the
old bend/tail load and histories. Focused saddle/load/silence tests and C++17
compilation pass. Broader tests, mechanical parity, isolated shipping-baseline
A/B renders and CPU measurements are retained in the external review package.

An unchanged 8.5-second production-Performer score exercises repeated notes,
Standard/DADGAD changes, master tuning and MPE bends. Against frozen 1.9.30,
all 816,000 microphone sample values are bit-identical with Piezo Mix zero,
as are 89,250 values from fourteen mechanical/input observer lanes. These
include junction load/motion, bridge force/power and unfiltered piezo force.
Only the sensor's transfer response changes. This is a tested trajectory,
not a proof of every possible mechanical state.

This fixes a stale parameter in the one-way sensor approximation. It does
not introduce electrical/mechanical backreaction or prove full energy closure.
No publication or listener preference is claimed.
