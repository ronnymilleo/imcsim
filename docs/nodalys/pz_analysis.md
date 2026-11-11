# Small-Signal Pole-Zero Analysis (`.pz`)

**Implementation files:**
- [`nodalys/src/linear/qz.h`](../../nodalys/src/linear/qz.h) / [`qz.cpp`](../../nodalys/src/linear/qz.cpp) (`ComputeGeneralizedEigenvalues`)
- [`nodalys/src/analyses/pz_analysis.cpp`](../../nodalys/src/analyses/pz_analysis.cpp) (`ComputePoleZero`)
- [`app-core/nodalys/nodalys_backend.cpp`](../../app-core/nodalys/nodalys_backend.cpp) (`NodalysBackend::RunPoleZero`)
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
`Nodalys` implements a complete, self-contained QZ decomposition in C++23 (`nodalys/src/linear/qz.cpp`) without any external linear algebra libraries (no LAPACK, BLAS, or Eigen):

1. **Hessenberg-Triangular Reduction (`QzHessenbergTriangular`):**
   - Uses Householder reflections to reduce $\mathbf{B}$ to upper triangular form while transforming $\mathbf{A}$.
   - Uses alternating Householder reflections and Givens plane rotations from the left and right to reduce $\mathbf{A}$ to upper Hessenberg form while preserving the upper triangular structure of $\mathbf{B}$.
2. **QZ Iteration (`QzStep`):**
   - Applies implicit Francis double-shift and single-shift iterations to drive subdiagonal elements to zero.
   - Monitors convergence and decouples deflated blocks ($1 \times 1$ and $2 \times 2$).
3. **Finite Eigenvalue Extraction:**
   - From each diagonal block $(\mathbf{A}_{ii}, \mathbf{B}_{ii})$, calculates $\alpha = \alpha_r + j\alpha_i$ and $\beta = \mathbf{B}_{ii}$.
   - Deflates infinite roots where $|\beta| < 10^{-12} \|\mathbf{B}\|$.
   - Yields finite generalized eigenvalues $\lambda = \frac{\alpha}{\beta}$ in rad/s.

---

### 1.5 Post-Processing & Robustness
The raw poles and zeros are post-processed through `Core::BuildPoleZero`:
- **Conjugate Pairing:** Roots with non-zero imaginary parts are paired into complex conjugate pairs $s = \sigma \pm j\omega$.
- **Natural Frequency and Q-Factor:**
  $$\omega_0 = \sqrt{\sigma^2 + \omega^2}, \qquad f_0 = \frac{\omega_0}{2\pi}, \qquad Q = \frac{\omega_0}{2 |\sigma|}, \qquad \zeta = \frac{1}{2Q}$$
- **Pole-Zero Cancellation:** Any pole and zero within $1\%$ of each other are identified as cancelling pairs.

### 1.6 Stability Comparison: Nodalys vs. ngspice
In `ngspice`, `.pz` relies on a heuristic numerical root-finding algorithm (Muller's method on determinant evaluations) which frequently fails to converge, diverges, or misses high-frequency poles on feedback op-amp circuits (documented in `AGENTS.md` and jay #182).
In contrast, **Nodalys** solves the generalized eigenvalue problem globally via the QZ algorithm on the exact linearized MNA pencil. It requires no initial frequency guesses, exhibits no divergence, and finds all theoretical poles and zeros deterministically.

---

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
