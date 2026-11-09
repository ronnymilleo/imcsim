# Small-Signal AC / Bode Analysis (`.ac`)

**Implementation files:**
- [`app-core/native/ac_analysis.cpp`](../../app-core/native/ac_analysis.cpp) (`ComputeACSweep`)
- [`app-core/native/mna_system.cpp`](../../app-core/native/mna_system.cpp) (`MnaSystem::SolveAc`, `MnaSystem::AddComplexA`, `MnaSystem::AddComplexRhs`)
- [`app-core/native/linear/dense_matrix.cpp`](../../app-core/native/linear/dense_matrix.cpp) (`BasicDenseMatrix<std::complex<double>>`)
- [`app-core/native/devices.cpp`](../../app-core/native/devices.cpp) and [`app-core/native/nonlinear.cpp`](../../app-core/native/nonlinear.cpp) (`StampAc`, `AcTerminalCurrent`)

---

## 1. Algorithms and Mathematical Formulation

### 1.1 Exact Linearization Around the DC Operating Point
Small-signal AC analysis computes the sinusoidal steady-state phasor response $\tilde{\mathbf{x}}(j\omega)\,e^{j\omega t}$ to small perturbations superimposed on the nonlinear DC equilibrium $\mathbf{x}_{\text{DC}}$:
1. First, the nonlinear DC operating point $\mathbf{x}_{\text{DC}}$ is solved via `SolveOperatingPoint`.
2. All independent DC sources are zeroed ($\Delta V_{\text{DC}} = 0$, $\Delta I_{\text{DC}} = 0$); only independent sources configured with an AC stimulus inject their complex phasor $\tilde{U}_{\text{AC}} = M_{\text{AC}}\,e^{j\phi_{\text{AC}}}$.
3. Every nonlinear semiconductor device is replaced by its exact tangent linear model evaluated at $\mathbf{x}_{\text{DC}}$:
   - **Diode (`DiodeDevice`):** differential conductance $g_d = \left.\frac{\partial I_D}{\partial V_j}\right|_{\text{DC}}$ in parallel with total junction capacitance $C_d = C_{\text{dep}}(V_j) + \tau_T g_d$ (complex admittance $g_d + j\omega C_d$) plus series resistance $R_S$.
   - **Gummel-Poon BJT (`BjtDevice`):** full hybrid-$\pi$ small-signal equivalent derived from the Gummel-Poon equations at $\mathbf{x}_{\text{DC}}$ ($g_\pi, g_\mu, g_m, g_o$, terminal resistances $R_B, R_C, R_E$) augmented by depletion and diffusion capacitances $C_\pi = C_{\text{dep,BE}} + \tau_F \frac{\partial(I_{\text{fwd}}/q_b)}{\partial V_{BE}}$, $C_\mu = C_{\text{dep,BC}} + \tau_R g_{\text{rev}}$, $C_{CS}$ (collector-substrate), and the **base-width modulation capacitive transadmittance** (`geqcb`):
     $$Y_{\text{cb}} = j\omega\,\tau_F\,\frac{\partial(I_{\text{fwd}}/q_b)}{\partial V_{BC}}$$
   - **Level-1 MOSFET (`MosfetDevice`):** channel transconductance $g_m = \frac{\partial I_{DS}}{\partial V_{GS}}$, output conductance $g_{ds} = \frac{\partial I_{DS}}{\partial V_{DS}}$, linearized bulk diodes BD/BS, and overlap/depletion capacitances ($C_{GS}, C_{GD}, C_{BD}, C_{BS}$).

### 1.2 $C^1$-Continuous Forward-Bias Depletion Capacitance Extrapolation ($v \ge \text{FC}\cdot V_J$)
To avoid the singularity of $C(v) = C_{J0}(1 - v/V_J)^{-M}$ as a forward-biased PN junction approaches $V_J$, `Nodalys` implements SPICE3f5's exact tangent linear extrapolation above $v = \text{FC}\cdot V_J$ in `EvaluateDepletion`:
$$C(v) = \frac{C_{J0}}{(1 - \text{FC})^M}\left(1 + \frac{M(v - \text{FC}\cdot V_J)}{V_J(1 - \text{FC})}\right)$$
guaranteeing both continuous charge $Q(v)$ and continuous capacitance $C(v) = dQ/dv$ across transient and high-frequency Miller-effect AC simulations.

### 1.3 Solving the Complex MNA System $\mathbf{Y}(j\omega)\,\tilde{\mathbf{x}} = \tilde{\mathbf{b}}_{\text{AC}}$
At each frequency $f_k$ on the logarithmic grid ($N_{\text{dec}}$ points per decade, $\omega_k = 2\pi f_k$):
- The real part of node-voltage diagonals receives `Gmin = 1e-12 S`, while the imaginary part remains free of artificial susceptance.
- Capacitors stamp $+j\omega_k C$ between their terminals; inductors stamp $-j\omega_k L$ on their branch-current diagonal; ideal transformers and controlled sources stamp their linear transfer ratios.
- `MnaSystem::SolveAc` solves the dense complex system $\mathbf{Y}(j\omega_k)\,\tilde{\mathbf{x}} = \tilde{\mathbf{b}}_{\text{AC}}$ using **complex magnitude row equilibration**, **partial-pivoting $PA = LU$ in $\mathbb{C}$**, and **1 step of complex iterative refinement**.

