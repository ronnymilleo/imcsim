/**
 * @file    run_s_parameters.cpp
 * @brief   Starts an S-parameter analysis.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/run_jobs.h"
#include <utility>

namespace GUI {

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

} // namespace GUI
