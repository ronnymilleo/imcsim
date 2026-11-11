# Small-Signal AC Noise Analysis (`.noise`)

**Implementation files:**
- [`app-core/native/noise_analysis.cpp`](../../app-core/native/noise_analysis.cpp) (`ComputeNoise`)
- [`app-core/native/mna_system.cpp`](../../app-core/native/mna_system.cpp) (`MnaSystem::SolveAcTranspose`)
- [`app-core/native/devices.cpp`](../../app-core/native/devices.cpp) & [`app-core/native/nonlinear.cpp`](../../app-core/native/nonlinear.cpp) (`Device::OutputNoiseVariance`)
- [`app-core/native/compiler.cpp`](../../app-core/native/compiler.cpp) (`CompiledCircuit::NoiseGroups`)
- [`tests/backend_comparison_tests.cpp`](../../tests/backend_comparison_tests.cpp) (`Both backends agree on the small-signal noise spectral densities, totals and device contributions`)

---

## 1. Algorithms and Mathematical Formulation

Small-signal noise analysis (`.noise V(out) Vin dec N fstart fstop`) computes:
1. **Output Noise Spectral Density** $e_{n,\text{out}}(f) = \sqrt{S_{v,\text{out}}(f)}$ in $\text{V}/\sqrt{\text{Hz}}$ at output node $n_{\text{out}}$.
2. **Input-Referred Noise Spectral Density** $e_{n,\text{in}}(f) = \sqrt{S_{\text{in}}(f)}$ in $\text{V}/\sqrt{\text{Hz}}$ (voltage input source) or $\text{A}/\sqrt{\text{Hz}}$ (current input source).
3. **Total Integrated RMS Noise** ($V_{\text{out,rms}}$ and $V_{\text{in,rms}}$ or $I_{\text{in,rms}}$) integrated across $[f_{\text{start}}, f_{\text{stop}}]$.
4. **Per-Component Noise Breakdown** (`NoiseContribution`), grouping internal macromodel/subcircuit primitives under each top-level schematic part and sorting descending by total output RMS noise.

### 1.1 Transposed Complex Adjoint Method (`SolveAcTranspose`)
A circuit may contain hundreds of noisy resistors, diodes, and transistors. Solving a forward AC system $\mathbf{Y}(j\omega)\mathbf{x} = \mathbf{b}_m$ for each individual noise source $m$ would require $O(N_{\text{sources}})$ matrix solves per frequency.

Instead, by **Tellegen's theorem** (adjoint network sensitivity), `Nodalys` solves a **single** transposed complex linear system per frequency $\omega_k = 2\pi f_k$:
$$\left(A_{\text{DC}}(\mathbf{x}_{\text{dc}}) + j\omega_k C_{\text{AC}}(\mathbf{x}_{\text{dc}})\right)^T \lambda(\omega_k) = \mathbf{e}_{n_{\text{out}}}$$
where $\mathbf{e}_{n_{\text{out}}}$ is the unit basis vector at output unknown $n_{\text{out}}$. Because $\lambda(\omega_k)^T = \mathbf{e}_{n_{\text{out}}}^T \mathbf{Y}(j\omega_k)^{-1}$, the complex transfer impedance from **any** internal current source between nodes $a$ and $b$ to the output node $n_{\text{out}}$ is simply:
$$Z_{\text{tf}}(a, b; \omega_k) = \lambda_a(\omega_k) - \lambda_b(\omega_k)$$

Simultaneously, the small-signal transfer gain $G_{\text{in}}(j\omega_k) = \tilde{V}_{n_{\text{out}}} / \tilde{S}_{\text{in}}$ from the independent input source $S_{\text{in}}$ to $n_{\text{out}}$ is extracted **directly from the same adjoint vector** $\lambda(\omega_k)$ without a second matrix solve:
$$G_{\text{in}}(j\omega_k) = \begin{cases} \lambda_{b_{\text{in}}}(\omega_k) & \text{if } S_{\text{in}} \text{ is a voltage source on MNA branch } b_{\text{in}} \\ \lambda_{n_-}(\omega_k) - \lambda_{n_+}(\omega_k) & \text{if } S_{\text{in}} \text{ is a current source from } n_+ \text{ to } n_- \end{cases}$$

