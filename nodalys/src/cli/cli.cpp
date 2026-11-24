/**
 * @file    cli.cpp
 * @brief   The command line of the standalone Nodalys tool: reads a netlist, runs its analyses and writes the results.
 */

#include "cli/cli.h"

#include "cli/cli_runner.h"
#include "nodalys/nodalys.h"
#include "nodalys/version.h"

#include <CLI/CLI.hpp>
#include <chrono>
#include <cstdlib>
#include <expected>
#include <format>
#include <fstream>
#include <istream>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace Nodalys {

namespace {

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
 * @param[in]     argc     Number of arguments, the program name included.
 * @param[in]     argv     Arguments, the program name first.
 * @param[out]    options  The values read.
 * @param[in,out] output   Stream of the help and version text.
 * @param[in,out] errors   Stream of the parse errors.
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
 * @param[in]     text     Text of the netlist.
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
 * @param[in,out] session   Run that names the file and receives the output and the error message.
 * @param[in]     exported  Text to write.
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
 * @param[in]     argc    Number of arguments, the program name included.
 * @param[in]     argv    Arguments, the program name first.
 * @param[in,out] input   Stream the netlist is read from when its path is "-".
 * @param[in,out] output  Stream of the results when no output file is given, and of the help and progress text.
 * @param[in,out] errors  Stream of the error messages.
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
