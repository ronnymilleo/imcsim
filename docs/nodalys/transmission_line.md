# Transmission Lines (`TL` / `T` Element)

**Implementation files:**
- [`nodalys/src/devices/passive/transmission_line_device.h`](../../nodalys/src/devices/passive/transmission_line_device.h) / [`transmission_line_device.cpp`](../../nodalys/src/devices/passive/transmission_line_device.cpp) (`TransmissionLineDevice`)
- [`nodalys/src/devices/device.h`](../../nodalys/src/devices/device.h) (`FrequencyDevice`, `ComplexStamper`)
- [`nodalys/src/compiler/circuit_lowering.cpp`](../../nodalys/src/compiler/circuit_lowering.cpp) (`Lower(const TransmissionLineSpec &)`)
- [`nodalys/src/analyses/hb/hb_residual.cpp`](../../nodalys/src/analyses/hb/hb_residual.cpp) (harmonic-balance frequency stamp)
- [`nodalys/src/parser/netlist/netlist_elements.cpp`](../../nodalys/src/parser/netlist/netlist_elements.cpp) / [`subcircuit_elements.cpp`](../../nodalys/src/parser/subcircuit/subcircuit_elements.cpp) (`T` line reader)
- [`nodalys/tests/devices/transmission_line_tests.cpp`](../../nodalys/tests/devices/transmission_line_tests.cpp)

---

## 1. Formulation

A two-port uniform transmission line connects port 1 (`p1+`, `p1-`) and port 2 (`p2+`, `p2-`) with nominal lossless characteristic impedance $Z_0 > 0$ and one-way delay $T_d > 0$, giving total distributed inductance $L_{\text{tot}} = Z_0 T_d$ and total distributed capacitance $C_{\text{tot}} = T_d / Z_0$. The device allocates two MNA branch-current unknowns $i_1$ and $i_2$ (positive entering `p1+` and `p2+`).

### 1.1 Dispersive Telegrapher Model in Frequency & DC

At frequency $f = \omega / (2\pi) \ge 0$, conductor skin effect and dielectric loss tangent give the total series impedance and shunt admittance:

$$R(f) = R_{\text{DC}} + R_{\text{skin}} \sqrt{\frac{f}{f_{\text{ref}}}}, \qquad G(f) = G_{\text{DC}} + \omega C_{\text{tot}} \tan\delta$$

$$Z_{\text{tot}}(\omega) = R(f) + j\omega L_{\text{tot}}, \qquad Y_{\text{tot}}(\omega) = G(f) + j\omega C_{\text{tot}}, \qquad \theta(\omega) = \sqrt{Z_{\text{tot}}(\omega)\,Y_{\text{tot}}(\omega)}$$

Plain two-port admittance matrices $\mathbf{Y}_{2\times 2} \propto \operatorname{csch}(\theta)$ blow up whenever $\theta = j k \pi$ ($f = 0$, half-wave, full-wave). Instead, `TransmissionLineDevice` writes the even/odd symmetric combinations of the telegrapher equations in terms of the entire functions

$$c(\theta) = \frac{1 + e^{-\theta}}{2}, \qquad s(\theta) = \frac{1 - e^{-\theta}}{\theta} \quad \bigl(s(0) = 1\bigr)$$

scaled in **volts** so MNA residual checks apply `VoltageAbsTol` consistently:

$$c(\theta)\,(V_1 - V_2) - \frac{Z_{\text{tot}}(\omega)}{2}\,s(\theta)\,(i_1 - i_2) = 0$$

$$\frac{Z_0 Y_{\text{tot}}(\omega)}{2}\,s(\theta)\,(V_1 + V_2) - Z_0 c(\theta)\,(i_1 + i_2) = 0$$

- At **DC** ($\omega = 0$), $R(0) = R_{\text{DC}}$ and $G(0) = G_{\text{DC}}$: for a lossless line ($R_{\text{DC}} = G_{\text{DC}} = 0$), the two rows reduce to $V_1 - V_2 = 0$ and $-Z_0(i_1 + i_2) = 0$, an exact ideal pass-through.
- At **half-wave resonance** ($\theta = j\pi \implies e^{-\theta} = -1$), $c(j\pi) = 0$ and $s(j\pi) = 2/(j\pi) \ne 0$: the rows become $i_1 = i_2$ and $V_1 = -V_2$ without any $0/0$ cancellation or singular matrix stamp.
- In **`.ac`, `.sp`, `.noise`, `.sens ... ac`, and `.hb`**, the DC Jacobian holds the $\omega = 0$ stamp and `FrequencyDevice::StampFrequencyDelta` adds the exact complex increment $\Delta\mathbf{F}(\omega) = \mathbf{F}(\omega) - \mathbf{F}(0)$ at each sweep frequency or harmonic $k\omega_0$.

### 1.2 Thermal Noise (`.noise`)

A passive reciprocal two-port in thermal equilibrium at temperature $T$ injects correlated port current noise with spectral covariance $4 k_B T \operatorname{Re}\{\mathbf{Y}_{\text{port}}(\omega)\}$ (Bosma / Twiss). Because row $b_k$ has port-voltage sign $+1$ in nodal KCL, the adjoint sensitivity to a current entering port $k$ equals $\lambda_{\text{port},k} = \lambda_{p_k^+} - \lambda_{p_k^-}$. Evaluating the Hermitian quadratic form directly from $c(\theta)$ and $s(\theta)$ gives exact thermal noise for resistive and dielectric losses, and identically zero noise when lossless.

### 1.3 Lossless Time-Domain Transient (`.tran`) — Bergeron's Method

 along the characteristics $x \pm v t$ of a lossless line ($R = G = 0$), the forward and backward wave invariants satisfy:

$$V_1(t) - Z_0 i_1(t) = V_2(t - T_d) + Z_0 i_2(t - T_d)$$

$$V_2(t) - Z_0 i_2(t) = V_1(t - T_d) + Z_0 i_1(t - T_d)$$

- **History interpolation & corner propagation:** Accepted port states $(t_n, V_1, i_1, V_2, i_2)$ are stored in a ring buffer trimmed past $t - 2 T_d$ and interpolated linearly at $t - T_d$. Whenever the transient solver lands on a source corner (pulse/PWL edge), `NoteCorner(t)` schedules a breakpoint at $t + T_d$ (chained across reflections up to stop time), and `MaxTimeStep()` caps the step at $T_d / 2$ whenever $T_d$ is comparable to the user step so sharp wavefronts are never stepped over.
- **Short-delay fallback ($h > T_d$):** When a picosecond interconnect sits inside a millisecond simulation where $t - T_d > t_n$, linear interpolation between $t_n$ and the current unknown $t_{n+1}$ yields a well-conditioned semi-implicit coupling that stays unconditionally stable and exact at DC.
- **Analysis restrictions:** Lossy/dispersive lines (`IsLossy()`) are refused in `.tran` (requiring causal impulse convolution / vector fitting beyond Bergeron's lossless characteristics), and all transmission lines are refused in `.pz` because a distributed delay element has infinitely many poles and zeros.
