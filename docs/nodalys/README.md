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
   - Algorithms: Exact linearization around the DC operating point, complex MNA solver $\mathbf{Y}(j\omega)\mathbf{V} = \mathbf{I}_{\text{AC}}$, $C^1$-continuous depletion capacitance ($f_1, f_2, f_3$) and Gummel-Poon diffusion charge ($Q_{DE} = \tau_F I_{\text{fwd}}/q_b$), and phase in $(-180^\circ, +180^\circ]$.
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

    - AC form: [Small-Signal AC Sensitivity Analysis (`.sens ... ac`)](./ac_sens_analysis.md), *exclusive to Nodalys*. A transposed complex adjoint solve per frequency $\mathbf{Y}(j\omega)^T \boldsymbol{\lambda} = \mathbf{d}$, the bias term of the nonlinear devices through the DC adjoint, and the change of gain and phase for 1 % of each part.

10. **[Harmonic Balance Analysis (`.hb`)](./hb_analysis.md)** *(exclusive to Nodalys)*
    - Algorithms: Alternating Frequency-Time (AFT) Newton-Raphson, internal pure C++23 radix-2 FFT (`FftPlan`), $(2H+1)\times(2H+1)$ Toeplitz/circulant harmonic conversion Jacobian, time-domain PN-junction limiting, and source/gmin stepping continuation.
    - Benchmarks: **`.tran` Steady-State + FFT vs. `Nodalys` HB** (`134.2x` speedup on $Q=200$ RLC diode limiter; `42.4x` speedup on $\tau=100\text{ ms}$ half-wave rectifier) and `imcsim` exclusive UI integration.

11. **[Fuse Device (`fuse`)](./fuse_device.md)**
    - Algorithms: heat equation $\tau\,\dot h = i^2/I_h^2 - h$ advanced by its exact solution, blow criterion and latch, a resistance that opens along a smooth step, and the blow predicted as a transient breakpoint.
    - Benchmarks: blow time against the closed form and the `ngspice` subcircuit.

12. **[Behavioral Sources (`B` element)](./behavioral_source.md)**
    - Algorithms: a recursive-descent parser of the `ngspice` expression language into a flat program, forward-mode automatic differentiation for the exact partial derivatives with respect to every node voltage and branch current, and the linearized stamp of a branch (`V=`) or a current (`I=`) at each Newton iterate.
    - Benchmarks: the parity tests against `ngspice` over 29 expressions in both forms and four analyses.

13. **[Subcircuits Kept as SPICE Text](./subcircuit_text.md)**
    - Algorithms: an expansion of the text of a `.subckt` into a `CircuitBuilder` with the scopes of SPICE (parameters, `.model` cards, nested subcircuits), private nodes and element names for each instance, values as expressions of parameters, and a refusal that names the line for everything the engine cannot simulate.
    - Benchmarks: the precision rectifier example, whose op-amp is such a block, against `ngspice`.

14. **[Transmission Lines (`TL` / `T` element)](./transmission_line.md)**
    - Algorithms: voltage-scaled even/odd telegrapher MNA equations free of half-wave singularities, $\sqrt{f}$ skin-effect and $\omega C \tan\delta$ dielectric dispersion across `.ac`, `.sp`, `.noise`, `.sens ... ac`, and `.hb`, Bosma passive two-port thermal noise, and Bergeron characteristic wave propagation with corner breakpoints in `.tran`.

---

## Architecture & Modular Layout

