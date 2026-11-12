/**
 * @file    analysis_runs_single.cpp
 * @brief   Starts the analyses that run once on the circuit: noise, transfer function, sensitivity, poles and zeros,
 *          S-parameters, truth table and harmonic balance.
 */

#include "analysis/analysis_controls.h"

#include "analysis/analysis_inputs.h"
#include "analysis/run_jobs.h"
#include "simulation/circuit_checks.h"
#include "simulation/monte_carlo.h"
#include <algorithm>
#include <format>
#include <memory>
#include <utility>

namespace GUI {

void AnalysisControls::RunNoiseAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::vector<std::string> sources = Core::GetNoiseInputSources(circuit);
    const std::optional<int> output = ResolveOutputNode(settings.NoiseOutput, circuit, m_Schematic);
    if (sources.empty() || !output) {
        m_Results.SetNoise(std::nullopt);
        Finish(Analysis::NOISE,
               sources.empty() ? "A noise analysis needs an AC source as its input; select a voltage or current "
                                 "source and set it to AC"
                               : "Connect some parts: the noise needs a node to measure",
               {});
        return;
    }
    const Core::NoiseSettings run_settings{.OutputNode = *output,
                                           .InputSource = ResolveOptionalInput(settings.NoiseSource, sources),
                                           .StartFrequency = settings.NoiseRange.StartFrequency,
                                           .StopFrequency = settings.NoiseRange.StopFrequency,
                                           .PointsPerDecade = settings.NoiseRange.PointsPerDecade};
    StartRun(Analysis::NOISE,
             MakeJob<Core::NoiseRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunNoise(copy, run_settings);
                 },
                 [this](Core::NoiseRun run) {
                     Finish(Analysis::NOISE,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetNoise(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::RunTransferFunctionAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::vector<std::string> sources = Core::GetTransferFunctionInputSources(circuit);
    const std::optional<int> output = ResolveOutputNode(settings.TransferFunctionOutput, circuit, m_Schematic);
    if (sources.empty() || !output) {
        m_Results.SetTransferFunction(std::nullopt);
        Finish(Analysis::TRANSFER_FUNCTION,
               sources.empty()
                   ? std::format("A transfer function analysis needs an input source; add {}", Core::SWEEPABLE_SOURCES)
                   : "Connect some parts: the transfer function needs a node to measure",
               {});
        return;
    }
    const Core::TransferFunctionSettings run_settings{
        .OutputNode = *output, .InputSource = ResolveOptionalInput(settings.TransferFunctionSource, sources)};
    StartRun(Analysis::TRANSFER_FUNCTION,
             MakeJob<Core::TransferFunctionRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunTransferFunction(copy, run_settings);
                 },
                 [this](Core::TransferFunctionRun run) {
                     Finish(Analysis::TRANSFER_FUNCTION,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetTransferFunction(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::RunSensitivityAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::optional<int> output = ResolveOutputNode(settings.SensitivityOutput, circuit, m_Schematic);
    if (!output) {
        m_Results.SetSensitivity(std::nullopt);
        Finish(Analysis::SENSITIVITY, "Connect some parts: the sensitivity needs a node to measure", {});
        return;
    }
    const Core::SensitivitySettings run_settings{.OutputNode = *output};
    StartRun(Analysis::SENSITIVITY,
             MakeJob<Core::SensitivityRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunSensitivity(copy, run_settings);
                 },
                 [this](Core::SensitivityRun run) {
                     Finish(Analysis::SENSITIVITY,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetSensitivity(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::RunPoleZeroAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::vector<std::string> sources = Core::GetTransferFunctionInputSources(circuit);
    const std::optional<int> output = ResolveOutputNode(settings.PoleZeroOutput, circuit, m_Schematic);
    if (sources.empty() || !output) {
        m_Results.SetPoleZero(std::nullopt);
        Finish(Analysis::POLE_ZERO,
               sources.empty()
                   ? std::format("A pole-zero analysis needs an input source; add {}", Core::SWEEPABLE_SOURCES)
                   : "Connect some parts: the pole-zero analysis needs a node to measure",
               {});
        return;
    }
    const Core::PoleZeroSettings run_settings{.OutputNode = *output,
                                              .InputSource = ResolveOptionalInput(settings.PoleZeroSource, sources)};
    StartRun(Analysis::POLE_ZERO,
             MakeJob<Core::PoleZeroRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunPoleZero(copy, run_settings);
                 },
                 [this](Core::PoleZeroRun run) {
                     Finish(Analysis::POLE_ZERO,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetPoleZero(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::RunSParameterAnalysis(const Core::Circuit &circuit) {
    const Core::SParameterSettings run_settings = m_Schematic.GetSimulationSettings().SParameters;
    StartRun(Analysis::S_PARAMETERS,
             MakeJob<Core::SParametersRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunSParameters(copy, run_settings);
                 },
                 [this](Core::SParametersRun run) {
                     Finish(Analysis::S_PARAMETERS,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetSParameters(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::RunTruthTableAnalysis(const Core::Circuit &circuit) {
    const std::vector<std::string> available = Core::GetTruthTableInputs(circuit);
    const std::vector<int> outputs = ListMeasuredNodes(circuit, m_Schematic);
    if (available.empty() || outputs.empty()) {
        m_Results.SetTruthTable(std::nullopt);
        Finish(Analysis::TRUTH_TABLE,
               available.empty() ? "A truth table needs a logic input to vary; add a Logic input"
                                 : "Measure a voltage with the Probe tool: the truth table reads the measured nodes",
               {});
        return;
    }
    const Core::TruthTableSettings run_settings{
        .Inputs = ResolveTruthTableInputs(m_Schematic.GetSimulationSettings().TruthTableInputs, available),
        .OutputNodes = outputs};
    StartRun(Analysis::TRUTH_TABLE,
             MakeJob<Core::TruthTableRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &control) {
                     return Core::RunTruthTable(copy, run_settings, &control);
                 },
                 [this](Core::TruthTableRun run) {
                     Finish(Analysis::TRUTH_TABLE,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetTruthTable(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::RunHarmonicBalanceAnalysis(const Core::Circuit &circuit) {
    const Core::HarmonicBalanceSettings run_settings = m_Schematic.GetSimulationSettings().HarmonicBalance;
    StartRun(Analysis::HARMONIC_BALANCE,
             MakeJob<Core::HarmonicBalanceRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &control) {
                     return Core::RunHarmonicBalance(copy, run_settings, &control);
                 },
                 [this](Core::HarmonicBalanceRun run) {
                     Finish(Analysis::HARMONIC_BALANCE,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetHarmonicBalance(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

} // namespace GUI
