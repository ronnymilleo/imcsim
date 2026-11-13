# **Nodalys** Technical Documentation

**Nodalys** (a contraction of ***nodal** ana**lys**is*) is the clean-room C++23 circuit simulation engine of `imcsim`. It is structured as an autonomous, self-contained library (`nodalys/`) with zero external dependencies, accompanied by a standalone CLI executable (`nodalys`) and an integration bridge connecting it directly into `imcsim` (`app-core/backends/nodalys/`).

---

## Documentation Index

1. **[Architecture Overview & Analysis Comparison (`README.md`)](./README.md)** *(this document)*
2. **[DC Analyses: Operating Point (`.op`) and DC Sweep (`.dc`)](./dc_analysis.md)**
   - Algorithms: DC MNA formulation, dense LU with row equilibration and iterative refinement, damped Newton-Raphson (`pnjlim`, `fetlim`, logic stage damping), *Gmin stepping*, *Source stepping*, and homotopic continuation.
   - Benchmark tables: **Analytical Golden vs. `ngspice` vs. `Nodalys`**.
3. **[Time-Domain Transient Analysis (`.tran`)](./transient_analysis.md)**
   - Algorithms: Norton charge companion models (`ChargeModel`), Backward Euler + Trapezoidal Rule, adaptive step control via 3rd divided-difference LTE (Nagel SPICE2 Eq. 5.26), waveform/switch *breakpoints*, inertial logic propagation delay, and linear output interpolation.
   - Benchmark tables: **Analytical Golden vs. `ngspice` vs. `Nodalys`**.
4. **[Small-Signal AC / Bode Analysis (`.ac`)](./ac_analysis.md)**
   - Algorithms: Exact linearization around the DC operating point, complex MNA solver $\mathbf{Y}(j\omega)\mathbf{V} = \mathbf{I}_{\text{AC}}$, $C^1$-continuous depletion capacitance ($f_1, f_2, f_3$) and Gummel-Poon diffusion charge ($Q_{DE} = \tau_F I_{\text{fwd}}/q_b$), and continuous phase unwrapping.
   - Benchmark tables: **Analytical Golden vs. `ngspice` vs. `Nodalys`** + all shipped AC examples in `imcsim`.
5. **[Small-Signal DC Transfer Function Analysis (`.tf`)](./tf_analysis.md)**
   - Algorithms: Tangent Jacobian $\mathbf{J}(\mathbf{x}_{\text{DC}})$, unit input excitation ($\delta u_{\text{in}} = 1 \implies \text{Gain}, Z_{\text{in}}$), and unit output test current injection ($\delta I_{\text{out}} = 1\text{ A} \implies Z_{\text{out}}$).
   - Benchmark tables: **Analytical Golden vs. `ngspice` vs. `Nodalys`**.
6. **[Small-Signal AC Noise Analysis (`.noise`)](./noise_analysis.md)**
   - Algorithms: Transposed complex adjoint solver $\mathbf{Y}(j\omega)^T \lambda = \mathbf{e}_{\text{out}}$, thermal ($4k_BTR, \frac{8}{3}k_BTg_m$) and shot ($2qI$) noise spectral densities, SPICE3f5 log-log power-law integration (`Nintegrate`), and hierarchical component noise grouping.
   - Benchmark tables: **Analytical Golden vs. `ngspice` vs. `Nodalys`**.
7. **[Small-Signal S-Parameter Analysis (`.sp`)](./s_parameters.md)**
   - Algorithms: Multiport power wave formulation, Norton port excitation ($I_{s,j} = 2\sqrt{G_j}$), single-LU multi-RHS factorization, and derived RF figures of merit (Return Loss, Insertion Loss, VSWR, Input Impedance, Touchstone export).
   - Benchmark tables: **Analytical Golden vs. `ngspice` vs. `Nodalys`**.
