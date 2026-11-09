# **Nodalys** Modularization, Public API & CLI Architecture Plan

This document establishes the architectural roadmap for transforming **Nodalys** from an internal solver embedded in `app-core/native/` into an autonomous, modular C++23 simulation library (`libnodalys.a`) and standalone command-line executable (`nodalys`).

---

## 1. Motivation & Strategic Vision

Currently, Nodalys resides directly within `app-core/native/`, tightly coupled to `app-core`'s data structures (`Core::Circuit`, `Core::AnalysisConfig`, `Core::SimulationResult`). While this facilitated rapid initial development and immediate integration with `imcsim-gui`, treating Nodalys as an autonomous engine offers key advantages:

1. **Independent Verification & Tooling**: Nodalys can be executed, benchmarked, and fuzzed via CLI against standard SPICE netlists independently of the GUI or core application lifecycle.
2. **Clean Separation of Concerns**: The numerical engine focuses strictly on circuit mathematics, matrix assembly, device physics, and time/frequency integration, while `app-core` handles schematic topology, file formats, undo/redo, and application state.
3. **Multi-Consumer Reusability**: The public API can be consumed by the existing desktop GUI, the new standalone CLI, automated regression pipelines, Python/C API bindings, or embedded hardware-in-the-loop systems.
4. **Gradual Repository Extraction**: Retaining Nodalys at the repository root (`/nodalys`) during active development avoids submodule desync friction while keeping the boundary strictly enforced via CMake. When mature, `git subtree split` can cleanly sever it into an independent repository with full commit history.

---

## 2. Monorepo Directory Layout

Nodalys is elevated from `app-core/native/` to a dedicated top-level directory `/nodalys` at the repository root:

```
imcsim/
├── CMakeLists.txt                      # Top-level CMake orchestration
├── app-core/                           # IMC-Sim application core (schematics, components, models)
├── app-gui/                            # Qt6 / QML graphical user interface
├── vendor/                             # Monorepo third-party vendor dependencies
│   └── CLI11/                          # Git submodule: https://github.com/CLIUtils/CLI11.git
└── nodalys/                            # Standalone C++23 circuit simulation engine
    ├── CMakeLists.txt                  # Independent CMake build definition
    ├── include/
    │   └── nodalys/                    # Public headers (clean API boundary)
    │       ├── nodalys.h               # Master umbrella include
    │       ├── version.h               # Version macros & feature-flag definitions
    │       ├── circuit.h               # Engine circuit representation & device handles
    │       ├── circuit_builder.h       # Fluent programmatic C++ circuit construction API
    │       ├── analyses.h              # DC, Tran, AC, TF, Noise parameter contracts
    │       ├── results.h               # Numerical result containers & column metadata
    │       ├── netlist_parser.h        # SPICE netlist (.cir, .sp, .net) parser
    │       └── export.h                # Exporters: SPICE Raw (.raw), CSV/TSV, JSON
    ├── src/                            # Engine private implementation
    │   ├── api/                        # Public API wrappers & dispatchers
    │   ├── compiler/                   # Node indexing, topology validation, matrix dimensioning
    │   ├── mna/                        # Real & complex MNA matrices, Gmin stamping, KCL checks
    │   ├── linear/                     # Dense LU factorization, row equilibration, refinement
    │   ├── newton/                     # Damped Newton-Raphson, pnjlim, fetlim, stepping
    │   ├── devices/                    # R, C, L, sources, switches, diodes, BJTs, MOSFETs
    │   ├── analyses/                   # dc, tran, ac, tf, noise execution loops
    │   └── io/                         # Netlist parser lexer/AST, .raw/csv/json writers
    ├── cli/                            # Standalone command-line executable
    │   ├── CMakeLists.txt
    │   └── main.cpp                    # CLI11 argument parsing & simulation dispatch
    └── tests/                          # Standalone engine test suite (Catch2)
        ├── CMakeLists.txt
        ├── test_dc.cpp
        ├── test_tran.cpp
        ├── test_ac.cpp
        ├── test_tf.cpp
        ├── test_noise.cpp
        └── test_netlist_parser.cpp
```

---

## 3. Public API Design

