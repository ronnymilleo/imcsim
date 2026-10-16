# Simulation models in imcsim

What each part is simulated with, which parameters it takes, and what it leaves out. The
[user guide](user_guide.md) covers how to read results; this page covers what is behind them.

## How circuits are simulated

imcsim writes the circuit as a SPICE netlist (shown in the Netlist window) and runs it through
[ngspice](https://ngspice.sourceforge.io/), the open-source successor of Berkeley SPICE. Every part maps to SPICE
elements, so the results are those of ngspice with the models below. Some things hold for every part:

- Everything is simulated at 27 °C. Models have no temperature dependence that you can change.
- Values are exact: there are no tolerances, aging or Monte Carlo spread.
- There is no noise analysis, so no part models noise.
- To report terminal currents in every analysis, the netlist adds a 0 V source in series with diodes, transistors,
  potentiometers, switches, relays, transformers, controlled sources and op-amp outputs. It does not change the circuit; the Netlist window leaves it out unless a
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
- A capacitor has no series resistance (ESR), leakage, voltage rating or dielectric effects. A transient starts
  from the DC operating point, so a capacitor starts charged to its voltage there.
- An inductor has no winding resistance, core losses or saturation, and is not coupled to other inductors; the
  transformer part below is.
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

## Independent sources

**Voltage source** (`V`, named Vin) and **current source** (`I`) take one of three waveforms:

| Waveform | Transient | Operating point and DC sweep | AC sweep |
|---|---|---|---|
| DC | the value | the value | not excited |
| AC | `SIN(offset amplitude frequency)` | the offset | the AC magnitude |
| Pulse | `PULSE(low high delay rise fall width period)` | the low level | not excited |

- The AC magnitude is set apart from the sine amplitude, as in SPICE, and is 1 by default so a Bode plot reads as
  gain. The sine has no phase, delay or damping.
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
the supply pins.

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
- Not modeled: input offset voltage and current, noise, temperature, the common-mode input range, power supply
  rejection, more than two poles, and an output current drawn from the supply pins (it returns through ground).

## References

- ngspice manual: [ngspice.sourceforge.io/docs.html](https://ngspice.sourceforge.io/docs.html), for every SPICE
  element and model parameter above.
- G. Boyle, B. Cohn, D. Pederson, J. Solomon, "Macromodeling of Integrated Circuit Operational Amplifiers", IEEE
  Journal of Solid-State Circuits, vol. SC-9, no. 6, December 1974.
- W. Jung, "Using the LTC Op Amp Macromodels", Linear Technology Application Note 48, 1991.
- B. Baker, "Operational Amplifier Macromodels: A Comparison", Burr-Brown Application Bulletin AB-046 (TI SBOA027),
  1993.
