/**
 * @file    cli_runner.h
 * @brief   Session state, directive selection, and analysis execution/export for the standalone Nodalys CLI.
 */

#ifndef NODALYS_CLI_RUNNER_H
#define NODALYS_CLI_RUNNER_H

#include "nodalys/netlist_parser.h"
#include "nodalys/types.h"
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

/**
 * @struct  CliOptions
 * @brief   Command-line flags and overrides parsed for a standalone Nodalys CLI invocation.
 */
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

/**
 * @struct  CliSession
 * @brief   Shared execution context across all steps of a single CLI run.
 */
struct CliSession {
    CliOptions Options;
    std::ostream &Output;
    std::ostream &Errors;
    std::vector<DiagnosticMessage> Diagnostics;

    /**
     * @brief   Reports whether interactive progress output is enabled (verbose and not batch mode).
     * @return  True when progress messages should be printed.
     */
    [[nodiscard]] bool IsChatty() const { return Options.IsVerbose && !Options.IsBatchMode; }

    /**
     * @brief   Writes a progress line to the output stream when chatty.
     * @param[in] text  Progress message including its trailing newline.
     */
    void Say(const std::string_view text) const {
        if (IsChatty()) {
            Output << text;
        }
    }

    /**
     * @brief   Returns the diagnostic message sink pointer for solver calls.
     * @return  Pointer to `Diagnostics` when verbose, or `nullptr` otherwise.
     */
    [[nodiscard]] std::vector<DiagnosticMessage> *GetDiagnosticsSink() {
        return Options.IsVerbose ? &Diagnostics : nullptr;
    }
};

// Configuration and execution
void ApplyToleranceOverrides(const CliOptions &options, ConvergenceOptions &solver);
void SelectAnalyses(const CliOptions &options, std::vector<ParsedAnalysis> &analyses);
[[nodiscard]] std::optional<std::string> RunAnalyses(CliSession &session, ParsedNetlist &parsed);

} // namespace Nodalys

#endif // NODALYS_CLI_RUNNER_H
