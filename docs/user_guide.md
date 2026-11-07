# imcsim user guide

How to read what imcsim measures, plots and exports. The [README](../README.md) covers building and running, and
[Simulation models](models.md) the model behind every part.

## Editing

- **Toolbar**: Select, Wire (W) and Probe (P) on the left, then the parts, each button showing its symbol. Sources,
  diodes, transistors, controlled sources and the adjustable parts share one button per group: it places the part last picked, and the
  arrow beside it (or a right click) lists the others. Hover a button for its name and shortcut.
- **Editing a part**: double-click it, or select it and press Enter, to open its properties at the top of the
  schematic; edits apply as you type, and Enter, Esc or a click outside closes them. With a part selected, typing a
  number starts editing its main value right away: `4k7` and Enter make a resistor 4.7k. Values take SPICE suffixes, and a whole
  number may put its decimals after the suffix, as resistor codes do (`4k7`, `2M2`, `3u3`). The Properties window
  beside Output keeps the same fields docked.
- **Renaming a part**: the Name field of its properties takes a new name when you press Enter or click away. The name
  keeps the letter the part starts with, which tells ngspice what the part is (`R` for a resistor, `X` for a block,
  `U` for a gate), then letters, digits or `_`, such as `RLOAD` or `XCOUNT`; no two parts may share a name, whatever
  its case. A name that cannot be used says why and the part keeps its own. The measured current of the part, a
  controlled source that follows it and the math channels that read `I(name)` follow the new name.
- **Potentiometer**: its properties hold the resistance between its ends and the wiper, a slider from 0% at terminal 1
  to 100% at terminal 2. Move the wiper and run again to see the output change. During a live run, use the mouse wheel
  over the part or its Wiper field to change it while the simulation continues (see Live runs).
- **Initial conditions**: a capacitor and an inductor have an Initial field beside the value, for the voltage the
  capacitor starts charged to or the current the inductor starts carrying; the sign is free. When any part has one,
  the transient runs with `uic`, so the circuit starts from those values without an operating point first, and says so
  as a warning. Capacitors and inductors left empty then start at zero, not at their operating point values, so set
  every part that should start charged. The operating point, the AC sweep and the DC sweep ignore the initial
  conditions.
- **Switches**: a switch and a push button have a Closed (or Pressed) box in their properties, and the symbol and a
  label under it show the state. Both start open. Outside a live run, the state holds for the whole simulation. During
  a live run, click a switch to flip it or hold a push button to press it (see Live runs).
- **Switches that follow a voltage**: a voltage-controlled switch has its threshold, hysteresis and resistances in its
  properties, and a relay its coil and contact figures. Both are drawn open, since they open and close during a run.
  The Switching examples use them: a buck converter and a relay driver with a flyback diode.
- **Fuses**: a fuse is in the Switches and fuses group of the toolbar. Its properties pick a ready fuse (100 mA, 500 mA,
  1 A and 5 A fast, 1 A slow-blow) or set its rated current, melting I2t and cold and blown resistance, and show the
  cooling time constant they imply. A fuse carries its rated current for ever and blows above it, open for the rest of
  the run: the branch loses its current at that instant, the symbol breaks with a gap, and the Output window warns when
  it blew. A fuse inside a block is simulated, but its blow is not drawn or warned about. Every run starts with a whole fuse, and an operating point, a DC sweep and an AC sweep always see it closed.
  The Protection example compares a fast and a slow-blow fuse (see docs/models.md).
- **Transformers**: the value of a transformer is its primary inductance, and its properties set the turns ratio, the
  coupling and the resistance of the windings. The secondary needs a path to ground, such as the bridge of the
  Transformers examples.
- **Logic gates and flip-flops**: the toolbar has a button for the gates and one for the flip-flops, and Space finds
  them by name (`nand`, `flip`). A part has a logic level and a delay in its properties; it reads 1 above 70% of its
  level and 0 below 30%, and an input left unconnected reads 0. AND, OR, NAND, NOR, XOR and XNOR also have an
  **Inputs** setting from 2 to 8. The body grows downward, one input at a time, and adding one leaves the wired inputs
  where they are and moves only the output, which stays at the middle of the body, so its wire has to be drawn again.
  Lowering the count removes the last pins and leaves their wires loose on the grid, so delete them or move them to a
  pin that is left. A many-input XOR is high for an odd number of high inputs. The Digital examples show a table of
  gates, the truth table of the basic gates, half and full adders, an SR latch and a ripple counter. In the Output
  window the signals of logic parts plot as lanes, like a logic analyzer's, under the analog traces (see Plots).
- **Clock**: the Sources group has a clock, a pulse source for digital circuits with one terminal, like a supply rail.
  Its properties hold a frequency, a duty cycle (the share of each period that is high, typed in %), a logic level and
  a start delay; it picks its own short rise and fall times, so they stay the same at every frequency. The start delay
  holds the clock low and the first rising edge begins there, so a delay of 0 starts the first edge at t = 0 and a
  nonzero one keeps the clock quiet.
- **Logic input**: a box in the supply row with Ground and VCC drives a net to a defined level. A High box in its
  properties drives the logic level (5 V by default, editable) and leaves it clear to drive 0 V; the symbol shows a 1
  while high and a 0 while low, and both start low. It is a source from its terminal to ground, so it gives a gate
  input or a latch's SET or RESET a defined level to start from. A circuit of only logic parts, clocks, logic inputs and VCC
  runs without a Ground symbol or a load on the outputs; add a Ground as soon as it has any other part.
- **Logic output**: the box beside the logic input, with an arrow on its lead, shows the level of the net it is wired
  to, read as a gate reads it: 1 at 70% of its logic level or above, 0 at 30% or below and X between (5 V by default,
  editable in its properties). It shows the operating point, or the transient at the instant of the time bar, and
  follows a live run; it is empty before there is a result. It takes no part in the simulation, so a digital circuit
  can be tried with logic inputs and outputs alone, with no LED, resistor or Ground.
- **7-segment display**: the box after the logic output has the inputs a to g down its left side (a the top segment,
  b to f around it clockwise, g across the middle) and lights each segment whose input reads 1, read as the logic output
  reads its node. A segment whose input is between the levels is filled faintly. Like the logic output, it takes no part
  in the simulation; wired to the BCD to 7-segment decoder block, it shows the digit the decoder gets.
- **Standard values**: Standard values in Edit > Preferences restricts resistors, capacitors and inductors to one of
  the E series (E6, E12, E24, E48, E96 or E192, with their tolerances of 20% down to 0.5%), or leaves any value
  allowed, which is the default. With a series chosen, a typed value becomes the nearest one of the series on a log scale (`5000` is
  4.7k in E12), and the arrow buttons beside the value, the up and down keys and the mouse wheel over the field step
  to the next or previous standard value, into the next decade at the ends. A part keeps a value outside the series
  until you edit it, and the sources, gains and parameters of the other parts are never restricted. The choice is a
  preference kept between sessions, not part of the schematic file.
- **Selecting**: click a part or a wire. Shift+click adds an item to the selection or removes it, and a drag on empty
  canvas selects everything the box touches (with Shift, it adds to the selection). A double click on a wire selects
  every wire of its node, and Ctrl+A (Edit > Select All) selects everything. Esc clears the selection.
- **Moving**: drag any selected item and the whole selection moves, with the wires around it following; a click on
  one item of a group without dragging leaves just that item selected. A single wire only slides across itself:
  drag a segment sideways and the wires that meet it stretch or bend to stay connected, as do the parts that touch
  its middle. If you drop a segment on another wire, the two join there, as when drawing a wire. Delete removes the
  whole selection in one step, and Undo brings it back the same way.