8. **[Small-Signal Pole-Zero Analysis (`.pz`)](./pz_analysis.md)**
   - Algorithms: Generalized eigenvalue problem $(\mathbf{G}, -\mathbf{C})$ and Rosenbrock system pencil $(\mathbf{A}_z, -\mathbf{B}_z)$ via the Moler-Stewart QZ algorithm, finite root filtering, natural frequency $\omega_0$, quality factor $Q$, and damping ratio $\zeta$.
   - Benchmark tables: **Analytical Golden vs. `ngspice` vs. `Nodalys`** (accuracy and execution time).
9. **[Small-Signal DC Sensitivity Analysis (`.sens`)](./sens_analysis.md)**
   - Algorithms: Converged DC Jacobian adjoint system $\mathbf{J}^T \boldsymbol{\lambda} = \mathbf{d}$ (`MnaSystem::SolveTranspose`), analytical component derivatives ($\mathcal{O}(1)$ linear solve per analysis), 100% normalized scale, and hierarchical subcircuit/potentiometer aggregation.
   - Benchmark tables: **Analytical Golden vs. `ngspice` vs. `Nodalys`** (accuracy and execution time).

10. **[Harmonic Balance Analysis (`.hb`)](./hb_analysis.md)** *(exclusive to Nodalys)*
    - Algorithms: Alternating Frequency-Time (AFT) Newton-Raphson, internal pure C++23 radix-2 FFT (`FftPlan`), $(2H+1)\times(2H+1)$ Toeplitz/circulant harmonic conversion Jacobian, time-domain PN-junction limiting, and source/gmin stepping continuation.
    - Benchmarks: **`.tran` Steady-State + FFT vs. `Nodalys` HB** (`134.2x` speedup on $Q=200$ RLC diode limiter; `42.4x` speedup on $\tau=100\text{ ms}$ half-wave rectifier) and `imcsim` exclusive UI integration.

11. **[Fuse Device (`fuse`)](./fuse_device.md)**
    - Algorithms: heat equation $\tau\,\dot h = i^2/I_h^2 - h$ advanced by its exact solution, blow criterion and latch, a resistance that opens along a smooth step, and the blow predicted as a transient breakpoint.
    - Benchmarks: blow time against the closed form and the `ngspice` subcircuit.

---

## Architecture & Modular Layout

```
├── nodalys/                           Autonomous simulation engine (static lib & CLI)
│   ├── include/nodalys/               Public C++23 API (zero project dependencies)
│   │   ├── nodalys.h                  Analysis execution entry points
│   │   ├── types.h                    Parameters, analysis settings, and results
│   │   ├── circuit.h                  Opaque Circuit handle
│   │   └── circuit_builder.h          Fluent CircuitBuilder for components & models
│   └── src/
│       ├── api/                       CircuitBuilder implementation and Circuit internals
│       ├── analyses/                  Solvers: .op, .dc, .tran, .ac, .tf, .noise, .sp, .pz, .sens, .hb
│       ├── bridge/                    LogicBridge: Verilog discrete-to-analog co-simulation
│       ├── cli/                       Standalone CLI binary entry point (`nodalys`)
│       ├── compiler/                  CompiledCircuit internal representation
│       ├── devices/                   Passive, source, semiconductor, and smooth logic models
│       ├── export/                    CSV, JSON, SPICE Raw, and Touchstone multi-format exporters
│       ├── linear/                    BasicDenseMatrix (LU) and QZ generalized eigenvalue solver
│       ├── mna/                       MnaSystem (real and complex matrix assembly)
│       ├── newton/                    Damped Newton-Raphson, Gmin/Source stepping
│       └── parser/                    SPICE netlist lexer, parser, and engineering unit evaluator
└── app-core/backends/nodalys/                  imcsim Integration Bridge
    ├── compiler.h / .cpp              Translates Core::Circuit to Nodalys::Circuit
    └── nodalys_backend.h / .cpp       Core::SimulationBackend implementation (NodalysBackend)
```

### 1. Autonomous Engine Modules (`nodalys/`)

