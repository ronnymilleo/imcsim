/**
 * @file    run_harmonic_balance.cpp
 * @brief   Starts a harmonic balance.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/run_jobs.h"
#include <utility>

namespace GUI {

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