- **Copy and paste**: Ctrl+C copies the selection, Ctrl+X cuts it and Ctrl+V pastes (Edit menu too). The copy goes to
  the system clipboard as text in the format of the files, so it pastes into another window of imcsim as well. A paste
  follows the cursor half see-through, held by the middle of the group; R, M and Shift+M turn it, a click places it
  and selects what was placed, and Esc or a right click cancels. Pasted parts take the next free names (R3, C2), so
  a part that points at another by name, such as a current-controlled source, still points at the original. A pasted
  wire or terminal that lands on one already there joins it, as when drawing. Probes stay where they are: a copy
  carries no measurements, and the probes of the schematic you paste into keep following their own nodes.
- **Import**: File > Import... adds the circuit of any `.imcsim` file to the schematic you have open, instead of
  replacing it as Open does. The circuit follows the cursor like a paste (R, M and Shift+M turn it, a click places it,
  Esc cancels), takes the next free part names and is selected once placed. The analysis settings and the probes of
  the file are left behind, and parts that cannot be read are reported and skipped. File > Insert Example does the
  same with an example circuit (both menus sort the examples into submenus by topic), and dropping a `.imcsim` file
  on the window imports it, with nothing replaced and nothing to confirm.
- **Net labels**: the circle button next to Ground and VCC, or L, places a label; its popover opens on the name, so you
  type it and press Enter. Labels with the same name are one node even with no wire between them, which keeps a
  large circuit readable: label the output `OUT` here and again where it is used. Names are 1 to 32 letters, digits,
  `_`, `-` or `.`, are compared without regard to case, and a label named `0` or `GND` is ground, like a Ground symbol, and one named `VCC` joins every VCC supply, so a rail can be labeled
  instead of wired. Do not use that name for supplies of different voltages. A new label starts with the
  name of the last one, so another point of the same net is a click away. A label with no name shows a `?` and joins
  nothing. A node with a label shows its name instead of its number where the plots and probes name it: `V(OUT)`
  rather than `V(3)`, in the legends, the trace lists, the operating point table (as `OUT (3)`), the probe markers
  and the hover. Math channels take the name too, as `V(OUT)`, and find the node again after edits that renumber
  the nodes. Labels are ordinary parts: they select, move, turn, copy and paste like the others, and the netlist has
  no line for them.
- **Rotating and mirroring**: R, M (left to right) and Shift+M (top to bottom) turn the selection about its center;
  with one part selected, that part turns in place. The wires that meet the selection follow it. Hold Alt (Alt+R,
  Alt+M, Alt+Shift+M, or Edit > Keep Wires in Place) to turn the parts and leave every wire where it is, so nothing
  moves out of place; the terminals that no longer meet a wire stay open for you to rewire.
- **Right click**: on a part, its properties, rotate, mirror, flip, measure its current and delete; on a wire,
  measure its voltage and delete; on empty canvas, find a part, run and fit. Right click on an item of a selected
  group keeps the group, and the menu acts on all of it.
- **Fit**: the frame button in the toolbar, or Home, shows the whole schematic.
- **Find a part**: press Space over the editor, type part of a name (`mos`, `zener`, `opamp`, `nmos`, `vcvs`), move
  with Up and Down, and press Enter to start placing it.
- **Running**: the ▶ button at the end of the toolbar, or F5, runs the analysis named beside it. That button opens
  the list of analyses (operating point, transient, AC sweep, DC sweep, noise, transfer function, sensitivity, poles and zeros, S-parameters and truth table) and the settings
  of the one picked, saved with the schematic. Suggest fills the transient times (five periods of the slowest source with a waveform, 200
  points in a period of the fastest) or the AC range (two decades past the RC, RL and LC corners and the op-amp
  bandwidths). By default nothing runs by itself: edit the circuit, then run again, or turn on Run again after edits
  in Edit > Preferences. With it on, once you have run the selected analysis, each change to the circuit (a value, a
  part added, moved or deleted, an undo) runs it again once the circuit has stayed unchanged for Wait after an edit
  (half a second unless you change it, from 0.1 to 5 seconds): while a value is being typed or something dragged it
  waits, a burst of edits runs it once, and a run still going on is stopped first.
  Opening another schematic does not run anything, and a live run already follows the parts set by hand. View >
  Analysis Settings keeps the same settings open in a window.
- **Stopping**: a run goes on in the background, so the editor stays usable. The ▶ button becomes a ■ Stop button, and the
  status bar shows the run and, for a stepped analysis or a truth table, how much of it is done. A transient runs in
  chunks: its plot opens in the Output window with the first one and grows as the others arrive, with a Stop button
  and "Running, simulated up to 230ms of 1s" above it. Stop keeps the points made so far ("Stopped, simulated up to
  530ms of 1s", and "Transient stopped" in the status bar). The transient plot shows a window of 2000 points at a time (a field
  at its top right sets 1000 to 10000), so a long run stays quick to draw and to measure: a scroll bar the width of the
  plot, with its time span written above it, moves the window along the run (the plot fits itself to every window), and
  while the run goes on a Follow box keeps the window on the newest points. The schematic plays the same window: its
  time bar, its heat colors and the playhead of the plot all go over the window, and while the window follows a run the
  schematic shows the newest point. The statistics and the cursors work on the
  points in the window. The SVG image is the window; a CSV table can be of the window or of the whole run, as Export
  asks. The math channels are worked out over the whole run, so an integral starts at the start of the simulation, and
  they wait, grayed out, for the end of a run that is still going on. The spectrum uses the whole run too. The other analyses stop between their steps (the values
  of a stepped analysis, the combinations of a truth table) and drop their result, and a single simulation finishes
  first; a Monte Carlo analysis keeps the runs made ("Stopped after 40 of 100 runs"). Editing the circuit also stops the run and drops what it made, since the answer would no longer fit.
- **Status bar**: below the schematic, how the last run went first ("Transient done", with warnings, or failed,
  followed by the reason, which then takes the whole line), then the keys of the current mode, shortened at a word
  when the window is narrow, and the grid position of the cursor on the right. Click the result for the
  errors, the warnings and the ngspice output. Among the warnings are the safe operating area ones: a diode, LED,
  Zener, transistor or MOSFET that a run pushes past its ratings (diode average forward current and power, and the VCE, IC,
  VDS, VGS, ID and power of transistors) is named with the value and the rating. Ratings are fields of a part's
  properties (IF max, the average forward current, P max, ...) and are only checked for ready and custom models, not for library ones.
- **Menus**: File (files, import of schematics and of SPICE libraries, examples and the schematic export), Edit (undo,
  redo, changes to the selection, blocks and Preferences) and View (fit, wire colors, live effects, theme, and the windows). On macOS the shortcuts use Cmd where
  this guide says Ctrl: Cmd+S saves, Cmd+Z undoes.
- **Preferences**: Edit > Preferences (Ctrl+,) gathers your own choices, whatever schematic is open: the theme, IEC
  or ANSI symbols, terminal numbers, wire colors, live effects, the series of standard values, whether a new result
  brings the Output window to the front and whether the analysis runs again after edits. A change applies at once and is kept between sessions; Restore Defaults
  sets them all back. Wire colors, live effects and the theme also have shortcuts in the View menu. The choices are
  kept in `preferences.json` in your data folder (`~/.local/share/imcsim` on Linux, `~/Library/Application
  Support/imcsim` on macOS, `%APPDATA%\imcsim` on Windows), which can be copied to another computer or deleted to
  start over; what belongs to a circuit, such as the simulation settings, stays in its file.
- **Themes**: View > Theme switches between nine themes, seven dark (Graphite, the default, Ember, Phosphor,
  Oscilloscope, Cyber Slate, Blueprint and Amber) and two light (Paper and Vintage); [Themes](themes.md) shows each
  one. Node and trace colors keep their hues in every theme: the light themes darken them to read on their
  background, and Phosphor, Oscilloscope, Cyber Slate, Blueprint and Amber brighten them.
- **Output window**: hidden until an analysis runs; it then comes to the front on the tab of the new result. Turn
  off Bring Output to front in Edit > Preferences to keep the schematic in front: a dot on the Output tab then tells a
  new result is waiting. The operating point tab lists the node voltages and part currents, which also show on the schematic. Reopen it from View.
