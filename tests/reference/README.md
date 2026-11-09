# Analytical Golden References (`tests/reference/`)

This directory holds the closed-form reference generator (`generate_goldens.py`) and the golden CSV
tables used for Layer 1 validation of **Nodalys** (`app-core/native/`), the clean-room simulation
engine of `imcsim`.

## Regenerating the CSVs

```bash
python3 tests/reference/generate_goldens.py
```

The script requires only the Python 3 standard library (`math`, `csv`, `pathlib`) so it runs in any
environment without external packages.

---

## Reference Cases & Published Equations

### 1. First-Order RC Step Response (`rc_step.csv`)
- **Circuit:** $V_s = 5\text{ V}$ step driving $R = 1\text{ k}\Omega$ in series with $C = 1\text{ }\mu\text{F}$ ($\tau = R C = 1\text{ ms}$), starting from $v_C(0) = 0\text{ V}$.
- **Analytical equations:**
  $$v_C(t) = V_s \left(1 - e^{-t / (RC)}\right), \qquad i_C(t) = \frac{V_s}{R} e^{-t / (RC)}$$
- **Source:** C. A. Desoer & E. S. Kuh, *Basic Circuit Theory*, McGraw-Hill, 1969, Ch. 4.
- **Test coverage:** [`tests/native_backend_tests.cpp`](../native_backend_tests.cpp) and [`tests/backend_comparison_tests.cpp`](../backend_comparison_tests.cpp).

### 2. First-Order RL Step Response (`rl_step.csv`)
- **Circuit:** $V_s = 1\text{ V}$ step driving $R = 10\text{ }\Omega$ in series with $L = 10\text{ mH}$ ($\tau = L / R = 1\text{ ms}$), starting from $i_L(0) = 0\text{ A}$.
- **Analytical equations:**
  $$i_L(t) = \frac{V_s}{R} \left(1 - e^{-R t / L}\right), \qquad v_L(t) = V_s e^{-R t / L}$$
- **Source:** C. A. Desoer & E. S. Kuh, *Basic Circuit Theory*, McGraw-Hill, 1969, Ch. 4.
- **Test coverage:** [`tests/native_backend_tests.cpp`](../native_backend_tests.cpp) and [`tests/backend_comparison_tests.cpp`](../backend_comparison_tests.cpp).

### 3. Second-Order Underdamped Series RLC Step Response (`rlc_underdamped.csv`)
- **Circuit:** $V_s = 10\text{ V}$ step driving $R = 20\text{ }\Omega$, $L = 10\text{ mH}$, $C = 1\text{ }\mu\text{F}$ in series ($\alpha = R / (2L) = 1000\text{ s}^{-1}$, $\omega_0 = 1/\sqrt{LC} = 10000\text{ rad/s}$, $\omega_d = \sqrt{\omega_0^2 - \alpha^2}$).
- **Analytical equations:**
  $$v_C(t) = V_s \left[1 - e^{-\alpha t}\left(\cos(\omega_d t) + \frac{\alpha}{\omega_d}\sin(\omega_d t)\right)\right], \qquad i_L(t) = \frac{V_s}{L \omega_d} e^{-\alpha t} \sin(\omega_d t)$$
- **Source:** J. Vlach & K. Singhal, *Computer Methods for Circuit Analysis and Design*, 2nd ed., Van Nostrand Reinhold, 1994, Ch. 9.
- **Test coverage:** [`tests/native_backend_tests.cpp`](../native_backend_tests.cpp).

### 4. Shockley Diode + Series Resistance DC Sweep (`diode_shockley_dc.csv`)
- **Circuit:** Swept DC source $V_s \in [0, 5]\text{ V}$ driving $R_{\text{load}} = 1\text{ k}\Omega$ in series with a 1N4148 diode ($I_S = 2.52\text{ nA}$, $N = 1.752$, $R_S = 0.568\text{ }\Omega$, $V_T = \frac{k_B}{q}\cdot 300.15\text{ K}$).
- **Analytical implicit equation:**
  $$I_D(V_j) = I_S \left(e^{V_j / (N V_T)} - 1\right) + G_{\min} V_j, \qquad V_s = V_j + (R_{\text{load}} + R_S) I_D(V_j)$$
- **Source:** L. W. Nagel, *SPICE2: A Computer Program to Simulate Semiconductor Circuits*, Memorandum No. UCB/ERL M520, University of California, Berkeley, 1975.
- **Test coverage:** [`tests/nonlinear_tests.cpp`](../nonlinear_tests.cpp) and [`tests/backend_comparison_tests.cpp`](../backend_comparison_tests.cpp).
