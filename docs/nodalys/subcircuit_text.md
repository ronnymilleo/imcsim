# Subcircuits Kept as SPICE Text

A vendor's library gives a `.subckt` as text: an op-amp macromodel, a regulator, a switch. `imcsim` keeps such a block as text (`SubcircuitDefinition::Spice`), and `ngspice` reads it as it came. `Nodalys` reads it too, with [`AddSubcircuitText()`](../../nodalys/include/nodalys/subcircuit_text.h), which expands one instance of the text into a `CircuitBuilder`, at the nodes the instance is connected to, with the parameters the instance sets. What it cannot simulate it refuses with a message that names the subcircuit and the line, so no line is dropped without a word.

`ParseNetlist()` places the `X` lines of a netlist through the same reader (section 5), so the `nodalys` command line has the subcircuits that `imcsim` has: the text is read on tokens with the scopes of SPICE, with no guessing of which tokens are nodes. The device parameter tables are read from [`model_cards.cpp`](../../nodalys/src/parser/syntax/model_cards.cpp), which both share.

---

## 1. API

```cpp
struct SubcircuitText {
    std::string Name;                  // for the messages
    std::vector<std::string> Ports;    // the node names of the .subckt line
    std::string Body;                  // everything between .subckt and .ends
};
struct InstanceObservers {
    std::function<void(const std::string &name, NodeId node)> OnInternalNode;
    std::function<void(const std::string &name, NodeId positive, NodeId negative)> OnSource;
};
struct SubcircuitTextInstance {
    std::string Prefix;                // "X1": the elements are named X1.R1, X1.D3...
    std::vector<NodeId> PortNodes;     // one node of the circuit per port
    std::map<std::string, double> Parameters;  // defaults and overrides, settled by the caller
    InstanceObservers Observers;       // optional node/source discovery callbacks
};

std::expected<std::vector<std::string>, std::string>
AddSubcircuitText(CircuitBuilder &, const SubcircuitText &, const SubcircuitTextInstance &);
std::optional<std::string> CheckSubcircuitText(const SubcircuitText &, const std::map<std::string, double> &);
```

`AddSubcircuitText()` returns the warnings, or the reason the text cannot be simulated; after a failure the builder holds part of the elements and is discarded. `CheckSubcircuitText()` expands the text into a builder of its own and says only whether it can be simulated. `imcsim` calls it from `BackendCapabilities::FindPartProblem`, so the editor shows the reason before a run, as it does for the expression of a behavioral source.

The compiler of `imcsim` ([`compiler.cpp`](../../app-core/backends/nodalys/compiler.cpp)) calls `AddSubcircuitText()` for a block with `Spice`, with the name of the instance (and of the blocks it sits in) as `Prefix`, the nodes of the instance as ports, and the defaults of the definition overridden by the instance as `Parameters`. The warnings reach the user as messages of the run.

## 2. What is read

| Content | Reading |
| :--- | :--- |
| Ports | Each port is the node the instance connects it to. Names are read without regard to case. |
| Internal nodes | Private to the instance: a name no port maps gets a new node of its own, so two instances never share one. `0` and `gnd` are the ground of the circuit. |
| Elements | `R`, `C`, `L` (value, `IC=`), `V` and `I` (a DC value, an `AC` magnitude and phase, and any waveform: `PULSE`, `SIN`, `EXP`, `PWL`, `SFFM`, `AM`, `TRNOISE`, `TRRANDOM`), `E` and `G` (gain, or `VALUE={expression}`), `H` and `F` (a gain, from a voltage source of the same block), `B` (`V=` or `I=` with an expression), `D` and `Q` (an area after the model or as `AREA=`, which scales the currents and capacitances up and the resistances down), `M` (`L=`, `W=`), `S`, `T` (`Z0=`/`ZO=`, `TD=` or `F=`/`NL=`, plus optional `RDC=`, `RSKIN=`, `GDC=`, `TAND=`, `FREF=`) and `X`. |
| Element names | The name of the instance, a dot, and the name of the element: `X1.R1`, `X1.XLOAD.R2`. Two instances of one block never collide, and the results of `imcsim` still hold the nodes of the circuit only. |
| Parameters | The values of the instance are the parameters of the block. A `.param` line adds names and may use the ones before it; a value the instance passes wins over a `.param` of the same name. A nested `.subckt` with `params:` takes its defaults, and an X line overrides them with `NAME=VALUE`. |
| Values | A literal (`4.7k`, `100n`, `1meg`) or an expression of parameters, in braces, quotes or bare: `{RTOP * 3}`, `'GAIN/1Meg'`. The language is that of the behavioral source (`pi`, `e`, `temper`, the functions of [`behavioral_source.md`](./behavioral_source.md)), evaluated once. A value that reads a voltage, a current or `time` is refused. |
| Source waveforms | Read by the code that `ParseNetlist()` uses for the same words ([`source_waveforms.cpp`](../../nodalys/src/parser/sources/source_waveforms.cpp)), so a source in a block gives the same signal as one written at the top level of a netlist. The numbers in the parentheses are literals or expressions of the parameters of the block, such as `PULSE(0 {VHIGH} 0 1n 1n {TON} {2*TON})`; one that cannot be evaluated is refused. |
| Behavioral expressions | The names inside `v(...)` are nodes of the block, which become the nodes of the circuit, and `i(...)` names a voltage source of the block. Parameters inside the expression are replaced by their values first. |
| `.model` | `D`, `NPN`, `PNP`, `NMOS`, `PMOS` (level 1) and `SW`/`VSWITCH`, with parameter values that may be expressions. The card is read in the scope it is written in, and a nested block sees the cards and parameters of the text around it. A card nobody uses is not checked for its type, as in `ngspice`. |
| Nested `.subckt` | Written inside the text, placed with X lines, nested as deep as 16 levels. A block that places itself is refused. |
| Syntax | Continuation lines (`+`), comment lines (`*`), inline comments (`;` and ` $`), spaces around `=` and inside braces. |