### 1.2 Physical Device Noise Sources (`OutputNoiseVariance`)
All uncorrelated physical current noise sources are evaluated at the nominal SPICE temperature $T_{\text{nom}} = 300.15\text{ K}$ ($27\,^\circ\text{C}$) using CODATA/ngspice constants ($k_B = 1.380649 \times 10^{-23}\text{ J/K}$, $q = 1.602176634 \times 10^{-19}\text{ C}$):

| Device | Noise Mechanism | Mean-Square Current Spectral Density $S_{i_n}(f)$ ($\text{A}^2/\text{Hz}$) | Output Voltage Variance Contribution ($\text{V}^2/\text{Hz}$) |
| :--- | :--- | :---: | :---: |
| **Resistor** (`ResistorDevice`) | Johnson–Nyquist thermal noise ($G = 1/R$) | $4 k_B T G$ | $4 k_B T G \left|\lambda_a - \lambda_b\right|^2$ |
| **Diode** (`DiodeDevice`) | Parasitic series resistance $R_S$ thermal + PN junction shot noise | $\frac{4 k_B T}{R_S} + 2 q \left|I_D\right|$ | $\frac{4 k_B T}{R_S}\left|\lambda_k - \lambda_{k'}\right|^2 + 2 q \left|I_D\right| \left|\lambda_a - \lambda_{k'}\right|^2$ |
| **BJT** (`BjtDevice`) | Terminal resistances ($R_C, R_B, R_E$) thermal + collector & base shot noise | $\frac{4 k_B T}{R_{C,B,E}}, \quad 2 q \left|I_C\right|, \quad 2 q \left|I_B\right|$ | $\sum_{t \in \{C,B,E\}} \frac{4 k_B T}{R_t}\left|\Delta\lambda_t\right|^2 + 2 q \left|I_C\right| \left|\lambda_{c'} - \lambda_{e'}\right|^2 + 2 q \left|I_B\right| \left|\lambda_{b'} - \lambda_{e'}\right|^2$ |
| **MOSFET** (`MosfetDevice`) | Drain/source resistances ($R_D, R_S$) thermal + channel thermal noise | $\frac{4 k_B T}{R_{D,S}}, \quad \frac{8}{3} k_B T \left|g_m\right|$ | $\sum_{t \in \{D,S\}} \frac{4 k_B T}{R_t}\left|\Delta\lambda_t\right|^2 + \frac{8}{3} k_B T \left|g_m\right| \left|\lambda_{d'} - \lambda_{s'}\right|^2$ |

*(Note: The internal $100\text{ M}\Omega$ gain resistor `R-U1` of the ideal op-amp macromodel is marked `noisy = false` (`noisy=0` in SPICE) so ideal op-amps do not inject artificial resistor noise).*

### 1.3 SPICE3f5 Log-Log Power-Law Integration (`IntegrateNoiseStep`)
Over each decade frequency interval $[f_{k-1}, f_k]$, the power spectral density $S(f)$ is modeled as a local power law $S(f) = S_{k-1} (f / f_{k-1})^\alpha$ with exponent:
$$\alpha = \frac{\ln(S_k / S_{k-1})}{\ln(f_k / f_{k-1})}$$
which integrates white noise ($\alpha = 0$), $1/f$ noise ($\alpha = -1$), and single-pole RC roll-off ($\alpha = -2$) in closed form:
$$\Delta\sigma_{\text{out},k}^2 = \int_{f_{k-1}}^{f_k} S(f)\,df = \begin{cases} S_k f_k \ln(f_k / f_{k-1}) & \text{if } |\alpha + 1| < 10^{-10} \\[6pt] \dfrac{S_k f_k - S_{k-1} f_{k-1}}{\alpha + 1} & \text{otherwise} \end{cases}$$
Matching SPICE3f5 / `ngspice` (`resnoise.c`, `bjtnoise.c`, `ninteg.c`), each step's integrated output variance $\Delta\sigma_{\text{out},k}^2$ is referred to the input via $\Delta\sigma_{\text{in},k}^2 = \Delta\sigma_{\text{out},k}^2 / |G_{\text{in}}(j\omega_k)|^2$.

