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
- To report terminal currents in every analysis, the netlist adds a 0 V source in series with diodes, transistors,
  potentiometers, switches, relays, transformers, controlled sources, op-amp outputs and the outputs of logic parts. It does not change the circuit; the Netlist window leaves it out unless a
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

The gates are AND, OR, NOT, NAND, NOR, XOR, XNOR and a buffer, with two inputs each (one for NOT and the buffer), and
the flip-flops are D, JK, T and SR, each with a clock, asynchronous SET and RESET inputs (active high), a Q output
and its inverse QN. They run as the digital code models of ngspice's XSPICE (`d_and`, `d_dff` and the like), which only
meet digital nodes, so each part is written with bridges around it:

- each input goes through an analog-to-digital bridge (`adc_bridge`) that reads a voltage above 70% of the part's
  logic level as 1 and one below 30% as 0, and a voltage between them as unknown;
- each output leaves through a digital-to-analog bridge (`dac_bridge`) that swings between 0 V and the logic level,
  or to half of it while the output is unknown, behind a 50 Ohm resistor that is the output resistance of the family;
- each input also has a 1 GOhm resistor to ground, so an unconnected input, such as the SET and RESET of a flip-flop
  that uses neither, reads 0.

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

A **clock** drives a digital circuit and is written as a SPICE pulse source from its two terminals:

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

## Independent sources

**Voltage source** (`V`, named Vin) and **current source** (`I`) take one of seven waveforms:

| Waveform | Transient | Operating point and DC sweep | AC sweep |
|---|---|---|---|
| DC | the value | the value | not excited |
| AC | `SIN(offset amplitude frequency)` | the offset | the AC magnitude |
| Pulse | `PULSE(low high delay rise fall width period)` | the low level | not excited |
| EXP | `EXP(v1 v2 td1 tau1 td2 tau2)` | v1, the value it starts from | not excited |
| SFFM | `SFFM(vo va fc mdi fs)` | vo, the offset | not excited |
| AM | `AM(vo 0 va mf fc td)` | vo, the offset | not excited |
| PWL | `PWL(t1 v1 t2 v2 ...)` | v1, the first point | not excited |

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

Ready models:

| Kind | Models |
|---|---|
| Diode | 1N4148 (small-signal switching, 100 V), 1N4007 (rectifier, 1000 V 1 A), 1N5819 (Schottky, 40 V 1 A) |
| Zener | 5V1, 3V3, 12V (named after their breakdown voltage) |
| LED | Red (1.8 V at 20 mA), Green (2.1 V at 20 mA), Blue (3.0 V at 20 mA) |

Any of them can switch to **Custom** and edit the parameters, starting from the model it replaces.

- A Zener regulates at BV in reverse. For the other diodes and LEDs, BV is their rating: the simulation still
  conducts in breakdown, as ngspice does, and imcsim warns when the reverse voltage reaches it.
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
- Like transistors, the ratings (VDS, VGS, ID, P max) are checked after each simulation.
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

## References

- ngspice manual: [ngspice.sourceforge.io/docs.html](https://ngspice.sourceforge.io/docs.html), for every SPICE
  element and model parameter above.
- G. Boyle, B. Cohn, D. Pederson, J. Solomon, "Macromodeling of Integrated Circuit Operational Amplifiers", IEEE
  Journal of Solid-State Circuits, vol. SC-9, no. 6, December 1974.
- W. Jung, "Using the LTC Op Amp Macromodels", Linear Technology Application Note 48, 1991.
- B. Baker, "Operational Amplifier Macromodels: A Comparison", Burr-Brown Application Bulletin AB-046 (TI SBOA027),
  1993.
