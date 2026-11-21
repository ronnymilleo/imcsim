/**
 * @file    cli.cpp
 * @brief   The command line of the standalone Nodalys tool: reads a netlist, runs its analyses and writes the results.
 */

#include "cli/cli.h"

#include "nodalys/nodalys.h"
#include "nodalys/version.h"
#include "text/text_case.h"

#include <CLI/CLI.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <expected>
#include <format>
#include <fstream>
#include <istream>
#include <iterator>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Nodalys {

namespace {

/// A name that `--analysis` accepts and the kind of directive it picks in the netlist.
struct AnalysisChoice {
    std::string_view Name;
    AnalysisKind Kind;
};

/// The names of `--analysis`; a sensitivity has two forms, so its name picks both kinds.
constexpr auto ANALYSIS_CHOICES = std::to_array<AnalysisChoice>({
    {"op", AnalysisKind::OPERATING_POINT},
    {"tran", AnalysisKind::TRANSIENT},
    {"dc", AnalysisKind::DC_SWEEP},
    {"ac", AnalysisKind::AC_SWEEP},
    {"tf", AnalysisKind::TRANSFER_FUNCTION},
    {"noise", AnalysisKind::NOISE},
    {"sp", AnalysisKind::S_PARAMETERS},
    {"pz", AnalysisKind::POLE_ZERO},
    {"sens", AnalysisKind::SENSITIVITY},
    {"sens", AnalysisKind::AC_SENSITIVITY},
    {"hb", AnalysisKind::HARMONIC_BALANCE},
});

/// Time step of the transient that `--analysis tran` synthesizes when the netlist has none.
constexpr double DEFAULT_TIME_STEP_S = 1e-6;
/// Stop time of the transient that `--analysis tran` synthesizes when the netlist has none.
constexpr double DEFAULT_STOP_TIME_S = 1e-3;

/// What the command line asks for.
struct CliOptions {
    std::string InputFile;
    std::string OutputFile;
    std::string Format = "raw";
    std::string AnalysisOverride;
    bool IsBatchMode = false;
    bool IsVerbose = false;
    std::optional<double> StepOverride;
    std::optional<double> StopOverride;
    std::optional<double> GminOverride;
    std::optional<double> RelTolOverride;
    std::optional<double> AbsTolOverride;
    std::optional<double> VnTolOverride;
    std::optional<int> MaxIterationsOverride;
};

/// What the steps of one run share: the options, the streams and the solver messages collected on the way.
struct CliSession {
    CliOptions Options;
    std::ostream &Output;
    std::ostream &Errors;
    std::vector<DiagnosticMessage> Diagnostics;

    /**
     * @brief   Tells whether the progress text is wanted: verbose, and not batch mode.
     */
    bool IsChatty() const { return Options.IsVerbose && !Options.IsBatchMode; }

    /**
     * @brief   Writes a progress line when the run is chatty.
     * @param[in] text  Text to write, with its line break.
     */
    void Say(const std::string_view text) const {
        if (IsChatty()) {
            Output << text;
        }
    }

