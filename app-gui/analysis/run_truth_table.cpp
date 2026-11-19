/**
 * @file    run_truth_table.cpp
 * @brief   Starts a truth table.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/analysis_inputs.h"
#include "analysis/run_starts.h"
#include "simulation/analysis_sources.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

void AnalysisControls::RunTruthTableAnalysis(const Core::Circuit &circuit) {
    const std::vector<std::string> available = Core::GetTruthTableInputs(circuit);
    if (available.empty()) {
        Refuse(Analysis::TRUTH_TABLE, "A truth table needs a logic input to vary; add a Logic input");
        return;
    }
    const std::vector<int> outputs = ListMeasuredNodes(circuit, m_Schematic);
    if (outputs.empty()) {
        Refuse(Analysis::TRUTH_TABLE,
               "Measure a voltage with the Probe tool: the truth table reads the measured nodes");
        return;
    }
    const Core::TruthTableSettings run_settings{
        .Inputs = ResolveTruthTableInputs(m_Schematic.GetSimulationSettings().TruthTableInputs, available),
        .OutputNodes = outputs};
    StartSingleRun(
        Analysis::TRUTH_TABLE,
        circuit,
        [run_settings](const Core::Circuit &copy, Core::RunControl &control) {
            return Core::RunTruthTable(copy, run_settings, &control);
        },
        [this](std::optional<Core::TruthTable> result) { m_Results.SetTruthTable(std::move(result)); });
}

} // namespace GUI