- **Window layout**: every window floats or docks. Drag a window by its title (or a docked one by its tab) onto
  the arrows that appear over another window to dock it on that side or as a tab; drop it anywhere else to let it
  float. By default the editor and Output sit side by side, and a column on the right holds Properties over
  Analysis Settings and Netlist, which share it as tabs and open from the View menu; a window opens in its place
  and the others make room, keeping the circuit centered, and every window keeps its share when the main window
  changes size. The layout is kept between sessions, and View > Reset Layout restores the default.

## Blocks

A block groups part of a schematic into a part of its own, with pins, which can be placed as many times as needed. The
schematic file keeps the definition of each block, so it opens anywhere without other files.

- **Pins**: a Block pin marker (in the supplies group of the toolbar, or Space and `pin`) marks a node of the circuit
  as a pin of the block. Its square stands for the edge of the block and its arrow points from that edge into the
  circuit: a marker whose arrow points right puts its pin on the left of the block, one pointing left on the right, one
  pointing down on the top and one pointing up on the bottom. Name it in its properties (a letter or `_`, then letters,
  digits or `_`); two markers with one name do not join, unlike net labels. Each pin needs a node of its own, and not
  ground: the ground of a block is the ground of the whole circuit already, so a pin on it would connect nothing, and
  imcsim refuses it.
- **Making a block**: select the circuit together with its pin markers and pick Edit > Create Block from Selection.
  The dialog names the block, lists the pins in the order of its terminals (Up and Down reorder them) with the edge
  of each, which a choice there turns the marker to, and holds its parameters with their defaults. Create block puts
  one instance where the selection was, moved aside when its box would cover the points where the markers were, and
  wires it to those points. Each wire leaves the box from the edge of its pin and goes around the box, clear of the
  other wires and terminals. Undo takes it back in one step.
- **Parameters**: inside a block, a resistor, capacitor or inductor can take its value from a parameter of the block:
  pick the parameter in Follows, in its properties, and its symbol shows `{R}` instead of a value. Each instance sets
  its own values in its properties, or keeps the defaults, and Reset parameters goes back to them.
- **Placing more**: the part search (Space) lists every block of the schematic as `Block: name`. Copy and paste carry
  the definitions of the blocks they copy, to this schematic or another one; a pasted block that differs from one of
  the same name already there joins under a name of its own, such as `RC_LOWPASS_2`.
- **Editing a block**: double click an instance, or pick Edit > Edit Block Contents, to open its contents in the
  editor, with the path from the schematic down to the block over the corner of the canvas, such as
  `Schematic > AMPLIFIER > STAGE`. Edit > Block Pin and Parameter Settings opens the dialog again, where Apply keeps
  the pin names, edges and parameters. A click on a level of the path, or Edit > Apply Block and Return, applies the
  changes to every instance at once, as one undo step, and Edit > Discard Block Changes leaves the block as it was. A
  pin that keeps its marker, even renamed or on another edge, keeps the wires of its instances, and a pin that moved
  to another edge gets a wire around the box from where its old wire leaves it; the wires of a pin that went away are
  removed. Quitting, or opening an example, applies the open blocks first, so their changes count as
  unsaved changes of the schematic.
- **Blocks inside blocks**: a block can hold other blocks. Select them with the rest of the circuit to make a bigger
  block, or place one while a block is open, from the part search or by pasting. Double click a block inside an open
  block to go one level down; the path leads back up a level at a time, each one applied on the way. A block can never
  hold itself, directly or through another block: the part search leaves those out, and a paste or an apply that would
  make one is refused.
- **Ready logic blocks**: the toolbar button after the flip-flops places one of eight blocks built from the gates and
  flip-flops, the last one picked from its list, and the part search lists them as `Logic block: ...`:
  - a 4-bit counter (a ripple counter whose Q3 Q2 Q1 Q0 count the rising edges of CLK);
  - a 4-bit shift register (each rising edge of CLK moves IN into Q0 and each Q on);
  - a 4-to-1 multiplexer (Y follows the input that S1 S0 select);
  - a 2-to-4 decoder (with EN high, the output that A1 A0 select is high);
  - a 4-bit adder (S3..S0 and COUT are A3..A0 plus B3..B0 plus CIN);
  - a 4-bit comparator (GT, EQ or LT is high as A3..A0 is greater than, equal to or less than B3..B0);
  - a 1-to-4 demultiplexer (D goes to the output that S1 S0 select);
  - a BCD to 7-segment decoder (a to g light the digit D3..D0 reads, 0 to 9; codes past 9 light nothing).

  RESET clears the counter and the shift register. Hovering one in either list tells what it does. Placed, it joins the
  schematic as any block does, so it can be opened to see its gates and changed there. Their logic level is 5 V, and a circuit of them,
  clocks and logic inputs needs no Ground part, as loose gates do not.
- **SPICE libraries**: File > Import SPICE Library reads a vendor's `.lib`, `.mod`, `.sub` or `.cir` file and lists its
  subcircuits and its diode and transistor models.
  - A picked subcircuit becomes a block kept as SPICE text: imcsim simulates its text as it came, with the `.model`,
    `.subckt` and `.param` lines of the file it uses, so the schematic does not need the file afterwards. The dialog
    shows the comment the vendor wrote above it, which usually says what each numbered port is, and asks for the name
    of each pin and the edge it goes on, starting from what the port names suggest (`VCC` on top, `OUT` on the
    right). The block then follows the cursor until a click places it. Double click it for its pins, the defaults of
    its parameters and its text, which can be read but not edited.
  - A picked model joins the schematic and shows in the Model choice of the parts it fits, as `1N4148 (library)`
    after the ready models; a diode model fits diodes, LEDs and Zeners, and a transistor model the transistors of its
    type.
  - `.include` and `.lib` lines are followed, with paths taken from the folder of the file that writes them:
    `.include file` reads the whole file in its place, `.lib file section` reads that section of it, and `.lib file`
    with no section reads the whole file, as LTspice writes it. What a block uses from those files is copied into its
    text, so it needs none of them afterwards. When the file picked has sections of its own, all of them are read and
    the first definition of each name wins. A file that cannot be read, a missing section or a file that includes
    itself is reported with the line that names it. Encrypted models, which only their vendor's simulator reads, are
    left out with a warning.

The RC_LOWPASS block of the Two RC filters example, open in the editor: a pin marker on each side, and a resistor and
a capacitor that follow the parameters R and C:

![The inside of a block: IN and OUT pin markers, a resistor whose value reads {R} and a capacitor whose value reads {C},
with the path Schematic > RC_LOWPASS over the corner of the canvas](images/blocks_contents.png)

Its settings, with the edge of each pin and the defaults of the parameters:

![The block dialog: the name RC_LOWPASS, the pins IN on the left and OUT on the right, and the parameters R = 1000 and
C = 1e-07](images/blocks_dialog.png)

Importing an op-amp from a library: the comment above its `.subckt` line says what each numbered port is, and each
port gets a name and an edge:

![The Import SPICE library dialog with GENERIC_OPAMP picked, the vendor's comment naming its five ports, its
parameters, and the pins INP and INN on the left, VP on top, VN at the bottom and OUT on the
right](images/spice_import.png)

## Wire colors

**View > Wire Colors** picks what the wires show. **Plain** keeps them in the theme color and **Nodes** colors each
wire by its node, numbered on the canvas. **Voltage**, **Current** and **Power** color the wires (Power only the
parts, since wires carry none) on a heat scale, with a legend at the bottom left. They need a result on the schematic:
run an operating point or a transient first, or they have nothing to show. **Sensitivity** colors the parts by their
influence on the output of a sensitivity analysis (see below) and leaves the wires plain, so it needs that analysis
run first.

Over these colors a selected wire gets a halo and a selected part an outline in the selection color, instead of turning that color,
so it does not pass for a value on the scale. The voltage of each node sits beside its wire, under a level one and
right of an upright one, on a small box led by a dot of the node color, so it reads over any wire color.

