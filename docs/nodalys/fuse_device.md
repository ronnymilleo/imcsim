# Fuse Device

The fuse of `imcsim` is a resistance that opens for good once the current has heated it enough. `ngspice` runs it as the subcircuit `imcsim_fuse` (see the Fuses section of [`docs/models.md`](../models.md)); `Nodalys` models the same behavior as one native device with a thermal state, [`FuseDevice`](../../nodalys/src/devices/fuse_device.cpp), built with `CircuitBuilder::AddFuse(name, a, b, FuseParameters)`.

---

## 1. Model

### 1.1 Heat

The state is the heat $h$ of the element, in units of what the rated current settles at:

$$\tau \frac{dh}{dt} = P - h, \qquad P = \frac{i^2}{I_h^2}, \qquad \tau = \frac{I^2t}{I_h^2}$$

with $I_h$ the rated current and $I^2t$ the melting integral. At a constant current $h$ settles at $P$, so a current at or below the rated one never reaches the blow level, and a short overload blows the fuse when $\int i^2\,dt$ reaches $I^2t$. From $h = 0$ at a constant $P > h_b$, the fuse blows at

$$t_b = \tau \ln \frac{P}{P - h_b}, \qquad h_b = 1.015$$

The blow level $h_b$ is a margin above the 1 that the rated current settles at, the same 1.015 where the latch of the `ngspice` subcircuit starts to charge.

### 1.2 Resistance

While $h < h_b$ the resistance is the cold one, $R_c$. When $h$ reaches $h_b$ at $t_b$, the fuse opens, and it never closes: the resistance goes from $R_c$ to the blown value $R_b$ along

$$R(t) = R_c \left(\frac{R_b}{R_c}\right)^{s}, \qquad s = \operatorname{smoothstep}\!\left(\frac{t - t_b}{10^{-4}\,\tau}\right)$$

The interpolation is geometric because the resistance changes by nine decades, and the smooth step has a zero slope at both ends, so the transition is a function of time with continuous derivative. It takes $10^{-4}\tau$, the time the latch of the `ngspice` model needs to charge to the middle of its range, so its resistance crosses the geometric mean about $5 \cdot 10^{-5}\tau$ after the blow in both.

### 1.3 How it maps to the `ngspice` subcircuit

| `imcsim_fuse` (`ngspice`) | `FuseDevice` (`Nodalys`) |
| :--- | :--- |
| Thermal node: 1 F with $\tau$ across it, charged by $v_g\,i^2 / (I_h^2 \tau)$ | The state `Heat`, advanced by the exact solution of $\tau\,\dot h = P - h$ |
| Latch node, charged at $10^4/\tau$ once $h > 1.015$ and by itself above 0.15, with no discharge | The flag `IsBlown` and the instant `BlowTime`, which never go back |
| $R = R_c \exp\!\bigl(\ln(R_b/R_c) / (1 + e^{-(l - 0.5)/0.02})\bigr)$ | $R = R_c (R_b/R_c)^{s(t - t_b)}$ |
| Gate $v_g$: 0 in `.op`, `.dc` and `.ac`, rising 1 ns into a transient | The stamp is the cold resistance outside a transient step; the heat starts at 0 |
| No noise (behavioral sources) | No noise contribution |

The sigmoids of the subcircuit exist to help `ngspice` converge. `Nodalys` has no use for them: the resistance is an explicit function of time and of the state at the start of the step, so each step is a linear problem for Newton and cannot fail to converge on the fuse. The one visible difference is that the sigmoid of the latch has a tail of 0.0004 in $h$, so `ngspice` starts to open a little before $h$ reaches 1.015; the closer the current is to the threshold, the longer the heat spends there and the more it shows (section 3).

---

## 2. Integration

- **The heat** advances after each converged step, from the state at its start, by the exact solution with the power at the step taken as the mean of its values at the two ends: $h_{n+1} = \bar P + (h_n - \bar P)\,e^{-\Delta t/\tau}$. The solution holds however long the step is, so a step much longer than $\tau$ is as exact as a short one, and the step control needs no truncation error for it. The power at the end of the step is read from the converged solution (`Stamp()` computes the pending state at each iterate, and the last stamp of a step is at the converged solution).
- **The blow instant** inside a step where $h$ crosses $h_b$ is the crossing of the same solution, clamped to the step.
- **Landing on the blow.** `NextBreakpoint()` predicts the blow instant from the last accepted point, assuming the current stays: the closed form of section 1.1 with the heat of that point. The transient lands a time point on it, so the blow is not stepped over, and the point at the blow starts a restart with Backward Euler. When the current changes, the prediction is wrong and the transient asks again after the next point (it asks after every accepted point now), so it closes in on the blow; with a constant current it lands exactly. After the blow, the breakpoint is the end of the opening, $t_b + 10^{-4}\tau$. A corner closer than $10^{-6}\tau$ is not asked for: the step passes it, and the error is that small a share of the time constant.
- **`HasSwitched()`** is true for the step that blows the fuse, so the step that follows restarts with Backward Euler, as it does after a switch.
- **Gear-2.** An inductor whose current the open fuse cuts makes the trapezoidal rule ring for ever; the `imcsim` backend runs a circuit with a fuse, in a block or not, with Gear-2, as the `ngspice` netlist writes `method=gear`.
- **The small-signal analyses and harmonic balance** see the cold resistance, as the gate of the `ngspice` model is 0 there. A rejected step restores the pending state to the accepted one.

---

## 3. Verification

`nodalys/tests/fuse_device_tests.cpp`: the blow time at a constant overcurrent against the closed form of section 1.1 (within 0.1 ms for three loads, and with an output step of 0.2 s, longer than the blow), a fuse at 0.97 A, at 1 A and at 1.001 A that never blows over 20 time constants, a fuse that stays blown, the operating point, DC sweep and AC sweep seeing it whole at 7.7 A, an inductive load cut with no ringing (within 10 $\mu$V of rest, where the trapezoidal rule stays 1.1 mV off), and overloads that cool between pulses and add up when close.

`tests/fuse_tests.cpp` runs both engines. Blow time of the 1 A, 150 ms fuse in a 5 V loop, read as `imcsim` reads it from the results (the first point whose resistance is above the geometric mean):

| Load | Current | Closed form | `ngspice` | `Nodalys` |
| ---: | ---: | ---: | ---: | ---: |
| 1.0 Ohm | 4.35 A | 8.278 ms | 8.281 ms | 8.293 ms |
| 2.5 Ohm | 1.89 A | 50.34 ms | 50.29 ms | 50.36 ms |
| 4.7 Ohm | 1.03 A | 465.2 ms | 457.3 ms | 465.2 ms |

`Nodalys` reads 15 $\mu$s after the closed form, the end of the opening. `ngspice` agrees within 0.2 % for large overloads and is 1.7 % early near the threshold, from the tail of its latch. The tests compare them within 3 %.

The fast and slow-blow fuse example runs on both engines in `tests/engine_cross_validation_tests.cpp` with no known difference, and `tests/fuse_tests.cpp` compares the blow times of its two fuses.

---

## 4. Limits

- The thermal state is not kept between runs, as in `ngspice`: every transient starts with a whole fuse. A live run keeps the state while it lasts.
- There is no arc, breaking capacity or voltage rating, and the cold resistance does not follow the temperature.
- `Nodalys` writes no rating warnings, so the "FU1 blew at ..." text is not added to its results; the blow is read from the voltage and the current, so the schematic and the flash show it.
