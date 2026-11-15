# Subcircuits Kept as SPICE Text

A vendor's library gives a `.subckt` as text: an op-amp macromodel, a regulator, a switch. `imcsim` keeps such a block as text (`SubcircuitDefinition::Spice`), and `ngspice` reads it as it came. `Nodalys` reads it too, with [`AddSubcircuitText()`](../../nodalys/include/nodalys/subcircuit_text.h), which expands one instance of the text into a `CircuitBuilder`, at the nodes the instance is connected to, with the parameters the instance sets. What it cannot simulate it refuses with a message that names the subcircuit and the line, so no line is dropped without a word.

The expansion does not go through `ParseNetlist()`: that function flattens text into lines and renames nodes by guessing which tokens are nodes, with no parameters, no expressions and no scopes, which a model with `{expressions}` and nested definitions cannot live with. The expansion works on tokens with the scopes of SPICE, and `ParseNetlist()` is left as it was, but for the device parameter tables, which both now read from [`model_cards.cpp`](../../nodalys/src/parser/model_cards.cpp).

---

## 1. API

```cpp
struct SubcircuitText {
    std::string Name;                  // for the messages
    std::vector<std::string> Ports;    // the node names of the .subckt line
    std::string Body;                  // everything between .subckt and .ends
};
struct SubcircuitTextInstance {
    std::string Prefix;                // "X1": the elements are named X1.R1, X1.D3...
    std::vector<NodeId> PortNodes;     // one node of the circuit per port
    std::map<std::string, double> Parameters;  // defaults and overrides, settled by the caller
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
| Elements | `R`, `C`, `L` (value, `IC=`), `V` and `I` (a DC value and an `AC` magnitude and phase), `E` and `G` (gain, or `VALUE={expression}`), `H` and `F` (a gain, from a voltage source of the same block), `B` (`V=` or `I=` with an expression), `D`, `Q`, `M` (`L=`, `W=`), `S` and `X`. |
| Element names | The name of the instance, a dot, and the name of the element: `X1.R1`, `X1.XLOAD.R2`. Two instances of one block never collide, and the results of `imcsim` still hold the nodes of the circuit only. |
| Parameters | The values of the instance are the parameters of the block. A `.param` line adds names and may use the ones before it; a value the instance passes wins over a `.param` of the same name. A nested `.subckt` with `params:` takes its defaults, and an X line overrides them with `NAME=VALUE`. |
| Values | A literal (`4.7k`, `100n`, `1meg`) or an expression of parameters, in braces, quotes or bare: `{RTOP * 3}`, `'GAIN/1Meg'`. The language is that of the behavioral source (`pi`, `e`, `temper`, the functions of [`behavioral_source.md`](./behavioral_source.md)), evaluated once. A value that reads a voltage, a current or `time` is refused. |
| Behavioral expressions | The names inside `v(...)` are nodes of the block, which become the nodes of the circuit, and `i(...)` names a voltage source of the block. Parameters inside the expression are replaced by their values first. |
| `.model` | `D`, `NPN`, `PNP`, `NMOS`, `PMOS` (level 1) and `SW`/`VSWITCH`, with parameter values that may be expressions. The card is read in the scope it is written in, and a nested block sees the cards and parameters of the text around it. A card nobody uses is not checked for its type, as in `ngspice`. |
| Nested `.subckt` | Written inside the text, placed with X lines, nested as deep as 16 levels. A block that places itself is refused. |
| Syntax | Continuation lines (`+`), comment lines (`*`), inline comments (`;` and ` $`), spaces around `=` and inside braces. |

### 2.1 Model parameters

The devices of `Nodalys` read the parameters of [`model_cards.cpp`](../../nodalys/src/parser/model_cards.cpp). Any other parameter of a card the text uses, such as `CJS` or the geometry of a MOSFET, is not read, and the warning says which: `Nodalys does not read the parameters CJS, VAR of the model DMOD in the subcircuit OPA1`. It is reported once for each model.

## 3. What is refused

Each refusal names the subcircuit and the line: `Nodalys cannot simulate the subcircuit OPA1: the line 'J1 a b c JMOD' cannot be simulated: elements that start with J (a JFET) are not simulated by Nodalys`.

| Content | Message says |
| :--- | :--- |
| `J`, `Z`, `K`, `T`, `O`, `U`, `W`, `A`, `P` and any other element letter | the letter and what it is |
| A source waveform (`PULSE`, `SIN`, `PWL`...) | the waveform, and that only DC and AC values are simulated |
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
