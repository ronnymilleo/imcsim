# **Nodalys** Technical Documentation (`app-core/native/`)

**Nodalys** (a contraction of ***nodal** ana**lys**is*) is the clean-room C++23 circuit simulation engine of `imcsim`, built on **Modified Nodal Analysis (MNA)** with zero external dependencies.

---

## Documentation Index

1. **[Architecture Overview & Analysis Comparison (`README.md`)](./README.md)** *(this document)*
2. **[DC Analyses: Operating Point (`.op`) and DC Sweep (`.dc`)](./dc_analysis.md)**
   - Algorithms: DC MNA formulation, dense LU with row equilibration and iterative refinement, damped Newton-Raphson (`pnjlim`, `fetlim`), *Gmin stepping*, *Source stepping*, and homotopic continuation.
   - Benchmark tables: **Analytical Golden vs. `ngspice` vs. `Nodalys`**.
3. **[Time-Domain Transient Analysis (`.tran`)](./transient_analysis.md)**
   - Algorithms: Norton charge companion models (`ChargeModel`), Backward Euler + Trapezoidal Rule, comparison against BDF-2 / Gear-2, adaptive step control via 3rd divided-difference LTE (Nagel SPICE2 Eq. 5.26), waveform/switch *breakpoints*, and linear output interpolation.
   - Benchmark tables: **Analytical Golden vs. `ngspice` vs. `Nodalys`**.
4. **[Small-Signal AC / Bode Analysis (`.ac`)](./ac_analysis.md)**
   - Algorithms: Exact linearization around the DC operating point, complex MNA solver $\mathbf{Y}(j\omega)\mathbf{V} = \mathbf{I}_{\text{AC}}$, $C^1$-continuous depletion capacitance ($f_1, f_2, f_3$) and Gummel-Poon diffusion charge ($Q_{DE} = \tau_F I_{\text{fwd}}/q_b$), and continuous phase unwrapping.
   - Benchmark tables: **Analytical Golden vs. `ngspice` vs. `Nodalys`** + all 8 shipped AC examples in `imcsim`.
5. **[Small-Signal DC Transfer Function Analysis (`.tf`)](./tf_analysis.md)**
   - Algorithms: Tangent Jacobian $\mathbf{J}(\mathbf{x}_{\text{DC}})$, unit input excitation ($\delta u_{\text{in}} = 1 \implies \text{Gain}, Z_{\text{in}}$), and unit output test current injection ($\delta I_{\text{out}} = 1\text{ A} \implies Z_{\text{out}}$).
   - Benchmark tables: **Analytical Golden vs. `ngspice` vs. `Nodalys`**.
6. **[Small-Signal AC Noise Analysis (`.noise`)](./noise_analysis.md)**
   - Algorithms: Transposed complex adjoint solver $\mathbf{Y}(j\omega)^T \lambda = \mathbf{e}_{\text{out}}$, thermal ($4k_BTR, \frac{8}{3}k_BTg_m$) and shot ($2qI$) noise spectral densities, SPICE3f5 log-log power-law integration (`Nintegrate`), and hierarchical component noise grouping.
   - Benchmark tables: **Analytical Golden vs. `ngspice` vs. `Nodalys`**.
7. **[Architecture Plan: Continuous Behavioral Logic Models and Verilog Integration](./behavioral_logic_and_verilog_plan.md)**
   - Rationale and differentiation against `ngspice` XSPICE `digital.cm`.
   - Mathematical formulation of smooth $C^\infty$ Boolean operators, dynamic inertial delay ($t_{\text{pd}}$), and Thévenin drive stages.
   - Architectural blueprint for future Verilog-A (compact device DAEs via OSDI) and Verilog-HDL (event-driven co-simulation via A2D/D2A bridges and breakpoint synchronization).

---

## Modular Code Layout (`app-core/native/`)

