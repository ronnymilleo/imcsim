# Thyristor Device

The thyristor (SCR) of `imcsim` is a latching switch: it blocks forward voltage until a gate pulse fires it, then stays on with the gate removed until its anode current falls below the holding current. `Nodalys` models it as one native device, [`ThyristorDevice`](../../nodalys/src/devices/semiconductors/thyristor_device.cpp), built with `CircuitBuilder::AddThyristor(name, anode, cathode, gate, ThyristorParameters)`. The `ngspice` companion model is a two-transistor regenerative pair written as the subcircuit `imcsim_scr`, which ships with the schematic part (see the Thyristors section of [`docs/models.md`](../models.md)).

---

## 1. Model

### 1.1 The latch

The state is a single flag, conducting or blocking. With $v_{AK}$ the anode-cathode voltage and $i_G$ the gate current:

- **Firing.** A blocking device starts conducting when $v_{AK} > 0$ and $i_G \ge I_{GT}$, or when $v_{AK} \ge V_{BO}$ (forward breakover, disabled when $V_{BO} = 0$), or when the anode voltage rises faster than the critical $dv/dt$ (disabled at 0), or when a forward voltage is re-applied inside the turn-off recovery window.
- **Holding.** A conducting device keeps conducting while the gate draws at least $I_{GT}$, whatever the anode current; with the gate removed it holds only while $i_A \ge I_H$.
- **Release.** Below $I_H$ with the gate removed, the latch opens. It cannot close again on its own: only a new gate pulse, breakover, a fast voltage rise or the recovery window fires it, so the latch itself is the hysteresis and the device does not chatter at the threshold.

The latching current $I_L$ is not a separate parameter: the gate sustains conduction until the anode current reaches $I_H$, which plays the role of both.

### 1.2 The second-order effects

Two datasheet figures refine the plain latch; both are optional and disabled at zero, and a TRIAC reads them on the magnitude of the main-terminal voltage.

- **Critical $dv/dt$.** A real device fires spuriously when the displacement current through its junction capacitance drives the regenerative pair: a rate of rise of the blocking voltage faster than the critical figure turns it on without a gate. The device reads the slope from the last accepted time point to the Newton iterate, $\dot v = (v_{AK} - v_{accepted}) / (t - t_{accepted})$, and fires when $\dot v \ge (dv/dt)_{crit}$ — on a fall too, for a TRIAC. The detection latches for the rest of the step: once fired this way, the device does not release again until the next time point, so the step that carries the edge converges instead of toggling between the two states.
- **Turn-off time $t_q$.** After a release the device recovers for $t_q$: while that window runs it has not regained its blocking, a forward voltage re-applied inside it fires it again (the commutation failure of a forced-commutated circuit), and the holding-current release is suspended until it closes. The re-fire reads a *rising* voltage: it fires when $v_{AK}$ crosses back above the on-state threshold $V_0$ from at or below it at the last accepted point. The voltage of an accepted on-state is always past $V_0$, so the release itself never re-fires on its own iterate, and the natural commutation of a rectifier — where the voltage stays up while the current falls — still lands.

### 1.3 The branches

The on-state is the datasheet line

$$v_{AK} = V_0 + R_T\, i_A \quad\Longleftrightarrow\quad i_A = \frac{v_{AK} - V_0}{R_T}$$

stamped as the conductance $1/R_T$ between anode and cathode with an equivalent current source $V_0/R_T$ from cathode to anode. The off-state is the conductance $1/R_{off}$. Both are linear at each Newton iterate, so the branch cannot fail to converge; only the discrete state changes.

The gate-cathode junction draws

$$i_G = \begin{cases} (v_{GK} - V_{GT})/R_G & v_{GK} > V_{GT} \\ 0 & v_{GK} \le V_{GT} \end{cases}$$

with a small leakage conductance always tying the gate to the cathode, so a gate left undriven never floats the matrix. The gate current returns through the cathode, as it does in a real device, so a gate drive must be referenced to the cathode.

### 1.4 How it maps to the `ngspice` subcircuit