- **Voltage** spreads the colors from the lowest to the highest node voltage, ground included.
- **Current** colors wires and parts by the current through them, on a logarithmic scale that reaches four decades
  below the largest current; the legend marks that floor with `<`.
- **Power** colors each part by the power it absorbs, on a logarithmic scale of four decades as well. It adds up the
  voltage of each terminal times the current into it, so a resistor shows its dissipation and a source that delivers
  power shows it too: colors follow the size of the power, and the hover tooltip gives its sign (negative when the part
  delivers power).
- **Sensitivity** colors each part the sensitivity analysis lists by the size of its normalized sensitivity, on a
  logarithmic scale of four decades below the largest one; the legend gives the volts for a 1 % change of the value (titled with
  the output it refers to), and marks the floor with `<`. Parts the analysis does not list, such as diodes, transistors and ground, keep the theme color.

## Transient on the schematic

After a transient run, the schematic shows the circuit at one instant of it, with the same colors, labels and live
effects as an operating point. A bar above the canvas moves that instant: **Play** plays the whole run in about five
seconds (**Loop** starts it over), and the slider picks any instant and stops the playback where you drop it. In the
Output window, the red line on the transient plot is the same instant: drag it to move the schematic, and it moves by
itself while the schematic plays.

The colors keep one scale for the whole run, from its lowest to its highest voltage and its largest current and power,
so a value keeps its color from one instant to the next. A new result starts again at the beginning of the run, paused.
The schematic shows the last of the operating point and the transient that ran; the bar is only there for a transient.

## Live runs

The **Live** button beside the analysis turns the next transient into a live run, which plays on the schematic while it
is simulated and goes on until you stop it. It covers the stop time every **Live run** seconds of the transient
settings (10 s at first, from 2 to 60), whatever the circuit: a circuit of a millisecond is slowed down so you can follow
it, and one of a minute is sped up. The stop time is also how much of the run the plot and the schematic show at a time,
the newest stretch, which scrolls as the run goes on.

Only switches, push buttons, logic inputs and potentiometers change during a live run: they are outlined on the
schematic while it lasts. Changing any other part, or the wiring, ends the run, and the properties of a part say so
before you edit it. While it runs:

- A click on a switch or a logic input flips it, a push button is pressed while the mouse button holds it down, and
  the mouse wheel over a potentiometer moves its wiper by 5% a notch. The Closed, Pressed, High and Wiper fields of the
  part's properties do the same. The circuit takes the change a few hundredths of a second later, and the plot shows it
  at the instant it happened.
- The schematic shows the newest point, the status bar reads "Transient live at 12.5ms", and the Output window marks
  its tab with a dot instead of coming to the front, so the schematic stays in view.
- **Stop** pauses the run and keeps every point it made: scroll back through it, play it on the time bar, export it, or
  set switches for when it goes on. **Continue** (the Run button, or F5) takes the run on from where it stopped, at the
  same pace. **Discard**, which takes the place of Live while a live run lasts, ends it and drops what it made; Run then
  starts a new one from zero. The Output window offers the same buttons above the plot.
- Running another analysis ends a paused live run and keeps its points, as a stopped transient. Any other edit ends
  the run and drops it, as it does any run.

A live run stops by itself after an hour on the clock, or sooner when it reaches a million time steps (a small time
step for its stop time), with a warning: ngspice keeps every point of the run and cannot let go of the old ones, so its
memory grows the longer the run goes on.

Live works only for a transient, and not for a stepped one; when it cannot run, the button is grayed out and its
tooltip says why. The result of a live run stays like any other, with the changes made during it, until the circuit is
edited.

## Live effects

**View > Live Effects** turns on effects drawn over any wire coloring, each on its own. Each reads what the schematic
shows, an operating point or an instant of a transient, so they show nothing until one runs.

- **Current Flow** slides arrows along the wires in the direction each current goes: the arrows all look and move the
  same, so they show the direction at a glance, and each takes the heat color of its current, which carries the
  magnitude. Wires whose current is below the heat scale (four decades under the largest) get no arrows, so leakage
  does not draw noise, and in a transient the arrows turn around with the current.
- **LED Glow** lights every LED with forward current in its color (an LED with custom parameters glows warm white). The
  glow grows on a logarithmic scale, from faint around 50 µA to full at 20 mA, the rated current of the ready models.
  On dark themes it adds light to the canvas, like a lit LED; on light themes it is a soft halo instead.
- **Heat Glow** makes every resistor that dissipates power glow orange, bigger and stronger with its power. The
  largest power of the circuit (of the whole run, for a transient) is full brightness, and the glow fades out two
  decades below it, so it shows where the power is lost and, while a transient plays, when.
- **Logic Lights** fills the box of every logic input that is high and every logic output that reads 1 with the
  accent color of the theme, as a lit indicator, and lights the segments of the 7-segment displays in that color too;
  with it off, the lit segments take the color of the symbol.
- **Fuse** bursts a few rays from a fuse for about 150 ms of the screen clock when it blows, as the result on display
  passes the instant it did (a result that is already open when it appears does not flash). It is on for a new user; the
  open symbol, with its gap and the word "blown" under it, shows whether the effect is on or not, and hovering the part
  says when it blew.

## Terminals

Every part with more than one terminal numbers them in the order the SPICE netlist lists them. Turn on
**Terminal numbers** in Edit > Preferences to show the numbers, each beside a small ring at its terminal.

| Part | Terminal 1 | Terminal 2 | Terminal 3 |
|---|---|---|---|
| Resistor, capacitor, inductor | left end, before rotating | right end | |
| Potentiometer | end at the left, before rotating | end at the right | wiper, on top |
| Switch, push button, fuse | left contact, before rotating | right contact | |
| Voltage-controlled switch (S) | contact, top | contact, bottom | control + (4: control -) |
| Relay (K) | contact, top | contact, bottom | coil + (4: coil -) |
| Transformer (T) | primary +, upper left | primary -, lower left | secondary + (4: secondary -), on the right |
| Diode, Zener diode, LED | anode | cathode | |
| Voltage source | positive (+) | negative (-) | |
| Current source | tail of its arrow | head of its arrow | |
| NPN or PNP transistor | collector | base | emitter |
| N- or P-channel MOSFET | drain | gate | source |
| VCVS or VCCS (E, G) | output, top of the diamond | output, bottom | control + (4: control -) |
| CCCS or CCVS (F, H) | output, top of the diamond | output, bottom | |
| Behavioral source (B) | +, top of the diamond | -, bottom | |
| Op-amp | output | non-inverting input (+) | inverting input (-) (4: V+, 5: V-) |
| Gate (AND, OR, NAND, NOR, XOR, XNOR) | input A, upper left | input B, lower left | output Y, on the right |
| NOT, buffer | input A, on the left | output Y, on the right | |
| D or T flip-flop | D or T, upper left | CLK, lower left | SET, top (4: RESET, bottom; 5: Q; 6: QN) |
| JK or SR flip-flop | J or S, upper left | K or R, lower left | CLK, middle left (4: SET; 5: RESET; 6: Q; 7: QN) |

Ground, the VCC rail and net labels have a single terminal and no number.

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
- **Potentiometers** report one current per terminal, `I(RV1.A)` for the first end, `I(RV1.B)` for the second and
  `I(RV1.W)` for the wiper, each positive into its terminal. The three always add up to zero.
- **Switches and push buttons** report one current like a two-terminal part, positive from terminal 1 to terminal 2;
  an open one carries picoamperes.
- **Fuses** report one current like a switch, positive from terminal 1 to terminal 2; a blown one carries nanoamperes.
- **Relays** report two currents, `I(K1.S)` into the first contact terminal and `I(K1.C)` into the coil +. A
  voltage-controlled switch reports the current into its first contact terminal, like a two-terminal part.
- **Transformers** report `I(T1.P)` into the primary + and `I(T1.S)` into the secondary +, each positive into its
  terminal. With a load on the secondary the two have opposite signs, in the ratio of the turns.