| File | Primary Responsibility |
| :--- | :--- |
| [`native_backend.h`](../../app-core/native/native_backend.h) / [`native_backend.cpp`](../../app-core/native/native_backend.cpp) | `Core::SimulationBackend` implementation (`NativeBackend`), capability matrix, and analysis dispatch. |
| [`compiler.h`](../../app-core/native/compiler.h) / [`compiler.cpp`](../../app-core/native/compiler.cpp) | Single-pass circuit compiler translating `Core::Circuit` (including hierarchical subcircuits and the Boyle op-amp macromodel) into `CompiledCircuit`. |
| [`mna_system.h`](../../app-core/native/mna_system.h) / [`app-core/native/mna_system.cpp`](../../app-core/native/mna_system.cpp) | Real ($A x = b$) and complex ($\mathbf{Y}(j\omega)\mathbf{x} = \mathbf{b}$) MNA assembly, automatic `Gmin` diagonal stamping, and KCL residual verification. |
| [`linear/dense_matrix.h`](../../app-core/native/linear/dense_matrix.h) / [`linear/dense_matrix.cpp`](../../app-core/native/linear/dense_matrix.cpp) | `BasicDenseMatrix<Scalar>` (`double` and `std::complex<double>`): partial-pivoting $PA = LU$ factorization, row equilibration, and iterative refinement. |
| [`newton.h`](../../app-core/native/newton.h) / [`newton.cpp`](../../app-core/native/newton.cpp) | Damped nonlinear Newton-Raphson solver with junction limiting (`pnjlim`, `fetlim`), *Gmin stepping*, and *Source stepping*. |
| [`devices.h`](../../app-core/native/devices.h) / [`devices.cpp`](../../app-core/native/devices.cpp) | DC, Transient, and AC stamps for resistors, capacitors, inductors, independent/controlled sources, hysteretic switches, relays, transformers, and ideal op-amps. |
| [`nonlinear.h`](../../app-core/native/nonlinear.h) / [`nonlinear.cpp`](../../app-core/native/nonlinear.cpp) | Physical semiconductor models: Shockley/Zener Diode (`DiodeDevice`), Gummel-Poon BJT (`BjtDevice`), and Level-1 Shichman-Hodges MOSFET (`MosfetDevice`). |
| [`dc_analysis.cpp`](../../app-core/native/dc_analysis.cpp) | DC Operating Point (`ComputeOperatingPoint`) and 1D/2D DC Sweep (`ComputeDCSweep`). |
| [`transient_analysis.cpp`](../../app-core/native/transient_analysis.cpp) | Time-domain Transient simulation (`ComputeTransient`) with Backward Euler / Trapezoidal integration, LTE step control, *breakpoints*, and *Live* mode. |
| [`ac_analysis.cpp`](../../app-core/native/ac_analysis.cpp) | Frequency-domain Small-Signal AC / Bode sweep (`ComputeACSweep`). |
| [`tf_analysis.cpp`](../../app-core/native/tf_analysis.cpp) | Small-Signal DC Transfer Function (`ComputeTransferFunction`). |
| [`noise_analysis.cpp`](../../app-core/native/noise_analysis.cpp) | Small-Signal AC Noise spectral density and integrated RMS analysis (`ComputeNoise`). |

---

## Feature & Analysis Comparison: `ngspice` vs. `Nodalys`

### 1. Core `imcsim` Analyses (`SimulationBackend` / `simulator.h`)

All **6 analog solver analyses** in [`SimulationBackend`](../../app-core/simulation/simulation_backend.h) are now **fully implemented and verified in `Nodalys`** (plus `TruthTable` in [`simulator.h`](../../app-core/simulation/simulator.h) for transistor/diode logic):

| Analysis (`AnalysisKind`) | SPICE Command | `ngspice` | `Nodalys` | `Nodalys` Implementation & Notes |
| :--- | :---: | :---: | :---: | :--- |
| **Operating Point** | `.op` | ✅ | ✅ **Ready** | [`dc_analysis.cpp`](../../app-core/native/dc_analysis.cpp) — Newton-Raphson + `pnjlim`/`fetlim` + *Gmin stepping* + *Source stepping*. |
| **DC Sweep** (1D & 2D) | `.dc` | ✅ | ✅ **Ready** | [`dc_analysis.cpp`](../../app-core/native/dc_analysis.cpp) — 1-source or 2-source (`Swept` + `Stepped`) sweep with homotopic continuation. |
| **Transient** | `.tran` | ✅ | ✅ **Ready** | [`transient_analysis.cpp`](../../app-core/native/transient_analysis.cpp) — Trapezoidal + Backward Euler, *breakpoints*, chunk streaming, cancel, and *Live* mode. |
| **AC Sweep (Bode)** | `.ac` | ✅ | ✅ **Ready** | [`ac_analysis.cpp`](../../app-core/native/ac_analysis.cpp) — DC operating point linearization + complex MNA solver $\mathbf{Y}(j\omega)\mathbf{V} = \mathbf{I}$ (dB & unwrapped phase). |
| **Transfer Function** | `.tf` | ✅ | ✅ **Ready** | [`tf_analysis.cpp`](../../app-core/native/tf_analysis.cpp) — Small-signal DC gain ($A_v$ or $R_m$), $Z_{\text{in}}$, and $Z_{\text{out}}$. |
| **Noise Analysis** | `.noise` | ✅ | ✅ **Ready** | [`noise_analysis.cpp`](../../app-core/native/noise_analysis.cpp) — Adjoint complex solver $\mathbf{Y}(j\omega)^T \lambda = \mathbf{e}_{\text{out}}$, output & input-referred spectral density ($\text{V}/\sqrt{\text{Hz}}$), log-log power-law RMS integration, and per-part breakdown. |
| **Truth Table** | Multiple `.op` | ✅ | 🟡 **In Progress** (`#133`) | Runs on `Nodalys` for transistor/diode logic circuits; primitive digital gates (`AND`, `OR`, Flip-Flops) transitioning to native behavioral models (see [Behavioral Logic Plan](./behavioral_logic_and_verilog_plan.md)). |

