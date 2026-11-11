# Small-Signal DC Sensitivity Analysis (`.sens`)

**Implementation files:**
- [`nodalys/src/analyses/sens_analysis.cpp`](../../nodalys/src/analyses/sens_analysis.cpp) (`ComputeSensitivity`)
- [`nodalys/src/mna/mna_system.cpp`](../../nodalys/src/mna/mna_system.cpp) (`MnaSystem::SolveTranspose`)
- [`app-core/nodalys/compiler.cpp`](../../app-core/nodalys/compiler.cpp) (`CompileCircuit`)
- [`app-core/nodalys/nodalys_backend.cpp`](../../app-core/nodalys/nodalys_backend.cpp) (`NodalysBackend::RunSensitivity`)
- [`nodalys/tests/sens_analysis_tests.cpp`](../../nodalys/tests/sens_analysis_tests.cpp)
- [`tests/backend_comparison_tests.cpp`](../../tests/backend_comparison_tests.cpp) (`Both backends agree on small-signal DC sensitivity analysis`)
- [`tests/nodalys_benchmark_tests.cpp`](../../tests/nodalys_benchmark_tests.cpp) (`Nodalys outperforms ngspice on DC sensitivity via single-solve adjoint method`)

---

## 1. Algorithms and Mathematical Formulation

Small-signal DC sensitivity analysis (`.SENS V(out)` or `.SENS V(pos, neg)`) computes the first-order derivative $\frac{\partial V_{\text{out}}}{\partial p_i}$ of a chosen output voltage with respect to every element value $p_i$ around the converged DC operating point, together with the 100% normalized influence $p_i \frac{\partial V_{\text{out}}}{\partial p_i}$.

### 1.1 Adjoint Network Method ($\mathbf{J}^T \boldsymbol{\lambda} = \mathbf{d}$)

Let the nonlinear MNA system at the DC operating point be:
$$\mathbf{f}(\mathbf{x}, p) = \mathbf{A}(p)\,\mathbf{x} - \mathbf{b}(p) = \mathbf{0}$$
For an output differential voltage $V_{\text{out}} = \mathbf{d}^T \mathbf{x}$ (where $\mathbf{d}$ selects $+1$ at `OutputPositiveNode` and $-1$ at `OutputNegativeNode`), differentiating $\mathbf{f}(\mathbf{x}, p) = \mathbf{0}$ with respect to any scalar parameter $p$ gives:
$$\mathbf{J}\,\frac{d\mathbf{x}}{dp} = -\frac{\partial \mathbf{f}}{\partial p}, \qquad \mathbf{J} = \frac{\partial \mathbf{f}}{\partial \mathbf{x}}$$

Instead of solving a linear system for every element parameter $p_i$ in the circuit, `Nodalys` solves **a single transposed linear system** (the adjoint network) once:
$$\mathbf{J}^T \boldsymbol{\lambda} = \mathbf{d}$$
via [`MnaSystem::SolveTranspose`](../../nodalys/src/mna/mna_system.cpp) (using row equilibration, partial-pivoting LU factorization, and iterative refinement). Every element sensitivity is then evaluated in $\mathcal{O}(1)$ work per device:
$$\frac{\partial V_{\text{out}}}{\partial p_i} = \mathbf{d}^T \frac{d\mathbf{x}}{dp_i} = \boldsymbol{\lambda}^T \mathbf{J}\,\frac{d\mathbf{x}}{dp_i} = -\boldsymbol{\lambda}^T \frac{\partial \mathbf{f}}{\partial p_i}$$

### 1.2 Analytical Device Derivatives

Given nodal voltages $\mathbf{x}_{\text{DC}}$ and adjoint variables $\boldsymbol{\lambda}$:

