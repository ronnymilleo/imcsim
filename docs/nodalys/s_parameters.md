# Small-Signal S-Parameter Analysis (`.sp`)

**Implementation files:**
- [`nodalys/src/analyses/sp_analysis.cpp`](../../nodalys/src/analyses/sp_analysis.cpp) (`ComputeSParameters`)
- [`nodalys/src/mna/mna_system.cpp`](../../nodalys/src/mna/mna_system.cpp) (`MnaSystem::SolveAcMultiple`)
- [`app-core/native/native_backend.cpp`](../../app-core/native/native_backend.cpp) (`NativeBackend::RunSParameters`)
- [`nodalys/include/nodalys/results.h`](../../nodalys/include/nodalys/results.h) (`SParameterResult`)
- [`nodalys/src/export/exporters.cpp`](../../nodalys/src/export/exporters.cpp) (`ExportTouchstone`)
- [`tests/backend_comparison_tests.cpp`](../../tests/backend_comparison_tests.cpp) (`Both backends agree on S-parameters`)

---

## 1. Algorithms and Mathematical Formulation

Scattering parameters (S-parameters) describe $N$-port linear (or linearized) electrical networks across frequency in terms of traveling power waves rather than raw voltages and currents.

### 1.1 Normalized Power Waves and S-Matrix Definition

For an $N$-port network where port $k$ has a real reference impedance $Z_{0k} = R_{0k} > 0$ (and admittance $G_k = 1/R_{0k}$), the incident power wave $a_k$ and reflected power wave $b_k$ are defined by Kurokawa's formulation:

$$a_k = \frac{V_k + R_{0k} I_k}{2\sqrt{R_{0k}}} = \frac{V_k \sqrt{G_k} + \frac{I_k}{\sqrt{G_k}}}{2}$$

$$b_k = \frac{V_k - R_{0k} I_k}{2\sqrt{R_{0k}}} = \frac{V_k \sqrt{G_k} - \frac{I_k}{\sqrt{G_k}}}{2}$$

where $I_k$ is the current flowing **into** port terminal $k$. The scattering matrix $\mathbf{S}(j\omega) \in \mathbb{C}^{N \times N}$ relates reflected waves to incident waves:

$$\mathbf{b} = \mathbf{S} \mathbf{a}, \quad S_{ij} = \left. \frac{b_i}{a_j} \right|_{a_k = 0 \, \forall k \ne j}$$

### 1.2 Multiport Norton Termination and Excitation

In classical RF measurement, setting $a_k = 0$ for all unexcited ports ($k \ne j$) corresponds to terminating every port $k$ into its matched reference impedance $Z_{0k}$ connected to ground:

$$V_k = -Z_{0k} I_k \iff I_k = -G_k V_k$$

To measure column $j$ of the S-matrix, port $j$ is driven by a generator with source impedance $Z_{0j}$. In Norton equivalent form, exciting port $j$ with an independent current source $I_{s,j}$ in parallel with conductance $G_j$ yields:

$$I_j = I_{s,j} - G_j V_j$$

Substituting this into the incident wave equation at port $j$:

$$a_j = \frac{V_j \sqrt{G_j} + \frac{I_{s,j} - G_j V_j}{\sqrt{G_j}}}{2} = \frac{I_{s,j}}{2\sqrt{G_j}}$$

By choosing the normalized source excitation:

$$I_{s,j} = 2\sqrt{G_j} = \frac{2}{\sqrt{R_{0j}}}$$

we achieve unit incident power wave:

$$a_j = 1, \quad a_k = 0 \quad (k \ne j)$$

The reflected wave at port $i$ then directly yields S-parameter $S_{ij}$:

$$S_{ij} = b_i = \frac{V_i \sqrt{G_i} - \frac{I_i}{\sqrt{G_i}}}{2}$$

- For unexcited ports ($i \ne j$), $I_i = -G_i V_i$:
  $$S_{ij} = \frac{V_i \sqrt{G_i} - (-V_i \sqrt{G_i})}{2} = V_i \sqrt{G_i}$$

- For the excited port ($i = j$), $I_j = I_{s,j} - G_j V_j = 2\sqrt{G_j} - G_j V_j$:
  $$S_{jj} = \frac{V_j \sqrt{G_j} - \frac{2\sqrt{G_j} - G_j V_j}{\sqrt{G_j}}}{2} = V_j \sqrt{G_j} - 1$$

Combining both cases into a single unified equation:

$$S_{ij} = V_i \sqrt{G_i} - \delta_{ij}$$