```
├── nodalys/                           Autonomous simulation engine (static lib & CLI)
│   ├── include/nodalys/               Public C++23 API (zero project dependencies)
│   │   ├── nodalys.h                  Umbrella header including all public headers
│   │   ├── analyses.h                 Analysis configs, options, and Compute* entry points
│   │   ├── results.h                  Analysis result structures and accessors
│   │   ├── types.h                    Component parameters, units, and enums
│   │   ├── circuit.h                  Opaque Circuit handle
│   │   ├── circuit_builder.h          Fluent CircuitBuilder for components & models
│   │   ├── expression.h               Expression of voltages, currents and time with exact derivatives
│   │   ├── netlist_parser.h           SPICE netlist parser and ParseSpiceNumber
│   │   ├── subcircuit_text.h          SPICE-text .subckt expander API
│   │   ├── exporters.h                CSV, JSON, SPICE Raw, and Touchstone exporters
│   │   └── logic_bridge.h             Discrete-to-analog Verilog co-simulation bridge
│   └── src/
│       ├── api/                       CircuitBuilder implementation and Circuit internals
│       ├── analyses/                  Solvers: .op, .dc, .tran, .ac, .tf, .noise, .sp, .pz, .sens (DC and AC), .hb
│       ├── bridge/                    LogicBridge: Verilog discrete-to-analog co-simulation
│       ├── cli/                       Standalone CLI binary entry point (`nodalys`)
│       ├── compiler/                  CircuitDescription, CircuitCompiler, macromodels, and CompiledCircuit
│       ├── devices/                   Passive, source, semiconductor, fuse, switch, and smooth logic models
│       ├── export/                    CSV, JSON, SPICE Raw, and Touchstone multi-format exporters
│       ├── expression/                Parser, evaluation and derivatives of behavioral source expressions
│       ├── linear/                    BasicDenseMatrix (LU), FFT, and QZ generalized eigenvalue solver
│       ├── mna/                       MnaSystem (real and complex matrix assembly)
│       ├── newton/                    Damped Newton-Raphson, Gmin/Source stepping
│       ├── parser/                    SPICE netlist parser, and the expansion of subcircuits kept as SPICE text
│       └── text/                      SPICE number and case-insensitive text helpers
└── app-core/backends/nodalys/         imcsim Integration Bridge
    ├── compiler.h / .cpp              Translates Core::Circuit to Nodalys::Circuit
    ├── compiler_context.h             Shared probe planning and node/parameter helpers
    ├── compiler_parts.h / .cpp        Passives, sources, semiconductors, switches, fuses, and op-amps
    ├── compiler_logic.h / .cpp        Continuous logic gates and flip-flops
    ├── model_parser.h / .cpp          SPICE .model card parsing into Nodalys parameters
    ├── parameter_conversion.h / .cpp  Component parameter mapping
    ├── spice_block_check.h / .cpp     Pre-run validation for SPICE-text blocks
    ├── nodalys_results.h / .cpp       Translates Nodalys results to Core result types
    └── nodalys_backend.h / .cpp       Core::SimulationBackend implementation (NodalysBackend)
```

### 1. Autonomous Engine Modules (`nodalys/`)

