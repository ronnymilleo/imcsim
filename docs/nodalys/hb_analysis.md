# Harmonic Balance Analysis (`.hb`)

**Implementation files:**
- [`nodalys/src/linear/fft.h`](../../nodalys/src/linear/fft.h) and [`nodalys/src/linear/fft.cpp`](../../nodalys/src/linear/fft.cpp) (`FftPlan`, `RealToHarmonics`, `RealToNormalizedSpectrum`, `HarmonicsToReal`)
- [`nodalys/src/analyses/hb_analysis.cpp`](../../nodalys/src/analyses/hb_analysis.cpp) (`ComputeHarmonicBalance`, `ComputeThd`)
- [`nodalys/include/nodalys/analyses.h`](../../nodalys/include/nodalys/analyses.h) (`HarmonicBalanceConfig`) and [`nodalys/include/nodalys/results.h`](../../nodalys/include/nodalys/results.h) (`HarmonicBalanceResult`)
- [`nodalys/src/parser/netlist_parser.cpp`](../../nodalys/src/parser/netlist_parser.cpp) (`.hb` directive)
- [`nodalys/src/cli/main.cpp`](../../nodalys/src/cli/main.cpp) (`-a hb`) and [`nodalys/src/export/`](../../nodalys/src/export/) (`csv_exporter.cpp`, `json_exporter.cpp`, `raw_exporter.cpp`)
- [`app-core/nodalys/nodalys_backend.cpp`](../../app-core/nodalys/nodalys_backend.cpp) (`NodalysBackend::RunHarmonicBalance` — exclusive to Nodalys in `imcsim`)
- [`app-gui/analysis/harmonic_balance_view.cpp`](../../app-gui/analysis/harmonic_balance_view.cpp) and [`app-gui/windows/output_tabs/harmonic_balance_tab.cpp`](../../app-gui/windows/output_tabs/harmonic_balance_tab.cpp)
- [`nodalys/tests/fft_tests.cpp`](../../nodalys/tests/fft_tests.cpp) and [`nodalys/tests/hb_analysis_tests.cpp`](../../nodalys/tests/hb_analysis_tests.cpp)

---

## 1. Overview and Scope

Single-tone Harmonic Balance (`.hb`) solves the **periodic steady state** of nonlinear circuits driven at a fundamental frequency $f_0$ ($T_0 = 1/f_0$) directly in the frequency domain, bypassing long transient settling times in high-$Q$ resonant tanks and long-$\tau$ rectifiers or filters. Because `ngspice` has no Harmonic Balance solver, `.hb` is Nodalys's first exclusive analysis in `imcsim`.

### 1.1 Supported Inputs and Guardrails
- **Accepted excitations:** DC sources, undamped `SIN`/`AC` sources at integer multiples $n f_0$ ($1 \le n \le H$), `PULSE`, and `CLOCK` waveforms whose period equals $T_0 / n$ (initial delays are wrapped modulo $T_0$).
- **Explicitly refused:**
  - Non-periodic or damped waveforms (`EXP`, `PWL`, `SFFM`, `AM`, `NOISE`, `RANDOM`, damped `SIN`).
  - Frequencies that are not integer multiples of $f_0$ or harmonics exceeding `Harmonics` ($H$).
  - Event-hysteretic switches (`V_SWITCH` / `RELAY` in `imcsim`; static switches and push buttons are supported).
- **Oversampling & truncation checks:** When `Samples == 0`, `ValidateAndResolveSampleCount` selects $M = \text{NextPowerOfTwo}(\max(16, 4H))$. Explicit power-of-two sample counts must satisfy $M > 2H$ (Nyquist guard against harmonic aliasing). Warnings are emitted when a `PULSE`/`CLOCK` edge is narrower than $T_0 / M$, when $|X_H| > 1\%\,|X_1|$ at any node (harmonic truncation), or when the dense HB Jacobian exceeds `MAX_DENSE_UNKNOWNS_WARNING = 1500` unknowns. Above `MAX_DENSE_UNKNOWNS = 4000` unknowns (128 MB of Jacobian) the run is refused before it allocates anything, asking for fewer harmonics.
- **Stopping:** `HarmonicBalanceConfig::ShouldStop` is asked before every Newton iteration, so the Stop button of imcsim ends a long run between iterations; once it asks for a stop, the source- and gmin-stepping fallbacks do not start.