    /**
     * @brief   Gives the list the solver writes its messages to.
     * @return  The list when verbose, or null when the messages are not wanted.
     */
    std::vector<DiagnosticMessage> *GetDiagnosticsSink() { return Options.IsVerbose ? &Diagnostics : nullptr; }
};

/**
 * @brief   Tells whether a directive of the netlist is the one that `--analysis` names.
 */
bool IsOverrideMatch(const std::string_view name, const AnalysisKind kind) {
    return std::ranges::any_of(ANALYSIS_CHOICES, [&](const AnalysisChoice &choice) {
        return choice.Name == name && choice.Kind == kind;
    });
}

std::optional<std::string> ReadFileContent(const std::string &path, std::istream &input) {
    if (path == "-") {
        std::ostringstream ss;
        ss << input.rdbuf();
        return ss.str();
    }
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

/**
 * @brief   Declares the options of the command line and reads them from the arguments.
 * @param[in] argc          Number of arguments, the program name included.
 * @param[in] argv          Arguments, the program name first.
 * @param[out] options      The values read.
 * @param[in,out] output    Stream of the help and version text.
 * @param[in,out] errors    Stream of the parse errors.
 * @return  The exit code when the run ends here, because the arguments are wrong or the help or the version was asked
 *          for; empty when the run goes on.
 */
[[nodiscard]] std::optional<int>
ParseOptions(const int argc, const char *const *argv, CliOptions &options, std::ostream &output, std::ostream &errors) {
    CLI::App app{"Nodalys - Autonomous SPICE Circuit Simulator\n"
                 "High-performance autonomous MNA simulation engine with zero external dependencies."};

    // Command-line arguments
    app.add_option("netlist", options.InputFile, "Path to SPICE netlist file (.cir, .sp, .net) or '-' for stdin");
    app.add_flag("-b,--batch", options.IsBatchMode, "Run in batch mode (suppress interactive messages)");
    app.add_option("-o,--output", options.OutputFile, "Destination output file (default: stdout)");
    app.add_option("-f,--format",
                   options.Format,
                   "Output format: raw (Nutmeg ASCII), csv, json, touchstone (default: raw)")
        ->check(CLI::IsMember({"raw", "raw-ascii", "csv", "json", "touchstone", "s2p"}, CLI::ignore_case));
    app.add_option("-a,--analysis",
                   options.AnalysisOverride,
                   "Override analysis: op, tran, dc, ac, tf, noise, sp, pz, sens (DC and AC), hb")
        ->check(CLI::IsMember({"op", "tran", "dc", "ac", "tf", "noise", "sp", "pz", "sens", "hb"}, CLI::ignore_case));
    app.add_option("-s,--step", options.StepOverride, "Override transient step time / sweep step");
    app.add_option("-t,--stop", options.StopOverride, "Override transient stop time");

    // Solver tolerance options
    app.add_option("--gmin", options.GminOverride, "Minimum conductance to ground in Siemens (default: 1e-12)");
    app.add_option("--reltol", options.RelTolOverride, "Relative tolerance for Newton-Raphson (default: 1e-3)");
    app.add_option("--abstol", options.AbsTolOverride, "Absolute current tolerance in Amperes (default: 1e-12)");
    app.add_option("--vntol", options.VnTolOverride, "Absolute voltage tolerance in Volts (default: 1e-6)");
    app.add_option("--maxiter",
                   options.MaxIterationsOverride,
                   "Maximum Newton-Raphson iterations per step (default: 100)");

    app.add_flag("-v,--verbose", options.IsVerbose, "Enable verbose output (solver diagnostics and timings)");
    app.set_version_flag("--version",
                         std::format("nodalys {} (imcsim engine, standalone SPICE simulator)", NODALYS_VERSION_STRING));

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        // CLI11 reports a parse failure, and a request for the help or the version, by throwing
        return app.exit(error, output, errors);
    }
    return std::nullopt;
}

/**
 * @brief   Reads the text of the netlist from its file, or from the input stream when the path is "-".
 * @param[in,out] session  Run that names the netlist and receives the error message.
 * @param[in,out] input    Stream read when the path is "-".
 * @return  The text, or empty after writing the error message when no netlist is named or it cannot be opened.
 */
[[nodiscard]] std::optional<std::string> LoadNetlistText(CliSession &session, std::istream &input) {
    if (session.Options.InputFile.empty()) {
        session.Errors << "Error: No input netlist specified. Run 'nodalys --help' for usage.\n";
        return std::nullopt;
    }

    std::optional<std::string> content = ReadFileContent(session.Options.InputFile, input);
    if (!content) {
        session.Errors << std::format("Error: Cannot open netlist file '{}'\n", session.Options.InputFile);
    }
    return content;
}

/**
 * @brief   Parses the text of the netlist and reports the time it took when the run is chatty.
 * @param[in,out] session  Run that receives the progress text and the error message.
 * @param[in] text         Text of the netlist.
 * @return  The parsed netlist, or empty after writing the error message when the text does not parse.
 */
[[nodiscard]] std::optional<ParsedNetlist> ParseNetlistText(CliSession &session, const std::string_view text) {
    session.Say(std::format("Nodalys Engine v{}\n", NODALYS_VERSION_STRING));
    session.Say(
        std::format("Parsing netlist: {}\n", session.Options.InputFile == "-" ? "<stdin>" : session.Options.InputFile));

    const auto parse_start = std::chrono::steady_clock::now();
    std::expected<ParsedNetlist, std::string> parsed = Nodalys::ParseNetlist(text);
    const auto parse_end = std::chrono::steady_clock::now();

    if (!parsed) {
        session.Errors << std::format("Parse Error: {}\n", parsed.error());
        return std::nullopt;
    }

    const auto parse_ms = std::chrono::duration<double, std::milli>(parse_end - parse_start).count();
    session.Say(std::format("Netlist parsed in {:.2f} ms: {} nodes, {} MNA unknowns\n",
                            parse_ms,
                            parsed->CircuitInstance.GetNodeCount(),
                            parsed->CircuitInstance.GetUnknownCount()));
    return std::move(*parsed);
}

/**
 * @brief   Replaces the solver options the command line gives.
 * @param[in] options     Overrides of the command line.
 * @param[in,out] solver  Options of the netlist, changed where an override is given.
 */
void ApplyToleranceOverrides(const CliOptions &options, ConvergenceOptions &solver) {
    if (options.GminOverride) {
        solver.Gmin = *options.GminOverride;
    }
    if (options.RelTolOverride) {
        solver.RelTol = *options.RelTolOverride;
    }
    if (options.AbsTolOverride) {
        solver.CurrentAbsTol = *options.AbsTolOverride;
    }
    if (options.VnTolOverride) {
        solver.VoltageAbsTol = *options.VnTolOverride;
    }
    if (options.MaxIterationsOverride) {
        solver.MaxIterations = *options.MaxIterationsOverride;
    }
}

/**
 * @brief   Builds the analysis that `--analysis` asks for when the netlist has no directive of that kind.
 * @param[in] name     Name given to `--analysis`, in lower case.
 * @param[in] options  Overrides of the command line, read for the transient.
 * @return  The analysis with its default settings, or empty for the names that need settings of their own (dc, ac,
 *          tf and noise), which leaves the run to the operating point.
 */
std::optional<ParsedAnalysis> MakeDefaultAnalysis(const std::string_view name, const CliOptions &options) {
    if (name == "op") {
        return ParsedAnalysis{AnalysisKind::OPERATING_POINT, std::monostate{}};
    }
    if (name == "tran") {
        TransientConfig config;
        config.TimeStep = options.StepOverride.value_or(DEFAULT_TIME_STEP_S);
        config.StopTime = options.StopOverride.value_or(DEFAULT_STOP_TIME_S);
        config.MaxStep = config.TimeStep;
        return ParsedAnalysis{AnalysisKind::TRANSIENT, config};
    }
    if (name == "sp") {
        return ParsedAnalysis{AnalysisKind::S_PARAMETERS, SParameterConfig{}};
    }
    if (name == "pz") {
        return ParsedAnalysis{AnalysisKind::POLE_ZERO, PoleZeroConfig{}};
    }
    if (name == "sens") {
        return ParsedAnalysis{AnalysisKind::SENSITIVITY, SensitivityConfig{}};
    }
    if (name == "hb") {
        return ParsedAnalysis{AnalysisKind::HARMONIC_BALANCE, HarmonicBalanceConfig{}};
    }
    return std::nullopt;
}

/**
 * @brief   Chooses the analyses to run: the ones the netlist asks for, narrowed by `--analysis`, or the operating
 *          point when there are none.
 * @param[in] options        Overrides of the command line.
 * @param[in,out] analyses   Directives of the netlist, replaced by the ones to run.
 */
void SelectAnalyses(const CliOptions &options, std::vector<ParsedAnalysis> &analyses) {
    if (!options.AnalysisOverride.empty()) {
        const std::string name = ToLower(options.AnalysisOverride);
        const auto is_match = [&](const ParsedAnalysis &analysis) { return IsOverrideMatch(name, analysis.Kind); };

        if (std::ranges::any_of(analyses, is_match)) {
            std::vector<ParsedAnalysis> matches;
            std::ranges::copy_if(analyses, std::back_inserter(matches), is_match);
            // Only a sensitivity keeps every match, since a netlist may ask for both of its forms
            if (name != "sens") {
                matches.resize(1);
            }
            analyses = std::move(matches);
        } else {
            analyses.clear();
            if (const std::optional<ParsedAnalysis> analysis = MakeDefaultAnalysis(name, options)) {
                analyses.push_back(*analysis);
            }
        }
    }

    if (analyses.empty()) {
        analyses.push_back({AnalysisKind::OPERATING_POINT, std::monostate{}});
    }
}

/**
 * @brief   Wraps the result of one analysis as a result of any analysis.
 */
template <typename Result>
std::expected<SimulationResult, std::string> ToSimulationResult(std::expected<Result, std::string> result) {
    if (!result) {
        return std::unexpected(std::move(result.error()));
    }
    return SimulationResult{std::move(*result)};
}

/**
 * @brief   Runs one analysis, after saying what it is when the run is chatty.
 * @param[in,out] session   Run that receives the progress text and the solver messages.
 * @param[in] parsed        Circuit and solver options of the netlist.
 * @param[in,out] analysis  Analysis to run; a transient takes the step and stop time of the command line.
 * @return  The result, or the message of the failure.
 */
std::expected<SimulationResult, std::string>
RunAnalysis(CliSession &session, const ParsedNetlist &parsed, ParsedAnalysis &analysis) {
    const Circuit &circuit = parsed.CircuitInstance;
    const ConvergenceOptions &solver = parsed.Options;
    std::vector<DiagnosticMessage> *diagnostics = session.GetDiagnosticsSink();

    switch (analysis.Kind) {
    case AnalysisKind::OPERATING_POINT:
        session.Say("Running Operating Point (.op) analysis...\n");
        return ToSimulationResult(ComputeOperatingPoint(circuit, solver, diagnostics));

    case AnalysisKind::TRANSIENT: {
        auto &config = std::get<TransientConfig>(analysis.Config);
        if (session.Options.StepOverride) {
            config.TimeStep = *session.Options.StepOverride;
        }
        if (session.Options.StopOverride) {
            config.StopTime = *session.Options.StopOverride;
        }
        if (config.MaxStep <= 0.0) {
            config.MaxStep = config.TimeStep;
        }
        session.Say(
            std::format("Running Transient (.tran) analysis: tstep={}, tstop={}\n", config.TimeStep, config.StopTime));
        return ToSimulationResult(
            ComputeTransient(circuit, config, config.UseInitialConditions, {}, solver, diagnostics));
    }

    case AnalysisKind::DC_SWEEP: {
        const auto &config = std::get<DcSweepConfig>(analysis.Config);
        session.Say(std::format("Running DC Sweep (.dc) analysis on source {}\n", config.SweptSource));
        return ToSimulationResult(ComputeDCSweep(circuit, config, solver, diagnostics));
    }

    case AnalysisKind::AC_SWEEP: {
        const auto &config = std::get<AcSweepConfig>(analysis.Config);
        session.Say(std::format("Running AC Sweep (.ac) analysis: {} Hz to {} Hz\n",
                                config.StartFrequency,
                                config.StopFrequency));
        return ToSimulationResult(ComputeACSweep(circuit, config, solver, diagnostics));
    }

    case AnalysisKind::TRANSFER_FUNCTION: {
        const auto &config = std::get<TransferFunctionConfig>(analysis.Config);
        session.Say(std::format("Running Transfer Function (.tf) analysis for source {}\n", config.InputSource));
        return ToSimulationResult(ComputeTransferFunction(circuit, config, solver, diagnostics));
    }

    case AnalysisKind::NOISE: {
        const auto &config = std::get<NoiseConfig>(analysis.Config);
        session.Say(std::format("Running Noise (.noise) analysis for source {}\n", config.InputSource));
        return ToSimulationResult(ComputeNoise(circuit, config, solver, diagnostics));
    }

    case AnalysisKind::S_PARAMETERS: {
        const auto &config = std::get<SParameterConfig>(analysis.Config);
        session.Say(std::format("Running S-Parameter (.sp) analysis: {} Hz to {} Hz\n",
                                config.StartFrequency,
                                config.StopFrequency));
        return ToSimulationResult(ComputeSParameters(circuit, config, solver, diagnostics));
    }

    case AnalysisKind::POLE_ZERO: {
        const auto &config = std::get<PoleZeroConfig>(analysis.Config);
        session.Say(std::format("Running Pole-Zero (.pz) analysis: output={}, input={}\n",
                                config.OutputNode,
                                config.InputSource));
        return ToSimulationResult(ComputePoleZero(circuit, config, solver, diagnostics));
    }

    case AnalysisKind::SENSITIVITY: {
        const auto &config = std::get<SensitivityConfig>(analysis.Config);
        session.Say(std::format("Running DC Sensitivity (.sens) analysis: pos={}, neg={}\n",
                                config.OutputPositiveNode,
                                config.OutputNegativeNode));
        return ToSimulationResult(ComputeSensitivity(circuit, config, solver, diagnostics));
    }

    case AnalysisKind::AC_SENSITIVITY: {
        const auto &config = std::get<AcSensitivityConfig>(analysis.Config);
        session.Say(std::format("Running AC Sensitivity (.sens ... ac) analysis: pos={}, neg={}, input={}\n",
                                config.OutputPositiveNode,
                                config.OutputNegativeNode,
                                config.InputSource));
        return ToSimulationResult(ComputeAcSensitivity(circuit, config, solver, diagnostics));
    }

    case AnalysisKind::HARMONIC_BALANCE: {
        const auto &config = std::get<HarmonicBalanceConfig>(analysis.Config);
        session.Say(std::format("Running Harmonic Balance (.hb) analysis: f0={} Hz, H={}\n",
                                config.FundamentalFrequency,
                                config.Harmonics));
        return ToSimulationResult(ComputeHarmonicBalance(circuit, config, solver, diagnostics));
    }

    default:
        return std::unexpected("Unknown analysis kind");
    }
}

/**
 * @brief   Writes a result in the format the command line asks for.
 * @param[in] result  Result of one analysis.
 * @param[in] format  Format named by `--format`, in lower case.
 * @param[in] title   Title of the raw file.
 * @return  The text of the result; a touchstone file of a result that has no S-parameters falls back to CSV.
 */
std::string ExportResult(const SimulationResult &result, const std::string_view format, const std::string_view title) {
    if (format == "csv") {
        return Nodalys::ExportCsv(result);
    }
    if (format == "json") {
        return Nodalys::ExportJson(result);
    }
    if (format == "touchstone" || format == "s2p") {
        if (const auto *s_parameters = std::get_if<SParameterResult>(&result)) {
            return Nodalys::ExportTouchstone(*s_parameters);
        }
        return Nodalys::ExportCsv(result);
    }
    // raw or raw-ascii
    return Nodalys::ExportRaw(result, title);
}

/**
 * @brief   Names the simulation in the title of the raw file: the title of the netlist, or else its file.
 */
std::string ChooseTitle(const ParsedNetlist &parsed, const std::string_view input_file) {
    if (!parsed.Title.empty()) {
        return parsed.Title;
    }
    return input_file == "-" ? "Simulation" : std::string(input_file);
}

/**
 * @brief   Runs every selected analysis and writes each result in the chosen format.
 * @param[in,out] session  Run that receives the progress text, the error message and the solver messages.
 * @param[in,out] parsed   Netlist whose analyses are run.
 * @return  The results of all analyses one after the other, or empty after writing the error message when an
 *          analysis fails.
 */
[[nodiscard]] std::optional<std::string> RunAnalyses(CliSession &session, ParsedNetlist &parsed) {
    const std::string format = ToLower(session.Options.Format);
    const std::string title = ChooseTitle(parsed, session.Options.InputFile);
    std::string exported;

    for (ParsedAnalysis &analysis : parsed.Analyses) {
        const auto start = std::chrono::steady_clock::now();
        const std::expected<SimulationResult, std::string> result = RunAnalysis(session, parsed, analysis);
        if (!result) {
            session.Errors << std::format("Simulation Error: {}\n", result.error());
            return std::nullopt;
        }

        const auto end = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        session.Say(std::format("Analysis completed in {:.2f} ms\n", elapsed_ms));

        exported.append(ExportResult(*result, format, title));
    }
    return exported;
}

/**
 * @brief   Lists the messages of the solver when the run is verbose.
 */
void ReportDiagnostics(const CliSession &session) {
    if (!session.Options.IsVerbose || session.Diagnostics.empty()) {
        return;
    }
    session.Output << "\nDiagnostic Messages:\n";
    for (const DiagnosticMessage &diagnostic : session.Diagnostics) {
        session.Output << "  - " << diagnostic.Text << "\n";
    }
}

/**
 * @brief   Writes the exported text to the output file, or to the output stream when no file is named.
 * @param[in,out] session  Run that names the file and receives the output and the error message.
 * @param[in] exported     Text to write.
 * @return  EXIT_SUCCESS, or EXIT_FAILURE when the file cannot be written.
 */
int WriteOutput(const CliSession &session, const std::string_view exported) {
    if (session.Options.OutputFile.empty()) {
        session.Output << exported;
        return EXIT_SUCCESS;
    }
    if (!Nodalys::SaveStringToFile(session.Options.OutputFile, exported)) {
        session.Errors << std::format("Error: Failed to write output file '{}'\n", session.Options.OutputFile);
        return EXIT_FAILURE;
    }
    session.Say(std::format("Simulation output written to '{}'\n", session.Options.OutputFile));
    return EXIT_SUCCESS;
}

} // namespace

