/**
 * @file    run_dc_sweep.cpp
 * @brief   Starts a DC sweep, which a parameter step or a Monte Carlo run repeats.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/analysis_inputs.h"
#include "analysis/run_starts.h"
#include "simulation/analysis_sources.h"
#include <format>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace GUI {

/**
 * @brief   A source that is unset, or that left the circuit, uses a default; a stepped parameter and a second stepped
 *          source would both make the family of curves, so only one may be on.
 */
void AnalysisControls::RunDCSweepAnalysis(const Core::Circuit &circuit, const RunRepeat &repeat) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::vector<SweepSource> sources = ListSweepSources(circuit);
    if (sources.empty()) {
        Refuse(Analysis::DC_SWEEP, std::format("Add {} to sweep", Core::SWEEPABLE_SOURCES));
        return;
    }
    if (settings.StepSource && std::holds_alternative<Core::MonteCarloSettings>(repeat)) {
        Refuse(Analysis::DC_SWEEP, "Monte Carlo makes one curve per run, so turn off the second stepped source");
        return;
    }
    if (settings.StepSource && std::holds_alternative<StepPlan>(repeat)) {
        Refuse(Analysis::DC_SWEEP, "Step either a second source or a parameter, not both");
        return;
    }
    Core::DCSweepSettings run_settings{.Swept = settings.SweptRange};
    run_settings.Swept.Source = ResolveSweepSource(settings.SweptRange, sources, SWEPT_SOURCE_INDEX);
    if (settings.StepSource) {
        run_settings.Stepped = settings.SteppedRange;
        run_settings.Stepped->Source = ResolveSweepSource(settings.SteppedRange, sources, STEPPED_SOURCE_INDEX);
    }
    StartRepeatable<Core::DCSweep>(
        Analysis::DC_SWEEP,
        circuit,
        repeat,
        {.RunOnce =
             [run_settings](const Core::Circuit &copy, const std::optional<Core::ParameterValue> &value) {
                 return Core::RunDCSweep(copy, run_settings, value);
             },
         .Measure = MeasureDCSweep,
         .StoreSingle = [this](std::optional<Core::DCSweep> result) { m_Results.SetDCSweep(std::move(result)); },
         .StoreFamily =
             [this](StepFamily<Core::DCSweep> family) { m_Results.SetDCSweep(JoinDCSweepSteps(std::move(family))); }});
}

} // namespace GUI