| Module / Directory | Responsibility |
| :--- | :--- |
| `include/nodalys/` | Public C++23 interface exporting `CircuitBuilder`, `Circuit`, and free analysis functions (`ComputeOperatingPoint`, `ComputeTransient`, `ComputeDCSweep`, `ComputeACSweep`, `ComputeTransferFunction`, `ComputeNoise`, `ComputeSParameters`, `ComputePoleZero`, `ComputeSensitivity`, `ComputeHarmonicBalance`). |
| `src/mna/` | Real and complex Modified Nodal Analysis (MNA) matrix assembly, transpose solve, dynamic $G_{\min}$ stabilization, and residual verification. |
| `src/linear/` | `BasicDenseMatrix<Scalar>` (LU decomposition), Moler-Stewart QZ generalized eigenvalue solver (`ComputeGeneralizedEigenvalues`), and internal radix-2 Cooley-Tukey FFT (`FftPlan`, `RealToHarmonics`, `HarmonicsToReal`). |
| `src/newton/` | Damped nonlinear Newton-Raphson driver with semiconductor junction damping (`pnjlim`, `fetlim`), logic output limiting, and fallback homotopy strategies (*Gmin stepping* and *Source stepping*). |
| `src/devices/` | Device models: linear passives, independent/controlled sources, hysteretic switches, relays, transformers, ideal & Boyle op-amps, nonlinear semiconductors (Shockley diode, Gummel-Poon BJT, Shichman-Hodges Level-1 MOSFET), and smooth continuous logic gates. |
| `src/analyses/` | Complete numerical solvers for DC Operating Point, DC Sweep (1D/2D), Time-Domain Transient, AC Small-Signal Sweep, DC Transfer Function, AC Adjoint Noise, Multiport S-Parameters, Pole-Zero, DC Sensitivity, and Harmonic Balance. |
| `src/bridge/` | `LogicBridge` translating discrete event-driven logic levels (`0`, `1`, `X`, `Z`) into continuous analog waveforms with realistic slew rates and configurable supply rails. |
| `src/parser/` & `src/export/` | SPICE netlist syntax parser supporting engineering scales (`1k`, `100n`, `4.7u`) and multi-format simulation result export (CSV, JSON, SPICE Raw, Touchstone). |
| `src/cli/` | Standalone CLI tool (`nodalys`) processing netlists from files or `stdin` with command-line options (`--analysis`, `--format`, `--output`). |

### 2. imcsim GUI & Model Bridge (`app-core/backends/nodalys/`)

| File | Responsibility |
| :--- | :--- |
| [`compiler.h`](../../app-core/backends/nodalys/compiler.h) / [`compiler.cpp`](../../app-core/backends/nodalys/compiler.cpp) | Translates `Core::Circuit` components, hierarchical subcircuits, parameter expressions, and logic parts directly into `Nodalys::CircuitBuilder`. |
| [`nodalys_backend.h`](../../app-core/backends/nodalys/nodalys_backend.h) / [`nodalys_backend.cpp`](../../app-core/backends/nodalys/nodalys_backend.cpp) | Implements `Core::SimulationBackend`, reporting capabilities and executing analyses directly on Nodalys with live chunk streaming and diagnostic forwarding. |

---

## Feature & Analysis Comparison: `ngspice` vs. `Nodalys`

### 1. Core Analyses (`SimulationBackend` / `simulator.h`)

