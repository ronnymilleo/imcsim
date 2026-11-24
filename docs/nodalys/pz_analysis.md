# Small-Signal Pole-Zero Analysis (`.pz`)

**Implementation files:**
- [`nodalys/src/linear/eigen/qz.h`](../../nodalys/src/linear/eigen/qz.h) / [`qz.cpp`](../../nodalys/src/linear/eigen/qz.cpp) (`ComputeGeneralizedEigenvalues`)
- [`nodalys/src/analyses/pz/pz_analysis.cpp`](../../nodalys/src/analyses/pz/pz_analysis.cpp) (`ComputePoleZero`)
- [`app-core/backends/nodalys/nodalys_backend.cpp`](../../app-core/backends/nodalys/nodalys_backend.cpp) (`NodalysBackend::RunPoleZero`)
- [`tests/backend_comparison_tests.cpp`](../../tests/backend_comparison_tests.cpp) (`Both backends agree on small-signal pole-zero analysis`)
- [`tests/nodalys_benchmark_tests.cpp`](../../tests/nodalys_benchmark_tests.cpp) (`Twin-T Notch Filter`)

---

## 1. Algorithms and Mathematical Formulation

Pole-Zero analysis computes the natural poles (eigenmodes) and transmission zeros of a linear or linearized small-signal electronic circuit around its DC operating point.

### 1.1 Small-Signal Descriptor MNA System
In Modified Nodal Analysis (MNA), the linearized small-signal state equation in the time domain is:
$$\mathbf{G} \mathbf{x}(t) + \mathbf{C} \frac{d\mathbf{x}(t)}{dt} = \mathbf{B} u(t)$$
$$y(t) = \mathbf{L}^T \mathbf{x}(t)$$

Taking the Laplace transform ($s = \sigma + j\omega$):
$$(\mathbf{G} + s\mathbf{C}) \mathbf{x}(s) = \mathbf{B} u(s)$$
$$H(s) = \frac{y(s)}{u(s)} = \mathbf{L}^T (\mathbf{G} + s\mathbf{C})^{-1} \mathbf{B}$$

- $\mathbf{G} \in \mathbb{R}^{N \times N}$ is the conductance/Jacobian matrix assembled at the DC operating point ($\mathbf{x}_{\text{DC}}$).
- $\mathbf{C} \in \mathbb{R}^{N \times N}$ is the dynamic reactive matrix stamped via `StampAc`:
  - Capacitors stamp $+C$ in node positions $(n_1, n_1)$ and $(n_2, n_2)$, and $-C$ in $(n_1, n_2)$ and $(n_2, n_1)$.
  - Inductors stamp $-L$ in the branch equation diagonal $(b_L, b_L)$.
- $\mathbf{B} \in \mathbb{R}^N$ is the input excitation incidence vector:
  - For independent voltage sources: $+1$ at the source branch equation row.
  - For independent current sources: $-1$ at the positive node, $+1$ at the negative node.
- $\mathbf{L} \in \mathbb{R}^N$ is the output observation incidence vector: $+1$ at the output node $n_{\text{out}}$.

---

### 1.2 Poles Computation via Generalized Eigenvalue Problem
The system poles are the complex frequencies $s$ where $\det(\mathbf{G} + s\mathbf{C}) = 0$.
Rewriting as a Generalized Eigenvalue Problem (GEP):
$$\mathbf{G} \mathbf{v} = \lambda (-\mathbf{C}) \mathbf{v}, \quad \text{with } \lambda = s$$

Because MNA systems contain algebraic constraint equations (e.g. KCL on resistive nodes, voltage source branch definitions), $\mathbf{C}$ is rank-deficient. The algebraic equations produce infinite generalized eigenvalues ($\lambda = \infty$). The actual physical system poles are the **finite generalized eigenvalues** $s = \sigma + j\omega$ (in rad/s).

---