- **Controlled and behavioral sources** report the current of their output like a two-terminal part; the control pins
  of a controlled source carry no current. A VCCS, CCCS or behavioral current source pushes its current from terminal 1 to terminal 2 through itself, as its arrow shows, so
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

A **behavioral source**, in the same menu, drives a free expression instead of a gain: a voltage from its + to its -,
or a current through itself from + to -. Type it in Properties, such as `V(IN)*V(LO)` for a multiplier,
`1m*tanh(V(IN)/26m)` for a soft limiter or `10m*V(TEMP)` for a sensor. It names voltages by net label, `V(OUT)` or
`V(IN, OUT)`, so put a label on every net it reads (`0` and `GND` are ground), and currents as the plots name them,
`I(R1)` or `I(Q1.C)`. Numbers take SPICE suffixes, and the functions and operators are those of ngspice: `tanh()`,
`exp()`, `abs()`, `min()`, `max()`, comparisons with `a ? b : c`, `time` and `temper`. Properties says at once what is
wrong, a net no label names, a current the circuit lacks or a parenthesis that does not close, and keeps the last
expression that read until the new one does; ngspice reports the rest when it runs. The schematic labels it as
`V=...` or `I=...`, cut after 24 characters. Renaming a part it reads renames it in the expression too. In an AC
sweep it is linearized at the operating point (see the models page).

An op-amp starts **Ideal**: no input current, no output resistance and 100 dB of gain, so its feedback network sets
the closed-loop gain. Its gain rolls off at the gain-bandwidth product (GBW, 1 MHz unless set in Properties): an
amplifier with a gain of 10 keeps its gain up to about GBW / 11. Its output cannot go past the voltages of its V+
and V- pins: connect them to supplies, and it clips within about 10 mV of them. It has no slew rate.

The **uA741**, **LM358**, **TL072**, **NE5532** and **OP07** models, or **Custom** specifications, make it a real part, simulated with the
Boyle macromodel that SPICE simulators have used since 1974: a transistor input pair draws the bias current and limits
the slew rate, the
output swings short of the supplies by its headrooms and limits its current at the short-circuit current, and the
part draws its supply current. The macromodel is built from datasheet values (open-loop gain, GBW, slew rate, phase
margin, bias current, CMRR, output resistance, short-circuit current, headrooms and supply current), so a datasheet
is enough to model another part; its GBW comes out about 10% low. The input pair is NPN in the uA741, PNP in the
LM358, whose inputs and output go down to V- on a single supply, and a JFET pair in the TL072, which draws picoamperes.
Each ready part carries the typical input offset of its datasheet at its + input, from 60 µV for the OP07 to 3 mV for the
LM358 and TL072, and a Custom part picks its input stage and its input offset voltage (VOS). It still leaves out
noise densities, temperature, the common-mode input range and the supply rejection, and its output current returns
through ground.

The Op-amps examples are an inverting and a non-inverting amplifier, an integrator, a Schmitt trigger and a
Sallen-Key low-pass filter.

## Measuring

Plots start empty; pick what to measure in either place:

- **Probe tool** (toolbar button or P): click a wire to measure the voltage of its node, a part to measure its current,
  or a transistor near one of its terminals to measure that terminal's current. Clicking a measured item again stops
  measuring it.
- **Trace list** in the Output window: check voltages `V(n)` and currents `I(name)`, or use All and None.

Measured items are marked on the schematic, in the colors of their traces:

- a dot on the first wire of a node for each measured voltage, labeled `V(n)` above or left of the wire;
- an arrow on a terminal lead for each measured current, labeled `I(name)` below or right of it.

The trace list is the only control for what the plots show, so the plots, their statistics and the exports always
agree. It is also their legend: each row shows its trace's color and line style (solid for a voltage, dashed for a
current, heavier for a math channel), faded while the trace is off, so the plots carry no legend over the curves.
Only the noise and S-parameter plots, whose curves the list does not hold, keep one.

## Plots

- Voltages are solid lines on the left axis, in the color of their node, the same color as in the editor.
- Currents are dashed lines on the right axis.
- The transient plots the measured nodes that gates and flip-flops are connected to, directly or through the pins of a
  block that holds them, as lanes of their own, in a plot under the analog traces with the same time axis, cursors and
  playhead. Each lane is named at its left, high is the upper level and low the lower, and a voltage between 30% and
  70% of the logic level, which a gate reads as unknown, is drawn halfway. Hovering a lane reads its state and voltage,
  such as `1 (4.72V)`. The Bit values box, off by default, divides the lanes into cells as wide as the shortest steady
  stretch of any of them, which is half a period
  when there is a clock, starting at the first change, and writes 0, 1 or X in a box at the middle of each cell that has
  room for it at the current zoom. A stretch that lasts several cells reads `1 1 1`, and a glitch inside a cell does not
  show. The Logic lanes box on the line over the statistics turns the lanes off, which draws every voltage on the analog
  axis again. The plot is left out when only logic signals are measured. The SVG export has the same panels and bit
  labels; the CSV lists the voltages.
- A logic bus reads several logic signals as one number, as a logic analyzer does, so a counter reads 0, 1, 2...
  instead of a lane per bit. Buses, under Math in the trace list when the circuit has logic parts, adds one and opens
  its editor: its name, its format (bin, hex or dec) and its bits, the most significant first, picked from the signals
  of the logic parts and moved with Up and Down. Each shown bus takes a lane over the others, drawn as a band that
  pinches at every change of its value, with the value written in it where it fits; a value is X while any of its
  bits is unknown. A bit names its net, such as `V(Q0)`, so the bus survives edits of the circuit, and buses are saved
  with the file. The SVG export draws them the same way, and the CSV lists their values as numbers.
- Math channels are heavier lines; volts and amperes share the axes above, any other unit or none goes on a third
  axis. Their trace pickers offer a labeled node by its name, and a typed expression may name it as `V(OUT)` or
  `V(OUT, IN)`; a name of digits alone, such as `V(3)`, is a node number. The channels are saved with the schematic,
  like the measured traces, but adding or editing one is not an unsaved change. A channel that names a net no label
  has any longer keeps its expression and reads in red, with the reason in its tooltip and above the plot, instead
  of disappearing.
- On an AC sweep a math channel works on the complex values of the traces, rebuilt from their magnitude and phase, and
  plots as a magnitude in dB on the magnitude plot and a phase in degrees on the phase plot. `V(OUT)/V(IN)` so reads
  the gain and the phase shift between two nodes, on the voltage axis, which reads as gain, and `V(IN)/I(V1)` the
  impedance a source sees, in ohms on the third axis. The magnitude is 20·log10 of the value relative to 1 of its unit, whatever that unit, as for every trace of
  the sweep. `ddt()` multiplies by jω and `integ()` divides by it, which is what they do to a sine, and `abs()` keeps
  the magnitude with a phase of 0. A value of zero, or one with no finite magnitude such as `integ()` at 0 Hz, leaves
  a gap. The channels are worked out on the step shown, and the statistics read their response like any other trace.
- The AC sweep is a Bode plot: magnitude in dB and phase in degrees, relative to the AC magnitude of the sources.
  That magnitude is set in Properties apart from the amplitude of the sine, as in SPICE, and is 1 by default, so
  the plot reads as gain while a transient uses the small amplitude a real circuit would see.
- A stepped DC sweep draws one curve per step, labeled with the step value where the curves part most, and lists
  every curve in the statistics, named by its step value.
- The statistics sit under the plots, one table with a row per reading and a column per trace, so two traces
  compare across a row; every reading has four significant digits. The transient, AC sweep and DC sweep have
  **Cursors** on the line over that table. Turning them on places two draggable vertical lines, whose positions
  show on the same line; the table then measures every shown trace between them, or over the visible range when
  they are off. The transient reads peak-to-peak, mean, RMS and frequency, and the value at each cursor with
  the change and the time between them; the AC sweep reads the gain and phase at each cursor and its response over
  the whole sweep; the DC sweep reads the value at each cursor, the change and the slope between them, and the
  minimum, maximum and mean over the span. The slope is in the unit of the trace over that of the sweep: a voltage
  over a swept voltage is a gain (V/V), and a current over a swept voltage a conductance (A/V), whose resistance
  (the inverse) is shown beside it, as `open` rather than an infinite value when the slope is zero.

