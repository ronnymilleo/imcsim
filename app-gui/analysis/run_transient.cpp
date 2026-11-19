/**
 * @file    run_transient.cpp
 * @brief   Starts a transient, live or not, which a parameter step or a Monte Carlo run repeats.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/live_run_plan.h"
#include "analysis/run_starts.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace GUI {

void AnalysisControls::RunTransientAnalysis(const Core::Circuit &circuit, const RunRepeat &repeat) {
    const Core::TransientSettings settings = m_Schematic.GetSimulationSettings().Transient;
    // Only a single run can be live, and the transient it makes is the one on display until another one starts
    m_IsLiveResult = IsSingleRun(repeat) && m_IsLiveModeOn && !FindLiveObstacle();
    if (IsSingleRun(repeat)) {
        StartSingleTransient(circuit, settings);
        return;
    }
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
 * @note    RunTransientAnalysis() has decided whether it runs live, in m_IsLiveResult.
 */
void AnalysisControls::StartSingleTransient(const Core::Circuit &circuit, const Core::TransientSettings &settings) {
    // The points arrive in chunks while the run goes on, so the old result gives way to the first one
    m_Results.SetTransient(std::nullopt);
    const bool is_live = m_IsLiveResult;
    m_IsLiveRun = is_live;
    m_Schematic.SetLiveRun(is_live);
    const LiveRunPlan plan = is_live ? PlanLiveRun(settings, circuit, m_Schematic.GetSimulationSettings().LiveSeconds)
                                     : LiveRunPlan{.StopTime = settings.StopTime};
    Core::TransientSettings run_settings = settings;
    run_settings.StopTime = plan.StopTime;
    const std::size_t circuit_version = m_Schematic.GetCircuitVersion();
    StartRun(Analysis::TRANSIENT,
             MakeJob<Core::TransientRun, KEEPS_PARTIAL_RESULT>(
                 circuit,
                 [this, run_settings, circuit_version, pace = plan.Pace](const Core::Circuit &copy,
                                                                         Core::RunControl &control) {
                     return Core::RunTransient(
                         copy,
                         run_settings,
                         std::nullopt,
                         {.Control = &control, .OnChunk = MakeChunkSink(control, circuit_version), .LivePace = pace});
                 },
                 [this, is_live](Core::TransientRun run) {
                     if (is_live) {
                         AddLiveLimitWarning(run);
                     }
                     Finish(Analysis::TRANSIENT,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetTransient(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

/**
 * @brief   Where a transient on the worker thread hands the points of each chunk, which reach the results on the
 *          thread that polls, unless the circuit changed since the run started.
 * @param[in] control          Control of the run, through which the chunks are posted.
 * @param[in] circuit_version  Version of the circuit the run started from.
 * @return  The sink to give the transient as its OnChunk.
 */
std::function<void(Core::Transient)> AnalysisControls::MakeChunkSink(Core::RunControl &control,
                                                                     const std::size_t circuit_version) {
    return [this, &control, circuit_version](Core::Transient chunk) {
        control.Post([this, circuit_version, points = std::make_shared<Core::Transient>(std::move(chunk))] {
            if (m_Schematic.GetCircuitVersion() == circuit_version) {
                m_Results.AppendTransient(std::move(*points));
            }
        });
    };
}

} // namespace GUI
