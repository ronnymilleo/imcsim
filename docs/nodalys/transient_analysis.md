# Time-Domain Transient Analysis (`.tran`)

**Implementation files:**
- [`app-core/native/transient_analysis.cpp`](../../app-core/native/transient_analysis.cpp) (`ComputeTransient`, `TransientCollector`, `EstimateLteRatio`)
- [`app-core/native/devices.cpp`](../../app-core/native/devices.cpp) (`CapacitorDevice::StampTransient`, `InductorDevice::StampTransient`)
- [`app-core/native/nonlinear.cpp`](../../app-core/native/nonlinear.cpp) (`ChargeModel` for diode, BJT, and MOSFET junction/diffusion charges)
- [`app-core/native/waveforms.cpp`](../../app-core/native/waveforms.cpp) (`SIN`, `PULSE`, `EXP`, `PWL`, `SFFM`, `AM`, `NOISE`, and *breakpoint* generation)

---

## 1. Algorithms and Mathematical Formulation

### 1.1 Differential-Algebraic Equation (DAE) System
Over the time span $t \in [0, T_{\text{stop}}]$, Modified Nodal Analysis yields an index-1 nonlinear Differential-Algebraic Equation (DAE) system:
$$\mathbf{f}(\mathbf{x}(t)) + \frac{d\mathbf{q}(\mathbf{x}(t))}{dt} = \mathbf{u}(t)$$
where:
- $\mathbf{f}(\mathbf{x})$ collects linear conductive branches and static nonlinear semiconductor currents.
- $\mathbf{q}(\mathbf{x})$ collects linear capacitor charges $Q = C v$, inductor flux linkages $\Phi = L i$, and nonlinear depletion/diffusion charges $Q(v)$ of PN junctions and MOSFET channels.
- $\mathbf{u}(t)$ represents time-dependent independent source excitations at time $t$.

### 1.2 Time Discretization: Backward Euler (Step 1) + Trapezoidal Rule
To advance from $t_n$ to $t_{n+1} = t_n + h$, `Nodalys` discretizes each time derivative $\dot{q} = dq/dt$ into an algebraic **Norton companion model** (`ChargeModel`):

1. **Backward Euler (1st-order, L-stable):** used on the first time step ($t_0 \to t_1$) and immediately after switching discontinuities to damp high-frequency numerical ringing:
   $$i_{n+1} = \frac{q(v_{n+1}) - q(v_n)}{h} \implies G_{\text{eq}} = \frac{C(v^{(k)})}{h}, \qquad I_{\text{eq}} = i_{n+1}^{(k)} - G_{\text{eq}}\,v^{(k)}$$
2. **Trapezoidal Rule (2nd-order, A-stable):** used on all subsequent smooth steps; by Dahlquist's Second Barrier Theorem, it is the 2nd-order A-stable linear multistep method with the smallest truncation error constant ($C_{\text{LTE}} = 1/12$):
   $$i_{n+1} = \frac{2}{h}\bigl(q(v_{n+1}) - q(v_n)\bigr) - i_n \implies G_{\text{eq}} = \frac{2\,C(v^{(k)})}{h}, \qquad I_{\text{eq}} = i_{n+1}^{(k)} - G_{\text{eq}}\,v^{(k)}$$

> [!TIP]
> **Trapezoidal Rule vs. BDF-2 / Gear-2:**
> - The **Trapezoidal Rule** is lossless on the imaginary axis (mapping $j\omega$ onto the unit circle $|z|=1$), preserving amplitude in high-$Q$ LC oscillators, but very stiff modes ($\tau \ll h$) exhibit *trapezoidal ringing* ($z \to -1$).
> - **BDF-2 (*2nd-Order Backward Differentiation Formula* / Gear-2)**:
>   $$\frac{3\,q_{n+1} - 4\,q_n + q_{n-1}}{2h} = i_{n+1}$$
>   is **L-stable** ($\lim_{h/\tau \to \infty} z = 0$), completely suppressing numerical ringing in stiff switching circuits at the cost of slight numerical damping in high-$Q$ resonators.

### 1.3 Adaptive Time-Step Control via Local Truncation Error (LTE)
After Newton-Raphson converges at $t_{n+1}$, `Nodalys` estimates the local truncation error of the Trapezoidal Rule using the 3rd divided difference over the 4-point history ($t_{n-2}, t_{n-1}, t_n, t_{n+1}$), following Nagel (SPICE2, Eq. 5.26):
$$\text{LTE}_{\text{trap}} \approx \frac{1}{2}\,h^3\,\bigl|d_{123}\bigr|$$
- When the LTE ratio exceeds the allowed tolerance, the step size is scaled according to $h_{\text{new}} = h \cdot \text{ratio}^{-1/3}$.
- If Newton-Raphson fails to converge on a step, `Nodalys` restores device states (`RestoreState`), cuts the step size ($h \leftarrow h / 4$), and retries with Backward Euler.

### 1.4 Breakpoints and Uniform Grid Interpolation
- **Waveform Breakpoints:** slope discontinuities of `PULSE` sources (delay, rise end, pulse width, fall end, period) and `PWL` corners are pre-registered so the integrator lands exactly on every corner.
- **Hysteretic Switching Events:** voltage-controlled switches and relays are checked after each converged step; if a threshold is crossed, the step is re-solved with the updated switch state before committing.
- **Output Grid Interpolation (`TransientCollector`):** internal adaptive steps are linearly interpolated onto the user-requested uniform output grid (`TimeStep`).