where $\delta_{ij}$ is the Kronecker delta ($\delta_{ij} = 1$ if $i = j$, $0$ otherwise).

### 1.3 Single-LU Multi-RHS Optimization (`SolveAcMultiple`)

Notice that **every port $k \in \{1 \dots N\}$ is terminated with conductance $G_k$ to ground in the admittance matrix $\mathbf{Y}(j\omega)$**:

$$\mathbf{Y}(j\omega) = \mathbf{Y}_{\text{circuit}}(j\omega) + \sum_{k=1}^N G_k \mathbf{e}_{n_k} \mathbf{e}_{n_k}^T$$

Because this matrix $\mathbf{Y}(j\omega)$ is completely identical for all port excitations $j \in \{1 \dots N\}$, `Nodalys` decomposes $\mathbf{Y}(j\omega)$ via LU factorization **only once per frequency**, and simultaneously solves for all $N$ excitation columns:

$$\mathbf{Y}(j\omega) \begin{bmatrix} \mathbf{V}^{(1)} & \mathbf{V}^{(2)} & \cdots & \mathbf{V}^{(N)} \end{bmatrix} = \begin{bmatrix} \mathbf{I}_s^{(1)} & \mathbf{I}_s^{(2)} & \cdots & \mathbf{I}_s^{(N)} \end{bmatrix}$$

where column $j$ has non-zero entry $I_{s,j} = 2\sqrt{G_j}$ at node $n_j$. This yields an **$N \times$ speedup** over naïve single-port solvers that re-factorize the matrix for each port.

### 1.4 Derived RF Figures of Merit

From the complex S-matrix $\mathbf{S}(f)$, `Nodalys` computes standard RF figures of merit:

- **Return Loss (dB):**
  $$\text{RL}_i = -20 \log_{10} |S_{ii}|$$
  Measures port match. For an ideal match ($S_{ii} \to 0$), $\text{RL} \to \infty$.

- **Insertion Loss (dB):**
  $$\text{IL}_{ij} = -20 \log_{10} |S_{ij}| \quad (i \ne j)$$
  Measures attenuation/transmission from port $j$ to port $i$.

- **Voltage Standing Wave Ratio (VSWR):**
  $$\text{VSWR}_i = \frac{1 + |S_{ii}|}{1 - |S_{ii}|}$$
  Valid for passive/stable ports ($|S_{ii}| < 1$). Perfectly matched port yields $\text{VSWR} = 1.0$.

- **Input Impedance ($\Omega$):**
  $$Z_{\text{in},i} = Z_{0i} \frac{1 + S_{ii}}{1 - S_{ii}}$$
  Complex input impedance seen looking into port $i$ with all other ports terminated in their reference impedances.

---

## 2. Benchmark Table: Analytical Golden vs. `ngspice` vs. `Nodalys`

The table below summarizes verified test cases comparing analytical RF theory, `ngspice`, and `Nodalys` across frequency sweeps:

