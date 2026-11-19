/**
 * @file    run_transient.cpp
 * @brief   Starts a transient, live or not, which a parameter step or a Monte Carlo run repeats.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/run_starts.h"
#include "simulation/check_transient.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

namespace GUI {

void AnalysisControls::RunTransientAnalysis(const Core::Circuit &circuit, const RunRepeat &repeat) {
    const Core::TransientSettings settings = m_Schematic.GetSimulationSettings().Transient;
    if (IsSingleRun(repeat)) {
        StartSingleTransient(circuit, settings);
        return;
    }
    m_IsLiveResult = false;
    StartRepeatable<Core::Transient>(
        Analysis::TRANSIENT,
        circuit,
        repeat,
        {.RunOnce =
             [settings](const Core::Circuit &copy, const std::optional<Core::ParameterValue> &value) {
                 return Core::RunTransient(copy, settings, value);
             },
         .Measure = MeasureTransient,
         .StoreSingle = [this](std::optional<Core::Transient> result) { m_Results.SetTransient(std::move(result)); },
         .StoreFamily =
             [this](StepFamily<Core::Transient> family) { m_Results.SetTransientSteps(std::move(family)); }});
}

/**
 * @brief   A single transient grows in the results chunk by chunk; a live one goes on to its limit at the pace the
 *          settings give, unless the user stops it first.
 */
void AnalysisControls::StartSingleTransient(const Core::Circuit &circuit, const Core::TransientSettings &settings) {
    // The points arrive in chunks while the run goes on, so the old result gives way to the first one
    m_Results.SetTransient(std::nullopt);
    const std::size_t circuit_version = m_Schematic.GetCircuitVersion();
    m_IsLiveRun = m_IsLiveModeOn && !FindLiveObstacle();
    m_IsLiveResult = m_IsLiveRun;
    m_Schematic.SetLiveRun(m_IsLiveRun);
    Core::TransientSettings run_settings = settings;
    double pace = 0.0;
    if (m_IsLiveRun) {
        pace = settings.StopTime / m_Schematic.GetSimulationSettings().LiveSeconds;
        // One step short of the most a transient may span, which the check would refuse after rounding
        // A noise or random source steps the transient on its own, so the finest step counts for the points
        const double finest_step =
            std::min(settings.TimeStep, Core::FindFinestSignalStep(circuit).value_or(settings.TimeStep));
        run_settings.StopTime =
            std::min(pace * LIVE_RUN_LIMIT_SECONDS, finest_step * static_cast<double>(Core::MAX_TRANSIENT_POINTS - 1));
    }
    const bool is_live = m_IsLiveRun;
    StartRun(Analysis::TRANSIENT,
             MakeJob<Core::TransientRun, KEEPS_PARTIAL_RESULT>(
                 circuit,
                 [this, run_settings, circuit_version, pace](const Core::Circuit &copy, Core::RunControl &control) {
                     const auto post_chunk = [this, &control, circuit_version](Core::Transient chunk) {
                         control.Post(
                             [this, circuit_version, points = std::make_shared<Core::Transient>(std::move(chunk))] {
                                 if (m_Schematic.GetCircuitVersion() == circuit_version) {
                                     m_Results.AppendTransient(std::move(*points));
                                 }
                             });
                     };
                     return Core::RunTransient(copy,
                                               run_settings,
                                               std::nullopt,
                                               {.Control = &control, .OnChunk = post_chunk, .LivePace = pace});
                 },
                 [this, is_live](Core::TransientRun run) {
                     if (is_live && run.Result && run.Result->IsComplete) {
                         run.Result->Warnings.emplace_back(
                             "The live run reached its limit, an hour on the clock or a million time steps, whichever "
                             "comes first; Run starts a new one");
                     }
                     Finish(Analysis::TRANSIENT,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetTransient(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

} // namespace GUI