| Module / Directory | Responsibility |
| :--- | :--- |
| `include/nodalys/` | Public C++23 interface exporting `CircuitBuilder`, `Circuit`, `AddSubcircuitText`, `ParseNetlist`, and free analysis functions in [`analyses.h`](../../nodalys/include/nodalys/analyses.h) (`ComputeOperatingPoint`, `ComputeTransient`, `ComputeDCSweep`, `ComputeACSweep`, `ComputeTransferFunction`, `ComputeNoise`, `ComputeSParameters`, `ComputePoleZero`, `ComputeSensitivity`, `ComputeAcSensitivity`, `ComputeHarmonicBalance`). |
| `src/mna/` | Real and complex Modified Nodal Analysis (MNA) matrix assembly, transpose solve, dynamic $G_{\min}$ stabilization, and residual verification. |
| `src/linear/` | `BasicDenseMatrix<Scalar>` (LU decomposition), Moler-Stewart QZ generalized eigenvalue solver (`ComputeGeneralizedEigenvalues`), and internal radix-2 Cooley-Tukey FFT (`FftPlan`, `RealToHarmonics`, `HarmonicsToReal`). |
| `src/newton/` | Damped nonlinear Newton-Raphson driver with semiconductor junction damping (`pnjlim`, `fetlim`), logic output limiting, and fallback homotopy strategies (*Gmin stepping* and *Source stepping*). |
| `src/devices/` | Device models: linear passives, independent/controlled/behavioral sources, hysteretic switches, relays, transformers, fuses, ideal & Boyle op-amps, nonlinear semiconductors (Shockley diode, Gummel-Poon BJT, Shichman-Hodges Level-1 MOSFET), and smooth continuous logic gates. |
| `src/expression/` | The language of the behavioral source: a recursive-descent parser to a flat program, forward-mode automatic differentiation, and the value and partial derivatives of every operator and function ([`behavioral_source.md`](./behavioral_source.md)). |
| `src/analyses/` | Complete numerical solvers for DC Operating Point, DC Sweep (1D/2D), Time-Domain Transient, AC Small-Signal Sweep, DC Transfer Function, AC Adjoint Noise, Multiport S-Parameters, Pole-Zero, DC and AC Sensitivity, and Harmonic Balance. |
| `src/bridge/` | `LogicBridge` translating discrete event-driven logic levels (`0`, `1`, `X`, `Z`) into continuous analog waveforms with realistic slew rates and configurable supply rails. |
| `src/parser/` & `src/export/` | SPICE netlist syntax parser supporting engineering scales (`1k`, `100n`, `4.7u`), the expansion of a subcircuit kept as SPICE text, which the `X` lines of a netlist use too (`subcircuit_*.cpp`, `netlist_subcircuits.cpp`, [`subcircuit_text.md`](./subcircuit_text.md)) and multi-format simulation result export (CSV, JSON, SPICE Raw, Touchstone). |
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
| **Operating Point** | `.op` | ✅ | ✅ **Ready** | [`dc_analysis.cpp`](../../nodalys/src/analyses/dc/dc_analysis.cpp) — Newton-Raphson + `pnjlim`/`fetlim` + *Gmin stepping* + *Source stepping* + bistable logic damping. |
| **DC Sweep** (1D & 2D) | `.dc` | ✅ | ✅ **Ready** | [`dc_analysis.cpp`](../../nodalys/src/analyses/dc/dc_analysis.cpp) — 1-source or 2-source (`Swept` + `Stepped`) sweep with homotopic warm-start continuation. |
| **Transient** | `.tran` | ✅ | ✅ **Ready** | [`transient_analysis.cpp`](../../nodalys/src/analyses/transient/transient_analysis.cpp) — Trapezoidal + Backward Euler, 3rd divided-difference LTE, *breakpoints*, live chunk streaming, and logic delays. |
| **AC Sweep (Bode)** | `.ac` | ✅ | ✅ **Ready** | [`ac_analysis.cpp`](../../nodalys/src/analyses/ac/ac_analysis.cpp) — Operating point linearization + complex MNA solver $\mathbf{Y}(j\omega)\mathbf{V} = \mathbf{I}$ (gain in dB and phase in $(-180^\circ, +180^\circ]$), `dec`, `oct` or `lin` sweep. |
| **Transfer Function** | `.tf` | ✅ | ✅ **Ready** | [`tf_analysis.cpp`](../../nodalys/src/analyses/tf/tf_analysis.cpp) — Small-signal DC gain ($A_v$ or $R_m$), input impedance $Z_{\text{in}}$, and output impedance $Z_{\text{out}}$. |
| **Noise Analysis** | `.noise` | ✅ | ✅ **Ready** | [`noise_analysis.cpp`](../../nodalys/src/analyses/noise/noise_analysis.cpp) — Adjoint complex solver $\mathbf{Y}(j\omega)^T \lambda = \mathbf{e}_{\text{out}}$, input/output spectral densities ($\text{V}/\sqrt{\text{Hz}}$), log-log RMS integration, and per-component noise shares, `dec`, `oct` or `lin` sweep. |
| **S-Parameters** | `.sp` | ✅ | ✅ **Ready** | [`sp_analysis.cpp`](../../nodalys/src/analyses/sp/sp_analysis.cpp) — Multiport scattering matrix $\mathbf{S}(j\omega)$ from one port up, single-LU multi-RHS solve, exact $\mathbf{Z}$ and $\mathbf{Y}$ matrices (absent where the circuit has none), Return Loss, Insertion Loss, VSWR, Input Impedance, and Touchstone export. |
| **Pole-Zero** | `.pz` | ✅ | ✅ **Ready** | [`pz_analysis.cpp`](../../nodalys/src/analyses/pz/pz_analysis.cpp) — Generalized eigenvalue solver $(\mathbf{G}, -\mathbf{C})$ and Rosenbrock pencil $(\mathbf{A}_z, -\mathbf{B}_z)$ via pure C++23 QZ algorithm, after balancing and exact removal of algebraic rows ([`pencil_reduction.cpp`](../../nodalys/src/linear/eigen/pencil_reduction.cpp)), with the checks of [`pz_checks.cpp`](../../nodalys/src/analyses/pz/pz_checks.cpp). |
| **Sensitivity** | `.sens` | ✅ | ✅ **Ready** | [`sens_analysis.cpp`](../../nodalys/src/analyses/sens/sens_analysis.cpp) — Transposed adjoint solve $\mathbf{J}^T \boldsymbol{\lambda} = \mathbf{d}$, analytical device derivatives, 100% normalized scale, and subcircuit/potentiometer aggregation. |
| **AC Sensitivity** | `.sens ... ac` | ❌ (wrong with forward-biased devices) | ✅ **Ready** | [`ac_sens_analysis.cpp`](../../nodalys/src/analyses/ac_sens/ac_sens_analysis.cpp) — Transposed complex adjoint solve per frequency, the bias term of the nonlinear devices through the DC adjoint, and the change of gain and phase for 1 % of each part, read from `.sens <out> ac dec|oct|lin <np> <fstart> <fstop>` by the netlist parser and the CLI ([`ac_sens_analysis.md`](./ac_sens_analysis.md)). |
| **Harmonic Balance** | `.hb` | ❌ | ✅ **Ready** | [`hb_analysis.cpp`](../../nodalys/src/analyses/hb/hb_analysis.cpp) — Exclusive AFT Newton-Raphson solver with pure C++23 radix-2 FFT, Toeplitz/circulant conversion Jacobian, and source/gmin stepping continuation. |
| **Truth Table** | Multiple `.op` | ✅ | ✅ **Ready** | Fully supported natively in imcsim UI without ngspice via smooth continuous logic macromodels ($C^\infty$). |

