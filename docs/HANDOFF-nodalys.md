# Handoff — Native Simulation Backend (`Nodalys`) — 2026-11-09

## Project Context
`imcsim` includes the clean-room native simulation backend **Nodalys** (a contraction of *nodal analysis* —
`nodal` + `-lys` from `analysis`, after the Modified Nodal Analysis at the core of the engine).
It implements all 6 analog analyses of the engine: **Operating Point (`.op`)**, **DC Sweep (`.dc`)**,
**Transient (`.tran`)**, **AC Sweep / Bode (`.ac`)**, **Transfer Function (`.tf`)**, and **Noise Analysis (`.noise`)**.
Event-driven digital models are scheduled for the next step. Users select the backend in the GUI (`"ngspice"` or `"Nodalys"`
in the `SimulationBackend` preference in `preferences.json`, via `PreferencesWindow`).
The CMake target is `nodalys` (producing `libnodalys.a`; source code in `app-core/native/`, namespace
`Core::Native`).

> **Detailed Technical Documentation & Benchmark Tables:** see [`docs/nodalys/README.md`](./nodalys/README.md) and the per-analysis documents in [`docs/nodalys/`](./nodalys/).

### Building and Running the Test Suite
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
XDG_RUNTIME_DIR=/tmp TMPDIR=/tmp cmake --build build -j$(nproc) --target imcsim-tests
./out/debug/bin/imcsim-tests
```
*(Note: `XDG_RUNTIME_DIR=/tmp` ensures Catch2's post-build `catch_discover_tests` step can write its temporary file even inside restricted sandboxes).*

---

## Didactic Engine Architecture (`app-core/native/`)

1. **Unified MNA System ([`mna_system.h`](../app-core/native/mna_system.h), [`mna_system.cpp`](../app-core/native/mna_system.cpp)) and Dense LU Solver ([`dense_matrix.h`](../app-core/native/linear/dense_matrix.h), [`dense_matrix.cpp`](../app-core/native/linear/dense_matrix.cpp))**
   - Assembles both real ($A \cdot x = \text{rhs}$) and complex ($\mathbf{Y}(j\omega)\cdot\mathbf{x} = \mathbf{b}$) linear systems with row equilibration scaling, partial-pivoting LU, and 1 step of iterative refinement.
   - Ground (`0`) maps to index `-1` and is silently ignored by `AddA()` / `AddRhs()`, eliminating boundary branches in device stamps.
   - `CheckResidual()` verifies the true physical KCL residual $|A(x)\cdot x - \text{rhs}(x)|$ after Newton convergence.

2. **Single-Pass Circuit Compiler ([`compiler.h`](../app-core/native/compiler.h), [`compiler.cpp`](../app-core/native/compiler.cpp))**
   - `Builder` traverses the circuit once (`AddCircuit` / `AddEntry`), allocating internal nodes (`AllocateInternalNode()`) and branch currents (`AllocateBranch()`) on demand.
   - The Boyle op-amp macromodel (`AddBoyleOpAmp`) is organized into its 5 physical stages (differential input pair, Miller-compensated gain stage, class-AB output stage, short-circuit current limiter, and positive/negative rail clamps).

3. **Linear and Nonlinear Devices ([`devices.h`](../app-core/native/devices.h), [`devices.cpp`](../app-core/native/devices.cpp), [`nonlinear.h`](../app-core/native/nonlinear.h), [`nonlinear.cpp`](../app-core/native/nonlinear.cpp), [`pn_junction.h`](../app-core/native/pn_junction.h))**
   - **`DiodeDevice`**: Smooth Shockley + reverse breakdown equation with effective knee $\text{BV}_{\text{eff}} = \text{BV} - N V_T \ln(I_{\text{BV}}/I_S)$ for `pnjlim` and $C^1$-continuous forward-bias depletion capacitance.
   - **`BjtDevice`**: Gummel-Poon model aligned with SPICE `bjtload.c` ($q_b = \frac{q_1}{2}(1 + \sqrt{1 + 4 q_2})$), including diffusion charge $Q_{DE} = \tau_F I_{\text{fwd}}/q_b$ and base-width modulation capacitive transadmittance (`geqcb`).
   - **`MosfetDevice`**: Level-1 Shichman-Hodges model with symmetric source/drain reversal (`d_eff`, `s_eff`), explicit body diodes, and overlap/depletion charges.

4. **Modular Analyses (`app-core/native/`)**
   - [`dc_analysis.cpp`](../app-core/native/dc_analysis.cpp): `ComputeOperatingPoint` and `ComputeDCSweep`
   - [`transient_analysis.cpp`](../app-core/native/transient_analysis.cpp): `ComputeTransient`
   - [`ac_analysis.cpp`](../app-core/native/ac_analysis.cpp): `ComputeACSweep`
   - [`tf_analysis.cpp`](../app-core/native/tf_analysis.cpp): `ComputeTransferFunction`
   - [`noise_analysis.cpp`](../app-core/native/noise_analysis.cpp): `ComputeNoise`

---

## Validation & Test Coverage
1. **Layer 1 — Closed-Form Analytical Goldens ([`tests/reference/`](../tests/reference/README.md), [`tests/native_backend_tests.cpp`](../tests/native_backend_tests.cpp))**:
   - Python generator [`tests/reference/generate_goldens.py`](../tests/reference/generate_goldens.py) and CSV reference tables (`rc_step.csv`, `rl_step.csv`, `rlc_underdamped.csv`, `diode_shockley_dc.csv`, `rc_bode.csv`, `rlc_bode.csv`).
2. **Layer 2 — Dual-Backend Parity ([`tests/backend_comparison_tests.cpp`](../tests/backend_comparison_tests.cpp))**:
   - Full parity across `.op`, `.dc`, `.tran`, `.ac`, `.tf`, and `.noise`.
3. **Layer 3 — All Shipped Examples ([`tests/examples_tests.cpp`](../tests/examples_tests.cpp))**:
   - `Every example either runs on Nodalys or is flagged unsupported` validates all 27 shipped schematics.