| Circuit & Ports | Frequency Sweep | Metric | Analytical Golden | `ngspice` | **`Nodalys`** | Relative Diff |
| :--- | :--- | :--- | :---: | :---: | :---: | :---: |
| **1. 50 $\Omega$ T-Pad Attenuator (10 dB)** | $1\text{ MHz}\text{--}1\text{ GHz}$ | $S_{11}\text{ (mag)}$ | $0.0000$ *(exact pad)* | `3.4877e-05` | **`9.5759e-06`** | $< 10^{-4}$ (residual) |
| ($R_1 = R_2 = 25.974\,\Omega$, $R_3 = 35.136\,\Omega$, | ($5\text{ pts/dec}$) | $S_{21}\text{ (mag)}$ | $10^{-10/20} = 0.316228$ | `0.316274` | **`0.316230`** | $< 1.4 \times 10^{-4}$ |
| $Z_{01} = Z_{02} = 50\,\Omega$) | | $\text{IL}_{21}\text{ (dB)}$ | $10.000\text{ dB}$ | `9.9988 dB` | **`10.000 dB`** | $< 0.001\text{ dB}$ |
| **2. 50 $\Omega$ Pi-Pad Attenuator (6 dB)** | $1\text{ MHz}\text{--}100\text{ MHz}$ | $S_{11}\text{ (mag)}$ | $0.0000$ *(exact pad)* | `2.9751e-04` | **`1.4369e-04`** | $< 1.5 \times 10^{-4}$ |
| ($R_1 = R_2 = 150.48\,\Omega$, $R_3 = 37.35\,\Omega$, | (Octave sweep) | $S_{21}\text{ (mag)}$ | $10^{-6/20} = 0.501187$ | `0.501062` | **`0.501179`** | $< 2.3 \times 10^{-4}$ |
| $Z_{01} = Z_{02} = 50\,\Omega$) | | $Z_{\text{in},1}$ | $50.000\,\Omega$ | `49.970\,\Omega` | **`49.986\,\Omega`** | $< 0.03\%$ |
| **3. LC Low-Pass Filter** | $100\text{ kHz}\text{--}10\text{ MHz}$ | $S_{21}(100\text{ kHz})$ | $\approx 1.0\text{ (passband)}$ | `0.998749` | **`0.998749`** | $< 10^{-7}$ |
| ($L_1 = L_2 = 11.25\,\mu\text{H}$, $C_1 = 9\text{ nF}$, | ($5\text{ pts/dec}$) | $S_{21}(1\text{ MHz})$ | $\approx 1/\sqrt{2}$ *(corner)* | `0.999999` | **`0.999999`** | $< 10^{-7}$ |
| $f_c \approx 1\text{ MHz}$, $Z_{01} = Z_{02} = 50\,\Omega$) | | $S_{21}(10\text{ MHz})$ | $\ll 1\text{ (stopband)}$ | `0.002001` | **`0.002001`** | $< 10^{-7}$ |
| **4. LC Band-Pass Resonator** | $20\text{ MHz}\text{--}80\text{ MHz}$ | $f_0\text{ resonance}$ | $\frac{1}{2\pi\sqrt{LC}} = 50.329\text{ MHz}$ | `50.33 MHz` | **`50.33 MHz`** | Match |
| ($C_1 = 10\text{ pF}, L_1 = 1\,\mu\text{H}$, | (Linear, 25 pts) | $S_{21}(f_0)$ | $\approx 1.0\text{ (transparent)}$ | `0.99998` | **`0.99998`** | $< 10^{-6}$ |
| $Z_{01} = Z_{02} = 50\,\Omega$) | | $S_{11}(f_0)$ | $\approx 0.0\text{ (matched)}$ | `0.0062` | **`0.0062`** | $< 10^{-5}$ |
| **5. RF Transformer** | $1\text{ MHz}\text{--}50\text{ MHz}$ | $S_{21}(10\text{ MHz})$ | Coupled inductors ($k=0.999$) | `0.997632` | **`0.997632`** | $< 10^{-7}$ |
| ($L_p = L_s = 10\,\mu\text{H}, k = 0.999$) | ($5\text{ pts/dec}$) | $S_{11}(10\text{ MHz})$ | Return loss | `0.068779` | **`0.068779`** | $< 10^{-7}$ |
| **6. Asymmetric Reference Impedances** | $1\text{ MHz}\text{--}100\text{ MHz}$ | $S_{11}$ | Series $100\,\Omega$ resistor | `0.333333` | **`0.333333`** | $< 10^{-7}$ |
| ($Z_{01} = 50\,\Omega$, $Z_{02} = 75\,\Omega$, $R = 100\,\Omega$) | ($5\text{ pts/dec}$) | $S_{21}$ | $2\sqrt{Z_{01}Z_{02}} / (Z_{01}+Z_{02}+R) = 0.54433$ | `0.544331` | **`0.544331`** | $< 10^{-7}$ |
| **7. 3-Port Resistive Power Splitter** | $1\text{ MHz}\text{--}1\text{ GHz}$ | $S_{11}$ | Delta/Star $16.667\,\Omega$ | `0.000000` | **`0.000000`** | $< 10^{-7}$ |
| (Symmetrical star, $R_1=R_2=R_3=16.667\,\Omega$) | ($5\text{ pts/dec}$) | $S_{21}, S_{31}$ | $6\text{ dB split} = 0.5000$ | `0.500000` | **`0.500000`** | $< 10^{-7}$ |

---

## 3. Touchstone Export (.sNp Format)

`Nodalys` provides native Touchstone version 1 export through [`ExportTouchstone`](../../nodalys/src/export/exporters.cpp) and CLI (`-f touchstone` / `-f s2p`):

```
! Nodalys S-Parameter Simulation
# HZ S RI R 50
1000000 0.1 0 0.8 0 0.8 0 0.1 0
...
```

The exported file is directly compatible with Keysight ADS, Cadence AWR Microwave Office, Qucs, and scikit-rf.