## Stepping a parameter

The operating point, the transient, the AC sweep and the DC sweep can run once per value of a parameter, as SPICE's
`.step`: set **Run mode** to **Step a parameter** under their settings, then pick:

- **Parameter**: the temperature, or a resistor, capacitor or inductor (shown with its value). Picking a part starts
  the range at half its value and ends it at twice its value.
- **Range** (start, stop and step, which may be negative to step downward) or **List** (values such as
  `1k, 2.2k, 4.7k`). A list starts from the values of the range. Up to 50 values; the line below tells how many runs
  that makes.

The part keeps its value in the schematic and in the Netlist window; each run simulates a copy at the step value.

Results:

- The transient and the AC sweep plot every step: one **shown step** in full colors, picked above the plot, and the
  others faint. Each trace the steps change is labeled with the step values. The statistics, the spectrum, the
  schematic and its playback read the shown step. The AC export includes every step (`V(4) at C1=22nF` columns);
  the transient export has the shown step only, since each step has its own time points.
- The DC sweep draws one curve per step, as when stepping a second source, so only one of the two can be on.
- The operating point picks its **shown step** at the top of its tab; the schematic (voltages, currents, wire colors,
  heat map) and the tables below read that step. Above the tables, a plot draws every measured voltage and current
  against the step value, with a marker at each step, so `V(OUT)` against R1 from 1k to 10k reads at once; measure the
  traces with the Probe tool or in the trace list, which lists the nodes and currents of the steps.
- ngspice's output lists each step under a `Step R1=10kOhm` line. A step that fails stops the run and names the
  step.

## Monte Carlo

Real parts are not exact: a 1k resistor with a 5 % tolerance may be anything from 950 to 1050 ohm. A Monte Carlo
analysis runs the operating point, the transient, the AC sweep or the DC sweep once with every value as it is (the **nominal** run) and then
once per **run**, each with the values of the parts drawn within their tolerances, and shows how a result spreads.

**Setting it up**

1. Give the parts a tolerance: select a resistor, capacitor, inductor or DC voltage source and fill **Tolerance** (in
   percent, above 0 and up to 50) in its properties. With a series picked in Preferences (E12, E24...), the properties
   offer **Use E24 (5%)**, the tolerance of that series. An empty field means the part keeps its value in every run.
   In the analysis settings, **Tolerance of all** gives every resistor, capacitor or inductor one percent at once, and
   **Clear all** takes the tolerances away. A source only varies while it is a DC one.
2. Pick the operating point, transient, AC sweep or DC sweep and set **Run mode** to **Monte Carlo** (the other modes are **Single
   run** and **Step a parameter**; only one is on). Live mode and a second stepped source of the DC sweep do not go
   with it.
3. Set **Runs** (2 to 1000, 100 at first), the **Distribution** and the **Seed**, and run.

**Distribution.** *Gaussian* takes the tolerance as three standard deviations and never draws a value beyond it, so
most parts land near their value; *uniform* draws any value within the tolerance with the same chance. Every part is
drawn on its own.

**Seed.** The same seed draws the same values, on any computer, and run 17 of a seed is the same whether or not the
runs before it were made. **New seed** picks another one. The seed is saved with the schematic, so a saved analysis can
be repeated.

**Blocks.** The parts inside a block are not varied yet: a block with a toleranced part refuses the run and names the
block and the parts; clear their tolerance, or take the parts out of the block.

**Results.** The transient and the AC sweep plot the runs as the stepped ones are plotted: the **shown step** is
**Nominal** or **Run n**, in full colors, and the others are faint; the DC sweep draws one curve per run. Only the
first 100 runs keep their curves (or, for the operating point, their values to show on the schematic), since a thousand transients would not fit in memory; every run keeps its
measurements. Runs that fail to converge are skipped, and a warning counts them and lists the first ones; the nominal
run has to work. Stop keeps the runs made.

The **Monte Carlo** tab of the Output window shows:

- a **histogram** of one quantity measured on one node in every run, with a line at the nominal value. The quantities
  are the ones the statistics of the other tabs read: for a transient the mean, RMS, peak to peak and frequency; for
  an AC sweep the upper and lower -3 dB frequency, peak gain, unity-gain frequency and phase margin; for a DC sweep the
  minimum, maximum and mean. An operating point measures the DC voltage of every node and the DC current of every
  part, so the first list offers the currents, such as `I(R1)`, after the nodes;
- on the right, the nominal reading and, over the drawn runs, their count, mean, standard deviation (with n - 1),
  minimum, maximum and the 1 % and 99 % percentiles;
- a **table of runs** with the value each part took and the quantity shown; click a header to sort, which puts the
  worst case first, and click a row to show that run in the operating point, transient or AC sweep tab and on the schematic (a row beyond the first 100
  runs has no curve to show);
- **Export runs...** saves every run, the values of the parts and all the measurements as a CSV table.

## Spectrum

The Spectrum tab shows the frequency content of the measured transient traces, as a spectrum analyzer would, and
measures their harmonic distortion.

- It covers the same stretch as the transient statistics: between cursors A and B when they are on, otherwise the
  time the transient plot shows. Zoom past the start-up of the circuit, or place the cursors on the steady state,
  before reading distortion.
- Amplitudes are peaks in dB relative to 1 V (dBV) or 1 A (dBA), on the axes the traces use in the transient; a
  sine of 1 V peak reads 0 dBV. The mean is left out of the plot and shown in the panel instead.
- The panel gives, for each trace, its mean, the frequency and peak amplitude of its fundamental (its strongest
  tone), the THD (total harmonic distortion: the root sum of squares of harmonics 2 to 10 over the fundamental), and
  each harmonic in dB below the fundamental (dBc). Dots on the plot mark the harmonics.
- **Hann** window (default): suits any stretch, but needs at least 5 periods of the fundamental in it.
  **Rectangular**: sharper peaks, and works from 3 periods, but only reads right over a whole number of periods,
  such as cursors placed exactly some periods apart; otherwise each tone leaks across the spectrum.
- A trace with no tone standing out of the floor around its strongest peak (at least 20 dB above the median of the
  bins next to it) is read as noise: the panel then gives its **Density** and **Noise** instead of a fundamental, THD
  and harmonics, and asks for no periods. The **Noise density** checkbox, which opens by itself when every trace is
  noise (and then also turns the frequency axis logarithmic), plots the spectrum of the noise traces as a density in
  V/√Hz (A/√Hz) on logarithmic axes; traces with a tone have no density and are left out of that view. The density
  does not depend on the stretch or the window; hover the plot for the density at a frequency.
- The resolution is one over the stretch: 10 ms resolves 100 Hz. The transient is read between its points by
  straight lines, which leaves a floor around -130 dB for a sine at 200 points per period; a smaller time step
  lowers it.

### Noise and random sources

A voltage or current source set to **Noise** adds noise to a transient on top of its Offset, and one set to
**Random** makes random values. Both show up in the Spectrum tab as a floor.

- **White rms** is the rms of the Gaussian white noise and **Sample step** the time between its samples; the output is
  a straight line between them. The line under the fields gives the density, the white rms times the square root of
  twice the step (`rms * sqrt(2 * step)` in V/√Hz or A/√Hz), and says it is flat up to `1 / (2 * step)`. For 100 mV
  and 10 us that is 447 uV/√Hz up to 50 kHz.