### 1.3 Zeros Computation via Rosenbrock System Matrix Pencil
The transmission zeros are the complex frequencies $s$ where the transfer function $H(s) = 0$. Using the Rosenbrock system matrix pencil:
$$\det \begin{bmatrix} \mathbf{G} + s\mathbf{C} & -\mathbf{B} \\ \mathbf{L}^T & 0 \end{bmatrix} = 0$$

Partitioning into constant and dynamic parts:
$$\mathbf{A}_z = \begin{bmatrix} \mathbf{G} & -\mathbf{B} \\ \mathbf{L}^T & 0 \end{bmatrix}, \qquad \mathbf{B}_z = \begin{bmatrix} \mathbf{C} & \mathbf{0} \\ \mathbf{0}^T & 0 \end{bmatrix}$$
$$\det(\mathbf{A}_z - s(-\mathbf{B}_z)) = 0$$

The transmission zeros are the **finite generalized eigenvalues** of the matrix pair $(\mathbf{A}_z, -\mathbf{B}_z) \in \mathbb{R}^{(N+1) \times (N+1)}$.

---

### 1.4 Standalone QZ Algorithm (Moler-Stewart Generalized Schur Form)
`Nodalys` implements a complete, self-contained QZ decomposition in C++23 (`nodalys/src/linear/eigen/qz.cpp`) without any external linear algebra libraries (no LAPACK, BLAS, or Eigen):

1. **Hessenberg-Triangular Reduction (`ReduceToHessenbergTriangular`):**
   - Uses Householder reflections to reduce $\mathbf{B}$ to upper triangular form while transforming $\mathbf{A}$.
   - Uses alternating Householder reflections and Givens plane rotations from the left and right to reduce $\mathbf{A}$ to upper Hessenberg form while preserving the upper triangular structure of $\mathbf{B}$.
2. **QZ Iteration (`IterateQz`):**
   - Applies implicit Francis double-shift and single-shift iterations to drive subdiagonal elements to zero.
   - Monitors convergence and decouples deflated blocks ($1 \times 1$ and $2 \times 2$).
3. **Finite Eigenvalue Extraction:**
   - From each diagonal block $(\mathbf{A}_{ii}, \mathbf{B}_{ii})$, calculates $\alpha = \alpha_r + j\alpha_i$ and $\beta = \mathbf{B}_{ii}$.
   - Deflates infinite roots where $|\beta| < 10^{-12} \|\mathbf{B}\|$.
   - Yields finite generalized eigenvalues $\lambda = \frac{\alpha}{\beta}$ in rad/s.

---

### 1.4.1 Conditioning Before QZ (`pencil_reduction.cpp`)
An MNA pencil mixes siemens with farads and henries, and its roots span 12 decades. Three steps keep QZ accurate:
1. **Frequency scaling:** $\mathbf{B}$ is multiplied by a power of two so that $\mathbf{A}$ and $\mathbf{B}$ have the same largest entry; the roots are multiplied back afterwards.
2. **Balancing:** rows and columns are scaled by powers of two (exact) until each has $|\mathbf{A}| + |\mathbf{B}|$ of about 1. On a BJT amplifier a pole at -5.7 rad/s came out at -5.74 without it and -5.75498 after, with the AC response off by 0.0175 dB instead of 7.5e-8 dB.
3. **Algebraic rows:** a row (or column) whose $\mathbf{B}$ entries are *exactly* zero is eliminated with the largest $\mathbf{A}$ pivot in it. Differences that cancel to rounding noise are set to exactly 0 where they are made, so a 1 pF capacitor beside 10 uF is never mistaken for noise (the old test, 1e-12 of the largest $\mathbf{B}$ entry, dropped real fast roots, and its 1x1 case compared against an absolute 1, which lost the right-half-plane zero gm/Cgd = 2e9 rad/s of a common-source stage).

