# Small-Signal AC Sensitivity Analysis (`.sens ... ac`)

*Exclusive to Nodalys: ngspice 47 reports a wrong AC sensitivity for circuits with forward-biased diodes or transistors, and for the current of a resistor, so `imcsim` does not use it.*

**Implementation files:**
- [`nodalys/src/analyses/ac_sens_analysis.cpp`](../../nodalys/src/analyses/ac_sens_analysis.cpp) (`ComputeAcSensitivity`)
- [`nodalys/src/analyses/ac_sens_elements.cpp`](../../nodalys/src/analyses/ac_sens_elements.cpp) (the elements varied and their derivatives)
- [`nodalys/src/analyses/ac_sens_bias.cpp`](../../nodalys/src/analyses/ac_sens_bias.cpp) (the bias term of the nonlinear devices)
- [`nodalys/src/mna/mna_system.cpp`](../../nodalys/src/mna/mna_system.cpp) (`MnaSystem::SolveAcTransposeFor`, `MnaSystem::SolveTransposeComplex`)
- [`app-core/backends/nodalys/nodalys_backend.cpp`](../../app-core/backends/nodalys/nodalys_backend.cpp) (`NodalysBackend::RunACSensitivity`)
- [`nodalys/src/parser/netlist_parser.cpp`](../../nodalys/src/parser/netlist_parser.cpp) (`.sens ... ac`), [`nodalys/src/cli/cli.cpp`](../../nodalys/src/cli/cli.cpp) and the exporters of [`nodalys/src/export/`](../../nodalys/src/export/)
- [`nodalys/tests/ac_sens_analysis_tests.cpp`](../../nodalys/tests/ac_sens_analysis_tests.cpp), [`nodalys/tests/ac_sens_netlist_tests.cpp`](../../nodalys/tests/ac_sens_netlist_tests.cpp)

---

## 1. Algorithms and Mathematical Formulation

The analysis answers: *by how many decibels and degrees does the response at a node change when one part grows by 1 %?*, at every frequency of a logarithmic sweep.

### 1.1 The Small-Signal System and Its Adjoint

Around the converged DC operating point $\mathbf{x}_{\text{DC}}$ the circuit is the complex system $\mathbf{Y}(j\omega)\,\mathbf{x} = \mathbf{b}$, where only the chosen input source is excited (`AcSensitivityConfig::InputSource`) and the output is $V = \mathbf{d}^T \mathbf{x}$. A single transposed solve per frequency gives the adjoint of the output:
$$\mathbf{Y}^T \boldsymbol{\lambda} = \mathbf{d}$$
and the derivative with respect to every element value $p$ follows from the same $\boldsymbol{\lambda}$:
$$\frac{\partial V}{\partial p} = -\boldsymbol{\lambda}^T \frac{\partial \mathbf{Y}}{\partial p}\,\mathbf{x} \;-\; \boldsymbol{\mu}^T \mathbf{g}_p$$

The first term is the **direct term**: the value appears in $\mathbf{Y}$ (a resistor as $1/R$, a capacitor as $j\omega C$, an inductor as $j\omega L$, a controlled source as its gain).

### 1.2 The Bias Term of Nonlinear Devices

A nonlinear device linearizes at $\mathbf{x}_{\text{DC}}$, so its conductances and capacitances move when an element value shifts the operating point. That is the second term, the **bias term**, and it is what makes the analysis right for amplifiers:

1. The weights $w_k = -\boldsymbol{\lambda}^T (\partial \mathbf{Y} / \partial x_{\text{DC},k})\,\mathbf{x}$ say how the output moves with each DC unknown. $\partial \mathbf{Y} / \partial x_{\text{DC},k}$ comes from central differences of the conductance and reactive stamps of the nonlinear devices, one DC unknown at a time ([`ac_sens_bias.cpp`](../../nodalys/src/analyses/ac_sens_bias.cpp)).
2. The DC adjoint $\mathbf{J}^T \boldsymbol{\mu} = \mathbf{w}$ turns them into the change for the shift of the operating point that a value causes, where $\mathbf{J}$ is the DC Jacobian and $\mathbf{g}_p = \partial \mathbf{f}_{\text{DC}} / \partial p$ the derivative of the DC equations with respect to the value ([`MnaSystem::SolveTransposeComplex`](../../nodalys/src/mna/mna_system.cpp)).

A linear circuit has no weights, so the bias term is zero and only the direct term remains. The DC value of a source moves nothing in a linear circuit and, in a biased stage, everything through the bias.

### 1.3 The Figures

For a value $p$ and the output phasor $V$, the relative change of the response for a 1 % change of $p$ is $\frac{p}{100}\,\frac{\partial V / \partial p}{V}$, whose real part is the change of $\ln|V|$ and whose imaginary part the change of the phase:

$$\Delta_{\text{dB}} = \frac{20}{\ln 10}\,\text{Re}\!\left(\frac{\partial V/\partial p}{V}\right)\frac{p}{100}, \qquad \Delta_{\text{deg}} = \frac{180}{\pi}\,\text{Im}\!\left(\frac{\partial V/\partial p}{V}\right)\frac{p}{100}$$

Where $|V| \le 10^{-9}$ of the AC magnitude of the input the output is too small for a relative change to mean anything, so those frequencies have no figure (`std::nullopt`) and the result carries one warning that says where. If the output is that small at every frequency, the source does not drive it and the analysis returns an error.