### 1.4 Magnitude in dB and Continuous Phase Unwrapping
- **Magnitude:** $|H|_{\text{dB}} = 20\log_{10}\bigl(\max(|\tilde{X}(j\omega_k)|, 10^{-20})\bigr)$.
- **Phase Unwrapping (`UnwrapPhaseDegrees`):** because `std::arg` wraps angles into $(-180^\circ, +180^\circ]$, higher-order filters with phase lag beyond $-180^\circ$ would exhibit artificial $+360^\circ$ jumps. `Nodalys` shifts each frequency point $k \ge 1$ by integer multiples of $\pm 360^\circ$ so that $|\phi_k - \phi_{k-1}| \le 180^\circ$, producing smooth Bode phase curves matching `ngspice`.

---

## 2. Benchmark Tables: Analytical Golden vs. `ngspice` vs. `Nodalys`

### 2.1 First-Order RC Low-Pass Filter ([`rc_bode.csv`](../../tests/reference/rc_bode.csv))
- **Circuit:** $R = 1\text{ k}\Omega$, $C = 100\text{ nF}$ ($f_c = \frac{1}{2\pi R C} = 1591.55\text{ Hz}$).
- **Analytical Transfer Function (Golden):**
  $$H(j\omega) = \frac{1}{1 + j\omega R C}$$

| Frequency | Golden (Mag / Phase) | `ngspice` (Mag / Phase) | `Nodalys` (Mag / Phase) | `Nodalys` vs. Golden Error |
| :--- | :---: | :---: | :---: | :---: |
| $10.00\text{ Hz}$ | $-0.000171\text{ dB}$ / $-0.359995^\circ$ | $-0.000171\text{ dB}$ / $-0.359995^\circ$ | $-0.000171\text{ dB}$ / $-0.359995^\circ$ | $8.69 \times 10^{-9}\text{ dB}$ / $1.80 \times 10^{-10\,\circ}$ |
| $100.00\text{ Hz}$ | $-0.017112\text{ dB}$ / $-3.595274^\circ$ | $-0.017112\text{ dB}$ / $-3.595274^\circ$ | $-0.017112\text{ dB}$ / $-3.595274^\circ$ | $8.65 \times 10^{-9}\text{ dB}$ / $1.79 \times 10^{-9\,\circ}$ |
| $1584.89\text{ Hz}$ ($\approx f_c$) | $-2.992137\text{ dB}$ / $-44.879937^\circ$ | $-2.992137\text{ dB}$ / $-44.879937^\circ$ | $-2.992137\text{ dB}$ / $-44.879937^\circ$ | $4.36 \times 10^{-9}\text{ dB}$ / $1.43 \times 10^{-8\,\circ}$ |
| $10.00\text{ kHz}$ | $-16.072235\text{ dB}$ / $-80.956939^\circ$ | $-16.072235\text{ dB}$ / $-80.956939^\circ$ | $-16.072235\text{ dB}$ / $-80.956939^\circ$ | $2.14 \times 10^{-10}\text{ dB}$ / $8.91 \times 10^{-9\,\circ}$ |
| $100.00\text{ kHz}$ | $-35.964697\text{ dB}$ / $-89.088186^\circ$ | $-35.964697\text{ dB}$ / $-89.088186^\circ$ | $-35.964697\text{ dB}$ / $-89.088186^\circ$ | $2.20 \times 10^{-12}\text{ dB}$ / $9.12 \times 10^{-10\,\circ}$ |
| **Max Error (41 pts)** | — | **$2.49 \times 10^{-14}\text{ dB}$** | **$8.69 \times 10^{-9}\text{ dB}$** | **$8.69 \times 10^{-9}\text{ dB}$ / $2.87 \times 10^{-8\,\circ}$** |

---

### 2.2 Second-Order Resonant Series RLC Filter ([`rlc_bode.csv`](../../tests/reference/rlc_bode.csv))
- **Circuit:** $R = 100\text{ }\Omega$, $L = 10\text{ mH}$, $C = 100\text{ nF}$ ($f_0 = \frac{1}{2\pi\sqrt{LC}} = 5032.92\text{ Hz}$, quality factor $Q \approx 3.16$).
- **Analytical Capacitor Transfer Function (Golden):**
  $$H_C(j\omega) = \frac{1}{1 - \omega^2 L C + j\omega R C}$$

