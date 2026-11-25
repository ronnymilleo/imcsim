# Thyristor Device

The thyristor (SCR) of `imcsim` is a latching switch: it blocks forward voltage until a gate pulse fires it, then stays on with the gate removed until its anode current falls below the holding current. `Nodalys` models it as one native device, [`ThyristorDevice`](../../nodalys/src/devices/semiconductors/thyristor_device.cpp), built with `CircuitBuilder::AddThyristor(name, anode, cathode, gate, ThyristorParameters)`. The `ngspice` companion model is a two-transistor regenerative pair written as the subcircuit `imcsim_scr`, which ships with the schematic part (see the Thyristors section of [`docs/models.md`](../models.md)).

---

## 1. Model

### 1.1 The latch

The state is a single flag, conducting or blocking. With $v_{AK}$ the anode-cathode voltage and $i_G$ the gate current:

- **Firing.** A blocking device starts conducting when $v_{AK} > 0$ and $i_G \ge I_{GT}$, or when $v_{AK} \ge V_{BO}$ (forward breakover, disabled when $V_{BO} = 0$).
- **Holding.** A conducting device keeps conducting while the gate draws at least $I_{GT}$, whatever the anode current; with the gate removed it holds only while $i_A \ge I_H$.
- **Release.** Below $I_H$ with the gate removed, the latch opens. It cannot close again on its own: only a new gate pulse or breakover fires it, so the latch itself is the hysteresis and the device does not chatter at the threshold.

The latching current $I_L$ is not a separate parameter: the gate sustains conduction until the anode current reaches $I_H$, which plays the role of both.

### 1.2 The branches

The on-state is the datasheet line

$$v_{AK} = V_0 + R_T\, i_A \quad\Longleftrightarrow\quad i_A = \frac{v_{AK} - V_0}{R_T}$$

stamped as the conductance $1/R_T$ between anode and cathode with an equivalent current source $V_0/R_T$ from cathode to anode. The off-state is the conductance $1/R_{off}$. Both are linear at each Newton iterate, so the branch cannot fail to converge; only the discrete state changes.

The gate-cathode junction draws

$$i_G = \begin{cases} (v_{GK} - V_{GT})/R_G & v_{GK} > V_{GT} \\ 0 & v_{GK} \le V_{GT} \end{cases}$$

with a small leakage conductance always tying the gate to the cathode, so a gate left undriven never floats the matrix. The gate current returns through the cathode, as it does in a real device, so a gate drive must be referenced to the cathode.

### 1.3 How it maps to the `ngspice` subcircuit

| `imcsim_scr` (`ngspice`) | `ThyristorDevice` (`Nodalys`) |
| :--- | :--- |
| PNP + NPN regenerative pair, collector of each into the base of the other | The flag `IsConducting`, fired by gate current or breakover |
| Gate resistor into the base of the NPN; holding current set by the base-emitter shunts and the betas | $I_{GT}$, $I_H$, $V_0$, $R_T$ taken as parameters |
| Latch emerges from the loop gain reaching unity | The latch is explicit, so it cannot fail to converge |

The two-transistor model is physical: its holding and trigger currents emerge from the transistor parameters, so they match the figures of `Nodalys` only approximately. `Nodalys` takes them as parameters and hits them exactly.

---

## 2. Integration

- **The state** is decided in `Stamp()` from the current Newton iterate and kept between the firing and the release, so the converged iterate, which stamps last, is the one `AcceptStep()` keeps. A rejected step restores the accepted state.
- **Landing on the commutation.** While conducting, `NextBreakpoint()` extrapolates the anode current of the last two accepted points to the holding current and returns that instant, so the transient lands a time point on the natural commutation instead of stepping over it. When the current changes, the prediction is wrong and the transient asks again after the next point, so it closes in on the zero, as the fuse closes in on its blow.
- **`HasSwitched()`** is true for the step that fires or releases the latch, so the step that follows restarts with Backward Euler, which damps the stiff modes a switching branch excites.
- **The small-signal analyses** see the conductance of the state at the operating point ($1/R_T$ or $1/R_{off}$); the threshold $V_0$ is a DC source and contributes nothing to AC. **Harmonic balance refuses the device**, as it does the hysteretic switch, because a latch has no smooth periodic steady state.
- **No noise.** The model is a switch, so it adds no noise contribution, as the switch and the fuse.

---

## 3. Verification

`nodalys/tests/devices/thyristor_device_tests.cpp`: blocking forward voltage until the gate fires, latching after the gate pulse ends, release below the holding current, reverse blocking with gate current, forward breakover with and without the supply past it, the on-state line $V_0 + R_T I$, the gate junction current, and the half-wave controlled rectifier.

The rectifier average is checked against its closed form over a steady-state period:

$$\langle v_{load}\rangle = \frac{V_m (1 + \cos\alpha)}{2\pi}$$

for firing angles of 60 and 90 degrees (within 0.05 V), and the turn-off is checked to land on the holding-current crossing $\bigl(\pi - \arcsin(I_H R / V_m)\bigr)/\omega$ (within 0.1 ms), which the predicted breakpoint must hit rather than a step late.

---

## 4. Limits

- The latching current $I_L$ is not modeled separately from $I_H$; the gate sustains conduction until the anode current reaches $I_H$.
- There is no $dv/dt$ firing, no turn-off time $t_q$ (the device blocks again as soon as the current releases), no reverse recovery, and no temperature dependence of the holding current. These are recorded as follow-up work.
- There is no junction capacitance or charge, so the device adds no dynamic state and no noise.
- The state is not kept between runs: every transient starts blocking, and fires when the circuit makes it.