A root counts as infinite when its $\beta$ is under 1e-13 of the largest $\mathbf{B}$ entry of the scaled pencil. 
### 1.4.1.1 Refinement of Each Root (`root_refinement.cpp`)
QZ finds every eigenvalue to about a rounding of the *largest* one, times a condition factor. Beside a junction pole at 3e12 rad/s that leaves a root near 1e4 rad/s good to a few parts in a thousand at best, and the way it falls depends on the rounding of the platform: on the transformer supply of the examples, a macOS arm64 build put the zero next to the pole at -15005.77 at -14867.8, and perturbing the entries of the pencil by 4 units in the last place reproduced it on Linux (-14867.75, -15114.44 or -15005.746 depending on the seed, a warning of 0.08 dB). So each root is refined on the deflated pencil, in complex arithmetic, by shifted inverse iteration: solve $(\mathbf{A} - \mu\mathbf{B})\mathbf{z} = \mathbf{B}\mathbf{x}$, read $\lambda = \mu + \mathbf{x}^H\mathbf{x} / \mathbf{x}^H\mathbf{z}$ (since $(\mathbf{A} - \mu\mathbf{B})^{-1}\mathbf{B}\mathbf{x} = \mathbf{x}/(\lambda - \mu)$ for an eigenvector), and shift to it, up to 40 times. Inverse iteration is used because it needs only $\mathbf{A} - \mu\mathbf{B}$ to be regular, which it is for any shift that is not a root, so a singular $\mathbf{B}$ (infinite eigenvalues left in the pencil) does no harm; a Newton step on $\det$ or a Rayleigh quotient with $\mathbf{B}^{-1}$ would not survive it.

A refined root replaces the QZ one only if it converged, its relative residual $\|(\mathbf{A} - \lambda\mathbf{B})\mathbf{x}\| / (\|\mathbf{x}\|(\|\mathbf{A}\| + |\lambda|\|\mathbf{B}\|))$ is under $10^3 n$ roundings and under that of the QZ root, and it moved by no more than its own size or $10^7$ roundings of the largest root. Two roots that were apart and now coincide are both put back to the QZ values, so refinement never merges two roots. Real roots stay real and conjugate pairs stay conjugate. A refined root whose eigenvector leaves $\mathbf{A}$ singular to 50 $n$ roundings is exactly 0, which is how a zero at the origin is told from a small root: by a residual, not by the size of the largest root. Only a root that could not be refined (a pencil of more than 150 states, or no convergence) falls back to the old rule, 0 under 10 roundings of the largest root.

### 1.4.2 Checks on the Result (`pz_checks.cpp`)
Both solve $(\mathbf{G} + j\omega\mathbf{C})\mathbf{x} = \mathbf{B}$ at 3 points per decade from a decade below the lowest root (not below 1e-3 rad/s) to a decade above the highest:
- **Undriven output:** if $|y / u_{port}|$ is under 1e-9 everywhere, the run fails with "The output does not respond to the input" (a diode that is off, a separate network). Gmin leaks 1e-12 through it, which would otherwise yield a list of meaningless roots.
- **Rebuild check:** the gain $K$ in $H = K \prod(s - z) / \prod(s - p)$ read at each point must be the same real number within 1e-3. If not, `PoleZeroResult::Warnings` says by how many dB and degrees and around which frequency, so a short or wrong list is never silent. It would see a missing root up to four decades above the highest found.

### 1.5 Post-Processing & Robustness
The raw poles and zeros are post-processed through `Core::BuildPoleZero`:
- **Conjugate Pairing:** Roots with non-zero imaginary parts are paired into complex conjugate pairs $s = \sigma \pm j\omega$.
- **Natural Frequency and Q-Factor:**
  $$\omega_0 = \sqrt{\sigma^2 + \omega^2}, \qquad f_0 = \frac{\omega_0}{2\pi}, \qquad Q = \frac{\omega_0}{2 |\sigma|}, \qquad \zeta = \frac{1}{2Q}$$
- **Pole-Zero Cancellation:** Any pole and zero within $1\%$ of each other are identified as cancelling pairs.