| Analysis (`AnalysisKind`) | SPICE Command | `ngspice` | `Nodalys` | `Nodalys` Implementation & Notes |
| :--- | :---: | :---: | :---: | :--- |
| **Operating Point** | `.op` | ✅ | ✅ **Ready** | [`dc_analysis.cpp`](../../nodalys/src/analyses/dc_analysis.cpp) — Newton-Raphson + `pnjlim`/`fetlim` + *Gmin stepping* + *Source stepping* + bistable logic damping. |
| **DC Sweep** (1D & 2D) | `.dc` | ✅ | ✅ **Ready** | [`dc_analysis.cpp`](../../nodalys/src/analyses/dc_analysis.cpp) — 1-source or 2-source (`Swept` + `Stepped`) sweep with homotopic warm-start continuation. |
| **Transient** | `.tran` | ✅ | ✅ **Ready** | [`transient_analysis.cpp`](../../nodalys/src/analyses/transient_analysis.cpp) — Trapezoidal + Backward Euler, 3rd divided-difference LTE, *breakpoints*, live chunk streaming, and logic delays. |
| **AC Sweep (Bode)** | `.ac` | ✅ | ✅ **Ready** | [`ac_analysis.cpp`](../../nodalys/src/analyses/ac_analysis.cpp) — Operating point linearization + complex MNA solver $\mathbf{Y}(j\omega)\mathbf{V} = \mathbf{I}$ (gain in dB and continuous unwrapped phase). |
| **Transfer Function** | `.tf` | ✅ | ✅ **Ready** | [`tf_analysis.cpp`](../../nodalys/src/analyses/tf_analysis.cpp) — Small-signal DC gain ($A_v$ or $R_m$), input impedance $Z_{\text{in}}$, and output impedance $Z_{\text{out}}$. |
| **Noise Analysis** | `.noise` | ✅ | ✅ **Ready** | [`noise_analysis.cpp`](../../nodalys/src/analyses/noise_analysis.cpp) — Adjoint complex solver $\mathbf{Y}(j\omega)^T \lambda = \mathbf{e}_{\text{out}}$, input/output spectral densities ($\text{V}/\sqrt{\text{Hz}}$), log-log RMS integration, and per-component noise shares. |
| **S-Parameters** | `.sp` | ✅ | ✅ **Ready** | [`sp_analysis.cpp`](../../nodalys/src/analyses/sp_analysis.cpp) — Multiport scattering matrix $\mathbf{S}(j\omega)$, single-LU multi-RHS solve, Return Loss, Insertion Loss, VSWR, Input Impedance, and Touchstone export. |
| **Pole-Zero** | `.pz` | ✅ | ✅ **Ready** | [`pz_analysis.cpp`](../../nodalys/src/analyses/pz_analysis.cpp) — Generalized eigenvalue solver $(\mathbf{G}, -\mathbf{C})$ and Rosenbrock pencil $(\mathbf{A}_z, -\mathbf{B}_z)$ via pure C++23 QZ algorithm. |
| **Sensitivity** | `.sens` | ✅ | ✅ **Ready** | [`sens_analysis.cpp`](../../nodalys/src/analyses/sens_analysis.cpp) — Transposed adjoint solve $\mathbf{J}^T \boldsymbol{\lambda} = \mathbf{d}$, analytical device derivatives, 100% normalized scale, and subcircuit/potentiometer aggregation. |
| **Harmonic Balance** | `.hb` | ❌ | ✅ **Ready** | [`hb_analysis.cpp`](../../nodalys/src/analyses/hb_analysis.cpp) — Exclusive AFT Newton-Raphson solver with pure C++23 radix-2 FFT, Toeplitz/circulant conversion Jacobian, and source/gmin stepping continuation. |
| **Truth Table** | Multiple `.op` | ✅ | ✅ **Ready** | Fully supported natively in imcsim UI without ngspice via smooth continuous logic macromodels ($C^\infty$). |

---

### 2. Device Modeling & Component Support