---

### 2. Device Modeling & Component Support

| Category | Components / Models | `ngspice` | `Nodalys` | Notes |
| :--- | :--- | :---: | :---: | :--- |
| **Passives** | Resistor, Capacitor, Inductor | ✅ | ✅ **Ready** | Exact companion models; Norton charge integration with initial conditions (`uic`). |
| **Transmission Lines** | Lossless & Dispersive Lossy (`TL` / `T`) | Lossless `T` / `LTRA` (no skin/$\tan\delta$ HB) | ✅ **Ready** | Telegrapher model with $\sqrt{f}$ skin effect and $\tan\delta$ dielectric loss in DC/AC/SP/Noise/AC-Sens/HB, plus Bergeron transient for lossless lines ([`transmission_line.md`](./transmission_line.md)). |
| **Sources** | DC/AC Voltage & Current, Clock, Pulse, Sine, EXP, PWL, SFFM, AM | ✅ | ✅ **Ready** | Waveform generators; the transient lands on the corners of each. |
| **Noise & Random Sources** | `TRNOISE`, `TRRANDOM` | ✅ | ✅ **Ready** | Held at their DC level outside a transient; in a transient, white, $1/f$ and telegraph noise and the uniform, Gaussian, exponential and Poisson draws from a `std::mt19937_64` seeded by the run seed. The draws are written out in [`random_draws.cpp`](../../nodalys/src/devices/sources/random_draws.cpp) (top 53 bits for the uniform number, Box-Muller, inversion, Knuth), not taken from the standard distributions, so a seed gives the same run on Linux, Windows and macOS up to the last digit of `log` and `cos`. The values differ from those of `ngspice`. |
| **Behavioral Sources** | `B` element | ✅ | ✅ **Ready** | An expression of node voltages, currents and time with exact derivatives for Newton, the operators and functions of `ngspice`; what `ngspice` reads and `Nodalys` does not (`limit`, the random functions, `hertz`) is refused by name before the run ([`behavioral_source.md`](./behavioral_source.md)). |
| **Fuses** | `imcsim_fuse` subcircuit | ✅ | ✅ **Ready** | Native device with a thermal state and a latch, with the blow time of `ngspice` within 3 % ([`fuse_device.md`](./fuse_device.md)). |
| **Controlled Sources** | VCVS, VCCS, CCCS, CCVS | ✅ | ✅ **Ready** | Stamped into linear MNA system; zero-voltage current probes handled natively. |
| **Semiconductors** | Diode, Zener, LED, BJT (NPN/PNP), MOSFET (NMOS/PMOS) | ✅ | ✅ **Ready** | Shockley diode, Gummel-Poon BJT, Shichman-Hodges Level-1 MOSFET with charge depletion/diffusion. |
| **Macromodels** | Ideal Op-Amp, Boyle Op-Amp (LTC AN48) | ✅ | ✅ **Ready** | Real datasheet-level macromodel with CMRR, slew rate, finite gain-bandwidth, and output clamp. |
| **Continuous Logic ($C^\infty$)** | NOT, BUFFER, AND, NAND, OR, NOR, XOR, XNOR, Schmitt Trigger | ✅ (`digital.cm`) | ✅ **Ready** | Hyperbolic tangent smooth macromodels with inertial delay RC filtering and Thevenin output stages. |
| **Sequential Logic** | D Flip-Flop, SR Latch, SR Flip-Flop, T Flip-Flop, JK Flip-Flop | ✅ (`digital.cm`) | ✅ **Ready** | Cross-coupled multi-stage implementations with Newton-Raphson voltage change damping; the D flip-flop is the classic 7474 six-gate form, whose active-high asynchronous set and reset (`CircuitBuilder::AddDFlipFlop()`) override the clock. |
| **Co-Simulation Bridge** | Verilog discrete-to-analog converter | ❌ | ✅ **Ready** | Converts discrete HDL levels (`0`, `1`, `X`, `Z`) to continuous analog ramps with slew limiting. |