The public API of `nodalys` lives entirely inside namespace `nodalys::` and guarantees **zero external dependencies** outside the C++23 Standard Library (`<vector>`, `<span>`, `<string>`, `<complex>`, `<memory>`, `<expected>`, `<variant>`).

### 3.1. In-Memory Programmatic Builder (`CircuitBuilder`)

Designed for direct programmatic usage by `app-core` and programmatic scripting. It avoids any string serialization overhead by directly populating engine structures.

```cpp
namespace nodalys {

class CircuitBuilder {
public:
    explicit CircuitBuilder(std::string name = "Circuit");

    // Node & Reference Management
    NodeId Ground(); // Always 0
    NodeId AddNode(std::string name);

    // Passive Linear Elements
    void AddResistor(std::string name, NodeId n1, NodeId n2, double resistance_ohms, double temp_c = 27.0);
    void AddCapacitor(std::string name, NodeId n1, NodeId n2, double capacitance_farads, double initial_voltage = 0.0);
    void AddInductor(std::string name, NodeId n1, NodeId n2, double inductance_henries, double initial_current = 0.0);

    // Independent Sources
    void AddVdc(std::string name, NodeId pos, NodeId neg, double dc_volts, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddIdc(std::string name, NodeId pos, NodeId neg, double dc_amps, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddVpulse(std::string name, NodeId pos, NodeId neg, const PulseParams& params);
    void AddVsin(std::string name, NodeId pos, NodeId neg, const SineParams& params);

    // Controlled Sources (E, F, G, H)
    void AddVcvs(std::string name, NodeId pos, NodeId neg, NodeId ctrl_pos, NodeId ctrl_neg, double gain);
    void AddCcvs(std::string name, NodeId pos, NodeId neg, const std::string& ctrl_vsource, double transresistance);
    void AddVccs(std::string name, NodeId pos, NodeId neg, NodeId ctrl_pos, NodeId ctrl_neg, double transconductance);
    void AddCccs(std::string name, NodeId pos, NodeId neg, const std::string& ctrl_vsource, double gain);

    // Semiconductor Devices & Models
    void DefineDiodeModel(std::string model_name, const DiodeParams& params);
    void AddDiode(std::string name, NodeId anode, NodeId cathode, const std::string& model_name, double area = 1.0);

    void DefineBjtModel(std::string model_name, const BjtParams& params);
    void AddBjt(std::string name, NodeId c, NodeId b, NodeId e, const std::string& model_name, double area = 1.0);

    void DefineMosfetModel(std::string model_name, const MosfetParams& params);
    void AddMosfet(std::string name, NodeId d, NodeId g, NodeId s, NodeId b, const std::string& model_name, double w, double l);

    // Finalize into immutable, validated Circuit
    std::expected<Circuit, DiagnosticReport> Build();
};

} // namespace nodalys
```

### 3.2. SPICE Netlist Parser (`netlist_parser.h`)

For the standalone CLI and inter-tool exchange, `nodalys` includes a lightweight, clean-room SPICE netlist parser:

- **Supported syntaxes**: Standard Berkeley SPICE3 / ngspice netlist dialect (`.cir`, `.net`, `.sp`).
- **Component cards**: `R`, `C`, `L`, `V`, `I`, `D`, `Q`, `M`, `E`, `F`, `G`, `H`, `S`, `W`.
- **Model cards**: `.model <name> <type> (params...)` for `D`, `NPN`, `PNP`, `NMOS`, `PMOS`, `SW`.
- **Hierarchy**: `.subckt <name> <pins...>` and subcircuit instantiation `X<name> <pins...> <subckt>`.
- **Directives**: `.op`, `.dc`, `.tran`, `.ac`, `.tf`, `.noise`, `.param`, `.include`, `.ic`, `.nodeset`.

```cpp
namespace nodalys {

struct NetlistParseResult {
    Circuit circuit;
    std::vector<AnalysisDirective> analyses;
    std::vector<DiagnosticMessage> diagnostics;
};

std::expected<NetlistParseResult, DiagnosticReport> ParseSpiceNetlist(std::string_view netlist_content);
std::expected<NetlistParseResult, DiagnosticReport> ParseSpiceNetlistFile(const std::filesystem::path& path);

} // namespace nodalys
```

