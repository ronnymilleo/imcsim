# Small-Signal DC Transfer Function Analysis (`.tf`)

**Implementation files:**
- [`app-core/native/tf_analysis.cpp`](../../app-core/native/tf_analysis.cpp) (`ComputeTransferFunction`)
- [`app-core/native/native_backend.cpp`](../../app-core/native/native_backend.cpp) (`NativeBackend::RunTransferFunction`)
- [`tests/backend_comparison_tests.cpp`](../../tests/backend_comparison_tests.cpp) (`Both backends agree on the small-signal DC transfer function`)

---

## 1. Algorithms and Mathematical Formulation

The `.tf V(out) Vin` analysis computes three fundamental small-signal DC ($\omega = 0$) quantities between an **independent input source** ($V_{\text{in}}$ or $I_{\text{in}}$) and an **output node** ($n_{\text{out}}$):
1. **Small-Signal DC Gain:**
   $$A_v = \frac{\partial V_{\text{out}}}{\partial V_{\text{in}}} \quad (\text{V/V, dimensionless}) \qquad \text{or} \qquad R_m = \frac{\partial V_{\text{out}}}{\partial I_{\text{in}}} \quad (\text{V/A, transresistance in }\Omega)$$
2. **Input Impedance ($Z_{\text{in}}$):** small-signal Thévenin resistance seen looking into the circuit from the input source terminals.
3. **Output Impedance ($Z_{\text{out}}$):** small-signal Thévenin resistance seen looking back into output node $n_{\text{out}}$ (with respect to ground) with all independent sources zeroed.

### 1.1 Assembling the DC Tangent Jacobian $\mathbf{J}(\mathbf{x}_{\text{DC}})$
1. First, `Nodalys` computes the nonlinear DC operating point $\mathbf{x}_{\text{DC}}$ via `SolveOperatingPoint`.
2. Next, it builds the small-signal Jacobian matrix at $\omega = 0$ by invoking `device->StampAc(system, 0.0, x_dc)` across all compiled devices:
   - Independent voltage sources stamp only their structural short-circuit constraint ($V_{n^+} - V_{n^-} = 0$, zero right-hand side).
   - Independent current sources stamp nothing (open circuit).
   - Inductors stamp their DC short-circuit constraint ($V_{n^+} - V_{n^-} - R_{\text{ser}} I_L = 0$) and capacitors stamp open circuits ($\omega = 0$).
   - Diodes, BJTs, and MOSFETs stamp their exact differential conductances ($g_d, g_\pi, g_\mu, g_m, g_o, g_{ds}$) evaluated at $\mathbf{x}_{\text{DC}}$.