| Category | Components / Models | `ngspice` | `Nodalys` | Notes |
| :--- | :--- | :---: | :---: | :--- |
| **Passives** | Resistor, Capacitor, Inductor | ✅ | ✅ **Ready** | Exact companion models; Norton charge integration with initial conditions (`uic`). |
| **Sources** | DC/AC Voltage & Current, Clock, Pulse, Sine, EXP, PWL, SFFM, AM | ✅ | ✅ **Ready** | Waveform generators; the transient lands on the corners of each. |
| **Noise & Random Sources** | `TRNOISE`, `TRRANDOM` | ✅ | 🟡 **Partial** | Held at their DC level outside a transient; a transient with one is refused. |
| **Behavioral Sources** | `B` element | ✅ | ❌ **Planned** | Refused with the reason, before the run. |
| **Fuses** | `imcsim_fuse` subcircuit | ✅ | ✅ **Ready** | Native device with a thermal state and a latch, with the blow time of `ngspice` within 3 % ([`fuse_device.md`](./fuse_device.md)). |
| **Controlled Sources** | VCVS, VCCS, CCCS, CCVS | ✅ | ✅ **Ready** | Stamped into linear MNA system; zero-voltage current probes handled natively. |
| **Semiconductors** | Diode, Zener, LED, BJT (NPN/PNP), MOSFET (NMOS/PMOS) | ✅ | ✅ **Ready** | Shockley diode, Gummel-Poon BJT, Shichman-Hodges Level-1 MOSFET with charge depletion/diffusion. |
| **Macromodels** | Ideal Op-Amp, Boyle Op-Amp (LTC AN48) | ✅ | ✅ **Ready** | Real datasheet-level macromodel with CMRR, slew rate, finite gain-bandwidth, and output clamp. |
| **Continuous Logic ($C^\infty$)** | NOT, BUFFER, AND, NAND, OR, NOR, XOR, XNOR, Schmitt Trigger | ✅ (`digital.cm`) | ✅ **Ready** | Hyperbolic tangent smooth macromodels with inertial delay RC filtering and Thevenin output stages. |
| **Sequential Logic** | D Flip-Flop, SR Latch, SR Flip-Flop, T Flip-Flop, JK Flip-Flop | ✅ (`digital.cm`) | ✅ **Ready** | Cross-coupled multi-stage implementations with Newton-Raphson voltage change damping. |
| **Co-Simulation Bridge** | Verilog discrete-to-analog converter | ❌ | ✅ **Ready** | Converts discrete HDL levels (`0`, `1`, `X`, `Z`) to continuous analog ramps with slew limiting. |

---

### 3. Cross-Cutting Modes & CLI Tooling

| Feature / Tool | Status in `Nodalys` | Description |
| :--- | :---: | :--- |
| **Standalone CLI (`nodalys`)** | ✅ **Ready** | Command-line tool accepting standard SPICE netlists from arguments or standard input (`stdin`). |
| **Multi-Format Export** | ✅ **Ready** | Standalone exporter formatting results into CSV, JSON, SPICE Raw, and Touchstone (.sNp) formats (`--format`). |
| **Component Parameter Stepping** | ✅ **Ready** | Steps $R, C, L$ across `.tran`, `.ac`, and `.dc` runs directly through `imcsim` controls. |
| **Fourier / Spectrum Analysis** | ✅ **Ready** | High-resolution FFT post-processed over `ComputeTransient` waveforms. |
| **Live Simulation Mode** | ✅ **Ready** | Live transient paced against the clock (`simulation/live_pace.h`, shared with ngspice), paused and resumed, with changes applied between chunks. |
| **Temperature Sweep ($T$)** | ❌ **Planned** | Semiconductor model evaluation at nominal $T_{\text{nom}} = 27\,^\circ\text{C}$; a stepped temperature is refused with the reason (`BackendCapabilities::TemperatureUnsupported`) until the thermal scaling equations land. |
| **Initial Conditions (`.ic`, `.nodeset`)** | 🟡 **Partial** | Element-level `uic` on capacitors ($V_0$) and inductors ($I_0$) supported; arbitrary node `.ic` pending. |
| **Gear-2 / BDF-2 Integration** | 🟡 **Partial** | 3rd divided-difference LTE step control with Trapezoidal/Euler supported; Gear-2 for stiff switching circuits pending. |
| **Vendor SPICE `.subckt` Parsing** | 🟡 **Partial** | Schematic hierarchy blocks and `.model` cards work; arbitrary external SPICE subcircuit netlist parser pending. |

---

### 4. Performance Benchmark Summary ([`nodalys_benchmark_tests.cpp`](../../tests/nodalys_benchmark_tests.cpp))

Measured on one development machine with `./out/release/bin/imcsim-tests "[.benchmark]"` (the benchmarks are hidden from the default run). The times depend on the machine and its load, so read the ratios as a rough guide, not as a guarantee:

