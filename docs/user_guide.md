# imcsim user guide

How to read what imcsim measures, plots and exports. The [README](../README.md) covers building and running, and
[Simulation models](models.md) the model behind every part.

## Editing

- **Toolbar**: Select, Wire (W) and Probe (P) on the left, then the parts, each button showing its symbol. Sources,
  diodes, transistors and controlled sources share one button per group: it places the part last picked, and the
  arrow beside it (or a right click) lists the others. Hover a button for its name and shortcut.
- **Editing a part**: double-click it, or select it and press Enter, to open its properties beside it; edits
  apply as you type, and Enter, Esc or a click outside closes them. With a part selected, typing a number starts
  editing its main value right away: `4k7` and Enter make a resistor 4.7k. Values take SPICE suffixes, and a whole
  number may put its decimals after the suffix, as resistor codes do (`4k7`, `2M2`, `3u3`). View > Properties keeps
  the same fields in a docked window.
- **Right click**: on a part, its properties, rotate, mirror, flip, measure its current and delete; on a wire,
  measure its voltage and delete; on empty canvas, find a part, run and fit.
- **Fit**: the frame button in the toolbar, or Home, shows the whole schematic.
- **Find a part**: press Space over the editor, type part of a name (`mos`, `zener`, `opamp`, `nmos`, `vcvs`), move
  with Up and Down, and press Enter to start placing it.
- **Running**: the ▶ button at the end of the toolbar, or F5, runs the analysis named beside it. That button opens
  the list of analyses (operating point, transient, AC sweep, DC sweep) and the settings of the one picked, saved
  with the schematic. Suggest fills the transient times (five periods of the slowest sine or pulse source, 200
  points in a period of the fastest) or the AC range (two decades past the RC, RL and LC corners and the op-amp
  bandwidths). Nothing runs by itself: edit the circuit, then run again. View > Analysis Settings keeps the same
  settings open in a window.
- **Status bar**: below the schematic, the keys of the current mode on the left; on the right, the grid position of
  the cursor and how the last run went ("Transient done", with warnings, or failed). Click the result for the
  errors, the warnings and the ngspice output.
- **Menus**: File (files, examples and the schematic export), Edit (undo, redo and changes to the selection) and
  View (fit, wire colors, symbol style, terminal numbers, theme, and the windows). The View choices are kept between
  sessions. On macOS the shortcuts use Cmd where this guide says Ctrl: Cmd+S saves, Cmd+Z undoes.
- **Themes**: View > Theme switches between nine themes, seven dark (Graphite, the default, Ember, Phosphor,
  Oscilloscope, Cyber Slate, Blueprint and Amber) and two light (Paper and Vintage); [Themes](themes.md) shows each
  one. Node and trace colors keep their hues in every theme: the light themes darken them to read on their
  background, and Phosphor, Oscilloscope, Cyber Slate, Blueprint and Amber brighten them.
- **Output window**: hidden until an analysis runs; it then comes to the front on the tab of the new result. Turn
  off View > Bring Output to Front on Results to keep the schematic in front: a dot on the Output tab then tells a
  new result is waiting. The operating point tab lists the node voltages and part currents, which also show on the schematic. Reopen it from View.
- **Window layout**: every window floats or docks. Drag a window by its title (or a docked one by its tab) onto
  the arrows that appear over another window to dock it on that side or as a tab; drop it anywhere else to let it
  float. By default the editor, Output, Analysis Settings and Netlist are tabs of the main area and Properties sits
  on its right. The layout is kept between sessions, and View > Reset Layout restores the default.

## Wire colors

**View > Wire Colors** picks what the wires show. **Plain** keeps them in the theme color and **Nodes** colors each
wire by its node, numbered on the canvas. **Voltage**, **Current** and **Power** color the wires (Power only the
parts, since wires carry none) on a heat scale, with a legend at the bottom left. They need a result on the schematic:
run an operating point or a transient first, or they have nothing to show.

- **Voltage** spreads the colors from the lowest to the highest node voltage, ground included.
- **Current** colors wires and parts by the current through them, on a logarithmic scale that reaches four decades
  below the largest current; the legend marks that floor with `<`.
- **Power** colors each part by the power it absorbs, on a logarithmic scale of four decades as well. It adds up the
  voltage of each terminal times the current into it, so a resistor shows its dissipation and a source that delivers
  power shows it too: colors follow the size of the power, and the hover tooltip gives its sign (negative when the part
  delivers power).

## Transient on the schematic

After a transient run, the schematic shows the circuit at one instant of it, with the same colors, labels and live
effects as an operating point. A bar above the canvas moves that instant: **Play** plays the whole run in about five
seconds (**Loop** starts it over), and the slider picks any instant and stops the playback where you drop it. In the
Output window, the red line on the transient plot is the same instant: drag it to move the schematic, and it moves by
itself while the schematic plays.

The colors keep one scale for the whole run, from its lowest to its highest voltage and its largest current and power,
so a value keeps its color from one instant to the next. A new result starts again at the beginning of the run, paused.
The schematic shows the last of the operating point and the transient that ran; the bar is only there for a transient.

## Live effects

**View > Live Effects** turns on effects drawn over any wire coloring, each on its own. Both read what the schematic
shows, an operating point or an instant of a transient, so they show nothing until one runs.

- **Current Flow** slides arrows along the wires in the direction each current goes: the arrows all look and move the
  same, so they show the direction at a glance, and each takes the heat color of its current, which carries the
  magnitude. Wires whose current is below the heat scale (four decades under the largest) get no arrows, so leakage
  does not draw noise, and in a transient the arrows turn around with the current.