### 1.2 Solve 1: Unit Input Excitation ($\delta u_{\text{in}} = 1 \implies \text{Gain and } Z_{\text{in}}$)
`Nodalys` solves the real linear system $\mathbf{J}(\mathbf{x}_{\text{DC}})\,\delta\mathbf{x}^{(1)} = \mathbf{b}_{\text{in}}$:
- **Voltage-source input** at MNA branch $b_{\text{in}}$ ($\mathbf{b}_{\text{in}}[b_{\text{in}}] = +1\text{ V}$):
  $$\text{Gain} = \delta V_{\text{out}}^{(1)}, \qquad Z_{\text{in}} = -\frac{1}{\delta I_{b_{\text{in}}}^{(1)}}$$
  *(The minus sign follows SPICE's MNA branch convention, where voltage-source branch current is positive when entering the positive terminal and leaving the negative terminal; thus a $+1\text{ V}$ source driving a passive load $R$ carries $\delta I_{b_{\text{in}}} = -1/R$).*
- **Current-source input** between $n^+$ and $n^-$ ($\mathbf{b}_{\text{in}}[n^+] = -1\text{ A}$, $\mathbf{b}_{\text{in}}[n^-] = +1\text{ A}$):
  $$\text{Gain} = \delta V_{\text{out}}^{(1)}, \qquad Z_{\text{in}} = \delta V_{n^-}^{(1)} - \delta V_{n^+}^{(1)}$$
- Whenever $|\delta I_{b_{\text{in}}}^{(1)}| \le 10^{-20}\text{ A}$ (open-circuit input), $Z_{\text{in}} = 10^{20}\text{ }\Omega$ is reported, matching `ngspice`.

### 1.3 Solve 2: Unit Output Test Current Injection ($\delta I_{\text{out}} = +1\text{ A} \implies Z_{\text{out}}$)
With all independent sources zeroed (voltage sources shorted, current sources opened), a $+1\text{ A}$ test current is injected into the output node ($\mathbf{b}_{\text{out}}[n_{\text{out}}] = +1\text{ A}$) and $\mathbf{J}(\mathbf{x}_{\text{DC}})\,\delta\mathbf{x}^{(2)} = \mathbf{b}_{\text{out}}$ is solved:
$$Z_{\text{out}} = \delta V_{\text{out}}^{(2)}$$

---

## 2. Benchmark Table: Analytical Golden vs. `ngspice` vs. `Nodalys`

The three benchmark circuits below span passive networks with closed-form solutions, a Gummel-Poon BJT emitter follower, and a closed-loop non-inverting amplifier using the full Boyle uA741 macromodel:

| Circuit | Metric | Golden (Analytical) | `ngspice` | **`Nodalys`** | Relative Diff (`Nodalys` vs. `ngspice`) |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **1. Resistive Divider** | Gain ($V/V$) | $\frac{2200}{1000 + 2200} = 0.687500$ | `0.687500` | **`0.687500`** | $6.9 \times 10^{-10}$ |
| ($V_1 = 12\text{ V}$, $R_1 = 1\text{ k}\Omega$, $R_2 = 2.2\text{ k}\Omega$) | $Z_{\text{in}}$ ($\Omega$) | $1000 + 2200 = 3200.00$ | `3200.00` | **`3200.00`** | $6.9 \times 10^{-10}$ |
| | $Z_{\text{out}}$ ($\Omega$) | $1000 \parallel 2200 = 687.500$ | `687.500` | **`687.500`** | $6.9 \times 10^{-10}$ |
| **2. BJT Emitter Follower** | Gain ($V/V$) | — *(Gummel-Poon)* | `0.988802` | **`0.988802`** | $< 10^{-6}$ |
| (2N3904, $V_{CC} = 12\text{ V}$, $R_B = 10\text{ k}\Omega$, $R_E = 1\text{ k}\Omega$) | $Z_{\text{in}}$ ($\Omega$) | — *(Gummel-Poon)* | `1.59483e+05` | **`1.59483e+05`** | $< 10^{-6}$ |
| | $Z_{\text{out}}$ ($\Omega$) | — *(Gummel-Poon)* | `11.1997` | **`11.1997`** | $< 10^{-6}$ |
| **3. Non-Inverting uA741 Op-Amp** | Gain ($V/V$) | $\frac{1 + R_2/R_1}{1 + (1 + R_2/R_1)/A_{\text{OL}}} \approx 9.99918$ | `9.999180` | **`9.999180`** | $4.0 \times 10^{-8}$ |
| ($R_1 = 1\text{ k}\Omega$, $R_2 = 9\text{ k}\Omega$, $G_{\text{ideal}} = 10$) | $Z_{\text{in}}$ ($\Omega$) | $\sim 2.09\text{ G}\Omega$ | `2.086678e+09` | **`2.086631e+09`** | $2.2 \times 10^{-5}$ ($0.0022\%$) |
| | $Z_{\text{out}}$ ($\Omega$) | $\frac{R_{\text{out,OL}}}{1 + \beta A_{\text{OL}}} \approx 3.8\text{ m}\Omega$ | `3.805862e-03` | **`3.769496e-03`** | $\Delta = 36\text{ }\mu\Omega$ |

> [!NOTE]
> **Why does the closed-loop uA741 output impedance differ by $36\text{ }\mu\Omega$ (`3.806 mOhm` vs. `3.769 mOhm`)?**
> In the Boyle uA741 macromodel ([`compiler.cpp`](../../app-core/native/compiler.cpp)), the short-circuit current limiter senses the drop across `RSO` ($1\text{ }\Omega$) through a controlled source `ECL` with gain $\approx 33.8$ driving two anti-parallel diodes (`D1` and `D2`) that feed into the high-impedance gain node (`n_gain`, impedance $R_2 \times G_B \times R_{O2} \approx 1.5 \times 10^7$).
> In `ngspice`, `CKTgmin = 1e-12 S` is placed in parallel with **each diode junction** (`2e-12 S` total across `D1 || D2`), creating a parasitic small-signal leakage path of $33.8 \times 2\times 10^{-12} \times 10 \times 1.5\times 10^7 \approx 0.01$ ($\sim 1\%$ of loop gain). In `Nodalys`, diodes follow the pure Shockley equation and `Gmin` is placed from nodes to ground, yielding $Z_{\text{out}} = 3.769\text{ m}\Omega$ (faithful to the theoretical loop gain without off-diode leakage).