| Device Class | Parameter $p$ | Analytical Sensitivity $\frac{\partial V_{\text{out}}}{\partial p}$ | Normalized 100% Scale | Output Vector Key |
| :--- | :---: | :---: | :---: | :--- |
| `ResistorDevice` | $R$ | $\dfrac{(v_a - v_b)(\lambda_a - \lambda_b)}{R^2}$ | $R \cdot \dfrac{\partial V_{\text{out}}}{\partial R}$ | `r1` / `r.x1.r1` |
| `VoltageSourceDevice` | $V_{\text{dc}}$ | $\lambda_{\text{branch}}$ | $V_{\text{dc}} \cdot \lambda_{\text{branch}}$ | `v1` / `v.x1.v1` |
| `CurrentSourceDevice` | $I_{\text{dc}}$ | $\lambda_b - \lambda_a$ | $I_{\text{dc}} \cdot (\lambda_b - \lambda_a)$ | `i1` / `i.x1.i1` |
| `VcvsDevice` ($E$) | Gain $A_v$ | $\lambda_{\text{branch}} \cdot (v_{c+} - v_{c-})$ | $A_v \cdot \dfrac{\partial V_{\text{out}}}{\partial A_v}$ | `e1_gain` |
| `VccsDevice` ($G$) | Gain $G_m$ | $(\lambda_b - \lambda_a) \cdot (v_{c+} - v_{c-})$ | $G_m \cdot \dfrac{\partial V_{\text{out}}}{\partial G_m}$ | `g1_gain` |
| `CccsDevice` ($F$) | Gain $A_i$ | $(\lambda_b - \lambda_a) \cdot I_{\text{ctrl}}$ | $A_i \cdot \dfrac{\partial V_{\text{out}}}{\partial A_i}$ | `f1_gain` |
| `CcvsDevice` ($H$) | Gain $R_m$ | $\lambda_{\text{branch}} \cdot I_{\text{ctrl}}$ | $R_m \cdot \dfrac{\partial V_{\text{out}}}{\partial R_m}$ | `h1_gain` |
| `CapacitorDevice` / `InductorDevice` | $C$ / $L$ | $0$ (open / short at DC) | $0$ | `c1` / `l1` |

Because the DC Jacobian $\mathbf{J}$ includes the exact linearized conductances of all nonlinear semiconductors (`DiodeDevice`, `BjtDevice`, `MosfetDevice`, switches, and op-amp macromodels), component sensitivities in nonlinear circuits are computed analytically at the converged operating point.

### 1.3 Hierarchical Subcircuits and Potentiometers
- `CompileCircuit` threads hierarchical instance prefixes (`X1.R1`, `X1.X2.R7`), producing Nutmeg-compatible keys (`r.x1.r1`, `r.x1.r1_scale`).
- Potentiometers register both track halves (`R-RV1a`, `R-RV1b`) in `ElementsByName`, allowing `Core::BuildSensitivity` to sum the normalized sensitivities of both halves into the single potentiometer influence.

---

## 2. Benchmark Tables: Analytical Golden vs. `ngspice` vs. `Nodalys`

### 2.1 Sensitivity Accuracy (`backend_comparison_tests.cpp`)

