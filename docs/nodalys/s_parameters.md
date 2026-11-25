# Small-Signal S-Parameter Analysis (`.sp`)

**Implementation files:**
- [`nodalys/src/analyses/sp/sp_analysis.cpp`](../../nodalys/src/analyses/sp/sp_analysis.cpp) (`ComputeSParameters`)
- [`nodalys/src/analyses/sp/port_matrices.cpp`](../../nodalys/src/analyses/sp/port_matrices.cpp) (`ComputeImpedanceMatrix`, `ComputeAdmittanceMatrix`)
- [`nodalys/src/mna/mna_system.cpp`](../../nodalys/src/mna/mna_system.cpp) (`MnaSystem::SolveAcMultiple`)
- [`app-core/backends/nodalys/nodalys_backend.cpp`](../../app-core/backends/nodalys/nodalys_backend.cpp) (`NodalysBackend::RunSParameters`)
- [`nodalys/include/nodalys/results.h`](../../nodalys/include/nodalys/results.h) (`SParameterResult`)
- [`nodalys/src/export/exporters.cpp`](../../nodalys/src/export/exporters.cpp) (`ExportTouchstone`)
- [`nodalys/tests/analyses/sp_analysis_tests.cpp`](../../nodalys/tests/analyses/sp_analysis_tests.cpp) (one port, Z and Y, sweeps, errors, exports)
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

### 1.5 Impedance and Admittance Matrices

With real reference impedances, the power-wave definitions of section 1.1 give $V = D (I + S) a$ and $I = D^{-1} (I - S) a$ with $D = \operatorname{diag}(\sqrt{Z_{0k}})$, so for every frequency `Nodalys` also returns

$$\mathbf{Z} = D (I + \mathbf{S}) (I - \mathbf{S})^{-1} D, \qquad \mathbf{Y} = D^{-1} (I - \mathbf{S}) (I + \mathbf{S})^{-1} D^{-1}$$

in `SParameterResult::ImpedanceMatrices` and `AdmittanceMatrices`, one `std::optional` matrix per frequency, row by row like `Values`. A matrix is **absent** (`std::nullopt`, never a huge number) at a frequency where the circuit has no finite one:

- $\mathbf{Z}$ does not exist when $I - \mathbf{S}$ is singular, as for two ports joined by a series element and nothing else (the ports float, so a current into one cannot set a voltage), or for an open one-port.
- $\mathbf{Y}$ does not exist when $I + \mathbf{S}$ is singular, as for a port shorted to ground.

A matrix is taken as singular when a pivot of its Gauss-Jordan inversion is below $10^{-9}$ of the larger of 1 and its biggest entry (`SINGULAR_PIVOT_RATIO` in `port_matrices.cpp`). That is an impedance above about $10^{9} Z_0$, an open circuit for practical purposes; the engine's own $10^{-12}$ S leak from every node to ground keeps a true open from being an exact infinity, and shows in $\mathbf{Y}$ at the picosiemens. `GetImpedance()` and `GetAdmittance()` of the imcsim result, and `Impedance()` and `Admittance()` of the engine's, read one entry and give no value for an absent matrix, or for a result built without them.

### 1.6 Ports and Sweeps

- **Ground-referenced and differential ports:** `CircuitPort` carries both `Node` ($n_k^+$) and `NegativeNode` ($n_k^-$, defaulting to ground `0`), exposed through `CircuitBuilder::AddPort(number, node, reference_ohms, negative_node)` and netlist `P<name> n+ n- z0=50` (or `V`/`I` sources with `portnum`). The Norton termination $G_k$ and unit-wave source $I_{s,j} = 2\sqrt{G_j}$ are stamped differentially between $n_k^+$ and $n_k^-$, and $S_{ij} = (V_{n_i^+} - V_{n_i^-})\sqrt{G_i} - \delta_{ij}$. Schematic pin markers in `imcsim` remain ground-referenced (`NegativeNode = 0`).
- **One port** gives $S_{11}$ directly, and its $Z$ and $Y$ are the impedance and admittance of the load against the port's return node. A circuit with no port is refused.
- Ports may be listed in any order in the netlist or the circuit; the matrix follows their numbers. The numbers must run from 1 to $N$ with no gap and no repeat: the matrix, the Touchstone file and the Output tab index the ports by number, so a gap would leave a row with no port.
- A linear sweep of 2 points gives both frequencies (start and stop), and of 1 point only the start.
- Each wrong setting is reported in words, with the same text as the checks that imcsim runs before any engine (`tests/s_parameters_tests.cpp` keeps the two in step):

| Cause | Message |
| :--- | :--- |
| No port | `The circuit has no S-parameter port` |
| Reference impedance of zero or less | `The reference impedance of port 1 must be positive` |
| Port on ground | `Port 1 is on ground; an S-parameter port needs a node of its own` |
| Two ports on one node | `Ports 1 and 2 are on the same node; each port needs a node of its own` |
| Number repeated | `Two S-parameter ports are numbered 1; give each port its own number` |
| Gap in the numbers | `The S-parameter ports must be numbered 1 to 2 with no gap; port 2 is missing` |
| Start of zero or less | `The start frequency must be positive` |
| Stop below start | `The stop frequency must be higher than the start frequency` |

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

### 2.2 Execution Performance ([`nodalys_benchmark_tests.cpp`](../../tests/nodalys_benchmark_tests.cpp))

| Circuit | Non-Ground Nodes | `ngspice` | **`Nodalys`** | Speedup |
| :--- | :---: | ---: | ---: | :---: |
| **3-Port Splitter (18 pts)** | 4 | 0.186 ms | **0.014 ms** | **13.21x** |
| **2-Port LC Resonator (25 pts)** | 2 | 0.154 ms | **0.011 ms** | **14.47x** |

---

## 3. Touchstone Export (.sNp Format)

`Nodalys` provides native Touchstone version 1 export through [`ExportTouchstone`](../../nodalys/src/export/exporters.cpp) and CLI (`-f touchstone` / `-f s2p`):

```
! Nodalys S-Parameter Simulation
# HZ S RI R 50
1000000 0.1 0 0.8 0 0.8 0 0.1 0
...
```

A single port is written as `.s1p`: one pair of numbers after each frequency. The exported file is directly compatible with Keysight ADS, Cadence AWR Microwave Office, Qucs, and scikit-rf.

The CSV (`-f csv`) and JSON (`-f json`) exports carry the impedance and admittance matrices after the S matrix: CSV columns `Z11_re,Z11_im,...,Y11_re,Y11_im,...` (empty where the matrix is absent) and the JSON arrays `impedance` and `admittance`, one entry per frequency, `null` where absent.
