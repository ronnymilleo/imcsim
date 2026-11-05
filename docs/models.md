# Simulation models in imcsim

What each part is simulated with, which parameters it takes, and what it leaves out. The
[user guide](user_guide.md) covers how to read results; this page covers what is behind them.

## How circuits are simulated

imcsim writes the circuit as a SPICE netlist (shown in the Netlist window) and runs it through
[ngspice](https://ngspice.sourceforge.io/), the open-source successor of Berkeley SPICE. Every part maps to SPICE
elements, so the results are those of ngspice with the models below. Some things hold for every part:

- Everything is simulated at 27 °C, unless a stepped analysis steps the temperature: ngspice then adjusts diodes and
  transistors (saturation currents, junction voltages) by their built-in laws, while resistors, capacitors and
  inductors keep their values, since no part sets a temperature coefficient.
- Values are exact: there are no tolerances, aging or Monte Carlo spread; a stepped analysis can try a list of
  values instead.
- In a noise analysis, resistors add thermal noise (4kTR); diodes and bipolar transistors the shot noise of their
  currents, MOSFETs the thermal noise of their channel, and all three the thermal noise of their series
  resistances. Flicker (1/f) noise is off, since no model sets KF.
  Sources, controlled sources, capacitors, inductors and the ideal op-amp add none, and an op-amp macromodel adds
  the noise of its internal parts, which does not follow its datasheet (see below).
- A transfer function analysis linearizes the circuit at its DC operating point and reports the DC gain from an
  independent source to an output node, with the resistance seen by the source and the resistance looking back into
  the output. It is the same small-signal model an AC sweep uses, so the gain agrees with what the sweep would show
  at 0 Hz.
- A sensitivity analysis is ngspice's DC `.sens`: it differentiates the voltage of one node with respect to the value
  of each element at the DC operating point. imcsim lists the value of resistors, capacitors, inductors, sources and
  supply rails, the gain of controlled sources, the two halves of a potentiometer together (the sum of their normalized
  sensitivities, which is the sensitivity to its total resistance), and for a block its most influential element.
  Parts that are several elements in the netlist, such as op-amps, fuses, relays and transformers, and parts with no
  value, such as switches, are not listed. ngspice also reports model parameters of diodes and transistors, but most
  read the same meaningless number or nan, so they are left out. Logic parts are refused, since ngspice cannot recover
  from a sensitivity analysis that holds them. The AC form of `.sens` is not used: ngspice leaves the small-signal
  conductance of forward-biased diodes and transistors out of it, so it is wrong for nonlinear circuits.
- A pole-zero analysis is ngspice's `.pz`: it finds the poles and zeros of the small-signal transfer function from an
  independent source to the voltage of a node, with the circuit linearized at its DC operating point, so a diode or
  transistor contributes its bias-point conductance and capacitances. The input port is the source's terminals, and
  the kind of source sets the transfer: a voltage source gives a voltage gain (ngspice's `vol`) and a current source a
  transimpedance (`cur`), since poles and zeros depend on whether the port is held to a voltage or a current. ngspice
  accepts a voltage source as the input only when it has an AC magnitude, so imcsim sets it to 1 for the run, which
  changes nothing else. The run leaves out the 0 V current probes, whose extra branch equations make the solver lose
  the poles of fast parts. Roots are in rad/s; a pole and a zero that coincide within a thousandth of the larger are
  dropped as cancelling, since ngspice reports them for parts the signal never reaches. Pole-zero relies on ngspice's
  root-finding, which has known failures: it can find nothing without saying why (the ideal op-amp Sallen-Key at
  gain-bandwidth products around 10 kHz to 30 kHz and 500 kHz to 1 MHz, for one; a uA741 or another product works), it
  can stop early with "iteration limit reached", which the tab shows as a warning that some roots may be missing, and
  it can return a wrong list with no warning, such as a ladder of right-half-plane poles for a rectifier at its
  unbiased operating point. `.options` do not help, and imcsim does not retry or work around it. Circuits with logic
  parts are refused, since the digital models leave the solver nothing to follow.
- A truth table runs one operating point per combination of the chosen logic inputs, each on a copy of the circuit
  with the inputs set, so a row never depends on the one before it. That describes combinational logic; a latch or
  flip-flop that holds its state has no operating point for those combinations, and the row reads X.
- To report terminal currents in every analysis, the netlist adds a 0 V source in series with diodes, transistors,
  potentiometers, switches, relays, fuses, transformers, controlled sources, op-amp outputs and the outputs of logic parts. It does not change the circuit; the Netlist window leaves it out unless a
  current-controlled source needs it.

- The power shown on the schematic is what a part absorbs: the sum over its terminals of the node voltage times the
  current into the terminal, at the operating point or at one instant of a transient. It is positive when the part
  dissipates power and negative when it delivers it. An op-amp reports a current only at its output, so its power is
  that of its output stage and leaves out what its supply pins give or take.

## Passive parts

| Part | SPICE element | Value |
|---|---|---|
| Resistor | `R` | resistance, positive |
| Capacitor | `C` | capacitance, positive |
| Inductor | `L` | inductance, positive |
| Potentiometer | two `R` | resistance between its ends, positive |

All three are ideal:

- A resistor has no parasitic inductance or capacitance, no temperature coefficient and no power rating.
- A capacitor has no series resistance (ESR), leakage, voltage rating or dielectric effects. It may carry an initial
  voltage, written as `IC=` on its line: a transient then runs with `uic`, which skips the operating point and starts
  the capacitor from that voltage, so an RC circuit discharges from it. The run applies to the whole circuit: every
  capacitor and inductor without an initial condition then starts at zero. When no part has one, a transient starts
  from the DC operating point and the capacitor is charged to its voltage there.
- An inductor has no winding resistance, core losses or saturation, and is not coupled to other inductors; the
  transformer part below is. It may carry an initial current the same way, which a transient with `uic` starts from; left
  without one, it starts at zero in such a run and from the DC operating point otherwise.
- A potentiometer is two resistors that meet at the wiper, terminal 3: the first end to the wiper is the value times the
  wiper position, and the wiper to the second end is the rest. Position 0 puts the wiper on terminal 1 and position 1
  on terminal 2; at an end the nearest half is 1 mOhm, since SPICE rejects 0 Ohm. The track is linear, so there is
  no log taper, no wiper resistance and no power rating, and the wiper moves only by editing the part, not during a
  run.

### Tolerance and Monte Carlo

A resistor, capacitor, inductor or DC voltage source may carry a tolerance in percent, which does nothing in an
ordinary run: the part is as ideal as before, at exactly its value. In a Monte Carlo analysis each run simulates a copy
of the circuit in which every toleranced part takes its value times a factor drawn within the tolerance, so the
spread of a result comes from the values alone:

- The Gaussian factor is `1 + t * z / 3`, with `t` the tolerance and `z` a normal number drawn again whenever it
  passes 3 in size; the uniform factor is `1 + t * (2u - 1)` with `u` from 0 up to 1.
- The numbers come from a Mersenne Twister (64 bits) started from the seed and the run number, a Box-Muller
  transform and the top 53 bits for `u`, written here instead of taken from the C++ distributions, whose results
  differ between standard libraries. The generator is exact everywhere; the logarithm and cosine of the transform may
  differ from platform to platform in the last digits.
- Only the main value varies: not the initial conditions, the model parameters of diodes and transistors, nor
  the parts inside blocks, which refuse the run. Parts are independent; there is no matching or correlation between
  two resistors of the same lot, and no temperature coefficient.
- ngspice runs one simulation at a time, so the runs are made one after the other.

## Switches and push buttons

A switch and a push button are the same part for the simulation: a resistor, `R-SW1` in the netlist, of 1 mOhm when
closed and 1 TOhm when open. A push button is closed while it is pressed. Both start open.

- The state is set in the properties and holds for the whole run; nothing opens or closes a contact during a
  transient. A contact that follows a voltage is a part of its own.
- The open resistance is high but not infinite, so an open switch leaks a few picoamperes at a few volts. There is
  no bounce, contact capacitance or voltage rating.

## Voltage-controlled switches and relays

A voltage-controlled switch is the SPICE `S` element, with its own `.model` line. It closes when the voltage between its
control pins, terminals 3 (+) and 4 (-), rises past the threshold plus the hysteresis, and opens when it falls below the
threshold minus the hysteresis. The parameters are the threshold (2.5 V), the hysteresis (0.1 V), the closed resistance
(10 mOhm) and the open resistance (1 GOhm); the control pins carry no current.

A relay is a coil and a normally open contact. The coil is its resistance in series with its inductance, written as
`R-K1coil` and `L-K1coil`, and the contact is an `S` element controlled by the voltage on the resistance, which is
the coil current times the resistance. So the contact closes when the current has built up to the pull-in value and
opens when it has died away to the drop-out value, a time of about L over R after the coil is switched, and a flyback
diode across the coil makes the opening later. The figures are the coil resistance (70 Ohm), the coil inductance
(50 mH), the pull-in voltage (3.5 V), the drop-out voltage (1 V) and the contact resistance (50 mOhm); the open
contact is 1 GOhm.

- A relay is a model of the coil and the contact, not of the mechanism: there is no contact bounce, travel time or
  coil heating, and the pull-in and drop-out voltages are steady-state ones.
- A switch or a relay that is open or closed at the operating point stays so in an AC sweep, since the sweep is
  linear around it. Fast switching can fail to converge in a transient; a smaller time step helps.

## Fuses

A fuse is a low resistance that opens for good once the current has run through it long enough. It is written as an
instance of the subcircuit `imcsim_fuse`, defined once in the netlist, and has two parts: a resistance between its
terminals, and a thermal state that decides it. You set the figures a datasheet gives, and the model derives the rest.

- **Rated current** (`Ih`, 1 A): the largest current the fuse carries for ever. It never blows below it.
- **Melting I2t** (`I2t`, 0.15 A2s): the current squared times the time that melts the element in a short overload.
- **Cold resistance** (150 mOhm) and **blown resistance** (1 GOhm).
- The **cooling time constant** `tau = I2t / Ih^2` follows from the two ratings, and the part editor shows it as a
  read-only line (0.15 s for the 1 A fast fuse).

The element heats with the square of the current and loses its heat with the time constant `tau`:
`dT/dt = (i^2 / Ih^2 - T) / tau`, with `T` in units of the melting point, so the fuse melts when `T` reaches 1. In
steady state `T = i^2 / Ih^2`, so a current below the rated one never melts it, and a short overload (much shorter than
`tau`) melts it when the integral of `i^2` over time reaches `Ih^2 tau`, the melting I2t. In between, the time to blow
is `-tau ln(1 - Ih^2 / i^2)`: 0.045 s at 1.96 A for the default fuse. Each overload leaves heat that the fuse loses
again with `tau`, so overloads spaced several time constants apart do not add up, and overloads close together do. In
the netlist `T` is the voltage of a capacitor of 1 F with a resistor of `tau` across it, charged by a behavioral
current source.

When `T` passes 1.015 a second node, the latch, starts to charge. It has no discharge path, so the fuse cannot close
again, and once it is under way it keeps charging by itself, since the open fuse stops carrying current and `T`
falls. The latch sets the resistance, from the cold to the blown value, through an exponential, so the resistance
changes by parts per million where the latch is far from its steps and the transition converges in ngspice. The part
blows when the heat reaches 1.015, a margin above the 1 that the rated current settles at, so a 1 A fuse carries
1 A for ever, and the time to blow and the melting I2t come out about 1.5% longer than the figures above.

- **The branch really opens**: the blown resistance goes into the same run, so the current in the branch and the
  voltages around it change from that instant, and the Output window warns "FU1 blew at 176ms". The instant is read
  from the results as the first point where the voltage across the fuse over its current is above the geometric mean of
  the cold and blown resistances.
- **Every run starts with a whole fuse**: the thermal state is not saved with the file nor kept from one run to the
  next, and there is no replace action. A gate source inside the model is 0 in an operating point, a DC sweep and an
  AC sweep, so they always see a whole fuse, even at a current far above its rating, and rises a nanosecond into a
  transient. A stepped transient or AC sweep starts each step whole. In a live run the fuse blows when the circuit
  makes it, and stays open until the run ends.
- **Inductive loads**: the open fuse cuts an inductor current at once, which the trapezoidal integration of ngspice
  turns into an oscillation that never dies. A circuit with a fuse, in a block or not, therefore writes
  `.options method=gear` once, and the Gear integration settles the node. Gear damps fast ringing a little, so a lightly
  damped resonance in the same circuit loses some of its ring.
- **The ready fuses** (the 100 mA, 500 mA, 1 A and 5 A fast fuses and the 1 A slow-blow) carry typical figures: about
  0.15 s of I2t per A2 of rating for a fast fuse and 1 s for a slow-blow, and cold resistances of 4 Ohm (100 mA) down to
  20 mOhm (5 A). They are not taken from one vendor's datasheet.
- A fuse is a model of the thermal state, not of the arc: there is no breaking capacity, arc voltage or voltage rating,
  no temperature dependence of the cold resistance, and the part carries a current through its probe like a switch.
  In a noise analysis the model adds none.
- The blown state is read from the voltage across the fuse and its current, so a fuse inside a block is simulated, but
  its blow is not shown on the schematic or warned about; the fuses of the main schematic are.

## Transformers

A transformer is two inductors coupled by a SPICE `K` element: `L-T1p` and `L-T1s`, each behind a resistor for its
winding (`R-T1p`, `R-T1s`). Its value is the primary inductance; the secondary's is that divided by the square of the
turns ratio, primary turns over secondary turns, so a 100 mH primary at 2:1 has a 25 mH secondary. The other figures are
the turns ratio (1), the coupling (0.999) and the resistance of each winding (0.1 Ohm). The dotted terminals, the +
ones, move together.

- The coupling must stay below 1, which SPICE rejects. The leakage inductance that a coupling under 1 gives is what
  makes the secondary sag under load.
- There is no core: no saturation, hysteresis, core loss, capacitance between windings or isolation rating. The
  magnetizing current is that of the primary inductance, so a small value draws a large no-load current.
- A DC source across a primary is only limited by the winding resistance, since an inductor passes DC.

## Logic gates and flip-flops

The gates are AND, OR, NOT, NAND, NOR, XOR, XNOR and a buffer, with two inputs each by default (one for NOT and the
buffer; the others take from 2 to 8, set in the part's properties), and the flip-flops are D, JK, T and SR, each with a clock, asynchronous SET and RESET inputs (active high), a Q output
and its inverse QN. They run as the digital code models of ngspice's XSPICE (`d_and`, `d_dff` and the like), which only
meet digital nodes, so each part is written with bridges around it:

- each input goes through an analog-to-digital bridge (`adc_bridge`) that reads a voltage above 70% of the part's
  logic level as 1 and one below 30% as 0, and a voltage between them as unknown;
- each output leaves through a digital-to-analog bridge (`dac_bridge`) that swings between 0 V and the logic level,
  or to half of it while the output is unknown, behind a 50 Ohm resistor that is the output resistance of the family;
- each input also has a 1 GOhm resistor to ground, so an unconnected input, such as the SET and RESET of a flip-flop
  that uses neither, reads 0.

A gate of several inputs is the same code model with a longer input vector: AND is high when every input is, OR when
any is, NAND and NOR are their inverses, XOR is high for an odd number of high inputs (the parity) and XNOR for an even
number. Each input has its own bridge and pull-down, named after its letter (`A`, `B`, `C`...).

The figures of a part are its logic level (5 V) and its propagation delay (10 ns), from every input to every output.
A flip-flop changes on the rising edge of its clock and starts with Q at 0. The currents reported are those of the
outputs: a gate's `U1`, and `U1.Q` and `U1.QN` of a flip-flop, positive into the output, so a part that drives its load
reports a negative current.

- The levels come from the part, not from a supply pin: a gate has no VCC or ground terminal, and drawing power from a
  supply is not modeled. Connecting parts of different logic levels works as the thresholds above say.
- The digital nodes inside a part are not shown. Only the outputs, which are analog voltages, appear in the plots and
  can be probed, so a logic signal is a trace that swings between 0 V and the logic level. The transient plot of the Output window
  stacks them as lanes.
- The parts need the `digital.cm` code model library of ngspice. The Windows zip, the macOS app and the AppImage carry
  it, and a system ngspice loads its own. Without it, simulating a circuit with a logic part stops with a message.
- A loop that holds two states, such as a latch, has no state to start from while both of its inputs are idle, so it
  has no operating point: ngspice gives up with "Too many analog/event-driven solution alternations", which imcsim
  reports as a message. A transient then starts from 0 V on every node instead of the operating point, as a circuit
  does at power-up, and says so with a warning: the loop toggles for a moment and settles into the state that small
  differences in its loads favor. To start it in a known state, assert one input at first, as the SR latch example
  does with its S input.
- Delays are fixed per part: there is no spread between the rise and fall delays, no setup or hold time, and no
  metastability from violating them. Operating point and DC sweep results read the digital outputs as they settle, and an AC sweep treats
  them as constants.

## Clock and logic input

A **clock** drives a digital circuit and is written as a SPICE pulse source from its terminal to ground, like a
supply rail:

```
V1 out 0 DC 0 PULSE(0 <level> <delay> <edge> <edge> <width> <period>)
```

- Its figures are a frequency, a duty cycle, a logic level (5 V) and a start delay. The period is 1 over the
  frequency and the clock swings between 0 V and the logic level.
- The part picks its own rise and fall times, each a fixed 0.1% of the period, so the user
  never sets them and the shape is the same at every frequency. The flat top is the duty share of the period less one
  edge, since half of the rising and half of the falling edge lie above half the level; a duty share smaller than one
  edge is clamped, so the clock then only shows the edge itself. The duty cycle is therefore exactly the share of each
  period the clock reads as high at half the logic level.
- The start delay holds the clock at 0 V, which is also its operating point value, and the first rising edge begins
  there: a delay of 0 puts the first edge at t = 0, while a nonzero one keeps the clock quiet until then. The delay is
  an absolute time, so it does not change with the frequency.
- It is an ideal voltage source: it has no output resistance and delivers any current the circuit asks for. It is not
  an AC source, so an AC sweep is not excited by it and a noise analysis cannot refer to it.

A **logic input** is a constant voltage source from its terminal to ground, written `V1 out 0 DC 5` when high and
`V1 out 0 DC 0` when low. High drives the logic level (5 V by default, editable) and Low drives 0 V; both start low,
so an unconnected net reads 0. It is the natural way to give a gate or a latch a defined level to start from, such as
the SET or RESET input of a flip-flop, and it holds that level for the whole run.

A circuit made only of logic parts, clocks, logic inputs and VCC supplies needs no Ground: the clocks, the inputs,
the supplies and the bridges of the logic parts all refer to node 0 in the netlist, so the reference exists without
the symbol, and the gate outputs need no load. Any other part brings the rule back: a resistor or an analog source
would have no node to return to, so the run stops with "The circuit has no ground".

A **logic output** has no model: it writes no netlist line and only reads the voltage of its node from the result,
against its own logic level, as a gate input would (1 at 70% of the level or above, 0 at 30% or below, unknown in
between). It loads nothing and needs no Ground. A **7-segment display** is the same with one input per segment.

## Independent sources

**Voltage source** (`V`, named Vin) and **current source** (`I`) take one of nine waveforms:

| Waveform | Transient | Operating point and DC sweep | AC sweep |
|---|---|---|---|
| DC | the value | the value | not excited |
| AC | `SIN(offset amplitude frequency)` | the offset | the AC magnitude |
| Pulse | `PULSE(low high delay rise fall width period)` | the low level | not excited |
| EXP | `EXP(v1 v2 td1 tau1 td2 tau2)` | v1, the value it starts from | not excited |
| SFFM | `SFFM(vo va fc mdi fs)` | vo, the offset | not excited |
| AM | `AM(vo 0 va mf fc td)` | vo, the offset | not excited |
| PWL | `PWL(t1 v1 t2 v2 ...)` | v1, the first point | not excited |
| Noise | `TRNOISE(na nt alpha namp rtsam capt emt)` | the offset | not excited |
| Random | `TRRANDOM(type ts td p1 0)` | the offset or mean | not excited |

- The AC magnitude is set apart from the sine amplitude, as in SPICE, and is 1 by default so a Bode plot reads as
  gain. The sine has no phase, delay or damping.
- Every waveform writes the value it starts from as the DC value, so the operating point and a DC sweep match the
  beginning of the transient, as the low level of a pulse does.
- The exponential `EXP(v1 v2 td1 tau1 td2 tau2)` starts at v1, rises towards v2 with time constant tau1 from td1 on,
  then decays back towards v1 with time constant tau2 from td2 on. It defaults to 5 V (1 mA for a current source),
  rising from 1 ms to a 0.5 ms time constant and falling from 5 ms with a 1 ms time constant, all within the default
  10 ms transient.
- The single-frequency FM `SFFM(vo va fc mdi fs)` is vo plus va times a carrier at fc whose phase swings by mdi
  times a sine at fs. It defaults to a 1 kHz carrier of 1 V (1 mA) with a modulation index of 5 and a 100 Hz signal.
- The amplitude modulation `AM(vo 0 va mf fc td)` is vo plus va times a sine at mf times a sine at fc, both from td
  on, and holds 0 until td. The `0` is the modulation offset of ngspice, so the carrier is suppressed and the
  waveform is the offset plus the two sines; a nonzero offset vo shows the carrier on a level instead, and is also
  the operating point value. It defaults to 0 V and 1 V (1 mA), a 100 Hz modulation on a 1 kHz carrier without
  delay.
- The piecewise linear `PWL(t1 v1 t2 v2 ...)` holds v1 before t1, joins the points with straight lines and holds the
  last value after the last point. It defaults to `0 0, 1m 5, 2m 0` (1 mA instead of 5 for a current source). The
  points are edited as one text of "time value" pairs in the part editor; at least two are needed, times are not
  negative and strictly increase.
- The noise source `TRNOISE(na nt alpha namp rtsam capt emt)` is `DC offset` plus transient noise: Gaussian white
  noise of rms na with a new sample every nt (the output is a straight line between samples, so its rms over time is
  sqrt(2/3) of na and its density is na times sqrt(2 nt) per root hertz, flat up to 1 / (2 nt)), 1/f noise of
  amplitude namp with the exponent alpha, and a telegraph signal between 0 and rtsam with mean capture and emission
  times capt and emt. A part whose amplitude is 0 is written as 0, and nt is 0 when the white and 1/f parts are both
  off, which ngspice then ignores. nt must be positive otherwise: a negative one makes ngspice loop forever, so it never
  reaches it, and alpha must be between 0 and 2. It defaults to 100 mV rms (100 uA for a current source) at 10 us, with
  the 1/f and telegraph parts off. ngspice 47 does not seed the white and 1/f noise, so they change on each run; the
  telegraph part follows the seed.
- The random source `TRRANDOM(type ts td p1 0)` holds a value for ts, from td on: type 1 is uniform (p1 the range
  either side of the offset), 2 Gaussian (p1 the standard deviation), 3 exponential (p1 the mean) and 4 Poisson (p1
  lambda). The offset or mean is the DC value of the source, since ngspice adds that to what TRRANDOM gives, and it is
  what the source holds before td. It defaults to a Gaussian with 100 mV at 10 us. The seed option makes it repeat.
- Both stop the transient at every sample, so a transient may not span more than a million points counting the
  finest step of a noise or random source, and a live run's stop time is cut to keep within it. The Seed of the
  transient settings is written as `.options seed=N` only when it is not 0. ngspice draws the Gaussian values in
  pairs and keeps the second across runs, so a seeded Gaussian random source repeats only every other run in one
  session; the uniform, exponential and Poisson ones, and the telegraph signal, repeat every run.
- Both sources are ideal: a voltage source has no internal resistance and a current source no output resistance,
  so they deliver any current or voltage the circuit asks for.
- A current source pushes its current from terminal 1 to terminal 2 through itself, into the node at terminal 2.

**VCC** is a DC voltage source from its terminal to ground, with the same limits. **Ground** is node 0, the
reference of every voltage; it is not an element and carries no current of its own. A **net label** is no element
either: it only joins the nodes of every point with the same name, compared without regard to case, so the netlist
has no line for it and the nodes are numbered as if those points were wired together. A net named `0` or `GND` is ground, and one named `VCC` joins every VCC supply, so two supplies of different
voltages must not share that name.

## Controlled sources

| Part | SPICE element | Output | Gain unit |
|---|---|---|---|
| VCVS | `E` | voltage = gain x control voltage | V/V |
| VCCS | `G` | current = gain x control voltage | A/V |
| CCCS | `F` | current = gain x control current | A/A |
| CCVS | `H` | voltage = gain x control current | V/A |

They are linear and ideal: no output resistance, no input loading on the control pins, no bandwidth limit and no
saturation, so their output can reach any value. A current-controlled source follows a current through a voltage
source, as ngspice requires: the source itself for a voltage source or VCC, or a 0 V probe that imcsim adds in
series with the controlling part.

## Diodes, Zener diodes and LEDs

All three use the SPICE diode model (`D`), with these parameters:

| Parameter | Meaning |
|---|---|
| IS | saturation current |
| N | emission coefficient |
| RS | series resistance |
| BV, IBV | reverse breakdown voltage, and the current at which it is reached |
| CJO, VJ, M | zero-bias junction capacitance, junction potential and grading coefficient |
| TT | transit time, which sets the reverse recovery charge |
| IF max, P max | datasheet average forward current and power dissipation, which are not part of the SPICE model |

Ready models:

| Kind | Models |
|---|---|
| Diode | 1N4148 (small-signal switching, 100 V), 1N4007 (rectifier, 1000 V 1 A), 1N5819 (Schottky, 40 V 1 A) |
| Zener | 5V1, 3V3, 12V (named after their breakdown voltage) |
| LED | Red (1.8 V at 20 mA), Green (2.1 V at 20 mA), Blue (3.0 V at 20 mA) |

Any of them can switch to **Custom** and edit the parameters, starting from the model it replaces.

- A Zener regulates at BV in reverse. For the other diodes and LEDs, BV is their rating: the simulation still
  conducts in breakdown, as ngspice does, and imcsim warns when the reverse voltage reaches it.
- The ratings (forward current, power) are checked after each simulation, as for transistors: the forward current
  and the power on their average over the run (the forward current is the datasheet IF(AV), so a rectifier that peaks
  above it into a capacitor is fine; only the forward part of the current counts), the voltages and currents of
  transistors at their peak, with the same warnings. A Zener regulating in reverse is judged
  on power alone, since its rating is the dissipation. A diode imported from a library has no ratings. The ready
  models carry the datasheet values (for example 200 mA and 500 mW for the 1N4148, 1 A and 3 W for the 1N4007, 30 mA
  and 100 mW for the red LED), and a custom diode starts from those of the model it replaces; files saved before the
  ratings existed take them from the default model.
- An LED is a diode whose parameters give its forward voltage; light output is not modeled.
- Not modeled: temperature, forward recovery and package parasitics. The gradual knee of a real Zener at low
  current is only approximated, through BV and IBV.

## Bipolar transistors

NPN and PNP transistors use the SPICE Gummel-Poon model (`Q`), with these parameters:

| Parameter | Meaning |
|---|---|
| IS | saturation current |
| BF, BR | forward and reverse current gain |
| VAF | forward Early voltage (0 turns the Early effect off) |
| IKF | knee current where the gain rolls off at high current (0 turns it off) |
| ISE, NE | base-emitter leakage, which lowers the gain at low current |
| RB, RC, RE | base, collector and emitter resistances |
| CJE, CJC | base-emitter and base-collector junction capacitances |
| TF | forward transit time, which sets the high-frequency limit |

Ready models: 2N3904, 2N2222A and BC547B (NPN), 2N3906, 2N2907A and BC557B (PNP). Custom parameters start from the
model they replace.

- The effective gain depends on the current: with ISE, a 2N3904 between 0.7 and 1.4 mA has a gain of about 140 to
  150, well below its BF of 416.
- The datasheet ratings (VCEO, IC max, P max) are not part of the SPICE model: imcsim checks the results against
  them and warns. Voltages and currents are checked at their peak, power on its average over the run.
- Gummel-Poon parameters not listed above (reverse Early voltage, reverse knee current, base-collector leakage,
  excess phase, substrate capacitance) stay at their ngspice defaults, which turn them off. Temperature and
  quasi-saturation, which Gummel-Poon does not describe, are not modeled.

## MOSFETs

N- and P-channel MOSFETs use the SPICE level 1 model (`M`), the Shichman-Hodges square law, with these parameters:

| Parameter | Meaning |
|---|---|
| VTO | threshold voltage, negative for P-channel |
| KP | transconductance parameter; the drain current is KP/2 x W/L x (VGS - VTO)² in saturation |
| LAMBDA | channel-length modulation |
| RD, RS | drain and source resistances |
| CGSO, CGDO | gate-source and gate-drain overlap capacitances |
| W, L | channel width and length |

Ready models: 2N7000, BS170 and IRF540N (N-channel), BS250 and IRF9540N (P-channel).

- The body is tied to the source, as in a discrete MOSFET, so there is no body effect.
- The body diode is the level 1 drain-body junction with ngspice defaults: it conducts in reverse, but drops about
  0.9 V at 30 A where an IRF540N drops about 1.3 V at 16 A, and has no reverse recovery.
- Like transistors, the ratings (VDS, VGS, ID, P max) are checked after each simulation, except for a model imported
  from a library (see [Blocks and library models](#blocks-and-library-models)).
- Not modeled: subthreshold conduction, velocity saturation and other short-channel effects, the nonlinear gate
  capacitances of power MOSFETs, and temperature. Level 1 is fine for switching and biasing studies, rough for
  precise analog work.

## Op-amps

Terminals: output, non-inverting input (+), inverting input (-), V+ and V-. An op-amp is either ideal or a Boyle
macromodel.

### Ideal

A transconductance drives a resistor and a capacitor, then a unity-gain buffer drives the output:

- 100 dB of open-loop gain, with one pole set by the gain-bandwidth product (GBW, 1 MHz by default).
- No input current and no output resistance.
- The output saturates within about 10 mV of the V+ and V- pins, through two sharp diodes that clamp the internal
  node. The supply pins carry only that clamp current, while the output saturates.

It has no slew rate, offset or current limit, and its output current returns through ground rather than through
the supply pins. It is noiseless: its internal resistor is marked `noisy=0`, and only the leakage of its clamp diodes
remains, picovolts per root hertz, which the noise analysis lists at 0.00%.

### Boyle macromodel (uA741 and Custom)

The macromodel of G. Boyle, B. Cohn, D. Pederson and J. Solomon (IEEE Journal of Solid-State Circuits, 1974), the
basis of most SPICE op-amp models from vendors for decades:

1. **Input stage**: an NPN differential pair with resistive loads and a tail current. It sets the input bias
   current, and with the compensation capacitor, the slew rate and the gain-bandwidth product.
2. **Gain stage**: a transconductance into a 100 kΩ resistor, with a 30 pF Miller capacitor, plus a second
   transconductance from the common-mode voltage that sets the CMRR.
3. **Output stage**: a transconductance into the output resistance, and the voltage and current limiters of Linear
   Technology's macromodels, which steal drive from the gain stage instead of clamping the output with large
   internal currents.
4. **Supply current**: a current source between the supply pins draws the rest of the quiescent current.

Its element values are worked out from datasheet specifications with the equations of Linear Technology's
application note AN48:

| Specification | Sets |
|---|---|
| Slew rate | tail current, IEE = SR x C2 |
| Gain-bandwidth product | load resistances, RC = 1 / (2π x GBW x C2) |
| Input bias current | transistor gain, β = IEE / 2 / IB |
| Phase margin | excess-phase capacitor, C1 = C2 / 2 x tan(90° - PM) |
| Open-loop gain | second-stage gain, GB = Avol / (GA x R2 x RO2) |
| CMRR | common-mode transconductance, GCM = GA / CMRR |
| Output resistance | RO2 |
| Short-circuit current | threshold of the current limiter |
| Headrooms | how close the output gets to V+ and V- |
| Supply current | the quiescent current source |

**uA741** uses typical datasheet values (106 dB, 1 MHz, 0.5 V/µs, 80 nA of bias current, 90 dB of CMRR, 75 Ω,
25 mA, an output 1 V short of each supply, 1.7 mA of supply current) and an assumed 60° of phase margin. Simulated, it gives
105.9 dB, 0.505 V/µs, 13.98 V of swing from ±15 V into 10 kΩ and 24.7 mA into a short. **Custom** takes the same
specifications from any datasheet.

Limitations:

- Its GBW comes out about 10% low, since the equations leave out the second pole that sets the phase margin.
- The input pair is NPN: parts with a PNP input (LM358, whose input range includes the negative supply) or a JFET
  input (TL072) are only approximated.
- Not modeled: input offset voltage and current, temperature, the common-mode input range, power supply
  rejection, more than two poles, and an output current drawn from the supply pins (it returns through ground).
- Noise comes from its input transistors and internal resistors, so it has the right shape (shot and thermal noise
  of the input pair) but not the voltage and current noise densities of a datasheet.

## Blocks and library models

- A block is written as a `.subckt` with one X line per instance. Its node numbers are its own, so two blocks or two
  instances never share an inner node, but node 0 inside a block is the ground of the whole circuit. Its parameters
  go on its `.subckt` line as `params:`, with their defaults, and an instance writes only the values it overrides.
- The `.model` lines inside a block are renamed after it (`imcsim_RC_LOWPASS_...`), since ngspice keeps model names
  global and two blocks with a custom part of the same name would otherwise share one model.
- A block kept as SPICE text is written as it came from its library, between a `.subckt` line with its own ports and
  `.ends`; imcsim reads none of it, so it simulates whatever the vendor modeled, and only what ngspice understands.
- The ready logic blocks are ordinary blocks of the gates and flip-flops above, at a 5 V logic level and a 10 ns
  propagation delay per part. They inherit the timing of their parts: the 4-bit counter is a ripple counter, so its
  outputs change one after another and pass through other values for a few tens of nanoseconds, which a bus lane shows
  as a short sliver; the adder's carry ripples through its four stages in the same way.
- A library model is written once as `LIB_` and its name, which no ready model uses. imcsim does not read its
  parameters: a part with one gets no rating warnings, since a `.model` holds no datasheet ratings, and a MOSFET with
  one takes its channel width and length from the model, or the ngspice defaults, instead of the W and L of the
  custom fields. An LED with a library model glows a warm white, since the model says nothing of its color.

## References

- ngspice manual: [ngspice.sourceforge.io/docs.html](https://ngspice.sourceforge.io/docs.html), for every SPICE
  element and model parameter above.
- G. Boyle, B. Cohn, D. Pederson, J. Solomon, "Macromodeling of Integrated Circuit Operational Amplifiers", IEEE
  Journal of Solid-State Circuits, vol. SC-9, no. 6, December 1974.
- W. Jung, "Using the LTC Op Amp Macromodels", Linear Technology Application Note 48, 1991.
- B. Baker, "Operational Amplifier Macromodels: A Comparison", Burr-Brown Application Bulletin AB-046 (TI SBOA027),
  1993.