- The dB plot shows the floor as the peak of a sine per bin, so it depends on the stretch: with the Hann window a bin
  of width `df` reads `sqrt(2 * density^2 * 1.5 * df)`, and a longer stretch lowers the floor. The tab does the
  division for you, `density = amplitude / sqrt(2 * 1.5 * df)` (1 in place of 1.5 for the Rectangular window): its
  **Density** is the level of the floor over the frequencies the plot shows (the band is cut in up to 8 slices,
  and the middle of the medians of the slices within 3 dB of the highest one is the level, so a part of the band past
  where the noise rolls off does not pull it down; the first bins, where the
  window spreads the mean, are left out), and **Noise** the rms of the noise over the whole band, which does
  include that part. The noise example's input is a flat 1 mV/√Hz, and its Density reads a little under that (about
  0.7 to 1 mV/√Hz), since the straight lines between the samples already take some off from 4 kHz.
- **1/f amplitude** with an **exponent** between 0 and 2 (both excluded) adds flicker noise, which falls 10 dB per
  decade of frequency at an exponent of 1. **Telegraph** adds a signal that jumps between 0 and its amplitude, staying
  up **Capture time** and down **Emission time** on average. An amplitude of 0 turns the part off.
- The step also steps the simulation: ngspice computes a point at each sample, so a step much shorter than the time
  step gives many points. A run that would span more than a million points is refused, noise steps included.
- The noise changes on each run. The **Seed** of the transient settings (0 draws new ones) makes the Random sources and
  the telegraph part repeat, but never the white and 1/f noise, which ngspice does not seed. A seeded Gaussian
  Random source repeats only every other run in one session, since ngspice keeps half of its last pair of Gaussian
  values for the next run; the other distributions repeat every run.
- **Random** holds a new value every **Sample step**, after the **Delay**, from a distribution whose two parameters
  are called after it: uniform (range around the offset), Gaussian (standard deviation, mean), exponential (mean,
  offset) or Poisson (lambda, offset).
- A source has one waveform, so noise on top of a sine or a pulse takes a second Noise source in series with the
  first.
- The AC sweep, operating point and DC sweep see only the offset. The noise analysis is separate: it computes the
  noise of the parts and ignores these sources.

## Noise

The Noise analysis computes the random noise of the circuit at one node across frequency, as ngspice's `.noise`
does. Pick it beside Run, then set:

- **Output**: the node whose noise is measured. It starts on the first measured voltage that no voltage source holds
  (a node a source holds has no noise), and follows its node through edits.
- **Input**: a voltage or current source set to AC. Its AC magnitude does not matter: it only defines the gain from
  the input to the output.
- **Start**, **Stop** and **Points per decade**, as in an AC sweep; Suggest picks the same range.

The Noise tab of the Output window plots, on logarithmic axes:

- the noise density at the output in V/√Hz, in the color of the output node;
- the input-referred noise, dashed: the output noise divided by the gain from the input source, so the noise a
  source at the input would need to cause it. It shares the axis with the output for a voltage source and has its
  own axis in A/√Hz for a current source. It climbs where the gain falls, as above the corner of a filter.

The panel gives the RMS noise over the whole range at the output and at the input, then the share of every part in
the output noise, largest first, as RMS and as a percentage of the noise power (the percentages add up to 100).
Check a part to plot its share; parts whose noise does not reach the output are left out.

When the input source hardly reaches the output (a gain under -120 dB, as through a logic gate), its input-referred
noise would only grow without meaning, so it is left out and the tab says why. When no noise reaches the output at
all, as on a node an ideal source holds, the tab says so instead of an empty plot. [Simulation models](models.md) lists which parts add noise.

## Transfer function

The Transfer function analysis reads the small-signal DC gain from one source to one node, and the resistances seen
at both ends, as ngspice's `.tf` does. It is instructive for amplifiers: it gives the gain, the input impedance the
source sees and the output impedance looking back into the output, without a frequency sweep. Pick it beside Run,
then set:

- **Output**: the node the gain is measured at, picked as in the noise analysis and kept by a point of its node.
- **Input**: a voltage or current source, VCC, clock or logic input. It needs no AC setting and no particular value:
  ngspice only uses it to define the gain from it to the output, so a plain DC source works.

The Transfer function tab of the Output window is a table of three values:

- **Gain**: the output over the input. It is dimensionless (V/V) for a voltage source and a transimpedance (V/A) for
  a current source.
- **Input impedance**: the resistance the source sees, in Ohm.
- **Output impedance**: the resistance looking back into the output node, in Ohm.

An output an ideal voltage source holds has an output impedance of 0 Ohm, and an input path held open reads as
`open` rather than a giant number. The table is a readout like the operating point, so it is not exported.

## Sensitivity

The Sensitivity analysis tells which parts matter most for one node voltage: ngspice's `.sens` changes the value of
every part a little, one at a time, around the DC operating point, and reports how much the voltage of the output moves.
Pick it beside Run, then set **Output**, the node whose voltage is measured, picked as in the noise analysis and kept by
a point of its node. A circuit with logic gates or flip-flops cannot run it (ngspice cannot recover from it), and the
analysis is disabled with that reason as its tooltip.

The parts listed are resistors, capacitors, inductors, voltage and current sources and supply rails (their DC value),
and the gain of controlled sources. A potentiometer counts as its total resistance, the sum of its two halves. A block
is listed by the element inside it that matters most, named as `X2.R1`. Diodes, transistors, op-amps, switches, relays,
fuses, transformers and logic parts are not listed, and the model parameters of diodes and transistors are not shown:
ngspice reports them, but unreliably.

The Sensitivity tab of the Output window is a table sorted by influence, with a click on a row selecting the part:

- **Value**: the value of the part, when the result carries it (not for an element inside a block).
- **Absolute**: the change of the output voltage per unit of the value, in V/Ohm, V/F, V/H or V/V.
- **Per 1 %**: the change of the output voltage, in volts, for a value 1 % higher. This normalized sensitivity compares
  parts of different kinds, which the absolute one does not, and it is what the colors follow.
- **Share of largest**: the size of the part's normalized sensitivity as a share of the largest one.

The sign says the direction: negative means a higher value lowers the output voltage, positive that it raises it. A
voltage source's row is the share of the output it gives, and a capacitor, which is open at DC, reads zero.

It is a small-signal DC analysis at the operating point, so a circuit driven only by AC sources has no DC output to
move and reads zero everywhere; give it a DC level, or use the AC sweep instead. The table is a readout like the
operating point, so it is not exported. To see the same values on the schematic, tick **Color the schematic** at the top of the tab, which is the same as
**View > Wire Colors > Sensitivity**; untick it to go back to plain wires. The legend then reads "Change of V(out) per
1 %", with the volts for a 1 % change at its two ends. A supply rail is named in the table as on the schematic, such
as `VCC 12V`; clicking its row still selects it.

## Poles and zeros

The Poles and zeros analysis finds where the transfer function from a source to a node has its poles and zeros, which
say how the circuit responds: poles set the corners of a Bode plot and its resonances, zeros its notches and where it
turns up. Pick it beside Run, then set **Output**, the node whose voltage is measured, and **Input**, the source that
drives the circuit, both picked as in the Transfer function analysis. A voltage source (or supply rail, clock or logic
input) gives the voltage gain from it to the node, and a current source the transimpedance in ohms.

The roots belong to the circuit linearized at its DC operating point and only to the part between the input and the
output, with the source's other terminal and every supply rail taken as AC ground. A circuit with no capacitor or
inductor between them has none, which reads as an error. Circuits with logic gates or flip-flops cannot run it, and the
analysis is disabled with that reason as its tooltip.

The Poles and zeros tab of the Output window has the s-plane at the top, with the table under it. Real parts run across and imaginary parts up,
both in rad/s, with a **×** for each pole and a **○** for each zero; a complex pair is drawn as its two members.
The axes are symmetric logarithmic, so roots decades apart stay visible (they start at 10 rad/s, with 0 in the middle),
and **Linear axes** shows their true spacing.
Hover a mark to read its kind and frequency. A root in the right half-plane is drawn and listed in the warning color:
it means the circuit is unstable at its operating point, or that ngspice's solver got it wrong; compare it with an AC
sweep.

The table has one row per real root and per complex pair:

