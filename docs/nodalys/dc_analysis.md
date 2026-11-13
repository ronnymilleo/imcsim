# DC Analyses in Nodalys: Operating Point (`.op`) and DC Sweep (`.dc`)

**Implementation files:**
- [`nodalys/src/analyses/dc_analysis.cpp`](../../nodalys/src/analyses/dc_analysis.cpp) (`ComputeOperatingPoint`, `ComputeDCSweep`)
- [`nodalys/src/newton/newton.cpp`](../../nodalys/src/newton/newton.cpp) (`SolveNewton`)
- [`nodalys/src/mna/mna_system.cpp`](../../nodalys/src/mna/mna_system.cpp) (`MnaSystem::Solve`, `MnaSystem::CheckResidual`)
- [`nodalys/src/linear/dense_matrix.cpp`](../../nodalys/src/linear/dense_matrix.cpp) (`BasicDenseMatrix<double>`)

---

## 1. Algorithms and Mathematical Formulation

### 1.1 DC Modified Nodal Analysis (MNA)
At DC steady state ($\omega = 0$, $d/dt = 0$):
- **Capacitors** act as open circuits ($I_C = 0$).
- **Inductors** act as short circuits ($V_L = 0$), stamped as zero-voltage branches (or small series parasitic resistances).
- The circuit is governed by the nonlinear algebraic MNA system:
  $$\mathbf{F}(\mathbf{x}) = \mathbf{A}_{\text{lin}}\,\mathbf{x} + \mathbf{f}_{\text{nl}}(\mathbf{x}) - \mathbf{b}_{\text{DC}} = \mathbf{0}$$
  where the unknown vector $\mathbf{x} \in \mathbb{R}^N$ partitions into **node voltages** $v_1, \dots, v_n$ (enforcing Kirchhoff's Current Law — KCL) and **branch currents** $i_{b_1}, \dots, i_{b_m}$ for voltage sources, inductors, and transformers (enforcing branch constitutive equations).

### 1.2 Global Minimum Conductance (`Gmin = 1e-12 S`)
In [`MnaSystem::Clear`](../../nodalys/src/mna/mna_system.cpp), every row corresponding to a node-voltage unknown receives a minimum diagonal conductance to ground:
$$G_{\min} = 10^{-12}\text{ S} \quad (\text{equivalent to a } 1\text{ T}\Omega \text{ bleed resistor to ground})$$
- **Why keep global `Gmin`?** It guarantees nonsingularity whenever a circuit contains nodes coupled strictly through capacitors (e.g., AC-coupled stages), open switches, or reverse-biased semiconductor junctions.

### 1.3 Dense Linear Solver (`MnaSystem::Solve`)
At each Newton-Raphson iteration, the linearized system $\mathbf{J}(\mathbf{x}^{(k)})\,\mathbf{x}^{(k+1)} = \mathbf{r}(\mathbf{x}^{(k)})$ is solved in three stages:
1. **Row Equilibration Scaling:** each row $i$ is divided by $\max_j |J_{ij}|$, balancing rows that mix $10^{-12}\text{ S}$ conductances with $\mathcal{O}(1)$ voltage-branch constraints.
2. **Partial-Pivoting $PA = LU$ Factorization:** selects the largest column pivot and rejects singular or near-singular matrices using a size-scaled pivot threshold.
3. **Iterative Refinement (1 step):** computes the residual $\mathbf{d} = \mathbf{b} - \mathbf{J}\hat{\mathbf{x}}$, solves $\mathbf{J}\,\delta\mathbf{x} = \mathbf{d}$ reusing the $L, U$ factors in $\mathcal{O}(N^2)$ time, and updates $\hat{\mathbf{x}} \leftarrow \hat{\mathbf{x}} + \delta\mathbf{x}$ to recover full IEEE-754 double-precision accuracy.

### 1.4 Damped Newton-Raphson and Norton Companion Stamping
Every nonlinear semiconductor branch $I = f(v)$ is linearized around the current iterate $v^{(k)}$ into its **Norton companion model** (`StampNortonBranch`):
$$I(v^{(k+1)}) \approx G_{\text{eq}}\,v^{(k+1)} + I_{\text{eq}}, \qquad G_{\text{eq}} = \left.\frac{\partial f}{\partial v}\right|_{v^{(k)}}, \qquad I_{\text{eq}} = f(v^{(k)}) - G_{\text{eq}}\,v^{(k)}$$
- **PN Junction Limiting (`pnjlim`, Nagel SPICE2):** prevents a single Newton step from jumping dozens of volts across an exponential $I_S(e^{v/N V_T}-1)$ (which would cause floating-point overflow), logarithmically compressing forward steps above the critical voltage $V_{\text{crit}} = N V_T \ln\!\left(\frac{N V_T}{\sqrt{2}\,I_S}\right)$.
- **MOSFET Gate-Voltage Limiting (`fetlim`):** limits large per-iteration $V_{GS}$ excursions across the threshold voltage $V_{\text{TO}}$.
- **Dual Convergence Criterion:** requires both:
  1. Small iterate update: $|x_i^{(k+1)} - x_i^{(k)}| \le \text{RelTol}_{\text{step}}\cdot \max(|x_i^{(k+1)}|, |x_i^{(k)}|) + \text{AbsTol}_i$ (with a $0.1\times$ tighter relative tolerance on node voltages).
  2. Satisfied physical KCL residual (`MnaSystem::CheckResidual`): $|\mathbf{J}\mathbf{x} - \mathbf{b}|_i \le \text{RelTol}\cdot |\mathbf{b}_i| + \text{AbsTol}_i$.

### 1.5 Homotopy Fallbacks (*Gmin Stepping* and *Source Stepping*)
When direct Newton-Raphson fails to converge from $\mathbf{x}^{(0)} = \mathbf{0}$ (common in high-gain feedback loops, op-amp macromodels, and biasing latches):
1. **Stage 1 — Direct Newton** ($G_{\min} = 10^{-12}\text{ S}$, $\text{SourceScale} = 1.0$).
2. **Stage 2 — *Gmin Stepping*:** raises the diagonal conductance to $G_{\min} = 10^{-2}\text{ S}$ (heavily damping all nonlinear junctions) and steps down decade-by-decade ($10^{-2} \to 10^{-3} \to \dots \to 10^{-12}\text{ S}$), warm-starting each decade from the previous solution.
3. **Stage 3 — *Source Stepping*:** scales all independent sources by $\lambda \in [0 \to 1]$ with adaptive step halving whenever an intermediate point fails to converge.

### 1.6 DC Sweep (`.dc`) with Homotopic Continuation
In [`ComputeDCSweep`](../../nodalys/src/analyses/dc_analysis.cpp), only the first point of each sweep curve requires a cold start ([`ComputeOperatingPoint`](../../nodalys/src/analyses/dc_analysis.cpp)). Every subsequent point $k = 1, 2, \dots, M$ seeds `SolveNewton` directly with the converged vector $\mathbf{x}_{k-1}$ from the preceding source value, typically converging in 2–4 iterations.

---

## 2. Benchmark Tables: Analytical Golden vs. `ngspice` vs. `Nodalys`

### 2.1 Nonlinear DC Curve: 1N4148 Shockley Diode ([`diode_shockley_dc.csv`](../../tests/reference/diode_shockley_dc.csv))
- **Circuit:** Swept DC voltage source $V_s \in [0, 5\text{ V}]$ driving $R = 1\text{ k}\Omega$ in series with a 1N4148 diode ($I_S = 2.52\text{ nA}$, $N = 1.752$, $R_S = 0.568\text{ }\Omega$, $V_T = \frac{k_B}{q}\cdot 300.15\text{ K}$).
- **Analytical Golden ([`generate_goldens.py`](../../tests/reference/generate_goldens.py)):** Solves the exact implicit equation $V_s = V_j + (R + R_S) I_D(V_j)$ via scalar Newton-Raphson to machine precision ($10^{-15}\text{ V}$).

| Sweep Point ($V_s$) | Quantity | Golden (Analytical) | `ngspice` | `ngspice` Error | **`Nodalys`** | **`Nodalys` Error** | Closer to Golden |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| **$0.3\text{ V}$** | $V_D\text{ (V)}$ | `0.298186019` | `0.298187225` | `1.21e-06` | **`0.298186019`** | **`1.36e-13`** | **`Nodalys` (~9,000,000× more accurate)** |
| **$0.3\text{ V}$** | $I_D\text{ (A)}$ | `1.813980603e-06` | `1.812775237e-06` | `1.21e-09` | **`1.813980304e-06`** | **`2.98e-13`** | **`Nodalys` (~4,000× more accurate)** |
| **$0.7\text{ V}$** | $V_D\text{ (V)}$ | `0.509199849` | `0.509232026` | `3.22e-05` | **`0.509199849`** | **`1.75e-12`** | **`Nodalys` (~18,000,000× more accurate)** |
| **$0.7\text{ V}$** | $I_D\text{ (A)}$ | `1.908001511e-04` | `1.907679742e-04` | `3.22e-08` | **`1.908001506e-04`** | **`5.01e-13`** | **`Nodalys` (~64,000× more accurate)** |
| **$1.0\text{ V}$** | $V_D\text{ (V)}$ | `0.548389902` | `0.548479614` | `8.97e-05` | **`0.548389903`** | **`1.30e-09`** | **`Nodalys` (~69,000× more accurate)** |
| **$1.0\text{ V}$** | $I_D\text{ (A)}$ | `4.516100984e-04` | `4.515203857e-04` | `8.97e-08` | **`4.516101109e-04`** | **`1.25e-11`** | **`Nodalys` (~7,100× more accurate)** |
| **$2.5\text{ V}$** | $V_D\text{ (V)}$ | `0.613976670` | `0.613993721` | `1.71e-05` | **`0.613976713`** | **`4.33e-08`** | **`Nodalys` (~395× more accurate)** |
| **$5.0\text{ V}$** | $V_D\text{ (V)}$ | `0.653210096` | `0.653228249` | `1.82e-05` | **`0.653210098`** | **`1.56e-09`** | **`Nodalys` (~11,600× more accurate)** |
| **$5.0\text{ V}$** | $I_D\text{ (A)}$ | `4.346789904e-03` | `4.346771751e-03` | `1.82e-08` | **`4.346790053e-03`** | **`1.49e-10`** | **`Nodalys` (~122× more accurate)** |

> [!NOTE]
> **Why does `Nodalys` outperform `ngspice` by 3 to 7 orders of magnitude on the Shockley DC curve?**
> `ngspice` uses SPICE3f5's default `reltol = 1e-3` and terminates Newton-Raphson as soon as the iterate update falls within $0.1\%$, stopping one iteration short of quadratic convergence floor. `Nodalys` enforces a $10\times$ tighter node step tolerance (`0.1 * RelTol`), verifies the true MNA KCL residual (`CheckResidual`), and applies LU iterative refinement.

---

### 2.2 DC Operating Points (`.op`): Linear, Op-Amp, and Transistor Circuits ([`backend_comparison_tests.cpp`](../../tests/backend_comparison_tests.cpp))

| Circuit | Quantity | Golden (Analytical) | `ngspice` | **`Nodalys`** | Difference (`Nodalys` vs. `ngspice`) |
| :--- | :--- | ---: | ---: | ---: | ---: |
| **Resistive Divider** ($12\text{ V}$, $1\text{ k}\Omega / 2.2\text{ k}\Omega$) | `V(2)` (V) | `8.250000000` | `8.250000000` | `8.249999994` | `5.67e-09 V` (`Gmin`) |
| **Resistive Divider** | `I(R1)` (A) | `3.750000000e-03` | `3.750000000e-03` | `3.750000006e-03` | `5.67e-12 A` |
| **Potentiometer** ($10\text{ V}$, $10\text{ k}\Omega$, $\alpha = 0.7$, $R_L = 10\text{ k}\Omega$) | `V(2) = 300/121` (V) | `2.479338843` | `2.479338843` | `2.479338839` | `3.97e-09 V` |
| **Inverting Op-Amp** ($V_{\text{in}} = 0.4\text{ V}$, $G = -10$, $A_{\text{OL}} = 10^5$) | `Vout` (V) | `-3.999559960` | `-3.999559960` | `-3.999560004` | `4.40e-08 V` |
| **Common-Source MOSFET Amp** (2N7000 in triode) | `VD` (V) | `0.119621031` | `0.119621031` | `0.119621043` | `1.20e-08 V` |
| **Common-Source MOSFET Amp** | `ID` (A) | `9.880378969e-03` | `9.880378969e-03` | `9.880378957e-03` | `1.20e-11 A` |
| **Common-Emitter BJT Amp** (2N3904) | `VC` (V) | — *(Gummel-Poon)* | `5.200568678` | `5.200498199` | `7.05e-05 V` (`0.0013%`) |
| **Common-Emitter BJT Amp** (2N3904) | `VB` (V) | — *(Gummel-Poon)* | `1.693658879` | `1.693657591` | `1.29e-06 V` (`0.00008%`) |
| **Common-Emitter BJT Amp** (2N3904) | `VE` (V) | — *(Gummel-Poon)* | `1.028520370` | `1.028535511` | `1.51e-05 V` (`0.0015%`) |

> [!TIP]
> **Origin of the 9th-decimal-place offset (`5.67e-09 V`) in purely resistive circuits:**
> In `ngspice`, `Gmin = 1e-12 S` is placed only across nonlinear semiconductor junctions. In `Nodalys`, `Gmin = 1e-12 S` is stamped on the diagonal of every voltage node for unconditional matrix safety. In the $12\text{ V}$ divider with Thévenin resistance $R_{\text{th}} = 1\text{ k}\Omega \parallel 2.2\text{ k}\Omega = 687.5\text{ }\Omega$, the exact analytical voltage pull-down caused by a $1\text{ T}\Omega$ resistor to ground is:
> $$\Delta V = -V(2)\cdot R_{\text{th}}\cdot G_{\min} = -8.25 \times 687.5 \times 10^{-12} = -5.671875 \times 10^{-9}\text{ V}$$
> which matches the observed `8.249999994 V` to the last digit.

---

### 2.3 Execution Performance ([`nodalys_benchmark_tests.cpp`](../../tests/nodalys_benchmark_tests.cpp))

| Circuit | Analysis | Nodes | `ngspice` | **`Nodalys`** | Speedup |
| :--- | :--- | ---: | ---: | ---: | :---: |
| **RC Ladder (100 nodes)** | DC OP | 101 | 0.910 ms | **0.236 ms** | **3.86x** |
| **RC Ladder (500 nodes)** | DC OP | 501 | 6.172 ms | **4.949 ms** | **1.25x** |
| **BJT Cascaded Amp (4 stages)** | DC Sweep | 14 | 0.335 ms | **0.216 ms** | **1.55x** |
| **Diode Ladder (30 diodes)** | DC OP | 31 | 0.388 ms | 0.976 ms | 0.40x |
| **Full Adder Truth Table (8 rows)** | DC OP ×8 | 8 | N/A | **0.110 ms** | Autonomous |
