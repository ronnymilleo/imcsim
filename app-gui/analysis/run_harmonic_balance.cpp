/**
 * @file    run_harmonic_balance.cpp
 * @brief   Starts a harmonic balance.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/run_starts.h"
#include <optional>
#include <utility>

namespace GUI {

void AnalysisControls::RunHarmonicBalanceAnalysis(const Core::Circuit &circuit) {
    const Core::HarmonicBalanceSettings run_settings = m_Schematic.GetSimulationSettings().HarmonicBalance;
    StartSingleRun(
        Analysis::HARMONIC_BALANCE,
        circuit,
        [run_settings](const Core::Circuit &copy, Core::RunControl &control) {
            return Core::RunHarmonicBalance(copy, run_settings, &control);
        },
        [this](std::optional<Core::HarmonicBalance> result) { m_Results.SetHarmonicBalance(std::move(result)); });
}

} // namespace GUI
