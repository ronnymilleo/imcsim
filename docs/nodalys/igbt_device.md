# IGBT Device

The IGBT of `imcsim` is the MOS-gated bipolar switch of power converters. `Nodalys` models it as one native device,
[`IgbtDevice`](../../nodalys/src/devices/semiconductors/igbt_device.cpp), with the capacitances of its datasheet and its
optional co-packaged diode around it, built with `CircuitBuilder::AddIgbt(name, collector, gate, emitter,
IgbtParameters)`. Every figure is one a datasheet gives. `ngspice` does not run it: the backend refuses the part, and
the Netlist window shows it as an instance of the `imcsim_igbt` subcircuit, an N-channel MOSFET driving a PNP, which no
run checks (see the IGBTs section of [`docs/models.md`](../models.md)).

---

## 1. Model

### 1.1 Conduction

The collector current is the smaller of two limits. The channel saturates at

$$I_{ch} = \frac{K}{2}\, v_{ov}^2, \qquad K = \frac{g_{fs}^2}{2 I_{gfs}}, \qquad v_{ov} = w_g \ln\!\left(1 + e^{(V_{GE} - V_{GE(th)})/w_g}\right)$$

so its slope at the test current $I_{gfs}$ is the transconductance $g_{fs}$ of the datasheet, and the softplus of width
$w_g = 50$ mV opens it smoothly at the threshold. The conducting bipolar part follows the on-state line of the datasheet,

$$I_{line} = \frac{w_k}{R_{CE}} \ln\!\left(1 + e^{(V_{CE} - V_0)/w_k}\right), \qquad w_k = 50 \text{ mV}$$

which is $(V_{CE} - V_0)/R_{CE}$ above the knee $V_0$ and fades to nothing below it. The current is their smooth minimum,

$$I_C = \frac{I_{line}\, I_{ch}}{\left(I_{line}^m + I_{ch}^m\right)^{1/m}}, \qquad m = 4$$

which follows the smaller one, is zero when either is, and keeps a continuous slope, as Newton and the harmonic balance
need. A gate driven hard on leaves the line in charge, $V_{CE(sat)} = V_0 + R_{CE} I_C$; a gate near its threshold leaves
the saturated channel in charge, the active region. A leakage of $10^{-9}$ S from collector to emitter and from gate to
emitter (the IGES of a datasheet) stands for the off-state and keeps an undriven gate from floating the matrix.

### 1.2 Capacitances

The datasheet gives $C_{ies} = C_{ge} + C_{gc}$, $C_{oes} = C_{ce} + C_{gc}$ and $C_{res} = C_{gc}$, measured at one
collector voltage. The compiler ([`igbt_macromodel.cpp`](../../nodalys/src/compiler/macromodels/igbt_macromodel.cpp))
adds the three as constant capacitors around the device. The gate-collector one makes the Miller plateau: while the
collector swings, the gate current goes into $C_{gc}$ and the gate holds where the channel carries the load current,
$V_{GE(th)} + \sqrt{2 I_C / K}$.

### 1.3 Tail current

When the channel turns off, the charge stored in the drift region keeps a current flowing for a while. The device keeps
a stored current $I_q$ that follows the conduction $I_{st}$ up at once and decays toward it with the tail time constant
$\tau$:

$$I_q(t_{n+1}) = I_{st} + \left(I_q(t_n) - I_{st}\right) e^{-h/\tau} \quad \text{when } I_{st} < I_q(t_n), \qquad I_q = I_{st} \text{ otherwise}$$

While $I_q$ is above the conduction, a share $\alpha$ of the difference flows as the tail,

$$I_{tail} = \alpha\, e^{-h/\tau} \left(I_q(t_n) - I_{st}\right) \cdot \tfrac{1}{2}\left(1 + \tanh\frac{V_{CE}}{0.1\text{ V}}\right)$$

which only flows out of a forward-biased device. After a sudden turn-off from $I$ the tail starts at $\alpha I$ and decays
with $\tau$, so it carries the charge $\alpha I \tau$. The stored current moves only when a transient accepts a point,
so a rejected step leaves it alone; the other analyses see no tail.

### 1.4 Newton limiting

The knee of the line is an exponential below $V_0$, and a saturated channel leaves the linearization almost flat, so
Newton could throw the collector far past the knee and back. `IgbtDevice::Limit()` limits the collector voltage above
the knee, $u = V_{CE} - V_0$: a large swing as SPICE limits the drain voltage of a MOSFET (`DEVlimvds`), and a step that
starts within 0.25 V of the knee logarithmically, as SPICE limits a junction (`pnjlim`), up or down. Far below the knee
the device only leaks, and a reverse voltage falls freely.

### 1.5 The co-packaged diode

`IgbtParameters::AntiParallelDiode` adds the native diode from emitter to collector. `imcsim` builds it from the forward
voltage at the rated current: 30 % of the drop is the series resistance and the rest the junction, with an emission
coefficient of 1.5, and its breakdown voltage is far beyond any rating, as neither the diode nor the IGBT models
avalanche.

---

## 2. Integration

The device reports the collector, gate and emitter currents as `Q1.C`, `Q1.G` and `Q1.E`, each the sum of the device,
the capacitors and the diode on that terminal. It runs in every analysis: the small-signal ones linearize it at the
operating point, and the harmonic balance takes it, since it has no latch. It adds no noise.

---

## 3. Verification

[`igbt_device_tests.cpp`](../../nodalys/tests/devices/igbt_device_tests.cpp) checks the on-state line at 10 A, the
blocking below threshold, the channel current with one volt of overdrive, the gate at the Miller plateau when the
collector crosses half the supply, the tail charge $\alpha I \tau$ within 25 % against a part without a tail, the diode,
a harmonic balance, the refused figures and the operating point across supplies, loads, gate drives and the diode, which
once failed to converge near the knee. [`tests/igbt_tests.cpp`](../../tests/igbt_tests.cpp) checks the part in `imcsim`
on the backend, and the IGBT chopper example its waveforms.

---

## 4. Limits

- The capacitances are constant: the datasheet curves that fall with the collector voltage, which shape the switching
  of a real part, are not modeled.
- The tail is a first-order lag on the current, without the dependence on the voltage and the temperature of a real
  part, and the turn-on has no current overshoot of its own.
- There is no avalanche, no latch-up, no short-circuit withstand and no temperature dependence; the reverse blocking is
  the leakage alone, without breakdown.