| Circuit & Topology | Part | Analytical Golden (Abs) | `ngspice` | **`Nodalys`** | Relative Diff (`Nodalys` vs. Golden) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **1. Voltage Divider** | `V1` | $\frac{2000}{3000} = 0.666666667$ | `6.666666666e-01` | **`6.666666662e-01`** | $6.7 \times 10^{-10}$ (`Gmin`) |
| ($V_1 = 10\text{ V}, R_1 = 1\text{ k}\Omega, R_2 = 2\text{ k}\Omega, C_1 = 1\text{ }\mu\text{F}$) | `R1` | $-\frac{10\cdot 2000}{3000^2} = -2.222222222\times 10^{-3}$ | `-2.222220000e-03` | **`-2.222222224e-03`** | **`8.0e-10`** *(~1,000× closer than `ngspice`)* |
| | `R2` | $+\frac{10\cdot 1000}{3000^2} = +1.111111111\times 10^{-3}$ | `1.111110000e-03` | **`1.111111110e-03`** | **`9.0e-10`** *(~1,000× closer than `ngspice`)* |
| | `C1` | `0` | `0.000000000e+00` | **`0.000000000e+00`** | Exact |
| **2. Potentiometer Divider** | `V1` | `0.600000000` | `6.000000000e-01` | **`5.999999986e-01`** | $2.3 \times 10^{-9}$ (`Gmin`) |
| ($V_1 = 12\text{ V}, R_{V1} = 10\text{ k}\Omega, \alpha = 0.4$) | `RV1` | `0` | `3.122502257e-14` | **`-1.727999965e-12`** | $\Delta = -1.73\times 10^{-12}$ (`Gmin`) |
| **3. Subcircuit Block** | `V1` | `0.500000000` | `5.000000000e-01` | **`4.999999988e-01`** | $2.4 \times 10^{-9}$ (`Gmin`) |
| ($V_1 = 15\text{ V}, R_{\text{top}} = 1\text{ k}\Omega, X_1:\{R_1=2\text{ k}, R_2=3\text{ k}\}$) | `Rtop` | $-1.250000000\times 10^{-3}$ | `-1.249998750e-03` | **`-1.250000004e-03`** | **`3.2e-09`** *(vs `1.0e-06` in `ngspice`)* |
| | `X1` (`R2`) | $+1.250000000\times 10^{-3}$ | `1.249998750e-03` | **`1.249999995e-03`** | **`4.0e-09`** *(vs `1.0e-06` in `ngspice`)* |
| **4. Controlled Sources (VCVS/VCCS)** | `E1` | `2.500000000` | `2.500000000e+00` | **`2.499999999e+00`** | $< 10^{-9}$ |
| ($V_1 = 5\text{ V}, E_1 = 2.5, G_1 = 1\text{ mS}, R_L = 500\,\Omega$) | `G1` | `6.250000000e+03` | `6.249999999e+03` | **`6.249999997e+03`** | $< 10^{-9}$ |
| | `RL` | $1.250000000\times 10^{-2}$ | `1.249998750e-02` | **`1.249999999e-02`** | **`8.0e-11`** *(vs `1.0e-06` in `ngspice`)* |

> [!NOTE]
> **Why is `Nodalys` ~1,000× more accurate on resistor sensitivities (`8.0e-10` vs. `1.0e-06`)?**
> `ngspice` exhibits a systematic $\sim 10^{-6}$ relative offset on resistor derivatives due to finite-difference parameter perturbation. `Nodalys` evaluates the analytical derivative $\frac{(v_a - v_b)(\lambda_a - \lambda_b)}{R^2}$ directly from the adjoint solution without numerical differentiation noise, remaining limited only by the diagonal `Gmin = 1e-12 S` term.

---

### 2.2 Execution Performance (`nodalys_benchmark_tests.cpp`)

| Circuit | Nodes | `ngspice` | **`Nodalys`** | Speedup |
| :--- | ---: | ---: | ---: | :---: |
| **BJT Cascaded Amp (4 stages)** | 14 | 1.367 ms | **0.030 ms** | **44.84x** |
| **RC Ladder (100 nodes, 200 resistors)** | 101 | 10.334 ms | **0.430 ms** | **24.03x** |
| **RC Ladder (500 nodes, 1,000 resistors)** | 501 | 139.776 ms | **7.662 ms** | **18.24x** |
| **Diode Ladder (30 diodes, nonlinear)** | 31 | 6.359 ms | **1.013 ms** | **6.28x** |

- Because the adjoint approach requires **only one linear transpose solve** regardless of how many resistors, capacitors, or sources exist in the network, running `.sens` on the 500-node ladder adds only ~2.7 ms over the DC operating point (`4.95 ms` → `7.66 ms`), whereas `ngspice` scales with the number of parameters (`6.17 ms` → `139.78 ms`).
- **Known scope boundaries:** `Nodalys` does not yet export sensitivities for internal semiconductor model parameters (e.g., `d1_area`, BJT `bf`) or AC frequency-domain `.sens`, neither of which is surfaced in `Core::BuildSensitivity`.