| Circuit | Analysis | Nodes | `ngspice` | **`Nodalys`** | Speedup |
| :--- | :--- | ---: | ---: | ---: | :---: |
| **RC Ladder (100 nodes)** | DC OP | 101 | 0.910 ms | **0.236 ms** | **3.86x** |
| **RC Ladder (500 nodes)** | DC OP | 501 | 6.172 ms | **4.949 ms** | **1.25x** |
| **Diode Ladder (30 diodes)** | DC OP | 31 | 0.388 ms | 0.976 ms | 0.40x |
| **Full Adder Truth Table (8 rows)** | DC OP ×8 | 8 | N/A | **0.110 ms** | Autonomous |
| **BJT Cascaded Amp (4 stages)** | DC Sweep | 14 | 0.335 ms | **0.216 ms** | **1.55x** |
| **RC Ladder (100 nodes, 50 us)** | Transient | 101 | 2.297 ms | 8.930 ms | 0.26x |
| **RLC Line (50 stages, 100 ns)** | Transient | 101 | 2.597 ms | 39.878 ms | 0.07x |
| **Ring Oscillator (31 smooth gates)** | Transient | 31 | N/A | **69.954 ms** | Autonomous |
| **RC Filter (100 nodes, AC 350 pts)** | AC Sweep | 101 | 19.878 ms | 58.571 ms | 0.34x |
| **BJT Emitter Follower** | Transfer | 4 | 0.072 ms | **0.004 ms** | **18.62x** |
| **BJT CE Amp (41 pts)** | Noise | 5 | 0.226 ms | **0.079 ms** | **2.87x** |
| **3-Port Splitter (18 pts)** | S-Params | 4 | 0.186 ms | **0.014 ms** | **13.21x** |
| **Twin-T Notch Filter** | Pole-Zero | 4 | 0.171 ms | **0.008 ms** | **22.19x** |
| **BJT Cascaded Amp (4 stages)** | Sensitivity | 14 | 1.367 ms | **0.030 ms** | **44.84x** |
| **RC Ladder (100 nodes)** | Sensitivity | 101 | 10.334 ms | **0.430 ms** | **24.03x** |
| **RC Ladder (500 nodes)** | Sensitivity | 501 | 139.776 ms | **7.662 ms** | **18.24x** |
| **Diode Ladder (30 diodes)** | Sensitivity | 31 | 6.359 ms | **1.013 ms** | **6.28x** |

#### Harmonic Balance vs. Steady-State `.tran` + FFT *(Exclusive to Nodalys)*

| Benchmark Circuit | `.tran` Steady-State + FFT | **`Nodalys` HB** | Speedup | Cross-Validation |
| :--- | ---: | ---: | :---: | :--- |
| **High-$Q$ RLC + Diode Limiter ($Q=200$)** | 94.613 ms *(400 ms span)* | **0.705 ms** *(H=8, M=64)* | **134.2x** | $\lvert V_3(f_0)\rvert$: `1.0470 V` vs. `1.0479 V` |
| **Long-$\tau$ Half-Wave Rectifier ($\tau=100\text{ ms}$)** | 104.624 ms *(500 ms span)* | **2.469 ms** *(H=16, M=128)* | **42.4x** | $V_{\text{DC}}$: `5.3116 V` vs. `5.3107 V` |

---

### 5. Roadmap

The goal of `Nodalys` is to become the default backend of `imcsim`. Until it covers what `ngspice` does, every gap
sends the user back to `ngspice`, so the phases go from parity to what only `Nodalys` can offer, and each phase
starts once the one before it is done.

#### Done

| Item | SPICE Command | Notes |
| :--- | :---: | :--- |
| **Pole-Zero Analysis** | `.pz` | QZ algorithm over $(\mathbf{G}, -\mathbf{C})$ and the Rosenbrock pencil. |
| **DC Sensitivity** | `.sens` | One transposed adjoint solve $\mathbf{J}^T \boldsymbol{\lambda} = \mathbf{d}$ for every part. |
| **RF S-Parameters** | `.sp` | Multiport power waves, Touchstone export, Smith chart in `imcsim`. |
| **Harmonic Balance (1-Tone)** | `.hb` | AFT Newton with source and gmin stepping; refused above 4000 dense unknowns. |
| **Live Transient** | — | Paced against the clock and paused through the pace `imcsim` shares with `ngspice`. |

#### Phase 1 — Parity, to Become the Default Backend

