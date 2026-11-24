/**
 * @file    cli_runner.cpp
 * @brief   Session state, directive selection, and analysis execution/export for the standalone Nodalys CLI.
 */

#include "cli/cli_runner.h"

#include "nodalys/nodalys.h"
#include "text/text_case.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <expected>
#include <format>
#include <iterator>
#include <utility>
#include <variant>

namespace Nodalys {

namespace {

/// Stop time of the transient that `--analysis tran` synthesizes when the netlist has none.
constexpr double DEFAULT_STOP_TIME_S = 1e-3;
/// Time step of the transient that `--analysis tran` synthesizes when the netlist has none.
constexpr double DEFAULT_TIME_STEP_S = 1e-6;

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

bool IsOverrideMatch(const std::string_view name, const AnalysisKind kind) {
    return std::ranges::any_of(ANALYSIS_CHOICES, [&](const AnalysisChoice &choice) {
        return choice.Name == name && choice.Kind == kind;
    });
}

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

template <typename Result>
std::expected<SimulationResult, std::string> ToSimulationResult(std::expected<Result, std::string> result) {
    if (!result) {
        return std::unexpected(std::move(result.error()));
    }
    return SimulationResult{std::move(*result)};
}

std::expected<SimulationResult, std::string>
ExecuteConfig(CliSession &session, const ParsedNetlist &parsed, std::monostate & /*config*/) {
    session.Say("Running Operating Point (.op) analysis...\n");
    return ToSimulationResult(
        ComputeOperatingPoint(parsed.CircuitInstance, parsed.Options, session.GetDiagnosticsSink()));
}

std::expected<SimulationResult, std::string>
ExecuteConfig(CliSession &session, const ParsedNetlist &parsed, TransientConfig &config) {
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
    return ToSimulationResult(ComputeTransient(parsed.CircuitInstance,
                                               config,
                                               config.UseInitialConditions,
                                               {},
                                               parsed.Options,
                                               session.GetDiagnosticsSink()));
}

std::expected<SimulationResult, std::string>
ExecuteConfig(CliSession &session, const ParsedNetlist &parsed, const DcSweepConfig &config) {
    session.Say(std::format("Running DC Sweep (.dc) analysis on source {}\n", config.SweptSource));
    return ToSimulationResult(
        ComputeDCSweep(parsed.CircuitInstance, config, parsed.Options, session.GetDiagnosticsSink()));
}

std::expected<SimulationResult, std::string>
ExecuteConfig(CliSession &session, const ParsedNetlist &parsed, const AcSweepConfig &config) {
    session.Say(
        std::format("Running AC Sweep (.ac) analysis: {} Hz to {} Hz\n", config.StartFrequency, config.StopFrequency));
    return ToSimulationResult(
        ComputeACSweep(parsed.CircuitInstance, config, parsed.Options, session.GetDiagnosticsSink()));
}

std::expected<SimulationResult, std::string>
ExecuteConfig(CliSession &session, const ParsedNetlist &parsed, const TransferFunctionConfig &config) {
    session.Say(std::format("Running Transfer Function (.tf) analysis for source {}\n", config.InputSource));
    return ToSimulationResult(
        ComputeTransferFunction(parsed.CircuitInstance, config, parsed.Options, session.GetDiagnosticsSink()));
}

std::expected<SimulationResult, std::string>
ExecuteConfig(CliSession &session, const ParsedNetlist &parsed, const NoiseConfig &config) {
    session.Say(std::format("Running Noise (.noise) analysis for source {}\n", config.InputSource));
    return ToSimulationResult(
        ComputeNoise(parsed.CircuitInstance, config, parsed.Options, session.GetDiagnosticsSink()));
}

std::expected<SimulationResult, std::string>
ExecuteConfig(CliSession &session, const ParsedNetlist &parsed, const SParameterConfig &config) {
    session.Say(std::format("Running S-Parameter (.sp) analysis: {} Hz to {} Hz\n",
                            config.StartFrequency,
                            config.StopFrequency));
    return ToSimulationResult(
        ComputeSParameters(parsed.CircuitInstance, config, parsed.Options, session.GetDiagnosticsSink()));
}

std::expected<SimulationResult, std::string>
ExecuteConfig(CliSession &session, const ParsedNetlist &parsed, const PoleZeroConfig &config) {
    session.Say(
        std::format("Running Pole-Zero (.pz) analysis: output={}, input={}\n", config.OutputNode, config.InputSource));
    return ToSimulationResult(
        ComputePoleZero(parsed.CircuitInstance, config, parsed.Options, session.GetDiagnosticsSink()));
}

std::expected<SimulationResult, std::string>
ExecuteConfig(CliSession &session, const ParsedNetlist &parsed, const SensitivityConfig &config) {
    session.Say(std::format("Running DC Sensitivity (.sens) analysis: pos={}, neg={}\n",
                            config.OutputPositiveNode,
                            config.OutputNegativeNode));
    return ToSimulationResult(
        ComputeSensitivity(parsed.CircuitInstance, config, parsed.Options, session.GetDiagnosticsSink()));
}

std::expected<SimulationResult, std::string>
ExecuteConfig(CliSession &session, const ParsedNetlist &parsed, const AcSensitivityConfig &config) {
    session.Say(std::format("Running AC Sensitivity (.sens ... ac) analysis: pos={}, neg={}, input={}\n",
                            config.OutputPositiveNode,
                            config.OutputNegativeNode,
                            config.InputSource));
    return ToSimulationResult(
        ComputeAcSensitivity(parsed.CircuitInstance, config, parsed.Options, session.GetDiagnosticsSink()));
}

std::expected<SimulationResult, std::string>
ExecuteConfig(CliSession &session, const ParsedNetlist &parsed, const HarmonicBalanceConfig &config) {
    session.Say(std::format("Running Harmonic Balance (.hb) analysis: f0={} Hz, H={}\n",
                            config.FundamentalFrequency,
                            config.Harmonics));
    return ToSimulationResult(
        ComputeHarmonicBalance(parsed.CircuitInstance, config, parsed.Options, session.GetDiagnosticsSink()));
}

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
    return Nodalys::ExportRaw(result, title);
}

std::string ChooseTitle(const ParsedNetlist &parsed, const std::string_view input_file) {
    if (!parsed.Title.empty()) {
        return parsed.Title;
    }
    return input_file == "-" ? "Simulation" : std::string(input_file);
}

} // namespace