---

## 2. Benchmark Table: Analytical Golden vs. `ngspice` vs. `Nodalys`

The five benchmark circuits below compare `Nodalys` against closed-form Johnson–Nyquist / shot-noise theory and `ngspice`:

| Circuit & Frequency Sweep | Metric / Contribution | Golden (Analytical) | `ngspice` | **`Nodalys`** | Relative Diff (`Nodalys` vs. `ngspice`) |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **1. RC Low-Pass Filter** | $e_{n,\text{out}}(10\text{ Hz})$ ($\text{V}/\sqrt{\text{Hz}}$) | $\sqrt{\frac{4k_BTR}{1+(f/f_c)^2}} = 4.071292\text{ nV}/\sqrt{\text{Hz}}$ | `4.071291e-09` | **`4.071292e-09`** | $2.5 \times 10^{-7}$ |
| ($R_1 = 1\text{ k}\Omega, C_1 = 100\text{ nF}$, | $e_{n,\text{in}}(10\text{ Hz})$ ($\text{V}/\sqrt{\text{Hz}}$) | $\sqrt{4k_BTR} = 4.071372\text{ nV}/\sqrt{\text{Hz}}$ | `4.071372e-09` | **`4.071372e-09`** | $< 10^{-7}$ |
| $f_c = 1591.55\text{ Hz}$, $10\text{ Hz}\text{--}100\text{ kHz}$, | `OutputTotal` ($\text{V}_{\text{rms}}$) | $\approx \sqrt{k_B T / C} = 203.57\text{ nV}_{\text{rms}}$ *(log-grid: $201.90\text{ nV}$)* | `2.018992e-07` | **`2.018992e-07`** | $< 10^{-7}$ |
| $10\text{ pts/dec}$) | `InputTotal` ($\text{V}_{\text{rms}}$) | $1.440533\text{ }\mu\text{V}_{\text{rms}}$ *(SPICE step rule)* | `1.440533e-06` | **`1.440533e-06`** | $< 10^{-7}$ |
| **2. Inverting Ideal Op-Amp** | $e_{n,\text{out}}(10\text{ Hz})$ ($\text{V}/\sqrt{\text{Hz}}$) | $\sqrt{10^2(4k_BTR_1) + 4k_BTR_2} = 42.6962\text{ nV}/\sqrt{\text{Hz}}$ | `4.269622e-08` | **`4.269623e-08`** | $2.3 \times 10^{-7}$ |
| ($R_1 = 1\text{ k}\Omega, R_2 = 10\text{ k}\Omega$, | $e_{n,\text{in}}(10\text{ Hz})$ ($\text{V}/\sqrt{\text{Hz}}$) | $e_{n,\text{out}} / 10 = 4.27009\text{ nV}/\sqrt{\text{Hz}}$ | `4.270092e-09` | **`4.270093e-09`** | $2.3 \times 10^{-7}$ |
| $A_v = -10$, $10\text{ Hz}\text{--}100\text{ kHz}$, | `OutputTotal` ($\text{V}_{\text{rms}}$) | — | `1.169431e-05` | **`1.169432e-05`** | $8.5 \times 10^{-7}$ |
| $5\text{ pts/dec}$) | `R1` total ($\text{V}_{\text{rms}}$) | — | `1.115009e-05` | **`1.115009e-05`** | $< 10^{-7}$ |
| | `R2` total ($\text{V}_{\text{rms}}$) | — | `3.525967e-06` | **`3.525968e-06`** | $2.8 \times 10^{-7}$ |
| | `U1` total ($\text{V}_{\text{rms}}$) | $\sqrt{2\cdot 2q(I_S + G_{\min}V_{\text{rail}})}$ clamp shot | `9.340616e-09` | **`9.340616e-09`** | $< 10^{-7}$ |
| **3. Common-Emitter BJT Amp** | $e_{n,\text{out}}(100\text{ Hz})$ ($\text{V}/\sqrt{\text{Hz}}$) | — *(Gummel-Poon + $2qI_C, 2qI_B$)* | `1.087762e-07` | **`1.087769e-07`** | $6.4 \times 10^{-6}$ |
| ($V_{CC} = 12\text{ V}, R_B = 10\text{ k}\Omega$, | `OutputTotal` ($\text{V}_{\text{rms}}$) | — | `1.021281e-04` | **`1.021288e-04`** | $6.9 \times 10^{-6}$ |
| $R_C = 2.2\text{ k}\Omega, R_E = 470\text{ }\Omega$, | `InputTotal` ($\text{V}_{\text{rms}}$) | — | `2.772753e-05` | **`2.772772e-05`** | $6.9 \times 10^{-6}$ |
| $100\text{ Hz}\text{--}1\text{ MHz}$, $5\text{ pts/dec}$) | `Q1` total ($\text{V}_{\text{rms}}$) | — | `8.903142e-05` | **`8.903222e-05`** | $9.0 \times 10^{-6}$ |
| **4. Common-Source NMOS Amp** | $e_{n,\text{out}}(10\text{ Hz})$ ($\text{V}/\sqrt{\text{Hz}}$) | $\sqrt{(\frac{8}{3}k_BTg_m + \frac{4k_BT}{R_D})R_{\text{out}}^2} = 146.137\text{ pV}/\sqrt{\text{Hz}}$ | `1.461369e-10` | **`1.461369e-10`** | $< 10^{-7}$ |
| ($V_{DD} = 10\text{ V}, R_D = 2\text{ k}\Omega$, | `OutputTotal` ($\text{V}_{\text{rms}}$) | $146.137\text{ pV}/\sqrt{\text{Hz}} \times \sqrt{99990\text{ Hz}} = 46.2102\text{ nV}_{\text{rms}}$ | `4.621024e-08` | **`4.621025e-08`** | $2.2 \times 10^{-7}$ |
| $V_{GS} = 3\text{ V}$, $10\text{ Hz}\text{--}100\text{ kHz}$) | `InputTotal` ($\text{V}_{\text{rms}}$) | $465.039\text{ nV}_{\text{rms}}$ | `4.650392e-07` | **`4.650392e-07`** | $< 10^{-7}$ |
| **5. Non-Inverting uA741 Amp** | $e_{n,\text{out}}(10\text{ Hz})$ ($\text{V}/\sqrt{\text{Hz}}$) | — *(Boyle macromodel)* | `3.752990e-05` | **`3.752927e-05`** | $1.7 \times 10^{-5}$ ($0.0017\%$) |
| ($R_1 = 1\text{ k}\Omega, R_2 = 9\text{ k}\Omega$, | `OutputTotal` ($\text{V}_{\text{rms}}$) | — | `1.058298e-02` | **`1.058283e-02`** | $1.4 \times 10^{-5}$ ($0.0014\%$) |
| $A_v = +10$, $10\text{ Hz}\text{--}100\text{ kHz}$) | `InputTotal` ($\text{V}_{\text{rms}}$) | — | `1.240157e-03` | **`1.240134e-03`** | $1.9 \times 10^{-5}$ ($0.0019\%$) |

---

### 2.2 Execution Performance ([`nodalys_benchmark_tests.cpp`](../../tests/nodalys_benchmark_tests.cpp))

| Circuit | Non-Ground Nodes | `ngspice` | **`Nodalys`** | Speedup |
| :--- | :---: | ---: | ---: | :---: |
| **BJT Common-Emitter Amp (41 pts)** | 5 | 0.226 ms | **0.079 ms** | **2.87x** |