### 1.6 Stability Comparison: Nodalys vs. ngspice
In `ngspice`, `.pz` relies on a heuristic numerical root-finding algorithm (Muller's method on determinant evaluations) which frequently fails to converge, diverges, or misses high-frequency poles on feedback op-amp circuits (see [`tests/pole_zero_robustness_tests.cpp`](../../tests/pole_zero_robustness_tests.cpp) and jay #182).
In contrast, **Nodalys** solves the generalized eigenvalue problem globally via the QZ algorithm on the exact linearized MNA pencil. It requires no initial frequency guesses, exhibits no divergence, and finds all theoretical poles and zeros deterministically.

---

### 1.7 What Was Verified (`nodalys/tests/analyses/pz/pz_analysis_tests.cpp`, `tests/pole_zero_robustness_tests.cpp`)
The oracle (`pole_zero_oracle.h`) rebuilds $H(j\omega)$ from the roots and compares it with the AC sweep; it holds for any circuit.

| Circuit | Result | Worst AC mismatch |
| :--- | :--- | :--- |
| Sallen-Key, ideal op-amp, GBW 1e4, 3e5, 5e5, 1e6, 2e6 Hz | 3 poles, equal to the roots of the nodal cubic to 1e-4 (ngspice: nothing at 1e4, 5e5, 1e6) | 1e-12 dB |
| Transformer power supply | 7 poles, none in the right half-plane (ngspice: a ladder of 11 poles, most in the right half-plane) | 5e-5 dB |
| 20-section RC ladder | 20 poles equal to $-(2/RC)(1-\cos((2k-1)\pi/(2N+1)))$ to 1e-6 | 7e-13 dB |
| 5 buffered RC sections, 1 Hz to 100 MHz | all 5 at $-1/RC$ to 1e-5 | 3e-14 dB |
| LC tank, integrator, repeated pole | on the axis, at -Gmin/C = -1e-6, double at -1000 | 1e-14 dB |
| Common-source stage | zero at gm/Cgd = 2e9 and the two poles of its quadratic, to 1e-6 | 1e-13 dB |
| Negative conductance, each controlled source | pole at +2000; 4 poles at -1000 | 1e-14 dB |
| BJT amplifier (5 poles, 4 zeros, 12 decades), 2N3904 example | complete, same under reordering | 1.5e-5 dB |
| Every example with a Vin source | match, or refused as undriven (full bridge rectifier, buck converter) | under 1e-3 dB |

### 1.8 Limits That Remain
- A root exactly at the origin shows at about -Gmin/C (1e-6 rad/s for 1 uF), since every node has Gmin to ground. A zero at the origin comes out exactly 0.
- A pole and a zero almost on top of each other (a mode the input barely excites) are ill-conditioned: a BJT amplifier shows them 1e-3 apart relative, within 1.5e-5 dB of the AC response. Core cancels pairs within 1e-3.
- A root above about 1e13 times the scale $\|\mathbf{A}\| / \|\mathbf{B}\|$ counts as infinite.
- Roots are accurate to their own size after refinement, but not beyond the conditioning of the circuit: entries of the stiff transistor pencil of `linear_tests.cpp` moved by up to 32 units in the last place keep every root within 1e-6, and the examples and stress circuits pass the AC check under the same perturbation. Moved by 256 units (which no platform difference produces) the check starts to warn on some circuits, as it should.
- A pencil of more than 150 states keeps the QZ roots, which are good to the rounding of the largest root only.
- The rebuild check is as accurate as the roots near the origin allow, so it starts at 1e-3 rad/s.
- A repeated root splits by the square root of the rounding (1e-8 relative); refinement leaves a root alone when it would land on another one.

## 2. Benchmark Tables: Analytical Golden vs. `ngspice` vs. `Nodalys`

### 2.1 Root Accuracy (`backend_comparison_tests.cpp`)

| Circuit & Topology | Root | Golden (Analytical) | `ngspice` ($\sigma \pm j\omega$, $f_0$) | **`Nodalys`** ($\sigma \pm j\omega$, $f_0$) | Relative Diff (`Nodalys` vs. `ngspice`) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **1. First-Order RC Low-Pass** | Pole | $s = -\frac{1}{RC} = -1000\text{ rad/s}$ | `-1000.000000` | **`-1000.000001`** | $1.0 \times 10^{-9}$ (`Gmin`) |
| ($R_1 = 1\text{ k}\Omega, C_1 = 1\text{ }\mu\text{F}$) | $f_0$ | `159.155 Hz` | `159.155 Hz` | **`159.155 Hz`** | $< 10^{-8}$ |
| **2. Series RLC Bandpass** | Pole pair | $-5000 \pm j\,31224.990\text{ rad/s}$ | `-5000.000000 ± j 31224.989992` | **`-5000.000004 ± j 31224.989990`** | $< 10^{-9}$ |
| ($L_1 = 10\text{ mH}, C_1 = 100\text{ nF}, R_1 = 100\,\Omega$) | $f_0$ | $\frac{1}{2\pi\sqrt{LC}} = 5032.921\text{ Hz}$ | `5032.921 Hz` | **`5032.921 Hz`** | $< 10^{-8}$ |
| | Zero | $0\text{ rad/s}$ | `0.000000` | **`0.000000`** | Exact |
| **3. Sallen-Key Active LPF** | Pole pair | $-5000 \pm j\,5000\text{ rad/s}$ ($Q = \frac{1}{\sqrt{2}}$) | `-5000.000000 ± j 5000.000000` | **`-5000.000075 ± j 5000.000075`** | $1.5 \times 10^{-8}$ |
| ($R_1=R_2=10\text{ k}\Omega, C_1=20\text{ nF}, C_2=10\text{ nF}, E_1=1$) | $f_0$ | $\frac{1}{2\pi\sqrt{R_1R_2C_1C_2}} = 1125.395\text{ Hz}$ | `1125.395 Hz` | **`1125.395 Hz`** | $< 10^{-7}$ |
| **4. Twin-T Notch Filter** | Pole 1 | Real left-half-plane pole | `-3083.527133` | **`-3083.527194`** | $2.0 \times 10^{-8}$ |
| ($R_1=R_2=10\text{ k}\Omega, R_3=5\text{ k}\Omega,$ | Pole 2 | Real left-half-plane pole | `-38916.472867` | **`-38916.473106`** | $6.1 \times 10^{-9}$ |
| $C_1=C_2=10\text{ nF}, C_3=20\text{ nF}, R_L=100\text{ k}\Omega$) | Zero pair | $\pm j\frac{1}{RC} = \pm j\,10000\text{ rad/s}$ (`1591.549 Hz`) | `0.000000 ± j 10000.000000` | **`0.000000 ± j 10000.000000`** | $< 10^{-10}$ |

> [!NOTE]
> **Why does the 1st-order RC pole differ by $1.0 \times 10^{-9}$ (`-1000.000001` vs `-1000.000000`)?**
> `Nodalys` includes the diagonal safety conductance `Gmin = 1e-12 S` in $\mathbf{G}$ across node 2, shifting the pole by $-G_{\min}/C_1 = -10^{-12}/10^{-6} = -10^{-6}\text{ rad/s}$ (`-1000.000001 rad/s`).

---

### 2.2 Execution Performance

| Circuit | Non-Ground Nodes | `ngspice` | **`Nodalys`** | Speedup |
| :--- | :---: | ---: | ---: | :---: |
| **First-Order RC Low-Pass** | 2 | 0.074 ms | **0.002 ms** | **38.52x** |
| **Series RLC Bandpass** | 3 | 0.178 ms | **0.005 ms** | **32.64x** |
| **Sallen-Key Active LPF** | 4 | 0.103 ms | **0.005 ms** | **19.31x** |
| **Twin-T Notch Filter** ([`nodalys_benchmark_tests.cpp`](../../tests/nodalys_benchmark_tests.cpp)) | 4 | 0.171 ms | **0.008 ms** | **22.19x** |