---

## 2. Algorithms and Mathematical Formulation

### 2.1 Real Fourier Expansion & Autonomous Radix-2 FFT (`FftPlan`)
Each MNA unknown $x_u(t)$ over one fundamental period $[0, T_0)$ is represented by a truncated real cosine/sine series up to harmonic $H$:
$$x_u(t) = a_{u,0} + \sum_{k=1}^{H} \bigl(a_{u,k}\cos(k\omega_0 t) + b_{u,k}\sin(k\omega_0 t)\bigr), \qquad \omega_0 = 2\pi f_0$$
which maps to peak complex phasors $X_{u,0} = a_{u,0}$ and $X_{u,k} = a_{u,k} - j\,b_{u,k}$ ($k \ge 1$). Total Harmonic Distortion (`ComputeThd`) is evaluated as:
$$\text{THD} = \frac{\sqrt{\sum_{k=2}^{H} |X_k|^2}}{|X_1|}$$

The FFT engine in [`fft.cpp`](../../nodalys/src/linear/fft.cpp) uses pure C++23 with precomputed bit-reversal indices and twiddle factors (`FftPlan`):
- `HarmonicsToReal` ($\Gamma^{-1}$): reconstructs $M$ uniform time samples $t_m = m T_0 / M$ from $[a_0, a_1, b_1, \dots, a_H, b_H]$.
- `RealToHarmonics` ($\Gamma$): recovers DC + $H$ harmonic $(a_k, b_k)$ pairs from $M$ time samples via an $M$-point real-to-complex FFT.
- `RealToNormalizedSpectrum`: computes $1/M$-normalized complex DFT coefficients for differential conductance and capacitance traces used in the harmonic conversion Jacobian.

### 2.2 Linear / Nonlinear Partitioning & AFT Residual
`ValidateAndPartitionDevices` splits the compiled circuit once into **linear/source devices** ($\mathbf{G}_{\text{lin}}, \mathbf{C}_{\text{lin}}$) and **nonlinear elements** (`DiodeDevice`, `BjtDevice`, `MosfetDevice`, smooth logic macromodels). Note that switches without state hysteresis or linear dependent sources stay in the linear MNA partition.

Assembling the frequency-domain balance equation yields the real system $\mathbf{F}(\mathbf{X}) = \mathbf{0}$ of dimension $N_{\text{MNA}} \times (2H + 1)$:
$$\mathbf{F}(\mathbf{X}) = \mathbf{Y}_{\text{lin}}\,\mathbf{X} + \Gamma\,\mathbf{f}_{\text{nl}}(\Gamma^{-1}\mathbf{X}) + \Omega\,\Gamma\,\mathbf{q}_{\text{nl}}(\Gamma^{-1}\mathbf{X}) - \mathbf{B} = \mathbf{0}$$
where:
1. **Linear & excitation assembly (pure frequency domain):** For every harmonic $k \ge 1$, the real $2\times 2$ MNA block is stamped directly without time-domain conversion:
   $$\mathbf{Y}_{\text{lin},k} = \begin{bmatrix} \mathbf{G}_{\text{lin}} & k\omega_0\,\mathbf{C}_{\text{lin}} \\ -k\omega_0\,\mathbf{C}_{\text{lin}} & \mathbf{G}_{\text{lin}} \end{bmatrix}$$
   and the RHS $\mathbf{B}$ contains sampled-and-transformed periodic source waveforms (`SIN`/`AC`, `PULSE`, `CLOCK`, DC). When no nonlinear devices exist, `SolveLinearCircuit` solves the linear system directly in a single pass.
