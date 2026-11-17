/**
 * @file    analysis_runner.cpp
 * @brief   Runs one analysis of a parsed netlist, whichever its kind, for the command line and the MCP server.
 */

#include "cli/analysis_runner.h"

#include <format>
#include <utility>
#include <variant>

namespace Nodalys {

namespace {

/// Wraps the result of one analysis in the variant, keeping the error text of a failed one.
template <typename Result>
std::expected<SimulationResult, std::string> ToSimulationResult(std::expected<Result, std::string> result) {
    if (!result) {
        return std::unexpected(std::move(result.error()));
    }
    return SimulationResult{std::move(*result)};
}

} // namespace

/**
 * @brief   Runs one analysis of a parsed netlist on its circuit and options.
 * @param[in]  parsed       Netlist with the compiled circuit and the solver options.
 * @param[in]  analysis     Analysis to run; its configuration must hold the kind it names.
 * @param[out] diagnostics  Receives the solver messages, or null to discard them.
 * @return  The result, or the error text of the solver.
 */
std::expected<SimulationResult, std::string> RunAnalysis(const ParsedNetlist &parsed, const ParsedAnalysis &analysis,
                                                         std::vector<DiagnosticMessage> *diagnostics) {
    const Circuit &circuit = parsed.CircuitInstance;
    const ConvergenceOptions &options = parsed.Options;

    switch (analysis.Kind) {
    case AnalysisKind::OPERATING_POINT:
        return ToSimulationResult(ComputeOperatingPoint(circuit, options, diagnostics));

    case AnalysisKind::TRANSIENT: {
        TransientConfig config = std::get<TransientConfig>(analysis.Config);
        if (config.MaxStep <= 0.0) {
            config.MaxStep = config.TimeStep;
        }
        return ToSimulationResult(
            ComputeTransient(circuit, config, config.UseInitialConditions, {}, options, diagnostics));
    }

    case AnalysisKind::DC_SWEEP:
        return ToSimulationResult(
            ComputeDCSweep(circuit, std::get<DcSweepConfig>(analysis.Config), options, diagnostics));

    case AnalysisKind::AC_SWEEP:
        return ToSimulationResult(
            ComputeACSweep(circuit, std::get<AcSweepConfig>(analysis.Config), options, diagnostics));

    case AnalysisKind::TRANSFER_FUNCTION:
        return ToSimulationResult(
            ComputeTransferFunction(circuit, std::get<TransferFunctionConfig>(analysis.Config), options, diagnostics));

    case AnalysisKind::NOISE:
        return ToSimulationResult(ComputeNoise(circuit, std::get<NoiseConfig>(analysis.Config), options, diagnostics));

    case AnalysisKind::S_PARAMETERS:
        return ToSimulationResult(
            ComputeSParameters(circuit, std::get<SParameterConfig>(analysis.Config), options, diagnostics));

    case AnalysisKind::POLE_ZERO:
        return ToSimulationResult(
            ComputePoleZero(circuit, std::get<PoleZeroConfig>(analysis.Config), options, diagnostics));

    case AnalysisKind::SENSITIVITY:
        return ToSimulationResult(
            ComputeSensitivity(circuit, std::get<SensitivityConfig>(analysis.Config), options, diagnostics));

    case AnalysisKind::AC_SENSITIVITY:
        return ToSimulationResult(
            ComputeAcSensitivity(circuit, std::get<AcSensitivityConfig>(analysis.Config), options, diagnostics));

    case AnalysisKind::HARMONIC_BALANCE:
        return ToSimulationResult(
            ComputeHarmonicBalance(circuit, std::get<HarmonicBalanceConfig>(analysis.Config), options, diagnostics));

    default:
        return std::unexpected("Unknown analysis kind");
    }
}

/**
 * @brief   Words the progress line the verbose command line prints before an analysis.
 * @param[in] analysis  Analysis about to run.
 * @return  Text such as "Running AC Sweep (.ac) analysis: 1 Hz to 1000000 Hz", without a line break.
 */
std::string DescribeAnalysis(const ParsedAnalysis &analysis) {
    switch (analysis.Kind) {
    case AnalysisKind::OPERATING_POINT:
        return "Running Operating Point (.op) analysis...";

    case AnalysisKind::TRANSIENT: {
        const auto &config = std::get<TransientConfig>(analysis.Config);
        return std::format("Running Transient (.tran) analysis: tstep={}, tstop={}", config.TimeStep, config.StopTime);
    }

    case AnalysisKind::DC_SWEEP:
        return std::format("Running DC Sweep (.dc) analysis on source {}",
                           std::get<DcSweepConfig>(analysis.Config).SweptSource);

    case AnalysisKind::AC_SWEEP: {
        const auto &config = std::get<AcSweepConfig>(analysis.Config);
        return std::format("Running AC Sweep (.ac) analysis: {} Hz to {} Hz", config.StartFrequency,
                           config.StopFrequency);
    }

    case AnalysisKind::TRANSFER_FUNCTION:
        return std::format("Running Transfer Function (.tf) analysis for source {}",
                           std::get<TransferFunctionConfig>(analysis.Config).InputSource);

    case AnalysisKind::NOISE:
        return std::format("Running Noise (.noise) analysis for source {}",
                           std::get<NoiseConfig>(analysis.Config).InputSource);

    case AnalysisKind::S_PARAMETERS: {
        const auto &config = std::get<SParameterConfig>(analysis.Config);
        return std::format("Running S-Parameter (.sp) analysis: {} Hz to {} Hz", config.StartFrequency,
                           config.StopFrequency);
    }

    case AnalysisKind::POLE_ZERO: {
        const auto &config = std::get<PoleZeroConfig>(analysis.Config);
        return std::format("Running Pole-Zero (.pz) analysis: output={}, input={}", config.OutputNode,
                           config.InputSource);
    }

    case AnalysisKind::SENSITIVITY: {
        const auto &config = std::get<SensitivityConfig>(analysis.Config);
        return std::format("Running DC Sensitivity (.sens) analysis: pos={}, neg={}", config.OutputPositiveNode,
                           config.OutputNegativeNode);
    }

    case AnalysisKind::AC_SENSITIVITY: {
        const auto &config = std::get<AcSensitivityConfig>(analysis.Config);
        return std::format("Running AC Sensitivity (.sens ... ac) analysis: pos={}, neg={}, input={}",
                           config.OutputPositiveNode, config.OutputNegativeNode, config.InputSource);
    }

    case AnalysisKind::HARMONIC_BALANCE: {
        const auto &config = std::get<HarmonicBalanceConfig>(analysis.Config);
        return std::format("Running Harmonic Balance (.hb) analysis: f0={} Hz, H={}", config.FundamentalFrequency,
                           config.Harmonics);
    }

    default:
        return "Running analysis";
    }
}

} // namespace Nodalys