| Item | Why | Today |
| :--- | :--- | :--- |
| **Sparse MNA Solver** (KLU-style, AMD ordering, symbolic reuse) | The dense LU makes the analyses users run most the slowest: the transient runs at 0.07x to 0.26x of `ngspice` and the AC sweep of a 100-node network at 0.34x (section 4), and it caps harmonic balance at 4000 unknowns. | Dense LU |
| **Gear-2 / BDF-2 Integration** | The trapezoidal rule rings on stiff switching, and the fuse needs Gear, as the `ngspice` backend writes `method=gear` for it. | Trapezoidal and Backward Euler |
| **Behavioral Sources** (`B` element) | Free expressions of node voltages and branch currents, already checked by `imcsim`. | Refused |
| **Temperature** | The SPICE temperature laws of the diode, BJT and MOSFET (`IS`, `VJ`, `EG`, `BF`...), so a stepped temperature runs. | Refused with the reason |
| **Noise and Random Sources in a Transient** | White, $1/f$ and telegraph noise, and the random distributions, drawn as `ngspice` draws them where possible. | DC level outside a transient; transient refused |
| **SPICE-Text Subcircuits** | Vendor `.subckt` models kept as SPICE text, such as the op-amp of the precision rectifier example. | Refused |
| **Node Initial Conditions** (`.ic`, `.nodeset`) | Starting a transient or guiding an operating point from node voltages, not only from parts. | Parts only (`uic`) |

**Done when** every example runs every analysis it offers on `Nodalys` and matches `ngspice` in the parity tests, and
the transient and AC benchmarks are no slower than `ngspice`.

#### Phase 2 — Cheap Wins for the Users of `imcsim`

| Item | Why |
| :--- | :--- |
| **Parallel Monte Carlo, Parameter Steps and Truth Tables** | `ngspice` is one global instance per process, so `imcsim` runs one simulation at a time; `Nodalys` keeps no global state, so independent runs can share the cores, with results that stay reproducible. |
| **Loop Gain / Stability** (`.stb`, Tian) | Phase and gain margin of a feedback loop without breaking it, over the existing AC solver; it fits the op-amp and regulator circuits `imcsim` is used for. |
| **Worst-Case (EVA) and RSS Tolerance** | The DC sensitivities times the tolerances `imcsim` already keeps: instant bounds beside Monte Carlo. |
| **2-Port RF Metrics** (Rollett $K$, $\mu$, MAG/MSG, stability circles, noise figure) | Post-processing of `SParameterResult` shown on the existing Smith chart. |

#### Phase 3 — Extending Harmonic Balance and Sensitivity

| Item | Why |
| :--- | :--- |
| **Power Sweep / P1dB / PAE / Large-Signal $S_{ij}$** | An amplitude loop over `ComputeHarmonicBalance`. |
| **AC Sensitivity** (`.sens ac`) | Adjoint solves over the complex AC MNA $\mathbf{Y}(j\omega)^T \boldsymbol{\lambda} = \mathbf{d}$. |
| **Modal Analysis and Participation Factors** | Every natural pole of $(\mathbf{G}, \mathbf{C})$ from the QZ solver, with the $L$ and $C$ that take part in each. |

#### Phase 4 — Long Term, Revisited on Demand

HB-AC / HB-Noise / HB-XF (`PAC`, `PNoise`, `PXF`), automatic transient noise, autonomous harmonic balance for
oscillators and phase noise, multi-tone harmonic balance (IMD, IIP3, mixers), circuit envelope and load-pull. They
serve RF and IC designers more than the interactive use `imcsim` is built for, they all grow with the dense HB
Jacobian until the sparse solver of phase 1 lands, and each one also costs `imcsim` an Output tab, its exports, file
settings and tests, as harmonic balance did.

#### Reference: Analyses Beyond Upstream `ngspice 47`

Comparison against upstream `ngspice 47` (`src/spicelib/analysis/`): upstream ships time-domain shooting `.pss`, small-signal Volterra distortion (`.disto`, up to 3rd order), manual `TRNOISE`/`TRRANDOM` sources, and only an unimplemented `WITH_HB` parser stub.