---

### 3. Cross-Cutting Modes & CLI Tooling

| Feature / Tool | Status in `Nodalys` | Description |
| :--- | :---: | :--- |
| **Standalone CLI (`nodalys`)** | ✅ **Ready** | Command-line tool accepting standard SPICE netlists from arguments or standard input (`stdin`); `--analysis sens` runs both the DC and the AC `.sens` of a netlist ([`ac_sens_analysis.md`](./ac_sens_analysis.md)). |
| **Multi-Format Export** | ✅ **Ready** | Standalone exporter formatting results into CSV, JSON, SPICE Raw, and Touchstone (.sNp) formats (`--format`), the AC sensitivity included. |
| **Component Parameter Stepping** | ✅ **Ready** | Steps $R, C, L$ across `.tran`, `.ac`, and `.dc` runs directly through `imcsim` controls. |
| **Fourier / Spectrum Analysis** | ✅ **Ready** | High-resolution FFT post-processed over `ComputeTransient` waveforms. |
| **Live Simulation Mode** | ✅ **Ready** | Live transient paced against the clock (`simulation/live_pace.h`, shared with ngspice), paused and resumed, with changes applied between chunks. |
| **Temperature Sweep ($T$)** | ✅ **Ready** | `Circuit::SetTemperature()` applies the SPICE temperature laws of the diode, BJT and MOSFET relative to the nominal $T_{\text{nom}} = 27\,^\circ\text{C}$, and `temper` in an expression reads it. |
| **Initial Conditions (`.ic`, `.nodeset`)** | ✅ **Ready** | Element-level `uic` on capacitors ($V_0$) and inductors ($I_0$), node `.ic` (held in the operating point, or the start of the capacitors on the node with `uic`; see [`transient_analysis.md`](./transient_analysis.md)) and `.nodeset`, a first Newton pass pulled to the guesses and then released ([`dc_analysis.md`](./dc_analysis.md), 1.7). |
| **Gear-2 / BDF-2 Integration** | ✅ **Ready** | Variable-step Gear-2 beside the trapezoidal rule and Backward Euler, picked by `TransientConfig::Method` or the integration method of the solver options, with 3rd divided-difference LTE step control ([`transient_analysis.md`](./transient_analysis.md), 1.2). |
| **Vendor SPICE `.subckt` Parsing** | ✅ **Ready** | `AddSubcircuitText()` reads a subcircuit kept as text, and `ParseNetlist()` and the CLI place the `X` lines of a netlist through it, with its parameters, `{expressions}`, `.model` cards and nested subcircuits; what it cannot simulate is refused with the line ([`subcircuit_text.md`](./subcircuit_text.md)). |

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