- **Real** and **Imaginary**: the root in rad/s; a pair reads `+/-` before its imaginary part.
- **Frequency**: the natural frequency, the size of the root over 2π, in Hz. For a first-order pole it is the corner of
  the Bode plot.
- **Q** and **Damping**: the quality factor, the size of the root over twice its real part, and the damping ratio, one
  over twice Q, for complex pairs; a real root reads a dash, and a pair on the imaginary axis an infinite Q.

A pole and a zero at the same place cancel, so they are left out and the note under the plot counts them: ngspice
reports such pairs for parts the signal never reaches. A warning above the plot says when ngspice gave up searching for
some roots, so some may be missing.

To compare with the Bode plot, run the AC sweep too and tick **Show poles and zeros** on its tab: each pole and zero is
a dashed vertical line at its natural frequency on both plots, colored as in the s-plane, and labelled on the magnitude
plot (`p 5.03k` for a pole, `z` for a zero). A second-order low-pass reaches
its -3 dB point near its natural frequency, a little above it when Q is over 0.71.

ngspice's pole-zero solver is not reliable on every circuit. It can find nothing without saying why, as with some ideal
op-amp circuits at certain gain-bandwidth products (try another product or a real op-amp model), and it may leave out
roots or return wrong ones; treat the result as a check on the AC sweep rather than as the last word. The table is a
readout like the operating point, so it is not exported.

## S-parameters

The S-parameters analysis measures how a circuit scatters a signal between its ports, as a network analyzer does:
S11 is the share of the wave sent into port 1 that comes back (the reflection), and S21 the share that arrives at
port 2 (the transmission). Waves are normalized to the reference impedance of each port, 50 ohm by default, so an S11
of 0 is a perfect match and an S21 of 0 dB passes everything.

**Ports**: the ports are Block pin markers of the top-level schematic. Select a marker, turn on **S-parameter port**
in its properties, and set its **Number** and **Z0**. A new port takes the lowest number no other marker uses; the
numbers must run from 1 to N with no gap and no repeat, and the symbol reads `P1`, `P2 (OUT)`. A marker that is not a
port does nothing here, and a port is invisible to every other analysis: only in an S-parameter run does imcsim put a
source and the reference impedance at its node. Inside a block, port settings are kept and ignored. The analysis needs two
or more ports in all, a Ground in the circuit, a node of its own for each port (not ground, not shared) and a Z0 above
zero; imcsim says which is wrong before it runs.

**Measuring a one-port**: the analysis needs two ports, as ngspice refuses a single one. Add a second port on an
isolated node, with nothing connected to it: S11 is then the reflection of the first port, and S22 reads 1.

**Bias**: a port holds its node at 0 V in DC and loads the circuit with its reference impedance, so a node that has a
bias belongs behind a capacitor (or an inductor, for a port on a supply line), as the bias tee of a network analyzer
does. The circuit's own sources keep their DC value and are AC ground for the measurement.

**Settings**: pick S-parameters beside Run, then the **Start** and **Stop** frequencies (1 MHz to 1 GHz by default),
the **Sweep** type and the points: log with points per decade (100 by default), or linear with the number of points in all (at least 3,
since ngspice gives a single point for two). The settings are saved with the schematic. Circuits with logic gates or
flip-flops cannot run it, and the analysis is disabled with that reason as its tooltip.

The **S-parameters** tab of the Output window has:

- **Entries shown**: a checkbox for every entry S(row, column) of the matrix. Each view keeps its own choice: the
  Smith chart is for reflections, so it starts with S11, S22... checked and no transmission, while the magnitude and
  phase plots start with S11 and S21. Check S21 on the chart too if you want it.
- **Smith chart**: each checked entry is a curve of its reflection coefficient over the circles of constant resistance
  (0.2, 0.5, 1, 2 and 5 times the reference) and the arcs of constant reactance (labelled `+j0.5`, `-j2`...), on
  equal axes; a dot marks the first frequency. A point inside the chart is passive; an S21 above 1 leaves the outer
  circle, and the chart grows to show it.
- **Magnitude and phase**: the checked entries in decibels (20 log |S|) over their phase in degrees, on a logarithmic
  frequency axis shared by both plots.
- **Readings**: hover a curve for its frequency, magnitude in decibels and angle. A reflection (S11, S22...) also reads
  the impedance the port sees, absolute and normalized, computed from S as Z0 (1 + S) / (1 - S), the VSWR,
  (1 + |S|) / (1 - |S|), and the return loss, -20 log |S| (positive when the port absorbs power); an open circuit or a
  perfect match reads infinite.

**Exports**: Export... saves the plot shown as an SVG image (the Smith chart or the magnitude and phase panels, redrawn
from the data, in the light or dark theme) or as a CSV table with the frequency and the real and imaginary parts of each
checked entry. **Save Touchstone...** opens a small popup for the frequency unit and format, then writes the whole
matrix as a Touchstone version 1 file, `.sNp` for N ports, in the frequency unit (Hz, kHz, MHz or GHz) and format you
pick there: RI (real and imaginary parts), MA (magnitude and angle in
degrees) or DB (decibels and angle in degrees). The default, the one most tools write, is `# GHz S MA R 50`. A two-port
lists its entries as S11 S21 S12 S22, as the format says, which is not the order of the matrix; larger matrices go row
by row, four pairs to a line. The file holds one reference impedance, so ports with different Z0 cannot be exported
and the message names them.

The S-parameters example (Examples > Basics) is a T low-pass of two 8 uH inductors and a 3.18 nF capacitor between two
50 ohm ports: S11 starts at the center of the chart, matched, and sweeps out to the rim as the inductors block the
signal, while S21 stays near 0 dB up to about 1 MHz.

## Truth table

The Truth table analysis builds the truth table of a digital circuit: it sets the logic inputs to every combination,
runs an operating point for each and reads the outputs, so nobody has to flip the inputs by hand. Pick it beside Run,
then choose:

- **Inputs to vary**: a checkbox per Logic input, all of them by default. At least one stays, and at most 10 (1024
  combinations) are allowed. A Logic input left unchecked keeps the High or Low it has in the schematic for every row.
- **Outputs**: the voltages you already measure with the Probe tool; the popover lists them. The table needs at least
  one.

The Truth table tab of the Output window lists one row per combination in counting order, the first input being the
most significant bit: the inputs as 0 and 1, then each output as 0, 1 or X. An output reads 1 above 70% of its logic
level and 0 below 30% of it, as the logic parts do; between them, or when ngspice found no operating point for the
combination, it reads X. The row the inputs have in the schematic right now is highlighted, and Copy as Markdown puts
the table on the clipboard.

Each row is an independent operating point, so the table describes combinational circuits. A latch or flip-flop whose
outputs depend on its history has no operating point for the combinations that hold its state; those rows read X and
the Run report says how many. The table is not exported as a figure, like the operating point.

## Exports

**Plots**: Export... on the right of the Output window tabs saves the plot of the current tab.

- **SVG image**: redrawn from the data, not captured from the screen, so it stays sharp at any size. It shows the
  visible X range, so zoom in first to export a detail. Choose its size in pixels and its theme: light for print,
  which darkens trace colors too light for white paper, or dark, in the colors of the Graphite theme.
- **CSV table**: every sample of every measured trace, including math channels, whatever the zoom (the S-parameters
  tab lists the real and imaginary parts of the checked entries, and also saves Touchstone files). The first column
  is the X axis; each header carries its unit, and on a Bode plot its panel (`Magnitude V(2) (dB)`). Missing
  samples, such as a division by zero in a math channel, are empty cells.

**Schematic**: File > Export Schematic... saves the circuit as an SVG image, cropped around it, with the symbol
style shown (IEC or ANSI), part names and values, terminal numbers when they are shown, and the measured voltages
and currents. Light, for print, darkens colors the same way as plot exports, so a probe and its trace keep matching
colors across the two figures; As on screen keeps the colors of the current theme.

The save dialogs suggest a name after the schematic file and the tab, such as `rc_filter-transient.svg` or
`rc_filter-schematic.svg`, next to the schematic.
