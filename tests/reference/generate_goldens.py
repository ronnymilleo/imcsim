#!/usr/bin/env python3
"""Generate closed-form golden reference CSVs for the Nodalys solver test suite.

Each generator implements the exact analytical solution cited in tests/reference/README.md
and writes a deterministic CSV file in the same directory as this script.
"""

from __future__ import annotations

import csv
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent

# Exact SPICE3f5 / Nodalys thermal voltage at T = 300.15 K (27 °C):
# k_B / q * 300.15 = 8.617087399994e-5 * 300.15
THERMAL_VOLTAGE = 8.617087399994e-5 * 300.15


def write_csv(filename: str, headers: list[str], rows: list[list[float]]) -> None:
    path = HERE / filename
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(headers)
        for row in rows:
            writer.writerow([f"{value:.12e}" for value in row])


def generate_rc_step() -> None:
    """First-order RC step response: V_s = 5 V, R = 1 kOhm, C = 1 uF (tau = 1 ms).

    Closed form (Desoer & Kuh, Basic Circuit Theory, ch. 4):
        v_C(t) = V_s * (1 - exp(-t / (R * C)))
        i_C(t) = (V_s / R) * exp(-t / (R * C))
    """
    vs = 5.0
    r = 1.0e3
    c = 1.0e-6
    tau = r * c
    dt = 20.0e-6
    steps = 250
    rows = []
    for k in range(steps + 1):
        t = k * dt
        vc = vs * (1.0 - math.exp(-t / tau))
        ic = (vs / r) * math.exp(-t / tau)
        rows.append([t, vc, ic])
    write_csv("rc_step.csv", ["time_s", "v_cap_V", "i_cap_A"], rows)


def generate_rl_step() -> None:
    """First-order RL step response: V_s = 1 V, R = 10 Ohm, L = 10 mH (tau = 1 ms).

    Closed form (Desoer & Kuh, Basic Circuit Theory, ch. 4):
        i_L(t) = (V_s / R) * (1 - exp(-R * t / L))
        v_L(t) = V_s * exp(-R * t / L)
    """
    vs = 1.0
    r = 10.0
    ind = 10.0e-3
    tau = ind / r
    dt = 20.0e-6
    steps = 500
    rows = []
    for k in range(steps + 1):
        t = k * dt
        il = (vs / r) * (1.0 - math.exp(-t / tau))
        vl = vs * math.exp(-t / tau)
        rows.append([t, vl, il])
    write_csv("rl_step.csv", ["time_s", "v_ind_V", "i_ind_A"], rows)


def generate_rlc_underdamped() -> None:
    """Series RLC underdamped step response: V_s = 10 V, R = 20 Ohm, L = 10 mH, C = 1 uF.

    Closed form (Vlach & Singhal, Computer Methods for Circuit Analysis and Design, ch. 9):
        alpha = R / (2 * L) = 1000 s^-1
        omega_0 = 1 / sqrt(L * C) = 10000 rad/s
        omega_d = sqrt(omega_0^2 - alpha^2)
        v_C(t) = V_s * (1 - exp(-alpha * t) * (cos(omega_d * t) + (alpha / omega_d) * sin(omega_d * t)))
        i_L(t) = (V_s / (L * omega_d)) * exp(-alpha * t) * sin(omega_d * t)
    """
    vs = 10.0
    r = 20.0
    ind = 10.0e-3
    c = 1.0e-6
    alpha = r / (2.0 * ind)
    omega_0 = 1.0 / math.sqrt(ind * c)
    omega_d = math.sqrt(omega_0 * omega_0 - alpha * alpha)
    dt = 10.0e-6
    steps = 500
    rows = []
    for k in range(steps + 1):
        t = k * dt
        decay = math.exp(-alpha * t)
        vc = vs * (1.0 - decay * (math.cos(omega_d * t) + (alpha / omega_d) * math.sin(omega_d * t)))
        il = (vs / (ind * omega_d)) * decay * math.sin(omega_d * t)
        rows.append([t, vc, il])
    write_csv("rlc_underdamped.csv", ["time_s", "v_cap_V", "i_ind_A"], rows)


def generate_diode_dc_curve() -> None:
    """Series resistor + Shockley diode DC curve solved by Newton-Raphson to machine precision.

    Model (1N4148 parameters in imcsim, Nagel SPICE2 Memorandum UCB/ERL M520):
        I_D(V_j) = I_S * (exp(V_j / (N * V_T)) - 1) + G_min * V_j
        V_s = I_D * (R_load + R_S) + V_j
    """
    i_s = 2.52e-9
    n = 1.752
    r_s = 0.568
    r_load = 1.0e3
    g_min = 1.0e-12
    n_vt = n * THERMAL_VOLTAGE

    rows = []
    for k in range(51):
        vs = k * 0.1
        # Solve f(vj) = vj + (r_load + r_s) * i_d(vj) - vs = 0 to 1e-15 V
        vj = 0.6 if vs > 0.6 else vs
        for _ in range(100):
            ev = math.exp(min(vj / n_vt, 80.0))
            id_val = i_s * (ev - 1.0) + g_min * vj
            gd_val = (i_s / n_vt) * ev + g_min
            f = vj + (r_load + r_s) * id_val - vs
            df = 1.0 + (r_load + r_s) * gd_val
            step = f / df
            vj -= step
            if abs(step) < 1.0e-15:
                break
        ev = math.exp(min(vj / n_vt, 80.0))
        id_val = i_s * (ev - 1.0) + g_min * vj
        v_anode = vj + r_s * id_val
        rows.append([vs, v_anode, id_val])
    write_csv("diode_shockley_dc.csv", ["v_source_V", "v_anode_V", "i_diode_A"], rows)


if __name__ == "__main__":
    generate_rc_step()
    generate_rl_step()
    generate_rlc_underdamped()
    generate_diode_dc_curve()
