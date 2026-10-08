# imcsim user guide

How to read what imcsim measures, plots and exports. The [README](../README.md) covers building and running.

## Terminals

Every part with more than one terminal numbers them in the order the SPICE netlist lists them. Check **Pins** in
the editor toolbar to show the numbers, each beside a small ring at its terminal.

| Part | Terminal 1 | Terminal 2 | Terminal 3 |
|---|---|---|---|
| Resistor, capacitor, inductor | left end, before rotating | right end | |
| Diode, Zener diode, LED | anode | cathode | |
| Voltage source | positive (+) | negative (-) | |
| Current source | tail of its arrow | head of its arrow | |
| NPN or PNP transistor | collector | base | emitter |
| N- or P-channel MOSFET | drain | gate | source |

Ground and the VCC rail have a single terminal and no number.

Rotating a part (R) or mirroring it (M, Shift+M) moves its terminals with it, so terminal 1 is not always on the
left or at the top. Pins shows where it ended up.

## Current signs

imcsim reports currents as ngspice computes them:

- **Two-terminal parts** report one current, positive when it flows into terminal 1, through the part, and out of
  terminal 2. A resistor between nodes 1 and 2 reports a positive current when node 1 is at the higher voltage and
  terminal 1 sits on node 1.
- **Current sources** follow the same rule: a source of 2 mA reports +2 mA and pushes that current out of
  terminal 2, into the node there.
- **Voltage sources and the VCC rail** also follow it, which is the SPICE convention: while a source delivers power,
  current leaves it through its positive terminal, so it reports a **negative** current. A 5 V supply feeding
  2.5 mA into a circuit reports -2.5 mA.
- **Transistors** report one current per terminal, `I(Q1.C)`, `I(Q1.B)` and `I(Q1.E)`, each positive into its
  terminal. The three always add up to zero.

On the schematic, a measured current is an arrow on the lead of its terminal, pointing into the part: the direction
the current flows when its trace is positive. If a trace comes out negative, the current flows against the arrow.

## Measuring

Plots start empty; pick what to measure in either place:

- **Probe tool** (Probe button or P): click a wire to measure the voltage of its node, a part to measure its current,
  or a transistor near one of its terminals to measure that terminal's current. Clicking a measured item again stops
  measuring it.
- **Trace list** in the Output window: check voltages `V(n)` and currents `I(name)`, or use All and None.

Measured items are marked on the schematic, in the colors of their traces:

- a dot on the first wire of a node for each measured voltage, labeled `V(n)` above or left of the wire;
- an arrow on a terminal lead for each measured current, labeled `I(name)` below or right of it.

The trace list is the only control for what the plots show; clicking a legend entry does nothing, so the plots,
their statistics and the exports always agree.

## Plots

- Voltages are solid lines on the left axis, in the color of their node, the same color as in the editor.
- Currents are dashed lines on the right axis.
- Math channels are heavier lines; volts and amperes share the axes above, any other unit or none goes on a third
  axis.
- The AC sweep is a Bode plot: magnitude in dB and phase in degrees, relative to the AC sources, so a source of
  1 V amplitude reads as gain.
- A stepped DC sweep draws one curve per step, labeled with the step value where the curves part most.

## Exports

**Plots**: Export... on the right of the Output window tabs saves the plot of the current tab.

- **SVG image**: redrawn from the data, not captured from the screen, so it stays sharp at any size. It shows the
  visible X range, so zoom in first to export a detail. Choose its size in pixels and its theme: light for print,
  which darkens trace colors too light for white paper, or dark, as on screen.
- **CSV table**: every sample of every measured trace, including math channels, whatever the zoom. The first column
  is the X axis; each header carries its unit, and on a Bode plot its panel (`Magnitude V(2) (dB)`). Missing
  samples, such as a division by zero in a math channel, are empty cells.

**Schematic**: Export... in the editor toolbar saves the circuit as an SVG image, cropped around it, with the symbol
style shown (IEC or ANSI), part names and values, terminal numbers when Pins is on, and the measured voltages and
currents. Light and dark themes darken colors the same way as plot exports, so a probe and its trace keep matching
colors across the two figures.

The save dialogs suggest a name after the schematic file and the tab, such as `rc_filter-transient.svg` or
`rc_filter-schematic.svg`, next to the schematic.
