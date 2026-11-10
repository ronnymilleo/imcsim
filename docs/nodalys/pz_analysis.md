# Small-Signal Pole-Zero Analysis (`.pz`)

**Implementation files:**
- [`nodalys/src/linear/qz.h`](../../nodalys/src/linear/qz.h) / [`qz.cpp`](../../nodalys/src/linear/qz.cpp) (`ComputeGeneralizedEigenvalues`)
- [`nodalys/src/analyses/pz_analysis.cpp`](../../nodalys/src/analyses/pz_analysis.cpp) (`ComputePoleZero`)
- [`app-core/nodalys/nodalys_backend.cpp`](../../app-core/nodalys/nodalys_backend.cpp) (`NodalysBackend::RunPoleZero`)
- [`tests/backend_comparison_tests.cpp`](../../tests/backend_comparison_tests.cpp) (`Both backends agree on small-signal pole-zero analysis`)

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