---

### 2. Cross-Cutting Modes & Refinements in Existing Analyses

| Feature / Mode | `ngspice` | `Nodalys` | Current Status in `Nodalys` |
| :--- | :---: | :---: | :--- |
| **Spectrum / FFT (Spectrum Tab)** | ✅ | ✅ **Ready** | Post-processed over `RunTransient` waveforms in [`spectrum.cpp`](../../app-gui/plots/spectrum.cpp). |
| **Component Parameter Stepping ($R, C, L$)** | ✅ | ✅ **Ready** | Works across `.tran`, `.ac`, and `.dc` via [`stepped_circuit.cpp`](../../app-core/simulation/stepped_circuit.cpp). |
| **Temperature Sweep ($T\text{ in }^\circ\text{C}$)** | ✅ | ❌ **Planned** | `temperature_celsius` is wired to the backend API, but semiconductor models are currently evaluated at nominal $T_{\text{nom}} = 27\,^\circ\text{C}$ ($V_T(T)$, $I_S(T)$, and $V_J(T)$ scaling pending). |
| **Initial Conditions (`.ic`, `.nodeset`, `uic`)** | ✅ | 🟡 **Partial** (`#86`) | `uic` on capacitors ($V_0$) and inductors ($I_0$) works; node-level `.ic` / `.nodeset` operating-point forcing is pending. |
| **Gear-2 (BDF-2) + Adaptive LTE Step Control** | ✅ | 🟡 **Partial** (`#125`) | Transient already uses 3rd divided-difference LTE with Trapezoidal integration + step cutback; BDF-2 / Gear-2 option for stiff switching circuits is pending. |
| **Arbitrary Raw SPICE Subcircuits (`.subckt`)** | ✅ | 🟡 **Partial** (`#141`) | Schematic hierarchy blocks and `.model` cards (`D`, `NPN`, `PNP`, `NMOS`, `PMOS`) work; raw vendor `.subckt` netlist parsing (used only in `precision_rectifier.imcsim`) is pending. |

---

### 3. Future Roadmap Analyses (`jay` Epics `P2`–`P6`)

| Analysis | SPICE Command | `ngspice` | `Nodalys` (`jay` Roadmap) | Brief Description |
| :--- | :---: | :---: | :---: | :--- |
| **Pole-Zero Analysis** | `.pz` | ✅ | ❌ Planned (`#136`, `#182`) | Generalized eigenvalues $(\mathbf{G} + s\mathbf{C})\mathbf{x} = \mathbf{0}$ via the QZ algorithm to extract poles and zeros of $H(s)$. |
| **DC / AC Sensitivity** | `.sens` | ✅ | ❌ Planned (`#109`, `#135`, `#180`) | Per-component sensitivity $\partial V_{\text{out}} / \partial p_i$ solved via the adjoint system $\mathbf{J}^T \lambda = \mathbf{e}_{\text{out}}$. |
| **S-Parameters (RF)** | `.sp` | ✅ | ❌ Planned (`#96`, `#97`, `#185`, `#186`) | Scattering matrix $S_{ij}(f)$, $Z(f)$, $Y(f)$, and Smith chart with $Z_0$ port elements. |
| **Distortion / Harmonic Balance** | `.disto` / HB | 🟡 (`.disto`) | ❌ Planned (`#113`, `#114`) | Frequency-domain periodic steady state for nonlinear RF circuits (single-tone and multi-tone). |
| **Parallel Monte Carlo** | `.control` scripts | 🟡 | ❌ Planned (`#107`, `#184`) | Parallel statistical tolerance sweeps across independent `Nodalys` solver instances. |
