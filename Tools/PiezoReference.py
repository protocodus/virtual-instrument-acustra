#!/usr/bin/env python3
"""Component-level reference simulation of the under-saddle piezo chain.

The engine's piezo (AcustraEngine::renderPiezo, PiezoDesign, Docs/decisions.md
2026-09-29 "Accurate piezo chain") is a real, documented signal chain solved
at audio rate with a few piecewise-linear shortcuts. This tool is the slow,
independent model it is checked against: every part of the chain written as
the component it is and integrated with a stiff solver at tight tolerance.
It shares no matrices, coefficients or code with the engine; only the part
values below, which the engine repeats with the same sources.

The chain, in signal order:

1. Mechanics. The engine gives the force a rigid saddle would press on its
   element (F_r). The saddle (mass M) rides on the element (stiffness k,
   material loss c_m) over a bridge whose local conductance is G, while the
   strings load the saddle with their summed wave impedance SZ. Compressing
   the element relieves F_r through both sides: the saddle moves against the
   strings, the base yields into the bridge. States: the saddle's velocity
   and the element's compression, both as deviations from the engine's rigid
   solution; the base yields through G without a state of its own.
2. Transducer: a charge source d * F_p across the element's capacitance Cp,
   written as a voltage source 0.2 V/N * F_p in series with Cp (Ovation EA-68,
   Zollner ch.6), with a 1 TOhm insulation leak to ground so the jack has a
   DC operating point.
3. Cable: 3 m of Mogami 2524 (130 pF/m) from the jack to ground.
4. The preamp: ESP Project 202 Fig. 1 (Rod Elliott, sound-au.com/project202),
   a bootstrapped OPA2134 buffer and a gain-of-two OPA2134 stage on one 9 V
   battery, into a Radial PZ-DI's 1 MOhm input with the volume pot at full.
   Every resistor and capacitor of the figure is a component here; the LED
   branch and the supply decoupling only load the battery and are left out.
   Each op-amp is a macro-model from the TI datasheet (SBOS058B): 120 dB
   open-loop gain, 8 MHz gain-bandwidth, 20 V/us slew (a tanh input stage),
   8 pF differential and 6 pF common-mode input capacitance, 5 pA bias
   current, an input stage that saturates at the common-mode limits
   (typical: 2 V inside each rail) and an output that stops at the
   datasheet swing for its load with a 1 mV exponential knee. The 10 Ohm
   open-loop output impedance is left out: with the loop closed it is
   milliohms, and as a resistor it gave the system 5e9 /s modes. The diodes
   are 1N4148s (Shockley, IS 2.52 nA, N 1.752, 4 pF junction capacitance;
   the widely reposted SPICE model). Every node the op-amps do not drive has
   1 pF of stray capacitance to ground (chosen; board wiring), which makes
   the node equations an ordinary differential system, except the output,
   which has the cable to the DI instead, 5 m at about 100 pF/m (chosen).

The tool computes the DC operating point (Newton), the small-signal response
(the Jacobian at that point, i.e. a SPICE AC analysis), periodic steady
states under a sine (shooting on one period), and transients (scipy
solve_ivp, Radau, rtol 1e-10 and 1 nV absolute, analytic Jacobian,
cross-checked with BDF), and writes the fixtures Tests/PiezoCircuitTests.cpp compares the engine with:

  python3 Tools/PiezoReference.py --write-fixtures Tests/Fixtures
  python3 Tools/PiezoReference.py --write-blamp-table Source/DSP/PiezoBlampTable.h
  python3 Tools/PiezoReference.py --report
  python3 Tools/PiezoReference.py --self-test

Nothing is downloaded and no audio is written. NumPy and SciPy are required.
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
from scipy.integrate import cumulative_trapezoid, solve_ivp

# numpy 2 renamed trapz to trapezoid (and removed trapz in 2.x); 1.x has only trapz.
trapezoid = getattr(np, "trapezoid", None) or np.trapz

# ---------------------------------------------------------------- mechanics
# The engine's string table (AcustraEngine.cpp: steelTensionNewtons), for
# the summed wave impedance the strings load the saddle with:
# stringImpedance at the standard open notes.
STEEL_TENSION = (110.759, 128.554, 133.002, 133.892, 103.643, 104.088)
OPEN_MIDI = (40, 45, 50, 55, 59, 64)


def string_impedance_sum() -> float:
    total = 0.0
    for index, midi in enumerate(OPEN_MIDI):
        frequency = 440.0 * 2.0 ** ((midi - 69) / 12.0)
        total += STEEL_TENSION[index] / (2.0 * 0.648 * frequency)
    return total


# Saddle and element (Docs/decisions.md 2026-09-29 has each source):
#   M: a bone saddle 72 x 3 x 9.5 mm at 1.95 g/cc (chosen within 2.7-5.1 g);
#   f0: the element and its seat under that mass, chosen within 5-7 kHz;
#   eta: the element's loss factor, 1/18 (Zollner's rig Q as a bound);
#   G: the bridge's conductance there, the mean of Re(Y) over 5-7 kHz of the
#      measured Fylde mobility (Carcagno et al. 2018) with the saddle removed.
MECH = {"M": 3.8e-3, "f0": 6000.0, "eta": 1.0 / 18.0, "G": 1.59e-3}
VOLTS_PER_NEWTON = 0.2          # Zollner ch.6: Ovation EA-68, 0.2 V/N at 1.45 nF


def mechanics(**override) -> dict:
    values = dict(MECH)
    values.update(override)
    omega = 2.0 * math.pi * values["f0"]
    values["k"] = values["M"] * omega * omega
    values["cm"] = values["eta"] * values["k"] / omega
    values["SZ"] = values.get("SZ", string_impedance_sum())
    return values


def mechanical_polynomials(**override):
    """F_p / F_r as numerator and denominator polynomials in s (highest
    power first): H = Zk Q / (1 + Zk Q), Zk = k/s + c_m,
    Q = 1/(s M + SZ) + G."""
    m = mechanics(**override)
    numerator = np.polymul([m["cm"], m["k"]], [m["G"] * m["M"], 1.0 + m["G"] * m["SZ"]])
    denominator = np.polyadd(numerator, [m["M"], m["SZ"], 0.0])
    return numerator, denominator


# ---------------------------------------------------------------- circuit
NET = {
    # transducer and cable
    "Cp": 1.45e-9, "Ccab": 390e-12, "Rleak": 1.0e12,
    # ESP Project 202 Fig. 1
    "C1": 4.7e-9, "R1": 1.0e6, "R2": 1.0e6, "R3": 1.0e6, "C2": 33e-6,
    "R4": 3.9e3, "C3": 220e-9, "R5": 47e3, "R6": 47e3, "R7": 10e3,
    "R8": 10e3, "C4": 33e-6, "R9": 100.0, "C5": 10e-6, "VR1": 10e3,
    # Radial PZ-DI input, and 5 m of instrument cable to it (chosen: about
    # 100 pF/m); it and R9 put a pole at 3 MHz
    "Rdi": 1.0e6, "Cout": 500e-12,
    "VCC": 9.0,
    # stray capacitance at every node (chosen)
    "Cstray": 1.0e-12,
}
OPAMP = {  # TI SBOS058B, OPA2134
    "A0": 1.0e6, "GBW": 8.0e6, "SR": 20.0e6, "Ro": 10.0,
    "Cdiff": 8.0e-12, "Ccm": 6.0e-12, "IB": 5.0e-12,
    "cm_margin": 2.0,        # typical common-mode range, 2 V inside each rail
    # output swing below V+ / above V-: (RL = 10 kOhm, RL = 2 kOhm)
    "swing_high": (1.2, 1.5), "swing_low": (0.5, 1.2),
    "knee": 1.0e-3,
}
DIODE = {"IS": 2.52e-9, "N": 1.752, "CJ": 4.0e-12}
THERMAL_VOLTAGE = 1.380649e-23 * 298.15 / 1.602176634e-19


def swing(load_ohms: float) -> tuple[float, float]:
    """The OPA2134's output limits for a load, interpolated in conductance
    between the datasheet's 10 kOhm and 2 kOhm rows (held at the 10 kOhm row
    for lighter loads)."""
    share = min(max((1.0 / load_ohms - 1.0e-4) / (5.0e-4 - 1.0e-4), 0.0), 1.0)
    high = OPAMP["swing_high"][0] + share * (OPAMP["swing_high"][1] - OPAMP["swing_high"][0])
    low = OPAMP["swing_low"][0] + share * (OPAMP["swing_low"][1] - OPAMP["swing_low"][0])
    return NET["VCC"] - high, low


def u1b_load() -> float:
    pot = 1.0 / (1.0 / NET["VR1"] + 1.0 / NET["Rdi"])
    return 1.0 / (1.0 / (NET["R7"] + NET["R8"]) + 1.0 / (NET["R9"] + pot))


def u1a_load() -> float:
    return 1.0 / (1.0 / NET["R5"] + 1.0 / NET["R6"])


def softplus(x):
    return np.logaddexp(0.0, x)


def sigmoid(x):
    return 0.5 * (1.0 + np.tanh(0.5 * x))


def soft_limit(value, low, high, knee):
    """value, softly held between low and high with an exponential knee."""
    out = value - knee * softplus((value - high) / knee) + knee * softplus((low - value) / knee)
    slope = 1.0 - sigmoid((value - high) / knee) - sigmoid((low - value) / knee)
    return out, slope


NODES = ("J", "IN", "B", "X", "Y", "N", "M", "P", "Q")
J, IN, B, X, Y, N, M, P, Q = range(9)
NODE_COUNT = len(NODES)
MECH_STATES = 2
NODE_OFFSET = MECH_STATES
U1A_STATE = NODE_OFFSET + NODE_COUNT
U1B_STATE = U1A_STATE + 1
STATE_COUNT = U1B_STATE + 1


class Circuit:
    """The whole chain as x' = f(t, x, F, F'), x = [v_s, x_c, nodes..., U1A,
    U1B]. Each op-amp's output is its internal node through the output
    stage's swing limit: with 120 dB of loop gain the open-loop 10 Ohm is
    milliohms closed-loop, and as a resistor it put 5e9 /s modes in the
    system that swamped the solver's tolerance with round-off."""

    def __init__(self, **mech_override):
        self.mech = mechanics(**mech_override)
        m = self.mech
        d1 = 1.0 + m["G"] * m["cm"]
        # Mechanics, linear: xm' = Am xm + Bm F; F_p = Cf xm + Df F.
        self.Am = np.array([
            [-(m["SZ"] + m["cm"] / d1) / m["M"], -(m["k"] / d1) / m["M"]],
            [1.0 / d1, -m["G"] * m["k"] / d1]])
        self.Bm = np.array([(1.0 - m["cm"] * m["G"] / d1) / m["M"], m["G"] / d1])
        self.Cf = np.array([m["cm"] / d1, m["k"] / d1])
        self.Df = m["cm"] * m["G"] / d1
        # Capacitance matrix of the nine nodes. Capacitors to U1A's output
        # O1 (C3, the diodes' junctions, U1A's differential input) count as
        # to ground here, and O1's rate enters as an injection.
        C = np.zeros((NODE_COUNT, NODE_COUNT))

        def cap(a, b, value):
            for node in (a, b):
                if node is not None:
                    C[node, node] += value
            if a is not None and b is not None:
                C[a, b] -= value
                C[b, a] -= value

        self.c_in_o1 = 2.0 * DIODE["CJ"] + OPAMP["Cdiff"]
        cap(J, None, NET["Cp"] + NET["Ccab"])
        cap(J, IN, NET["C1"])
        cap(B, X, NET["C2"])
        cap(Y, None, NET["C3"])
        cap(M, None, NET["C4"])
        cap(P, Q, NET["C5"])
        cap(Q, None, NET["Cout"])
        cap(IN, None, self.c_in_o1)
        cap(IN, None, OPAMP["Ccm"])
        cap(Y, N, OPAMP["Cdiff"])
        cap(Y, None, OPAMP["Ccm"])
        cap(N, None, OPAMP["Ccm"])
        for node in (J, IN, B, X, Y, N, M):
            cap(node, None, NET["Cstray"])
        self.C = C
        self.Cinv = np.linalg.inv(C)
        # Linear resistors: currents into nodes = Is - G V (resistors to an
        # op-amp output are handled with it).
        G = np.zeros((NODE_COUNT, NODE_COUNT))
        Is = np.zeros(NODE_COUNT)

        def res(a, b, value):
            g = 1.0 / value
            if a == "VCC" or b == "VCC":
                node = b if a == "VCC" else a
                G[node, node] += g
                Is[node] += g * NET["VCC"]
                return
            for node in (a, b):
                if node is not None:
                    G[node, node] += g
            if a is not None and b is not None:
                G[a, b] -= g
                G[b, a] -= g

        res(J, None, NET["Rleak"])
        res("VCC", B, NET["R1"])
        res(B, None, NET["R2"])
        res(IN, B, NET["R3"])
        res(X, None, NET["R4"])        # R4 to O1: the O1 term is added in evaluate
        res("VCC", Y, NET["R5"])
        res(Y, None, NET["R6"])
        res(N, None, NET["R7"])        # R7 to O2
        res(N, M, NET["R8"])
        res(P, None, NET["R9"])        # R9 to O2
        res(Q, None, NET["VR1"])
        res(Q, None, NET["Rdi"])
        self.G = G
        self.Is = Is
        self.u1a_rails = swing(u1a_load())
        self.u1b_rails = swing(u1b_load())
        margin = OPAMP["cm_margin"]
        self.cm_limits = (margin, NET["VCC"] - margin)
        self.omega_u = 2.0 * math.pi * OPAMP["GBW"]
        self.omega_p = self.omega_u / OPAMP["A0"]
        self.slew_volts = OPAMP["SR"] / self.omega_u
        self.nvt = DIODE["N"] * THERMAL_VOLTAGE

    # ------------------------------------------------------------ pieces
    def opamp(self, v, vp, vn, rails, vn_follows=False):
        """The internal node's rate, the output, and their slopes. With
        vn_follows the inverting input is the output itself (a follower)."""
        knee = OPAMP["knee"]
        out, dout = soft_limit(v, rails[1], rails[0], knee)
        if vn_follows:
            vn = out
        low, high = self.cm_limits
        p, dp = soft_limit(vp, low, high, knee)
        n, dn = soft_limit(vn, low, high, knee)
        th = np.tanh((p - n) / self.slew_volts)
        dth = 1.0 - th * th
        # The internal node is held 0.5 V outside the output's swing, so
        # overload recovers at the slew rate, not over the dominant pole.
        clamp_knee = 0.01
        kc = 1.0e9
        a = (v - rails[0] - 0.5) / clamp_knee
        b = (rails[1] - 0.5 - v) / clamp_knee
        rate = (self.omega_u * self.slew_volts * th - self.omega_p * (v - 0.5 * NET["VCC"])
                - kc * clamp_knee * (softplus(a) - softplus(b)))
        drate_dv = -self.omega_p - kc * (sigmoid(a) + sigmoid(b))
        drate_dvp = self.omega_u * dth * dp
        drate_dvn = -self.omega_u * dth * dn
        if vn_follows:
            drate_dv += drate_dvn * dout
            drate_dvn = 0.0
        return rate, drate_dv, drate_dvp, drate_dvn, out, dout

    def outputs(self, x):
        """U1A's and U1B's output voltages."""
        rails1, rails2, knee = self.u1a_rails, self.u1b_rails, OPAMP["knee"]
        return (soft_limit(x[U1A_STATE], rails1[1], rails1[0], knee)[0],
                soft_limit(x[U1B_STATE], rails2[1], rails2[0], knee)[0])

    def evaluate(self, x, force, dforce, want_jacobian=True):
        """Returns (x', dx'/dx, dx'/dF, dx'/dF')."""
        xm = x[:MECH_STATES]
        V = x[NODE_OFFSET:U1A_STATE]
        v1, v2 = x[U1A_STATE], x[U1B_STATE]
        f = np.empty(STATE_COUNT)
        dxm = self.Am @ xm + self.Bm * force
        f[:MECH_STATES] = dxm
        dFp = self.Cf @ dxm + self.Df * dforce
        # The op-amps: U1A follows IN (its - input is its output), U1B
        # takes Y and N.
        r1, r1v, r1p, _, o1, do1 = self.opamp(v1, V[IN], None, self.u1a_rails, True)
        r2, r2v, r2p, r2n, o2, do2 = self.opamp(v2, V[Y], V[N], self.u1b_rails)
        o1_rate = do1 * r1
        # Node currents (into each node).
        I = self.Is - self.G @ V
        dI = np.zeros((NODE_COUNT, STATE_COUNT))
        dI[:, NODE_OFFSET:U1A_STATE] = -self.G
        I[J] += NET["Cp"] * VOLTS_PER_NEWTON * dFp
        dI[J, :MECH_STATES] = NET["Cp"] * VOLTS_PER_NEWTON * (self.Cf @ self.Am)
        I[X] += o1 / NET["R4"]
        dI[X, U1A_STATE] += do1 / NET["R4"]
        I[N] += o2 / NET["R7"]
        dI[N, U1B_STATE] += do2 / NET["R7"]
        I[P] += o2 / NET["R9"]
        dI[P, U1B_STATE] += do2 / NET["R9"]
        # Capacitors to O1 carry C dO1/dt into IN and Y.
        I[IN] += self.c_in_o1 * o1_rate
        I[Y] += NET["C3"] * o1_rate
        # D1 and D2, antiparallel between IN and O1.
        vd = V[IN] - o1
        arg = min(max(vd / self.nvt, -80.0), 80.0)
        i_d = 2.0 * DIODE["IS"] * math.sinh(arg)
        g_d = 2.0 * DIODE["IS"] * math.cosh(arg) / self.nvt
        I[IN] -= i_d
        dI[IN, NODE_OFFSET + IN] -= g_d
        dI[IN, U1A_STATE] += g_d * do1
        # Input bias currents (U1A's inverting input draws from its own
        # output).
        for node in (IN, Y, N):
            I[node] -= OPAMP["IB"]
        f[NODE_OFFSET:U1A_STATE] = self.Cinv @ I
        f[U1A_STATE] = r1
        f[U1B_STATE] = r2
        if not want_jacobian:
            return f, None, None, None
        # O1's rate's slopes: d(do1 r1) = ddo1 r1 dv1 + do1 dr1.
        knee = OPAMP["knee"]
        rails = self.u1a_rails
        sa = sigmoid((v1 - rails[0]) / knee)
        sb = sigmoid((rails[1] - v1) / knee)
        ddo1 = -(sa * (1.0 - sa) - sb * (1.0 - sb)) / knee
        dr1 = np.zeros(STATE_COUNT)
        dr1[U1A_STATE] = r1v
        dr1[NODE_OFFSET + IN] = r1p
        do1_rate = do1 * dr1
        do1_rate[U1A_STATE] += ddo1 * r1
        dI[IN] += self.c_in_o1 * do1_rate
        dI[Y] += NET["C3"] * do1_rate
        Jac = np.zeros((STATE_COUNT, STATE_COUNT))
        Jac[:MECH_STATES, :MECH_STATES] = self.Am
        Jac[NODE_OFFSET:U1A_STATE, :] = self.Cinv @ dI
        Jac[U1A_STATE] = dr1
        Jac[U1B_STATE, U1B_STATE] = r2v
        Jac[U1B_STATE, NODE_OFFSET + Y] = r2p
        Jac[U1B_STATE, NODE_OFFSET + N] = r2n
        dF = np.zeros(STATE_COUNT)
        dF[:MECH_STATES] = self.Bm
        injection = np.zeros(NODE_COUNT)
        injection[J] = NET["Cp"] * VOLTS_PER_NEWTON * (self.Cf @ self.Bm)
        dF[NODE_OFFSET:U1A_STATE] = self.Cinv @ injection
        ddF = np.zeros(STATE_COUNT)
        injection = np.zeros(NODE_COUNT)
        injection[J] = NET["Cp"] * VOLTS_PER_NEWTON * self.Df
        ddF[NODE_OFFSET:U1A_STATE] = self.Cinv @ injection
        return f, Jac, dF, ddF

    # ------------------------------------------------------------ analyses
    def operating_point(self) -> np.ndarray:
        """Newton on the currents (not the rates, whose capacitance inverse
        makes the floating jack ill-conditioned), from the obvious guess."""
        x = np.zeros(STATE_COUNT)
        half = 0.5 * NET["VCC"]
        for node in (IN, B, X, Y, N, M, P):
            x[NODE_OFFSET + node] = half
        x[U1A_STATE] = x[U1B_STATE] = half
        for _ in range(100):
            f, Jac, _, _ = self.evaluate(x, 0.0, 0.0)
            g = f.copy()
            Jg = Jac.copy()
            g[NODE_OFFSET:U1A_STATE] = self.C @ f[NODE_OFFSET:U1A_STATE]
            Jg[NODE_OFFSET:U1A_STATE, :] = self.C @ Jac[NODE_OFFSET:U1A_STATE, :]
            step = np.linalg.solve(Jg, -g)
            x = x + step
            if np.max(np.abs(step)) < 1.0e-15:
                break
        return x

    def small_signal(self, frequencies, x0=None):
        """Complex responses from F_r (newtons) to the named outputs."""
        x0 = self.operating_point() if x0 is None else x0
        _, A, B0, B1 = self.evaluate(x0, 0.0, 0.0)
        outputs = self.output_rows(x0)
        result = {name: np.empty(len(frequencies), complex) for name in outputs}
        identity = np.eye(STATE_COUNT)
        for index, frequency in enumerate(frequencies):
            s = 2j * math.pi * frequency
            response = np.linalg.solve(s * identity - A, B0 + s * B1)
            for name, (row, direct) in outputs.items():
                result[name][index] = row @ response + direct
        return result

    def output_rows(self, x0):
        """Linear output maps about x0: name -> (row, feedthrough of F)."""
        rows = {}

        def node(which):
            row = np.zeros(STATE_COUNT)
            row[NODE_OFFSET + which] = 1.0
            return row

        rows["Vout"] = (node(Q), 0.0)
        rows["VJ"] = (node(J), 0.0)
        rows["VIN"] = (node(IN), 0.0)
        row = np.zeros(STATE_COUNT)
        rails, knee = self.u1b_rails, OPAMP["knee"]
        row[U1B_STATE] = soft_limit(x0[U1B_STATE], rails[1], rails[0], knee)[1]
        rows["O2"] = (row, 0.0)
        row = np.zeros(STATE_COUNT)
        row[:MECH_STATES] = VOLTS_PER_NEWTON * self.Cf
        rows["Voc"] = (row, VOLTS_PER_NEWTON * self.Df)
        return rows

    def o2(self, x):
        rails, knee = self.u1b_rails, OPAMP["knee"]
        return soft_limit(x[U1B_STATE], rails[1], rails[0], knee)

    def signals(self, x, force):
        """Named signals of a trajectory x (states, samples)."""
        V = x[NODE_OFFSET:U1A_STATE]
        o1, o2 = self.outputs(x)
        return {"Vout": V[Q], "VJ": V[J], "VIN": V[IN], "O1": o1,
                "O2": o2, "Voc": VOLTS_PER_NEWTON * (self.Cf @ x[:MECH_STATES] + self.Df * force),
                "VC3": o1 - V[Y], "VC4": V[M], "VC5": V[P] - V[Q]}

    def simulate(self, force, dforce, t_span, x0, t_eval=None, method="Radau",
                 rtol=1.0e-10, max_step=np.inf, dense=False):
        # 1 nV on every voltage, and what a nanovolt at the element is in
        # the saddle's velocity and the element's compression: tighter, and
        # the round-off of states that sit at zero stalls the step size.
        atol = np.full(STATE_COUNT, 1.0e-9)
        atol[0] = 1.0e-11          # saddle velocity, m/s
        atol[1] = 1.0e-15          # element compression, m

        def fun(t, x):
            return self.evaluate(x, force(t), dforce(t), False)[0]

        def jac(t, x):
            return self.evaluate(x, force(t), dforce(t))[1]

        return solve_ivp(fun, t_span, x0, method=method, jac=jac, rtol=rtol,
                         atol=atol, t_eval=t_eval, dense_output=dense,
                         max_step=max_step)

    def periodic(self, amplitude, frequency, x0=None, iterations=6, rtol=1.0e-10):
        """The periodic steady state under F_r = A sin(2 pi f t), by shooting
        on one period with a finite-difference monodromy matrix."""
        period = 1.0 / frequency
        omega = 2.0 * math.pi * frequency
        force = lambda t: amplitude * math.sin(omega * t)
        dforce = lambda t: amplitude * omega * math.cos(omega * t)
        x = self.operating_point() if x0 is None else x0.copy()
        # A few transient periods first bring the fast and mid modes close.
        for _ in range(3):
            x = self.simulate(force, dforce, (0.0, period), x, rtol=rtol).y[:, -1]
        steps = np.array([1e-9, 1e-12] + [1e-6] * NODE_COUNT + [1e-6, 1e-6])
        for _ in range(iterations):
            end = self.simulate(force, dforce, (0.0, period), x, rtol=rtol).y[:, -1]
            residual = end - x
            if np.max(np.abs(residual[NODE_OFFSET:])) < 1.0e-12:
                break
            monodromy = np.empty((STATE_COUNT, STATE_COUNT))
            for column in range(STATE_COUNT):
                shifted = x.copy()
                shifted[column] += steps[column]
                moved = self.simulate(force, dforce, (0.0, period), shifted, rtol=rtol).y[:, -1]
                monodromy[:, column] = (moved - end) / steps[column]
            x = x + np.linalg.solve(np.eye(STATE_COUNT) - monodromy, residual)
        return x, force, dforce

    def harmonics(self, amplitude, frequency, count=5, points=4096, rtol=1.0e-10,
                  band=21600.0):
        """Complex Fourier coefficients 1..count of Vout (peak volts) in the
        periodic steady state, and the total harmonic distortion of the
        harmonics below `band` (0.45 of 48 kHz), which a host-rate chain can
        carry."""
        x, force, dforce = self.periodic(amplitude, frequency, rtol=rtol)
        period = 1.0 / frequency
        times = np.arange(points) * period / points
        run = self.simulate(force, dforce, (0.0, period), x, t_eval=times, rtol=rtol)
        vout = run.y[NODE_OFFSET + Q]
        spectrum = np.fft.rfft(vout) * 2.0 / points
        coefficients = spectrum[1:count + 1]
        top = min(int(band / frequency), points // 4)
        total = math.sqrt(float(np.sum(np.abs(spectrum[2:top + 1]) ** 2)))
        return coefficients, total / abs(spectrum[1])


# ---------------------------------------------------------------- helpers
def clip_amplitude(circuit: Circuit, frequency: float, x0=None) -> float:
    """F_r amplitude (N) at which U1B's small-signal output reaches its
    nearer rail."""
    x0 = circuit.operating_point() if x0 is None else x0
    gain = abs(circuit.small_signal([frequency], x0)["O2"][0])
    o2_bias, _ = circuit.o2(x0)
    headroom = min(circuit.u1b_rails[0] - o2_bias, o2_bias - circuit.u1b_rails[1])
    return headroom / gain


def burst(amplitude, frequency, length, ramp):
    """A sine burst with raised-cosine ramps, and its derivative."""
    omega = 2.0 * math.pi * frequency

    def envelope(t):
        if t <= 0.0 or t >= length:
            return 0.0, 0.0
        if t < ramp:
            phase = math.pi * t / ramp
            return 0.5 * (1.0 - math.cos(phase)), 0.5 * math.pi / ramp * math.sin(phase)
        if t > length - ramp:
            phase = math.pi * (length - t) / ramp
            return 0.5 * (1.0 - math.cos(phase)), -0.5 * math.pi / ramp * math.sin(phase)
        return 1.0, 0.0

    def force(t):
        return amplitude * envelope(t)[0] * math.sin(omega * t)

    def dforce(t):
        e, de = envelope(t)
        return amplitude * (de * math.sin(omega * t) + e * omega * math.cos(omega * t))

    return force, dforce


def decibels(value):
    return 20.0 * np.log10(np.abs(value))



def _map(function, jobs):
    """In parallel where the platform allows processes, else in turn."""
    try:
        with ProcessPoolExecutor() as pool:
            return list(pool.map(function, jobs))
    except (OSError, PermissionError):
        return [function(job) for job in jobs]


def _harmonics_job(job):
    frequency, _level, amplitude = job
    coefficients, thd = Circuit().harmonics(amplitude, frequency)
    return np.abs(coefficients), thd


def _burst_job(amplitude):
    """Vout through one burst at 48 kHz, then the slow states every 5 ms
    through its recovery, all about the operating point."""
    steel = Circuit()
    x0 = steel.operating_point()
    force, dforce = burst(amplitude, BURST["frequency"], BURST["length"], BURST["ramp"])
    wave_times = np.arange(int(BURST["waveform"] * THD_RATE)) / THD_RATE
    tail_times = BURST["length"] + BURST["tail_step"] * np.arange(
        int(round((BURST["tail"] - BURST["length"]) / BURST["tail_step"])) + 1)
    first = steel.simulate(force, dforce, (0.0, BURST["length"]), x0,
                           t_eval=wave_times[wave_times <= BURST["length"]])
    quiet = lambda t: 0.0
    second = steel.simulate(quiet, quiet, (BURST["length"], BURST["tail"] + 1e-6), first.y[:, -1],
                            t_eval=np.union1d(wave_times[wave_times > BURST["length"]], tail_times))
    times = np.concatenate([first.t, second.t])
    states = np.concatenate([first.y, second.y], axis=1)
    signals = steel.signals(states, np.array([force(t) for t in times]))
    bias = steel.signals(x0[:, None], np.zeros(1))
    at = {round(t * 1e7): i for i, t in enumerate(times)}
    wave = [signals["Vout"][at[round(t * 1e7)]] - bias["Vout"][0] for t in wave_times]
    tail = [[signals[k][at[round(t * 1e7)]] - bias[k][0]
             for k in ("Vout", "VC3", "VC4", "VC5", "VIN")] for t in tail_times]
    return wave, tail


# ---------------------------------------------------------------- BLAMP table
# The engine band-limits U1B's clip corners with a 12-tap BLAMP (band-limited
# ramp) residual: a Kaiser-windowed sinc (cutoff 0.45 fs, beta 8, six
# samples either side) integrated twice, less the ideal ramp, tabulated at
# 129 fractional positions and interpolated linearly between them
# (Source/DSP/PiezoBlampTable.h). The 4-point polyBLAMP (Esqueda, Bilbao
# and Valimaki, IEEE TSP 2016) was measured first: its cubic B-spline droops
# inside the band, every corner leaves an extra 1/6 of its slope change at
# DC, and against the band-limited clip it moved HD2-HD5 by up to 7 dB at
# 3 kHz and left a clipping strum 12 dB worse than a bare clip. The
# windowed sinc keeps them within 0.25 dB below 18 kHz and the strum 7 dB
# better than a bare clip, for four more samples of latency.
BLAMP_HALF, BLAMP_CUTOFF, BLAMP_BETA, BLAMP_PHASES = 6, 0.9, 8.0, 128


def blamp_residual():
    """The residual on a fine grid of time in samples, t in [-6, 6]."""
    t = np.linspace(-BLAMP_HALF, BLAMP_HALF, 480001)
    kernel = BLAMP_CUTOFF * np.sinc(BLAMP_CUTOFF * t) * np.kaiser(len(t), BLAMP_BETA)
    kernel /= trapezoid(kernel, t)
    step = cumulative_trapezoid(kernel, t, initial=0.0)
    ramp = cumulative_trapezoid(step, t, initial=0.0)
    return t, ramp - np.maximum(t, 0.0)


def blamp_table() -> np.ndarray:
    """table[j, k]: the residual at sample k - 5 after the corner's sample
    when the corner lies d = j / 128 samples past it (k = 0..11)."""
    t, residual = blamp_residual()
    table = np.empty((BLAMP_PHASES + 1, 2 * BLAMP_HALF))
    for j in range(BLAMP_PHASES + 1):
        d = j / BLAMP_PHASES
        offsets = np.arange(-BLAMP_HALF + 1, BLAMP_HALF + 1) - d
        table[j] = np.interp(offsets, t, residual)
    return table


def blamp_header() -> str:
    table = blamp_table()
    rows = []
    for row in table:
        rows.append("    {{ " + ", ".join(f"{v:.17g}" for v in row) + " }},")
    return ("// Generated by Tools/PiezoReference.py --write-blamp-table; do not edit.\n"
            "// The 12-tap BLAMP residual AcustraEngine::renderPiezo band-limits U1B's\n"
            "// clip corners with: a Kaiser-windowed sinc (cutoff 0.45 fs, beta 8, six\n"
            "// samples either side) integrated twice, less the ideal ramp. Row j holds\n"
            "// the residual at the twelve samples from five before the corner's sample\n"
            "// to six after it, for a corner j/128 of a sample past that sample.\n"
            "#pragma once\n\n#include <array>\n\nnamespace acustra\n{\n"
            f"inline constexpr int piezoBlampPhases = {BLAMP_PHASES};\n"
            f"inline constexpr int piezoBlampTaps = {2 * BLAMP_HALF};\n"
            "inline constexpr std::array<std::array<double, 12>, 129> piezoBlampTable {{\n"
            + "\n".join(rows) + "\n}};\n} // namespace acustra\n")


def blamp_harmonic_errors(frequency_bin, level_db, size=1 << 14):
    """Aliasing (dB re the fundamental) and each harmonic's departure from
    the band-limited clip (dB) of a coherent sine through the engine's
    corner correction, emulated here from the table."""
    table = blamp_table()
    high, low = 3.2625, -3.9125
    n = np.arange(size)
    amplitude = 10 ** (level_db / 20) * high
    u = amplitude * np.sin(2 * np.pi * frequency_bin * n / size)
    pad = 8
    u = np.concatenate([u[-pad:], u, u[:pad]])
    y = np.clip(u, low, high)
    for i in range(BLAMP_HALF, len(u) - BLAMP_HALF - 1):
        for rail in (high, low):
            before, after = u[i] - rail, u[i + 1] - rail
            if not before * after < 0:
                continue
            p0, p3 = u[i - 1] - rail, u[i + 2] - rail
            c1 = -p0 / 3 - before / 2 + after - p3 / 6
            c2 = 0.5 * p0 - before + 0.5 * after
            c3 = -p0 / 6 + before / 2 - after / 2 + p3 / 6
            d = before / (before - after)
            for _ in range(2):
                value = before + d * (c1 + d * (c2 + d * c3))
                slope = c1 + d * (2 * c2 + 3 * d * c3)
                if slope:
                    d -= value / slope
                d = min(max(d, 0.0), 0.999999)
            slope = c1 + d * (2 * c2 + 3 * d * c3)
            entering = after > 0 if rail > 0 else after < 0
            p = d * BLAMP_PHASES
            j = int(p)
            residual = (1 - (p - j)) * table[j] + (p - j) * table[j + 1]
            y[i - BLAMP_HALF + 1:i + BLAMP_HALF + 1] += (-slope if entering else slope) * residual
    y = y[pad:-pad]
    spectrum = np.fft.rfft(y)
    power = np.abs(spectrum) ** 2
    index = np.arange(len(power))
    harmonic = index % frequency_bin == 0
    aliasing = 10 * np.log10(power[(~harmonic) & (index > 0)].sum() / power[frequency_bin])
    fine = np.arange(size * 16)
    ideal = np.fft.rfft(np.clip(amplitude * np.sin(2 * np.pi * frequency_bin * fine / (size * 16)),
                                low, high)) / 16
    errors = [float(decibels(abs(spectrum[h * frequency_bin]) / abs(ideal[h * frequency_bin])))
              for h in range(2, 6) if h * frequency_bin < 0.375 * size]
    return aliasing, errors


# ---------------------------------------------------------------- fixtures
AC_FREQUENCIES = np.geomspace(2.0, 96000.0, 241)
THD_RATE, THD_FFT = 48000.0, 16384
THD_BINS = (35, 341, 1707)                  # coherent: 102.5, 999.0, 5001.0 Hz
THD_LEVELS_DB = (1.0, 3.0, 6.0, 12.0)
BURST = {"frequency": 500.0, "length": 0.040, "ramp": 0.005, "tail": 3.0,
         "levels_db": (3.0, 10.0, 20.0), "waveform": 0.045, "tail_step": 0.005}


def write_fixtures(directory: Path, only=None) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    steel = Circuit()
    x0 = steel.operating_point()
    if only is None or 'dc' in only:
        # 1. DC operating point and the clip points about it.
        o2_bias = steel.o2(x0)[0]
        lines = ["# Tools/PiezoReference.py: DC operating point (V) and the chain's limits",
                 "name,value"]
        for index, name in enumerate(NODES):
            lines.append(f"{name},{x0[NODE_OFFSET + index]:.12g}")
        lines.append(f"O2,{o2_bias:.12g}")
        lines.append(f"u1b_rail_high,{steel.u1b_rails[0] - o2_bias:.12g}")
        lines.append(f"u1b_rail_low,{steel.u1b_rails[1] - o2_bias:.12g}")
        lines.append(f"u1a_cm_high,{steel.cm_limits[1] - x0[NODE_OFFSET + IN]:.12g}")
        lines.append(f"u1a_cm_low,{steel.cm_limits[0] - x0[NODE_OFFSET + IN]:.12g}")
        (directory / "piezo-reference-dc.csv").write_text("\n".join(lines) + "\n")
    if only is None or 'ac' in only:
        # 2. Small-signal responses from F_r (N) to Vout and VJ.
        response = steel.small_signal(AC_FREQUENCIES, x0)
        lines = ["# Tools/PiezoReference.py: small-signal response, steel, "
                 "from the rigid-saddle force (N)",
                 "hz,vout_re,vout_im,vj_re,vj_im,voc_re,voc_im"]
        for index, frequency in enumerate(AC_FREQUENCIES):
            v, j, o = (response[k][index] for k in ("Vout", "VJ", "Voc"))
            lines.append(f"{frequency:.10g},{v.real:.12g},{v.imag:.12g},"
                         f"{j.real:.12g},{j.imag:.12g},{o.real:.12g},{o.imag:.12g}")
        (directory / "piezo-reference-ac-steel.csv").write_text("\n".join(lines) + "\n")
    if only is None or 'thd' in only:
        # 3. Harmonics against level, steel, above the clip.
        lines = ["# Tools/PiezoReference.py: periodic steady-state harmonics of Vout, steel",
                 "# amplitude in N of F_r; h1 peak volts; hd2..hd5 and thd (harmonics below 21.6 kHz) in dB re h1",
                 "hz,level_db,amplitude,h1,hd2,hd3,hd4,hd5,thd"]
        jobs = []
        for bin_ in THD_BINS:
            frequency = bin_ * THD_RATE / THD_FFT
            clip = clip_amplitude(steel, frequency, x0)
            for level in THD_LEVELS_DB:
                jobs.append((frequency, level, clip * 10.0 ** (level / 20.0)))
        results = _map(_harmonics_job, jobs)
        for (frequency, level, amplitude), (h, thd) in zip(jobs, results):
            lines.append(f"{frequency:.10g},{level:g},{amplitude:.10g},{h[0]:.10g},"
                         + ",".join(f"{decibels(v / h[0]):.4f}" for v in h[1:5])
                         + f",{decibels(thd):.4f}")
        (directory / "piezo-reference-thd.csv").write_text("\n".join(lines) + "\n")
    if only is None or 'burst' in only:
        # 4. Overload bursts: Vout at 48 kHz through the burst, then the slow
        # states every 5 ms through the recovery.
        clip = clip_amplitude(steel, BURST["frequency"], x0)
        wave_times = np.arange(int(BURST["waveform"] * THD_RATE)) / THD_RATE
        tail_times = BURST["length"] + BURST["tail_step"] * np.arange(
            int(round((BURST["tail"] - BURST["length"]) / BURST["tail_step"])) + 1)
        results = _map(_burst_job, [clip * 10.0 ** (level / 20.0) for level in BURST["levels_db"]])
        waves = [wave for wave, _ in results]
        tails = [tail for _, tail in results]
        lines = ["# Tools/PiezoReference.py: Vout (V, about its operating point) through "
                 f"{BURST['frequency']:g} Hz bursts at +3/+10/+20 dB over U1B's clip, sampled at 48 kHz",
                 f"# amplitudes in N: " + ",".join(f"{clip * 10 ** (l / 20):.10g}" for l in BURST["levels_db"]),
                 "t,db3,db10,db20"]
        for i, t in enumerate(wave_times):
            lines.append(f"{t:.8f}," + ",".join(f"{w[i]:.10g}" for w in waves))
        (directory / "piezo-reference-burst.csv").write_text("\n".join(lines) + "\n")
        lines = ["# Tools/PiezoReference.py: recovery after the bursts, every 5 ms from the "
                 "burst's end; volts about the operating point",
                 "t," + ",".join(f"{k}_db{int(l)}" for l in BURST["levels_db"]
                                 for k in ("vout", "vc3", "vc4", "vc5", "vin"))]
        for i, t in enumerate(tail_times):
            lines.append(f"{t:.4f}," + ",".join(f"{v:.10g}" for tail in tails for v in tail[i]))
        (directory / "piezo-reference-recovery.csv").write_text("\n".join(lines) + "\n")


# ---------------------------------------------------------------- report
def report() -> None:
    num, den = mechanical_polynomials()
    poles = np.roots(den)
    pole = poles[np.argmax(np.abs(poles.imag))]
    f0, q = abs(pole) / (2 * math.pi), abs(pole) / (-2 * pole.real)
    fr = np.array([1000.0, f0, 10000.0, 20000.0])
    h = np.polyval(num, 2j * np.pi * fr) / np.polyval(den, 2j * np.pi * fr)
    print(f"steel: SZ {string_impedance_sum():.4f} kg/s, saddle pole "
          f"{f0:.0f} Hz Q {q:.2f}; |H| 1k {decibels(h[0]):+.2f}, at pole "
          f"{decibels(h[1]):+.2f}, 10k {decibels(h[2]):+.2f}, 20k {decibels(h[3]):+.2f} dB")
    circuit = Circuit()
    x0 = circuit.operating_point()
    print("operating point:", ", ".join(f"{n} {x0[NODE_OFFSET + i]:.6f}" for i, n in enumerate(NODES)))
    freqs = np.array([5.0, 10.0, 20.0, 31.0, 63.0, 1000.0, 6000.0, 10000.0, 20000.0])
    response = circuit.small_signal(freqs, x0)
    print("Vout / Voc (dB):", " ".join(f"{f:g}:{decibels(v / o):+.2f}" for f, v, o in
                                       zip(freqs, response["Vout"], response["Voc"])))
    print("U1B rails about O2:", [r - circuit.o2(x0)[0] for r in circuit.u1b_rails])
    for f in (100.0, 1000.0, 5000.0):
        print(f"clip amplitude at {f:g} Hz: {clip_amplitude(circuit, f, x0):.4f} N")


# ---------------------------------------------------------------- self-test
def self_test() -> None:
    """Runs without fixtures: the component model against closed forms."""
    steel = Circuit()
    x0 = steel.operating_point()
    # DC: the bootstrapped input and the bias sit at half the battery.
    for node in (IN, B, Y):
        assert abs(x0[NODE_OFFSET + node] - 4.5) < 1.0e-5, (NODES[node], x0[NODE_OFFSET + node])
    assert abs(steel.outputs(x0)[0] - 4.5) < 1.0e-5 and abs(steel.outputs(x0)[1] - 4.5) < 1.0e-5
    assert abs(x0[NODE_OFFSET + J]) < 1.0e-5 and abs(x0[NODE_OFFSET + Q]) < 1.0e-9
    # Mechanics: the component model is the closed-form H(s).
    num, den = mechanical_polynomials()
    freqs = np.geomspace(20.0, 20000.0, 40)
    response = steel.small_signal(freqs, x0)
    closed = np.polyval(num, 2j * np.pi * freqs) / np.polyval(den, 2j * np.pi * freqs)
    assert np.max(np.abs(response["Voc"] / (VOLTS_PER_NEWTON * closed) - 1.0)) < 1.0e-9
    # Mid-band: the jack's capacitive divider - the element against the
    # cable and a stray picofarad, C1 in series with the op-amp's 6 pF and a
    # stray picofarad at IN - a unity buffer, gain two, and the 100 Ohm into
    # the pot and the DI: 1.5504, with ideal op-amps otherwise.
    shunt = NET["Cp"] + NET["Ccab"] + NET["Cstray"]
    c_in = OPAMP["Ccm"] + NET["Cstray"]
    total = shunt + NET["C1"]
    divider = NET["C1"] * NET["Cp"] / (NET["C1"] * shunt + c_in * total)
    pot = 1.0 / (1.0 / NET["VR1"] + 1.0 / NET["Rdi"])
    ideal = divider * 2.0 * pot / (NET["R9"] + pot)
    mid = abs(steel.small_signal([1000.0], x0)["Vout"][0] / steel.small_signal([1000.0], x0)["Voc"][0])
    assert abs(mid / ideal - 1.0) < 1.0e-3, (mid, ideal)
    # The only audible low corner is C3's, 1/(2 pi 23.5k 220n) = 30.8 Hz.
    corner = abs(steel.small_signal([30.78], x0)["Vout"][0] / steel.small_signal([30.78], x0)["Voc"][0])
    assert 0.5 < corner / mid < 1.2
    # U1B's rails for its 6.7 kOhm load, about 4.5 V.
    high, low = (r - steel.o2(x0)[0] for r in steel.u1b_rails)
    assert abs(high - 3.2625) < 1.0e-3 and abs(low + 3.9125) < 1.0e-3, (high, low)
    # A short overload: Radau and BDF agree, and the clip holds at the rail.
    clip = clip_amplitude(steel, 1000.0, x0)
    force, dforce = burst(2.0 * clip, 1000.0, 0.006, 0.002)
    times = np.linspace(0.0, 0.006, 97)
    radau = steel.simulate(force, dforce, (0.0, 0.006), x0, t_eval=times, rtol=1e-9)
    bdf = steel.simulate(force, dforce, (0.0, 0.006), x0, t_eval=times, method="BDF", rtol=1e-9)
    o2 = steel.signals(radau.y, np.array([force(t) for t in times]))["O2"]
    assert np.max(o2) < steel.u1b_rails[0] + 1.0e-3 and np.max(o2) > steel.u1b_rails[0] - 0.05
    difference = np.max(np.abs(radau.y[NODE_OFFSET + Q] - bdf.y[NODE_OFFSET + Q]))
    assert difference < 1.0e-5, difference
    # The engine's BLAMP table is this tool's, and it does its job: a sine
    # 6 dB over the rail keeps its harmonics below 0.375 fs within 0.3 dB of
    # the band-limited clip's, and folds back less than a bare clip does.
    header = Path(__file__).resolve().parent.parent / "Source/DSP/PiezoBlampTable.h"
    written = [float(v) for v in re.findall(r"-?[0-9][0-9.eE+-]*", header.read_text().split("{{", 1)[1])]
    # A few ULP of slack: numpy 2's summation rounds the table's last digit
    # differently from the numpy 1.26 that wrote it.
    assert np.max(np.abs(np.array(written) - blamp_table().ravel())) < 1.0e-14
    aliasing, errors = blamp_harmonic_errors(341, 6.0)
    assert aliasing < -65.0 and max(abs(e) for e in errors) < 0.3, (aliasing, errors)
    print("PiezoReference self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--report", action="store_true")
    parser.add_argument("--write-fixtures", type=Path)
    parser.add_argument("--write-blamp-table", type=Path)
    parser.add_argument("--only", help="with --write-fixtures: some of dc,ac,thd,burst")
    arguments = parser.parse_args()
    if arguments.self_test:
        self_test()
    if arguments.report:
        report()
    if arguments.write_fixtures:
        write_fixtures(arguments.write_fixtures,
                       arguments.only.split(",") if arguments.only else None)
    if arguments.write_blamp_table:
        arguments.write_blamp_table.write_text(blamp_header())
    if not (arguments.self_test or arguments.report or arguments.write_fixtures
            or arguments.write_blamp_table):
        parser.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