For an RC low-pass at its corner, with $x = \omega R C = 1$, $\partial \ln H / \partial \ln R = \partial \ln H / \partial \ln C = -jx/(1+jx) = -\tfrac12 - \tfrac12 j$, so a 1 % higher $R$ or $C$ moves the gain by $-0.0434$ dB and the phase by $-0.286^\circ$.

### 1.4 Elements Varied

| Device | Value $p$ | Notes |
| :--- | :---: | :--- |
| `ResistorDevice` | $R$ | Direct and bias terms. |
| `CapacitorDevice` | $C$ | $-j\omega\,(\lambda_a - \lambda_b)(x_a - x_b)$; no effect on the bias. |
| `InductorDevice` | $L$ | Through its branch current. |
| `VoltageSourceDevice`, `CurrentSourceDevice` | DC value | Only through the bias of nonlinear devices. |
| `VcvsDevice`, `VccsDevice`, `CccsDevice`, `CcvsDevice` | Gain | Direct and bias terms. |

---

## 2. Verification (`ac_sens_analysis_tests.cpp`)

- An RC low-pass at its corner gives the closed-form $-0.043429$ dB and $-0.28648^\circ$ per 1 % for both $R$ and $C$.
- A voltage divider at low frequency equals the DC sensitivity of `ComputeSensitivity`, scaled by the AC magnitude over the DC value of the source.
- A series RLC matches the derivatives of $H = 1/(1 - \omega^2 LC + j\omega RC)$.
- On a biased BJT stage with junction capacitances and a transit time, and on an ideal op-amp stage followed by VCVS, VCCS, CCCS and CCVS stages, every element agrees with central finite differences of `ComputeACSweep`, with tight convergence tolerances, to $2 \times 10^{-4}$ relative. This covers the bias term and the signs of the reactive and controlled-source terms.
- A Wien bridge held at its null shows the warning once and no figure at the null frequency; a source that does not drive the output, has no AC magnitude or does not exist is refused.

The Sallen-Key example of `imcsim` also checks that the four changes of $\ln H$ for 1 % of $R_1$, $R_2$, $C_1$ and $C_2$ add up to twice $d\ln H / d\ln f$, since scaling every $R$ and $C$ by one factor scales $\omega R C$ by its square.

The netlist parser and the command line check the same: `ac_sens_netlist_tests.cpp` reads a netlist through `ParseNetlist()` and `RunCli()` and compares every number with `ComputeAcSensitivity()` on the same circuit, with the RC corner above as the reference.

---

## 3. Netlist and Command Line

```
.sens <out_var> ac dec|oct|lin <np> <fstart> <fstop>
```

- `<out_var>` is `V(node)` or the differential `V(node1,node2)`, as in the DC `.sens`. The positive node cannot be ground (`V(0,out)` is refused; write `V(out,0)` or swap the nodes and read the sign), and the two nodes must differ.
- `dec` and `oct` give `<np>` points per decade or per octave, from `<fstart>` to `<fstop>` with the stop included; `lin` gives `<np>` points in all, spaced evenly. `0 < fstart <= fstop` and `<np>` is a whole number of at least 1. The three sweep types are an extension of `AcSensitivityConfig` (`SweepType`, with the decade as default, which is what `imcsim` uses).
- The ngspice syntax has no input source, so it is the one independent source of the netlist with an AC magnitude (`V1 in 0 DC 0 AC 1`, or a current source). If none or more than one has an AC magnitude the netlist is refused with a message that says so and names the sources found; leave `AC` on one source only. An AC magnitude of 0 does not count, and sources inside a block are not looked at. The magnitude only sets the scale of the output phasor: the figures per 1 % do not depend on it.
- The directive may stand before or after the sources, and a netlist may carry both this and the DC `.sens`.

On the command line, `--analysis sens` runs every `.sens` of the netlist, the DC and the AC form. There is no separate `acsens` choice: without a `.sens ... ac` line `--analysis sens` runs the DC one, since the AC form needs an input source and a range that no default can stand in for.

The output of `--format`:

| Format | Content |
| :--- | :--- |
| `csv` | One row per frequency: `Frequency`, `Output_re`, `Output_im` (the output phasor), then for each element `<name>_dB_per_percent` and `<name>_deg_per_percent`. The cell is empty at a frequency with no figure. |
| `json` | `analysis` (`ac_sensitivity`), the output nodes, `input_source`, `frequencies`, `output_re`, `output_im`, `warnings` and `elements`: each with `name`, `parameter`, `value`, `derivative_re`, `derivative_im` and the two figures, `null` where there is none. |
| `raw` | Nutmeg ASCII, `Flags: complex`: the vectors `frequency`, the output (`v(n)` or `v(n1,n2)`, numbers of nodes) and one per element named as the netlist writes it, holding the derivative $\partial V / \partial p$ in volts per unit of the value, as ngspice writes the sensitivity of `.sens ... ac`. |

The raw file differs from ngspice's in that the figures per 1 % are not in it (they follow from the derivative and the output, and are in the CSV and the JSON), and in the order of the vectors (elements by name). A source with a DC value only (`V1` above) appears with a derivative of zero, as it does not move the small-signal output of a linear circuit.

---

## 4. Use in `imcsim`

The Sensitivity analysis has a kind, DC or AC. The AC kind is disabled with the reason as its tooltip when the engine in use is ngspice. [`ac_sensitivity.cpp`](../../app-core/simulation/ac_sensitivity.cpp) turns the figures by element name into those of each part (a potentiometer as the sum of its halves, a block by its element with the largest peak effect), and the Output tab ranks the parts at a chosen frequency, by default the -3 dB point of the response.
