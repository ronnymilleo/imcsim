/**
 * @file    run_truth_table.cpp
 * @brief   Starts a truth table.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/analysis_inputs.h"
#include "analysis/run_jobs.h"
#include "simulation/analysis_sources.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

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

} // namespace GUI