### 3.3. Execution & Numerical Result Containers (`results.h`)

Simulation outputs are represented as contiguous memory tables to guarantee peak cache efficiency during plotting or exporting:

```cpp
namespace nodalys {

class TableResult {
public:
    std::string x_variable_name;
    std::string x_variable_unit;
    std::vector<double> x_values; // Independent axis (e.g. time, frequency, swept source)

    std::vector<std::string> column_names;
    std::vector<std::string> column_units;
    // Stored as column-major or contiguous rows: [num_columns * num_points]
    std::vector<double> data;

    std::span<const double> GetColumn(size_t col_index) const;
    std::span<const double> GetColumn(std::string_view name) const;
};

struct SimulationResult {
    bool success{false};
    std::string error_message;
    SolverStatistics stats;

    std::optional<OperatingPointResult> op_result;
    std::optional<TableResult> dc_sweep_result;
    std::optional<TableResult> transient_result;
    std::optional<TableResult> ac_result;
    std::optional<TransferFunctionResult> tf_result;
    std::optional<NoiseResult> noise_result;
};

// Top-level simulation dispatch
SimulationResult RunSimulation(const Circuit& circuit, const AnalysisConfig& config,
                              std::function<bool(double progress)> progress_callback = nullptr);

} // namespace nodalys
```

### 3.4. Exporters (`export.h`)

Nodalys provides built-in formatters to export `TableResult` and `SimulationResult` without third-party dependencies:

1. **SPICE Raw (`.raw`)**:
   - Both ASCII (`raw-ascii`) and binary (`raw-binary`) compatible with standard SPICE viewers (Gaw, ngnutmeg, Qucs-S, PySpice).
   - Variables declared with standard types (`voltage`, `current`, `frequency`, `time`).
2. **Tabular CSV / TSV (`.csv`, `.tsv`)**:
   - First-row headers with units (e.g. `Time [s], V(out) [V], I(R1) [A]`).
   - Highly compatible with pandas, NumPy, Excel, and MATLAB.
3. **Structured JSON (`.json`)**:
   - Schema containing circuit metadata, solver statistics (iterations, LU factorizations, cutbacks), operating point values, and data series arrays. Ideal for automated web pipelines and regression testing.

---

## 4. Standalone CLI (`nodalys`) & CLI11 Submodule

### 4.1. Vendor Submodule Strategy

As decided, `CLI11` is integrated as a git submodule in `vendor/CLI11`:

```bash
git submodule add https://github.com/CLIUtils/CLI11.git vendor/CLI11
```

- **Why CLI11**: Single-header or clean CMake target, zero runtime overhead, rich subcommands and options formatting, C++23 friendly, and header-only integration into `nodalys_cli`.
- `libnodalys.a` itself remains 100% free of external dependencies. Only the `nodalys_cli` executable target links to `CLI11`.

### 4.2. CLI Command Syntax

```bash
nodalys [OPTIONS] <netlist.cir>
```

#### Primary CLI Flags

| Flag | Description | Default |
| :--- | :--- | :--- |
| `-o, --output <file>` | Path to output file | `<netlist_basename>.<ext>` |
| `-f, --format <fmt>` | Output format: `raw` (binary), `raw-ascii`, `csv`, `tsv`, `json` | `raw` |
| `-a, --analysis <type>` | Override analysis from netlist: `op`, `dc`, `tran`, `ac`, `tf`, `noise` | Netlist directive |
| `-s, --step <val>` | Override transient step / frequency sweep resolution | From netlist |
| `-t, --stop <val>` | Override transient stop time | From netlist |
| `--gmin <val>` | Minimum conductance to ground for convergence | `1e-12` |
| `--reltol <val>` | Relative tolerance for Newton-Raphson & LTE | `1e-3` |
| `--abstol <val>` | Absolute current tolerance | `1e-12` |
| `--vntol <val>` | Absolute voltage tolerance | `1e-6` |
| `-v, --verbose` | Verbose logging (iteration stats, convergence monitors) | Disabled |
| `--version` | Display Nodalys engine version, commit hash, and build features | - |

---

