/**
 * @file    run_ac_sweep.cpp
 * @brief   Starts an AC sweep, which a parameter step or a Monte Carlo run repeats.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/run_starts.h"
#include <optional>
#include <utility>

namespace GUI {

void AnalysisControls::RunACSweepAnalysis(const Core::Circuit &circuit, const RunRepeat &repeat) {
    const Core::ACSweepSettings settings = m_Schematic.GetSimulationSettings().ACSweep;
    StartRepeatable<Core::ACSweep>(
        Analysis::AC_SWEEP,
        circuit,
        repeat,
        {.RunOnce =
             [settings](const Core::Circuit &copy, const std::optional<Core::ParameterValue> &value) {
                 return Core::RunACSweep(copy, settings, value);
             },
         .Measure = MeasureACSweep,
         .StoreSingle = [this](std::optional<Core::ACSweep> result) { m_Results.SetACSweep(std::move(result)); },
         .StoreFamily = [this](StepFamily<Core::ACSweep> family) { m_Results.SetACSweepSteps(std::move(family)); }});
}

} // namespace GUI
