# **Nodalys** Technical Documentation

**Nodalys** (a contraction of ***nodal** ana**lys**is*) is the clean-room C++23 circuit simulation engine of `imcsim`. It is structured as an autonomous, self-contained library (`nodalys/`) with zero external dependencies, accompanied by a standalone CLI executable (`nodalys`) and an integration bridge connecting it directly into `imcsim` (`app-core/nodalys/`).

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
└── app-core/nodalys/                  imcsim Integration Bridge
    ├── compiler.h / .cpp              Translates Core::Circuit to nodalys::Circuit
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

### 2. imcsim GUI & Model Bridge (`app-core/nodalys/`)

| File | Responsibility |
| :--- | :--- |
| [`compiler.h`](../../app-core/nodalys/compiler.h) / [`compiler.cpp`](../../app-core/nodalys/compiler.cpp) | Translates `Core::Circuit` components, hierarchical subcircuits, parameter expressions, and logic parts directly into `nodalys::CircuitBuilder`. |
| [`nodalys_backend.h`](../../app-core/nodalys/nodalys_backend.h) / [`nodalys_backend.cpp`](../../app-core/nodalys/nodalys_backend.cpp) | Implements `Core::SimulationBackend`, reporting capabilities and executing analyses directly on Nodalys with live chunk streaming and diagnostic forwarding. |

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
| **Sources** | DC/AC Voltage & Current, Clock, Pulse, Sine, SFFM | ✅ | ✅ **Ready** | Waveform generators with exact analytical derivatives and breakpoint registration. |
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
| **Live Simulation Mode** | ✅ **Ready** | Real-time interactive simulation streaming chunk deliveries and altering netlist values on the fly. |
| **Temperature Sweep ($T$)** | ❌ **Planned** | Semiconductor model evaluation at nominal $T_{\text{nom}} = 27\,^\circ\text{C}$; thermal scaling equations pending. |
| **Initial Conditions (`.ic`, `.nodeset`)** | 🟡 **Partial** | Element-level `uic` on capacitors ($V_0$) and inductors ($I_0$) supported; arbitrary node `.ic` pending. |
| **Gear-2 / BDF-2 Integration** | 🟡 **Partial** | 3rd divided-difference LTE step control with Trapezoidal/Euler supported; Gear-2 for stiff switching circuits pending. |
| **Vendor SPICE `.subckt` Parsing** | 🟡 **Partial** | Schematic hierarchy blocks and `.model` cards work; arbitrary external SPICE subcircuit netlist parser pending. |

---

### 4. Performance Benchmark Summary ([`nodalys_benchmark_tests.cpp`](../../tests/nodalys_benchmark_tests.cpp))

Measured via `./out/release/bin/imcsim-tests "[benchmark]"` across all implemented analyses (`All tests passed`):

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

### 5. Roadmap Analyses (`jay` Epics)

| Analysis | SPICE Command | Description | Status |
| :--- | :---: | :--- | :---: |
| **Pole-Zero Analysis** | `.pz` | Generalized eigenvalue solver $(\mathbf{G} + s\mathbf{C})\mathbf{x} = \mathbf{0}$ via the QZ algorithm to find transfer function poles and zeros. | ✅ **Ready** |
| **DC Sensitivity** | `.sens` | Per-component adjoint sensitivity $\partial V_{\text{out}} / \partial p_i$ solved via transposed system $\mathbf{J}^T \boldsymbol{\lambda} = \mathbf{d}$. | ✅ **Ready** |
| **RF S-Parameters** | `.sp` | Scattering matrix $S_{ij}(f)$, return loss, VSWR, and Smith chart generation with $Z_0$ port termination. | ✅ **Ready** |
| **Harmonic Balance (HB)** | `.hb` | Frequency-domain single-tone periodic steady-state AFT solver + `imcsim` exclusive UI integration *(multi-tone HB remains future work)*. | ✅ **Ready** |
| **AC Sensitivity** | `.sens ac` | Small-signal frequency-domain adjoint sensitivity over complex MNA $\mathbf{Y}(j\omega)^T \boldsymbol{\lambda} = \mathbf{d}$. | Planned |
| **Sparse MNA Solver** | Infrastructure | Sparse LU decomposition (e.g., KLU-style AMD ordering) for >100-node transient and AC acceleration. | Planned |
| **Parallel Monte Carlo** | Statistical | Multi-threaded statistical parameter tolerance sweeps across independent `nodalys::Circuit` instances. | Planned |