## 5. Phased Feature Branch Workflow

Development follows a strict feature branch strategy based on the epic integration trunk `dev/nodalys`:

```mermaid
gitGraph
   commit id: "Rename to dev/nodalys"
   branch feat/nodalys-modularization
   checkout feat/nodalys-modularization
   commit id: "Extract /nodalys & CMake"
   commit id: "Add CircuitBuilder API"
   commit id: "Wire app-core to nodalys"
   checkout dev/nodalys
   merge feat/nodalys-modularization id: "Merge Phase 1"
   branch feat/nodalys-spice-parser
   checkout feat/nodalys-spice-parser
   commit id: "Implement SPICE netlist lexer"
   commit id: "Netlist to Circuit parser"
   checkout dev/nodalys
   merge feat/nodalys-spice-parser id: "Merge Phase 2"
   branch feat/nodalys-outputs
   checkout feat/nodalys-outputs
   commit id: "Implement .raw, CSV, JSON writers"
   checkout dev/nodalys
   merge feat/nodalys-outputs id: "Merge Phase 3"
   branch feat/nodalys-cli
   checkout feat/nodalys-cli
   commit id: "Add vendor/CLI11 submodule"
   commit id: "Implement nodalys CLI binary"
   checkout dev/nodalys
   merge feat/nodalys-cli id: "Merge Phase 4"
   branch feat/behavioral-logic
   checkout feat/behavioral-logic
   commit id: "Add smooth continuous logic gates"
   checkout dev/nodalys
   merge feat/behavioral-logic id: "Merge Phase 5"
   checkout dev/nodalys
   commit id: "PR into develop"
```

### Detailed Phase Milestones

1. **Phase 1: `feat/nodalys-modularization` (Immediate next step)**
   - Create root `/nodalys` with clean internal directory layout (`include/`, `src/`).
   - Move MNA solver, dense LU, Newton-Raphson, device models, and analysis loops from `app-core/native/` to `nodalys/`.
   - Implement `nodalys::CircuitBuilder` and `nodalys::Circuit` programmatic API.
   - Update `app-core/CMakeLists.txt` to link against `nodalys`.
   - Update `NativeBackend` in `app-core` to construct `nodalys::Circuit` via `nodalys::CircuitBuilder` and invoke `nodalys::RunSimulation`.
   - Verify all **765 test cases / 70,924 assertions** pass without regression.
   - Merge `feat/nodalys-modularization` into `dev/nodalys`.

2. **Phase 2: `feat/nodalys-spice-parser`**
   - Implement fast, zero-dependency SPICE netlist lexer and AST parser in `nodalys/src/io/spice_parser.*`.
   - Support standard `.cir` deck containing elements, models, and simulation commands.
   - Add Catch2 tests comparing netlist parsing against analytical circuits.
   - Merge `feat/nodalys-spice-parser` into `dev/nodalys`.

3. **Phase 3: `feat/nodalys-outputs`**
   - Implement `nodalys::ExportRawBinary`, `nodalys::ExportRawAscii`, `nodalys::ExportCsv`, and `nodalys::ExportJson`.
   - Benchmark formatting speed and verify raw file compatibility with external tools (`ngnutmeg` / `gaw`).
   - Merge `feat/nodalys-outputs` into `dev/nodalys`.

4. **Phase 4: `feat/nodalys-cli`**
   - Initialize git submodule `vendor/CLI11`.
   - Implement `nodalys/cli/main.cpp` providing comprehensive flags, netlist invocation, analysis execution, and file output.
   - Add automated CLI regression tests (`nodalys tests/circuits/rc.cir -f csv -o rc.csv`).
   - Merge `feat/nodalys-cli` into `dev/nodalys`.

5. **Phase 5: `feat/behavioral-logic`**
   - Implement continuous $C^\infty$ logic operators, dynamic inertial delay ($t_{\text{pd}}$), and bistables as documented in `behavioral_logic_and_verilog_plan.md`.
   - Merge `feat/behavioral-logic` into `dev/nodalys`.

6. **Epic Finalization**:
   - Perform comprehensive verification across all example circuits in `imcsim`.
   - Submit clean Pull Request from `dev/nodalys` into `develop`.