---

## 2. Benchmark Tables: Analytical Golden vs. `ngspice` vs. `Nodalys`

### 2.1 Step Responses of RC, RL, and Underdamped Series RLC Circuits
- **[`rc_step.csv`](../../tests/reference/rc_step.csv):** $0 \to 5\text{ V}$ step into $R = 1\text{ k}\Omega$, $C = 1\text{ }\mu\text{F}$ ($\tau = RC = 1\text{ ms}$, $\Delta t = 20\text{ }\mu\text{s}$):
  $$v_C(t) = V_s\left(1 - e^{-t/\tau}\right), \qquad i_C(t) = \frac{V_s}{R}\,e^{-t/\tau}$$
- **[`rl_step.csv`](../../tests/reference/rl_step.csv):** $0 \to 1\text{ V}$ step into $R = 10\text{ }\Omega$, $L = 10\text{ mH}$ ($\tau = L/R = 1\text{ ms}$, $\Delta t = 20\text{ }\mu\text{s}$):
  $$i_L(t) = \frac{V_s}{R}\left(1 - e^{-t/\tau}\right), \qquad v_L(t) = V_s\,e^{-t/\tau}$$
- **[`rlc_underdamped.csv`](../../tests/reference/rlc_underdamped.csv):** $0 \to 10\text{ V}$ step into $R = 20\text{ }\Omega$, $L = 10\text{ mH}$, $C = 1\text{ }\mu\text{F}$ ($\alpha = 1000\text{ s}^{-1}$, $\omega_0 = 10000\text{ rad/s}$, $\omega_d = \sqrt{\omega_0^2 - \alpha^2}$, $\Delta t = 10\text{ }\mu\text{s}$):
  $$v_C(t) = V_s\left[1 - e^{-\alpha t}\left(\cos(\omega_d t) + \frac{\alpha}{\omega_d}\sin(\omega_d t)\right)\right], \qquad i_L(t) = \frac{V_s}{L\omega_d}e^{-\alpha t}\sin(\omega_d t)$$

| Circuit / Time | Quantity | Golden (Analytical) | `ngspice` | `ngspice` Error | **`Nodalys`** | **`Nodalys` Error** | Closer to Golden |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| **RC Step** ($t = 1\text{ ms} = 1\tau$) | $V_C\text{ (V)}$ | `3.16060279` | `3.16059543` | `7.36e-06` | `3.16029981` | `3.03e-04` | `ngspice` |
| **RC Step** ($t = 2\text{ ms} = 2\tau$) | $V_C\text{ (V)}$ | `4.32332358` | `4.32334343` | `1.98e-05` | `4.32323468` | `8.89e-05` | `ngspice` |
| **RC Step** ($t = 5\text{ ms} = 5\tau$) | $V_C\text{ (V)}$ | `4.96631027` | `4.96631582` | `5.56e-06` | **`4.96630920`** | **`1.06e-06`** | **`Nodalys` (5.2× more accurate)** |
| **RL Step** ($t = 1\text{ ms} = 1\tau$) | $V_L\text{ (V)}$ | `0.36787944` | `0.36788595` | `6.51e-06` | `0.36794004` | `6.06e-05` | `ngspice` |
| **RL Step** ($t = 1\text{ ms} = 1\tau$) | $I_L\text{ (A)}$ | `0.06321206` | `0.06321141` | `6.51e-07` | `0.06320600` | `6.06e-06` | `ngspice` |
| **RL Step** ($t = 5\text{ ms} = 5\tau$) | $V_L\text{ (V)}$ | `0.00673795` | `0.00673717` | `7.79e-07` | **`0.00673816`** | **`2.11e-07`** | **`Nodalys` (3.7× more accurate)** |
| **RL Step** ($t = 5\text{ ms} = 5\tau$) | $I_L\text{ (A)}$ | `0.09932621` | `0.09932628` | `7.79e-08` | **`0.09932618`** | **`2.11e-08`** | **`Nodalys` (3.7× more accurate)** |
| **RLC Underdamped** ($t = 0.31\text{ ms}$, peak) | $V_C\text{ (V)}$ | `17.28041170` | `17.27384181` | `6.57e-03` | `17.24243422` | `3.80e-02` | `ngspice` |
| **RLC Underdamped** ($t = 1.00\text{ ms}$) | $V_C\text{ (V)}$ | `13.36851681` | `13.38489402` | `1.64e-02` | **`13.37265394`** | **`4.14e-03`** | **`Nodalys` (4.0× more accurate)** |
| **RLC Underdamped** ($t = 2.50\text{ ms}$) | $I_L\text{ (A)}$ | `-0.00210537` | `-0.00227594` | `1.71e-04` | **`-0.00227580`** | **`1.70e-04`** | Tie (`1.7e-4 A`) |

> [!NOTE]
> **Physical and numerical interpretation of the transient results:**
> - Immediately after the $t = 0$ step ($t = 1\tau$), `ngspice` takes dozens of nanosecond sub-steps across the `PULSE` rise ramp, whereas `Nodalys` takes its initial step in Backward Euler to damp the impulse discontinuity before switching to the 2nd-order Trapezoidal Rule.
> - In the settling and intermediate oscillation regimes ($t = 5\tau$ in RC and RL, and $t = 1.0\text{ ms}$ in the underdamped RLC), **`Nodalys` is 3.7× to 5.2× closer to the closed-form analytical solution** than `ngspice`.