`Nodalys` is the default simulation backend of `imcsim`. Having reached feature parity on the analyses and part models used across the shipped examples, the phases below record what has landed alongside the remaining performance and capability work.

#### Done

| Item | SPICE Command | Notes |
| :--- | :---: | :--- |
| **Default Backend in `imcsim`** | — | `Nodalys` is the default simulation engine in `imcsim` (`Edit > Preferences > Simulation`). |
| **Pole-Zero Analysis** | `.pz` | QZ algorithm over $(\mathbf{G}, -\mathbf{C})$ and the Rosenbrock pencil. |
| **DC Sensitivity** | `.sens` | One transposed adjoint solve $\mathbf{J}^T \boldsymbol{\lambda} = \mathbf{d}$ for every part. |
| **AC Sensitivity** | `.sens ... ac` | One transposed complex solve per frequency, plus the bias term of the nonlinear devices; the Sensitivity analysis of `imcsim` has an AC kind for it. |
| **RF S-Parameters** | `.sp` | One or more ports, power waves, Z and Y matrices, Touchstone export, Smith chart in `imcsim`. |
| **2-Port RF Stability Metrics** | `.sp` | Rollett $K$, Edwards-Sinsky $\mu$/$\mu'$, MAG/MSG, unilateral gain, and Smith-chart stability circles in `imcsim`. |
| **Harmonic Balance (1-Tone)** | `.hb` | AFT Newton with source and gmin stepping; refused above 4000 dense unknowns. |
| **Live Transient** | — | Paced against the clock and paused through the pace `imcsim` shares with `ngspice`. |
| **Gear-2 / BDF-2 Integration** | `.options method=gear` | Variable-step BDF-2 beside Trapezoidal and Backward Euler, used automatically on circuits with a fuse when Integration is Default ([`transient_analysis.md`](./transient_analysis.md)). |
| **Behavioral Sources** | `B` element | Free expressions of node voltages, currents, and time with forward-mode automatic differentiation ([`behavioral_source.md`](./behavioral_source.md)). |
| **Temperature** | `.temp`, `tnom` | SPICE temperature laws of the diode, BJT, and MOSFET relative to `tnom`, plus `temper` in expressions. |
| **Noise & Random Sources in Transient** | `TRNOISE`, `TRRANDOM` | White, $1/f$, and telegraph noise plus uniform, Gaussian, exponential, and Poisson draws from a seeded generator ([`random_draws.cpp`](../../nodalys/src/devices/sources/random_draws.cpp)). |
| **SPICE-Text Subcircuits** | `.subckt` / `X` | Vendor `.subckt` models kept as SPICE text, with parameters, `{expressions}`, `.model` cards, and nested blocks ([`subcircuit_text.md`](./subcircuit_text.md)). |
| **Node Initial Conditions** | `.ic`, `.nodeset` | Node `.ic` clamped in transient startup OP ([`transient_analysis.md`](./transient_analysis.md)) and `.nodeset` two-pass pull/release ([`dc_analysis.md`](./dc_analysis.md), 1.7). |
| **Parallel Monte Carlo, Parameter Steps & Truth Tables** | — | Independent runs dispatched across worker threads via `RunBatch` ([`parallel_runs.h`](../../app-core/simulation/parallel_runs.h)) when `SupportsConcurrentRuns` is true. |
| **Transmission Lines & Differential Ports** | `T`, `P` | Differential `CircuitPort` (`NegativeNode`), lossless Bergeron `.tran`, and dispersive skin-effect / $\tan\delta$ telegrapher lines in `.ac`, `.sp`, `.noise`, `.sens ... ac`, and `.hb` ([`transmission_line.md`](./transmission_line.md)). |

#### Phase 1 — Remaining Performance Parity