| Analysis / Capability | Domain | `ngspice 47` (Upstream) | `Nodalys` / `imcsim` Today | Effort | Notes & Value Over `ngspice` |
| :--- | :--- | :--- | :--- | :---: | :--- |
| **Harmonic Balance (1-Tone Driven, `.hb`)** | RF / Nonlinear | Stub only (`WITH_HB` unimplemented; `.pss` uses shooting) | ✅ **Ready** | — | Direct frequency-domain steady state without waiting out long startup transients. |
| **Power Sweep / P1dB / PAE / LSSP** | RF / Large-Signal | Not available (`.ac`/`.sp` are strictly small-signal) | Planned | Low | Amplitude/power loop over `ComputeHarmonicBalance` for compression (`P1dB`), PAE, and large-signal $S_{ij}$. |
| **Load-Pull Contours** | RF / Large-Signal | Not available | Planned | Low / Medium | Complex load-impedance sweep via HB plotted as constant-power / efficiency contours on the Smith chart. |
| **Loop Gain / Stability (`.stb` — Tian)** | Analog / Control | Not available | Planned | Low | Virtual bidirectional V/I probe in feedback loops computing open-loop $T(j\omega)$, phase margin, and gain margin without breaking DC bias or impedance loading. |
| **Worst-Case (EVA) & RSS Tolerance** | Tolerance / Yield | Not available (manual scripting or Monte Carlo only) | Monte Carlo only | Low | Combines sensitivity vectors with `imcsim` component tolerances for instant RSS bounds and extreme-value corners. |
| **2-Port RF Stability & Noise (`NF`, `Fmin`, `Rn`, `Sopt`)** | RF / Small-Signal | `.sp` supports `CKTspnoise`, but lacks $K$/$\mu$/circles | `SParameterResult` (no `donoise` or $K$/$\mu$) | Low | Post-processing on `SParameterResult` for Rollett $K$, $\mu$, MAG/MSG, Smith circles, plus multiport RF noise figures. |
| **Modal / Eigenvalue & Participation Factors** | Linear Dynamics | SISO `.pz` only | SISO `.pz` only | Low / Medium | Computes all natural poles of $(\mathbf{G}, \mathbf{C})$ via QZ and ranks $L$/$C$ participation per mode to pinpoint resonances without choosing an I/O pair. |
| **AC, HB & Transient Adjoint Sensitivity** | Sensitivity | Has `.sens ac`; legacy `.sens2` transient unmaintained/off | DC `.sens` only | Medium | Adjoint solves over complex AC MNA (`AC Sensitivity` above), periodic HB Jacobian, and time-domain companion matrices. |
| **HB-AC / HB-Noise / HB-XF (`PAC`/`PNoise`/`PXF`)** | Periodic Small-Signal | Upstream has `.pss` shooting only (no HB conversion) | Planned | Medium / High | Linearizes around the converged HB periodic solution via the harmonic conversion matrix for mixers, choppers, and SMPS. |
| **Automatic Transient Noise (`.trannoise`)** | Time / Noise | Manual `TRNOISE`/`TRRANDOM` voltage/current sources only | Planned | Medium / High | Automatically injects bandwidth-scaled thermal, shot, and $1/f$ noise per device during `.tran` for SNR and oscillator/comparator jitter. |
| **Autonomous HB (Oscillators + Phase Noise)** | RF / Oscillators | `.pss` shooting only (needs stabilization time) | Planned *(fixed `f0`)* | High | Solves unknown oscillation frequency $f_0$ jointly in HB Newton with a phase constraint; suited to high-$Q$ crystal and LC oscillators. |
| **Multi-Tone HB (IMD2 / IMD3 / IIP3 / Mixers)** | RF / Nonlinear | Not available (`.disto` is 3rd-order small-signal only) | Planned *(single-tone)* | High | Multidimensional spectral basis ($k_1 f_1 + k_2 f_2$) for non-commensurate LO+RF tones and large-signal intermodulation. |
| **Circuit Envelope** | RF / Modulated | Not available | Planned | High | Integrates slow baseband modulation envelopes in time while solving the RF carrier spectrum via HB at each step. |