2. **Alternating Frequency-Time (AFT) nonlinear evaluation:**
   - **IDFT:** `UnpackCoefficientsToSamples` transforms $\mathbf{X}$ into time samples $\mathbf{x}_m = \Gamma^{-1}(\mathbf{X})$ for $m = 0 \dots M-1$.
   - **Time-domain point evaluation:** At each sample $m$, every nonlinear device stamps conductive contributions $\mathbf{G}_{\text{nl}}(\mathbf{x}_m)$, currents $\mathbf{f}_{\text{nl}}(\mathbf{x}_m)$, charges $\mathbf{q}_{\text{nl}}(\mathbf{x}_m)$, and differential capacitances $\mathbf{C}_{\text{nl}}(\mathbf{x}_m)$ with zero artificial integration history (`TimeContext{}`).
   - **FFT back to frequency:** $\mathbf{f}_{\text{nl}}$ and $\mathbf{q}_{\text{nl}}$ traces transform back via `RealToHarmonics`; capacitive displacement currents apply the frequency-domain derivative operator $\Omega$ ($I_{\text{cos},k} = k\omega_0\,Q_{\text{sin},k}$, $I_{\text{sin},k} = -k\omega_0\,Q_{\text{cos},k}$).

### 2.3 Harmonic Conversion Matrix Jacobian & Globalization
When building the Newton Jacobian, `ActiveMatrixTraces` collects only nonzero matrix entries $(r, c)$ across nonlinear devices, computes their $1/M$-normalized spectra $\hat{g}_n = \mathcal{F}\{g_{rc}(t)\}/M$ and $\hat{c}_n = \mathcal{F}\{c_{rc}(t)\}/M$, and stamps $(2H+1)\times(2H+1)$ real harmonic conversion blocks (`StampConversionBlock`):
- Each pair $(k, l)$ couples frequency bins through sum ($k+l$) and difference ($k-l$, with Hermitian conjugate for $k < l$) convolution coefficients of $\hat{g}_n$ and $\hat{c}_n$ (the classical real-form Toeplitz + Hankel conversion matrix for cos/sin mixing).
- Reactive blocks pre-multiply rows by $\pm k\omega_0$ ($\Omega$).
- Inside `RunHbNewton`, candidate updates apply time-domain PN-junction / smooth-logic step limiting (`device->Limit`) across the $M$ samples, falling back to backtracking line search when unlimited steps overshoot.

For strongly driven nonlinear circuits where direct Newton from the converged DC operating point fails, `SolveHarmonicBalanceNonlinear` applies sequential homotopy continuation:
1. **`"direct"`:** Full excitation ($\lambda = 1$) seeded from the DC bias solution (`SolveDcBiasForB0`).
2. **`"source stepping"`:** Adaptive continuation scaling all AC harmonic excitations $\lambda \in (0 \to 1]$ with step halving down to $10^{-4}$ and growth on fast convergence.
3. **`"gmin stepping"`:** Adds shunt `GMIN_START = 1e-3 S` on all node diagonals during source continuation, then steps `gmin` down by decades (`0.1×`) to `options.Gmin`.

Finally, `BuildHarmonicBalanceResult` reconstructs one-cycle waveforms for every circuit node and probes component terminal currents (combining `TerminalCurrent` and $\Omega\,\mathcal{F}\{\text{TerminalCharge}\}$ for nonlinear devices, or DC + `AcTerminalCurrent` for linear elements).

---

## 3. SPICE Syntax, CLI & `imcsim` Integration