- **LED Glow** lights every LED with forward current in its color (an LED with custom parameters glows warm white). The
  glow grows on a logarithmic scale, from faint around 50 µA to full at 20 mA, the rated current of the ready models.
  On dark themes it adds light to the canvas, like a lit LED; on light themes it is a soft halo instead.

## Terminals

Every part with more than one terminal numbers them in the order the SPICE netlist lists them. Turn on
**View > Terminal Numbers** to show the numbers, each beside a small ring at its terminal.

| Part | Terminal 1 | Terminal 2 | Terminal 3 |
|---|---|---|---|
| Resistor, capacitor, inductor | left end, before rotating | right end | |
| Diode, Zener diode, LED | anode | cathode | |
| Voltage source | positive (+) | negative (-) | |
| Current source | tail of its arrow | head of its arrow | |
| NPN or PNP transistor | collector | base | emitter |
| N- or P-channel MOSFET | drain | gate | source |
| VCVS or VCCS (E, G) | output, top of the diamond | output, bottom | control + (4: control -) |
| CCCS or CCVS (F, H) | output, top of the diamond | output, bottom | |
| Op-amp | output | non-inverting input (+) | inverting input (-) (4: V+, 5: V-) |

Ground and the VCC rail have a single terminal and no number.

Rotating a part (R) or mirroring it (M, Shift+M) moves its terminals with it, so terminal 1 is not always on the
left or at the top. Terminal numbers show where it ended up.

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
- **Controlled sources** report the current of their output like a two-terminal part; their control pins carry no
  current. A VCCS or CCCS pushes its current from terminal 1 to terminal 2 through itself, as its arrow shows, so
  into a load on terminal 1 it drives the load negative.
- **Op-amps** report the current into their output, which returns through ground. An ideal op-amp draws no input
  current, and its supply pins only carry a small current while the output saturates; a macromodel draws its bias
  current at the inputs and its supply current at the supply pins.

On the schematic, a measured current is an arrow on the lead of its terminal, pointing into the part: the direction
the current flows when its trace is positive. If a trace comes out negative, the current flows against the arrow.

## Controlled sources and op-amps

A controlled source multiplies a voltage or a current elsewhere in the circuit by its gain, which may be negative:

| Source | Output | Gain unit |
|---|---|---|
| VCVS (E) | voltage between its outputs = gain x voltage between its control pins | V/V |
| VCCS (G) | current through it = gain x voltage between its control pins | A/V |
| CCCS (F) | current through it = gain x a current of the circuit | A/A |
| CCVS (H) | voltage between its outputs = gain x a current of the circuit | V/A |

Two examples show them at work: **Small-signal model of a BJT** replaces a transistor stage by its hybrid-pi model, a
resistor and a VCCS, and **Ideal transformer** builds a 1:2 transformer from a VCVS and a CCCS.

A CCCS or CCVS follows a current picked in Properties among those the plots offer, such as `I(R1)` or `I(Q1.C)`,
with the sign that current has in the results. Simulating one without a current, or with a current whose part was
deleted, stops with a message.

An op-amp starts **Ideal**: no input current, no output resistance and 100 dB of gain, so its feedback network sets
the closed-loop gain. Its gain rolls off at the gain-bandwidth product (GBW, 1 MHz unless set in Properties): an
amplifier with a gain of 10 keeps its gain up to about GBW / 11. Its output cannot go past the voltages of its V+
and V- pins: connect them to supplies, and it clips within about 10 mV of them. It has no slew rate.

The **uA741** model, or **Custom** specifications, make it a real part, simulated with the Boyle macromodel that
SPICE simulators have used since 1974: a transistor input pair draws the bias current and limits the slew rate, the
output swings short of the supplies by its headrooms and limits its current at the short-circuit current, and the
part draws its supply current. The macromodel is built from datasheet values (open-loop gain, GBW, slew rate, phase
margin, bias current, CMRR, output resistance, short-circuit current, headrooms and supply current), so a datasheet
is enough to model another part; its GBW comes out about 10% low. It still leaves out offset voltage, noise,
temperature, the common-mode input range and the supply rejection, and its output current returns through ground.

The Examples menu has an inverting amplifier.

## Measuring

Plots start empty; pick what to measure in either place:

- **Probe tool** (toolbar button or P): click a wire to measure the voltage of its node, a part to measure its current,
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
- The AC sweep is a Bode plot: magnitude in dB and phase in degrees, relative to the AC magnitude of the sources.
  That magnitude is set in Properties apart from the amplitude of the sine, as in SPICE, and is 1 by default, so
  the plot reads as gain while a transient uses the small amplitude a real circuit would see.
- A stepped DC sweep draws one curve per step, labeled with the step value where the curves part most.

## Exports

**Plots**: Export... on the right of the Output window tabs saves the plot of the current tab.

- **SVG image**: redrawn from the data, not captured from the screen, so it stays sharp at any size. It shows the
  visible X range, so zoom in first to export a detail. Choose its size in pixels and its theme: light for print,
  which darkens trace colors too light for white paper, or dark, in the colors of the Graphite theme.
- **CSV table**: every sample of every measured trace, including math channels, whatever the zoom. The first column
  is the X axis; each header carries its unit, and on a Bode plot its panel (`Magnitude V(2) (dB)`). Missing
  samples, such as a division by zero in a math channel, are empty cells.

**Schematic**: File > Export Schematic... saves the circuit as an SVG image, cropped around it, with the symbol
style shown (IEC or ANSI), part names and values, terminal numbers when they are shown, and the measured voltages
and currents. Light, for print, darkens colors the same way as plot exports, so a probe and its trace keep matching
colors across the two figures; As on screen keeps the colors of the current theme.

The save dialogs suggest a name after the schematic file and the tab, such as `rc_filter-transient.svg` or
`rc_filter-schematic.svg`, next to the schematic.