| Frequency | Golden (Mag / Phase) | `ngspice` (Mag / Phase) | `Nodalys` (Mag / Phase) | `Nodalys` vs. Golden Error |
| :--- | :---: | :---: | :---: | :---: |
| $100.00\text{ Hz}$ | $+0.003258\text{ dB}$ / $-0.360137^\circ$ | $+0.003258\text{ dB}$ / $-0.360137^\circ$ | $+0.003258\text{ dB}$ / $-0.360137^\circ$ | $8.69 \times 10^{-10}\text{ dB}$ / $2.25 \times 10^{-9\,\circ}$ |
| $1.00\text{ kHz}$ | $+0.331313\text{ dB}$ / $-3.742631^\circ$ | $+0.331313\text{ dB}$ / $-3.742631^\circ$ | $+0.331313\text{ dB}$ / $-3.742631^\circ$ | $9.17 \times 10^{-10}\text{ dB}$ / $2.31 \times 10^{-8\,\circ}$ |
| $5011.87\text{ Hz}$ (peak $Q \approx 3.16$) | $+10.033352\text{ dB}$ / $-88.481654^\circ$ | $+10.033352\text{ dB}$ / $-88.481654^\circ$ | $+10.033352\text{ dB}$ / $-88.481654^\circ$ | $8.75 \times 10^{-9}\text{ dB}$ / $2.69 \times 10^{-8\,\circ}$ |
| $10.00\text{ kHz}$ | $-9.583037\text{ dB}$ / $-167.967731^\circ$ | $-9.583037\text{ dB}$ / $-167.967731^\circ$ | $-9.583037\text{ dB}$ / $-167.967731^\circ$ | $1.21 \times 10^{-9}\text{ dB}$ / $5.87 \times 10^{-9\,\circ}$ |
| $100.00\text{ kHz}$ | $-51.906271\text{ dB}$ / $-179.085871^\circ$ | $-51.906271\text{ dB}$ / $-179.085871^\circ$ | $-51.906271\text{ dB}$ / $-179.085871^\circ$ | $8.68 \times 10^{-10}\text{ dB}$ / $5.00 \times 10^{-10\,\circ}$ |
| **Max Error (51 pts)** | — | **$6.48 \times 10^{-14}\text{ dB}$** | **$8.75 \times 10^{-9}\text{ dB}$** | **$8.75 \times 10^{-9}\text{ dB}$ / $2.88 \times 10^{-8\,\circ}$** |

> [!NOTE]
> In passive RC and RLC filters, the $\sim 8.7 \times 10^{-9}\text{ dB}$ difference (at the **9th decimal place**) between `Nodalys` and the lossless formula comes solely from the safety diagonal conductance `Gmin = 1e-12 S` ($20\log_{10}(1 + 1\text{ k}\Omega \cdot 10^{-12}\text{ S}) \approx 8.686 \times 10^{-9}\text{ dB}$).

---

### 2.3 Parity Across All 8 Shipped AC / Bode Examples in `imcsim` ([`examples_tests.cpp`](../../tests/examples_tests.cpp))

| Shipped Example | Freq. Points | Active & Passive Devices | Max Magnitude Diff (`Nodalys` vs. `ngspice`) | Max Phase Diff (`Nodalys` vs. `ngspice`) |
| :--- | :---: | :--- | :---: | :---: |
| `rc_low_pass_filter.imcsim` | 101 | $R, C$ (1st-order low-pass filter) | **$8.69 \times 10^{-9}\text{ dB}$** | **$2.87 \times 10^{-8\,\circ}$** |
| `series_rlc.imcsim` | 151 | $R, L, C$ (2nd-order series resonance) | **$8.88 \times 10^{-9}\text{ dB}$** | **$3.64 \times 10^{-8\,\circ}$** |
| `rc_blocks.imcsim` | 101 | Cascaded parameterized RC subcircuit blocks | **$8.65 \times 10^{-8}\text{ dB}$** | **$2.87 \times 10^{-7\,\circ}$** |
| `sallen_key_low_pass.imcsim` | 201 | 2nd-order active Sallen-Key low-pass filter (Op-Amp) | **$2.61 \times 10^{-7}\text{ dB}$** | **$2.03 \times 10^{-6\,\circ}$** |
| `non_inverting_amplifier.imcsim` | 101 | Non-inverting op-amp amplifier with GBW roll-off | **$3.11 \times 10^{-6}\text{ dB}$** | **$9.90 \times 10^{-6\,\circ}$** |
| `hybrid_pi_model.imcsim` | 161 | 2N3904 BJT ($C_\pi, C_\mu, r_o$) vs. explicit hybrid-$\pi$ ($100\text{ Hz}\text{–}100\text{ MHz}$) | **$4.29 \times 10^{-4}\text{ dB}$** | **$1.37 \times 10^{-3\,\circ}$** |
| `op_amp_integrator.imcsim` | 201 | Active Miller integrator with op-amp | **$4.35 \times 10^{-4}\text{ dB}$** | **$2.85 \times 10^{-3\,\circ}$** |
| `common_emitter_amplifier.imcsim` | 161 | Full AC-coupled & bypassed BJT Common-Emitter amplifier | **$9.30 \times 10^{-4}\text{ dB}$** | **$2.99 \times 10^{-3\,\circ}$** |