/**
 * @brief   Runs the command line: parses the arguments, reads the netlist, runs its analyses and writes the results.
 * @param[in] argc         Number of arguments, the program name included.
 * @param[in] argv         Arguments, the program name first.
 * @param[in,out] input    Stream the netlist is read from when its path is "-".
 * @param[in,out] output   Stream of the results when no output file is given, and of the help and progress text.
 * @param[in,out] errors   Stream of the error messages.
 * @return  EXIT_SUCCESS, or EXIT_FAILURE when the arguments, the netlist or an analysis fails; asking for the help
 *          or the version succeeds.
 */
int RunCli(const int argc, const char *const *argv, std::istream &input, std::ostream &output, std::ostream &errors) {
    CliOptions options;
    if (const std::optional<int> exit_code = ParseOptions(argc, argv, options, output, errors)) {
        return *exit_code;
    }
    CliSession session{.Options = std::move(options), .Output = output, .Errors = errors, .Diagnostics = {}};

    const std::optional<std::string> text = LoadNetlistText(session, input);
    if (!text) {
        return EXIT_FAILURE;
    }
    std::optional<ParsedNetlist> parsed = ParseNetlistText(session, *text);
    if (!parsed) {
        return EXIT_FAILURE;
    }

    ApplyToleranceOverrides(session.Options, parsed->Options);
    SelectAnalyses(session.Options, parsed->Analyses);

    const std::optional<std::string> exported = RunAnalyses(session, *parsed);
    if (!exported) {
        return EXIT_FAILURE;
    }

    ReportDiagnostics(session);
    return WriteOutput(session, *exported);
}

} // namespace Nodalys