### 2.1 Model parameters

The devices of `Nodalys` read the parameters of [`model_cards.cpp`](../../nodalys/src/parser/syntax/model_cards.cpp). Any other parameter of a card the text uses, such as `CJS` or the geometry of a MOSFET, is not read, and the warning says which: `Nodalys does not read the parameters CJS, VAR of the model DMOD in the subcircuit OPA1`. It is reported once for each model.

## 3. What is refused

Each refusal names the subcircuit and the line: `Nodalys cannot simulate the subcircuit OPA1: the line 'J1 a b c JMOD' cannot be simulated: elements that start with J (a JFET) are not simulated by Nodalys`.

| Content | Message says |
| :--- | :--- |
| `J`, `Z`, `K`, `O`, `U`, `W`, `A`, `P` and any other element letter | the letter and what it is |
| A source word that is not `DC`, `AC` or a waveform | the word, and that only DC and AC values and the waveforms are simulated |
| `POLY`, `TABLE`, `LAPLACE`, `FREQ` forms of `E`, `G`, `H`, `F` | the form |
| An option such as `TC1=`, `AD=`, `M=` or an initial state `ON` | the option |
| `.func`, `.include`, `.lib`, `.global`, `.options`... | the directive |
| A model of another type (`NJF`, `R`...), or a level other than 1 | the model, its type or level |
| A name that is not defined (model, subcircuit, parameter, controlling source) | the name |
| An expression `Nodalys` cannot read (`limit()`, `hertz`, random functions) | the message of the expression reader |
| A port count that does not match | the two counts |

`.include` and `.lib` are never met in practice: `imcsim` puts what a block needs into its text when it imports the library.

## 4. Limits

- The check before the run reads the text itself, so it cannot know the analysis: a switch in a block that runs the harmonic balance is refused by the analysis, with the usual message, not by the check.
- The substrate node of a bipolar transistor is read and ignored, since the device has no substrate junction.
- A parameter default that is an expression of another parameter is evaluated where the block is placed, in the scope of its definition, as in `ngspice`.

## 5. Subcircuits of a netlist

`ParseNetlist()` no longer flattens `.subckt` blocks into text lines. It keeps what a netlist defines for its instances, in the order written: the `.subckt` blocks (with their `.ends`, nested blocks included), the `.model` cards and the `.param` lines of the top level. Each `X` line is then placed by [`AddNetlistInstance()`](../../nodalys/src/parser/netlist/netlist_subcircuits.h), which opens a root `TextScope` for the netlist, maps the instance's port nodes, and expands the call directly via `ExpandTextInstance()` ([`netlist_subcircuits.cpp`](../../nodalys/src/parser/netlist/netlist_subcircuits.cpp)). The definitions are therefore the text around the instance, as the text of a block is for the blocks nested in it.

| In a netlist | Reading |
| :--- | :--- |
| `.subckt NAME a b params: RT=1k RB=2k` | The ports, then the parameters with their defaults; the word `params:` may be left out. |
| `X1 n1 n2 NAME RT=2k` | The nodes, the subcircuit and the values that override the defaults; they may be expressions of the top-level `.param` lines, such as `RB={2*RTOP}`. |
| `.param` at the top level | Visible inside every block, as the parameters of the text around a definition are. A default or an override with the same name wins, as in a block of `imcsim`. |
| `.model` at the top level or inside a block | A block sees the cards of the netlist and its own; the inner card hides an outer one of the same name. |
| `B`, `E`, `G`, `H`, `F`, `S` and waveform sources inside a block | Everything the reader of a block accepts, with the names of the instance. |
| Nested `.subckt` | Accepted, up to 16 levels. |

Names: the elements are named `X1.R1` and `X1.XLOAD.R2`, as before, so the names of the currents of a result and the sources named by `.dc`, `.tf` and `.noise` keep their meaning. The internal nodes are named the same way, in upper case: `X1.MID`, `X1.X2.N`. `AddSubcircuitText()` tells its caller each one through `SubcircuitTextInstance::Observers` (`InstanceObservers::OnInternalNode` and `InstanceObservers::OnSource`), and `ParseNetlist()` binds the names to the nodes, so `.noise V(X1.MID) ...`, `.tf`, `.ic` and `.pz` at the top level can name a node inside a block, wherever the directive stands in the netlist, and a `.pz` written by the nodes of its input finds a source inside a block. The instances are placed before the directives are read for that reason. The nodes get numbers after those of the netlist, so results list them after the others; `imcsim` places its blocks without those callbacks and keeps only the nodes of its own circuit in the results, as before.

The refusals are those of section 3, with the called subcircuit as the name in the message, and a netlist whose `.subckt` has no `.ends` is refused. A `.model` card that a block does not use is not checked for its type; the warnings about the parameters a device does not read reach `ParsedNetlist::Diagnostics`, once each.