| `imcsim_scr` (`ngspice`) | `ThyristorDevice` (`Nodalys`) |
| :--- | :--- |
| PNP + NPN regenerative pair, collector of each into the base of the other | The flag `IsConducting`, fired by gate current or breakover |
| Gate resistor into the base of the NPN; holding current set by the base-emitter shunts and the betas | $I_{GT}$, $I_H$, $V_0$, $R_T$ taken as parameters |
| Latch emerges from the loop gain reaching unity | The latch is explicit, so it cannot fail to converge |

The two-transistor model is physical: its holding and trigger currents emerge from the transistor parameters, so they match the figures of `Nodalys` only approximately. `Nodalys` takes them as parameters and hits them exactly. The subcircuit has no breakover, no critical $dv/dt$ and no turn-off time: its fixed junction capacitances soften the switching but fire nothing, and it recovers its blocking as soon as the current releases.

---

## 2. Integration

- **The state** is decided in `Stamp()` from the current Newton iterate and kept between the firing and the release, so the converged iterate, which stamps last, is the one `AcceptStep()` keeps. A rejected step restores the accepted state, the recovery window and the $dv/dt$ latch with it. A new time point clears the $dv/dt$ latch, which holds only for the step whose voltage edge fired it.
- **The accepted history** keeps the time, the main current and the main-terminal voltage of each accepted point: the current feeds the commutation prediction, and the voltage is what the $dv/dt$ slope and the recovery re-fire read.
- **Landing on the commutation.** While conducting, `NextBreakpoint()` extrapolates the anode current of the last two accepted points to the holding current and returns that instant, so the transient lands a time point on the natural commutation instead of stepping over it. When the current changes, the prediction is wrong and the transient asks again after the next point, so it closes in on the zero, as the fuse closes in on its blow.
- **`HasSwitched()`** is true for the step that fires or releases the latch, so the step that follows restarts with Backward Euler, which damps the stiff modes a switching branch excites.
- **The small-signal analyses** see the conductance of the state at the operating point ($1/R_T$ or $1/R_{off}$); the threshold $V_0$ is a DC source and contributes nothing to AC. **Harmonic balance refuses the device**, as it does the hysteretic switch, because a latch has no smooth periodic steady state.
- **No noise.** The model is a switch, so it adds no noise contribution, as the switch and the fuse.

---

## 3. Verification

`nodalys/tests/devices/thyristor_device_tests.cpp`: blocking forward voltage until the gate fires, latching after the gate pulse ends, release below the holding current, reverse blocking with gate current, forward breakover with and without the supply past it, the on-state line $V_0 + R_T I$, the gate junction current, and the half-wave controlled rectifier. The second-order effects have tests of their own: a $10\ \mathrm{V}$ rise in $1\ \mu\mathrm{s}$ fires a device set to a critical $dv/dt$ of $0.5\ \mathrm{V}/\mu\mathrm{s}$ with the gate silent, the same rise spread over $1\ \mathrm{ms}$ does not, and a fall never fires an SCR while a TRIAC fires on a rise and on a fall alike; a supply that collapses and returns inside the $t_q$ window re-fires the device without a gate, and one that returns past the window finds it blocking, for an SCR and for a TRIAC the negative way.

The rectifier average is checked against its closed form over a steady-state period:

$$\langle v_{load}\rangle = \frac{V_m (1 + \cos\alpha)}{2\pi}$$

for firing angles of 60 and 90 degrees (within 0.05 V), and the turn-off is checked to land on the holding-current crossing $\bigl(\pi - \arcsin(I_H R / V_m)\bigr)/\omega$ (within 0.1 ms), which the predicted breakpoint must hit rather than a step late.

---

## 4. Limits

- The latching current $I_L$ is not modeled separately from $I_H$; the gate sustains conduction until the anode current reaches $I_H$.
- The $dv/dt$ firing reads the slope between accepted time points, so an edge shorter than a step fires on the average slope the step sees, and the detection is transient-only: an operating point has no rate of rise.
- The $t_q$ recovery re-fires on the voltage crossing back above $V_0$; there is no charge behind it, no reverse-recovery current, and no re-applied-voltage rating below which the re-fire would not happen. The temperature dependence of the holding current is not modeled either.
- There is no junction capacitance or charge, so the device adds no dynamic state and no noise; the $dv/dt$ firing is a detection on the voltage history, not a stamped capacitance.
- The state is not kept between runs: every transient starts blocking, and fires when the circuit makes it.