/**
 * @brief   Replaces the solver options the command line gives.
 * @param[in]     options  Overrides of the command line.
 * @param[in,out] solver   Options of the netlist, changed where an override is given.
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
 * @brief   Chooses the analyses to run: the ones the netlist asks for, narrowed by `--analysis`, or the operating
 *          point when there are none.
 * @param[in]     options   Overrides of the command line.
 * @param[in,out] analyses  Directives of the netlist, replaced by the ones to run.
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
 * @brief   Runs every selected analysis and writes each result in the chosen format.
 * @param[in,out] session  Run that receives the progress text, the error message and the solver messages.
 * @param[in,out] parsed   Netlist whose analyses are run.
 * @return  The results of all analyses one after the other, or empty after writing the error message when an
 *          analysis fails.
 */
std::optional<std::string> RunAnalyses(CliSession &session, ParsedNetlist &parsed) {
    const std::string format = ToLower(session.Options.Format);
    const std::string title = ChooseTitle(parsed, session.Options.InputFile);
    std::string exported;

    for (ParsedAnalysis &analysis : parsed.Analyses) {
        const auto start = std::chrono::steady_clock::now();
        const std::expected<SimulationResult, std::string> result =
            std::visit([&](auto &config) { return ExecuteConfig(session, parsed, config); }, analysis.Config);
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

} // namespace Nodalys