- **Netlist directive:** `.hb <f0> <harmonics> [samples=<M>]` (optionally sets `FundamentalFrequency`, `Harmonics`, and optional `Samples`).
- **CLI & exporters:** `nodalys -a hb` (target `nodalys-cli`) runs Harmonic Balance and exports harmonic spectra and reconstructed time waveforms in CSV, JSON, and RAW formats.
- **`imcsim` exclusive support:** `AnalysisKind::HARMONIC_BALANCE` is registered solely in `NodalysBackend::GetCapabilities()`; `NgspiceBackend` leaves it disabled, and `imcsim` locks the Harmonic Balance controls with an explanatory tooltip when `ngspice` is active.
- **UI & user preference:** Settings panel provides Fundamental frequency (with a `"From sources"` suggestion button via `Core::SuggestHarmonicBalanceSettings`), Harmonics (`1..64`), and Samples (`Auto` or powers of two up to `65536`). Output displays a Peak vs. RMS toggle synced to `Preferences::HarmonicAmplitudes`, Linear / dB / dBc stem spectrum plot, 1–3 period reconstructed waveform plot, Summary (DC & THD%) and Harmonics tables, plus SVG/CSV export.

---

## 4. Verification and Benchmark Results: `.tran` Steady-State + FFT vs. `Nodalys` HB

### 4.1 Functional & Cross-Validation Coverage (`hb_analysis_tests.cpp`)
- **Linear parity:** RC low-pass filter, high-$Q$ series RLC resonant tank, and multi-tone ($f_0 + 3 f_0$) superposition match `.ac` and closed-form phasors.
- **DC degeneracy (`H = 0`):** Reproduces `.op` DC node voltages and currents on nonlinear diode circuits.
- **Nonlinear cross-validation vs. steady-state `.tran` + FFT:**
  - Half-wave diode rectifier with capacitive RC load (DC shift, harmonic magnitudes/phases, diode/source currents, THD).
  - Symmetric anti-parallel diode clipper (confirms suppression of even harmonics).
  - Current-driven parallel diode-RC network and phase-delayed `PULSE` source (modulo-$T_0$ periodicity).
  - BJT common-emitter amplifier driven into saturation and harmonic distortion.
  - Level-1 NMOS common-source stage with nonlinear channel + overlap capacitances ($C_{GS}, C_{GD}$).
  - Continuous smooth logic inverter (`LogicGateType::NOT`) excited by a periodic `PULSE` train.

### 4.2 Speedup Benchmark vs. Long-Time Transient + FFT (Release `[.benchmark]`)

High-$Q$ and slow-decay circuits require simulating hundreds of RF/switching cycles in `.tran` before startup transients decay, whereas Harmonic Balance solves periodic steady state directly in $\mathcal{O}(\text{Newton iterations})$ independent of circuit settling time $\tau$:

| Benchmark Circuit | Settling Span Simulated in `.tran` | HB Config ($f_0, H, M$) | `.tran` Steady-State + FFT | **`Nodalys` HB** | Speedup | Measured Cross-Check (`Nodalys` HB vs. `.tran`+FFT) |
| :--- | :---: | :---: | ---: | ---: | :---: | :--- |
| **High-$Q$ Series RLC + Shunt Diode Limiter** ($R=1\,\Omega, L=31.831\text{ mH}, C=795.77\text{ nF}, Q=200, \tau=63.66\text{ ms}$) | `400 ms` ($400\,T_0$) | `1 kHz`, `H=8`, `M=64` | 94.613 ms | **0.705 ms** *(10 iters, direct)* | **134.2x** | $\lvert V_3(f_0)\rvert$: HB `1.0470 V` vs. Tran `1.0479 V` ($\Delta = 9.14 \times 10^{-4}\text{ V}$) |
| **Long-$\tau$ Half-Wave Diode Rectifier** ($R=10\text{ k}\Omega, C=10\,\mu\text{F}, \tau=100\text{ ms}$) | `500 ms` ($500\,T_0$) | `1 kHz`, `H=16`, `M=128` | 104.624 ms | **2.469 ms** *(14 iters, direct)* | **42.4x** | $V_{\text{DC}}$: HB `5.3116 V` vs. Tran `5.3107 V` ($\Delta = 9.02 \times 10^{-4}\text{ V}$) |