| Item | Why | Today |
| :--- | :--- | :--- |
| **Sparse MNA Solver** (KLU-style, AMD ordering, symbolic reuse) | The dense LU makes the analyses users run most the slowest on larger circuits: the transient runs at 0.07x to 0.26x of `ngspice` and the AC sweep of a 100-node network at 0.34x (section 4), and it caps harmonic balance at 4000 unknowns. | Dense LU |

**Done when** the transient and AC benchmarks are no slower than `ngspice`.

#### Phase 2 — Cheap Wins for the Users of `imcsim`

| Item | Why |
| :--- | :--- |
| **Loop Gain / Stability** (`.stb`, Tian) | Phase and gain margin of a feedback loop without breaking it, over the existing AC solver; it fits the op-amp and regulator circuits `imcsim` is used for. |
| **Worst-Case (EVA) and RSS Tolerance** | The DC sensitivities times the tolerances `imcsim` already keeps: instant bounds beside Monte Carlo. |
| **2-Port RF Noise Figure** (`NF`, `Fmin`, `Rn`, `Sopt`) | Extends the existing S-parameter and two-port stability views with small-signal RF noise figures (`donoise`). |

#### Phase 3 — Extending Harmonic Balance and Sensitivity

| Item | Why |
| :--- | :--- |
| **Power Sweep / P1dB / PAE / Large-Signal $S_{ij}$** | An amplitude loop over `ComputeHarmonicBalance`. |
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
| **2-Port RF Stability & Noise (`NF`, `Fmin`, `Rn`, `Sopt`)** | RF / Small-Signal | `.sp` supports `CKTspnoise`, but lacks $K$/$\mu$/circles | $K$, $\mu$, MAG/MSG & stability circles **Ready**; RF noise (`donoise`) planned | Low | Rollett $K$, $\mu$, $\mu'$, MAG/MSG, and Smith-chart stability circles are live in `imcsim`; multiport RF noise figures remain planned. |
| **Modal / Eigenvalue & Participation Factors** | Linear Dynamics | SISO `.pz` only | SISO `.pz` only | Low / Medium | Computes all natural poles of $(\mathbf{G}, \mathbf{C})$ via QZ and ranks $L$/$C$ participation per mode to pinpoint resonances without choosing an I/O pair. |
| **AC Sensitivity** | Sensitivity | Has `.sens ac`, wrong with forward-biased devices | ✅ **Ready** | — | Adjoint solves over the complex AC MNA plus the bias term of the nonlinear devices ([`ac_sens_analysis.md`](./ac_sens_analysis.md)). |
| **HB & Transient Adjoint Sensitivity** | Sensitivity | Legacy `.sens2` transient unmaintained/off | DC and AC `.sens` only | Medium | Periodic HB Jacobian and time-domain companion matrices. |
| **HB-AC / HB-Noise / HB-XF (`PAC`/`PNoise`/`PXF`)** | Periodic Small-Signal | Upstream has `.pss` shooting only (no HB conversion) | Planned | Medium / High | Linearizes around the converged HB periodic solution via the harmonic conversion matrix for mixers, choppers, and SMPS. |
| **Automatic Transient Noise (`.trannoise`)** | Time / Noise | Manual `TRNOISE`/`TRRANDOM` voltage/current sources only | Planned | Medium / High | Automatically injects bandwidth-scaled thermal, shot, and $1/f$ noise per device during `.tran` for SNR and oscillator/comparator jitter. |
| **Autonomous HB (Oscillators + Phase Noise)** | RF / Oscillators | `.pss` shooting only (needs stabilization time) | Planned *(fixed `f0`)* | High | Solves unknown oscillation frequency $f_0$ jointly in HB Newton with a phase constraint; suited to high-$Q$ crystal and LC oscillators. |
| **Multi-Tone HB (IMD2 / IMD3 / IIP3 / Mixers)** | RF / Nonlinear | Not available (`.disto` is 3rd-order small-signal only) | Planned *(single-tone)* | High | Multidimensional spectral basis ($k_1 f_1 + k_2 f_2$) for non-commensurate LO+RF tones and large-signal intermodulation. |
| **Circuit Envelope** | RF / Modulated | Not available | Planned | High | Integrates slow baseband modulation envelopes in time while solving the RF carrier spectrum via HB at each step. |
