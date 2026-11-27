/**
 * @file    analysis_controls.h
 * @brief   Runs of the analyses on the worker thread and what they leave in the results, shared by the editor
 *          toolbar and the Analysis Settings window.
 */

#ifndef IMCSIM_ANALYSIS_CONTROLS_H
#define IMCSIM_ANALYSIS_CONTROLS_H

#include "analysis/run_repeat.h"
#include "analysis/run_report.h"
#include "results/parameter_step.h"
#include "results/result_store.h"
#include "schematic/schematic.h"
#include "schematic/simulation_settings.h"
#include "simulation/simulation_service.h"
#include "simulation/simulator.h"
#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/// Defined in run_jobs.h, which only the sources that start a run include.
template <typename Result>
struct RepeatableRun;

/**
 * @enum    RunAction
 * @brief   What the Run button does now: start the selected analysis, stop the run in progress, or take a paused live
 *          run on.
 */
enum class RunAction {
    RUN,
    STOP,
    CONTINUE
};

/**
 * @class   AnalysisControls
 * @brief   Runs the selected analysis on a worker thread and keeps what the last run of each analysis reported.
 * @details The settings live in the Schematic and are saved with it; the results live in the ResultStore, and
 *          disappear as soon as the circuit changes. The error and the ngspice output belong to the last run of
 *          each analysis and stay until its next run. The Application owns one instance, which the editor toolbar
 *          and the Analysis Settings window both use. A run works on a copy of the circuit, so the user keeps editing
 * while it goes on; an edit of the circuit stops it, as it drops the results of the last one. Call Update() every
 * frame, since the results arrive there. With Live mode on, a transient runs paced against the clock, covering its stop
 * time in the live seconds of the settings, and goes on until it is stopped, up to LIVE_RUN_LIMIT_SECONDS or
 * MAX_TRANSIENT_POINTS; the parts the user sets by hand change in it (see Schematic::MarkLiveChanged()). Stopping a
 * live run pauses it with every point kept, ContinueLive() goes on from there and DiscardLive() drops it.
 */
class AnalysisControls {
public:
    AnalysisControls(Schematic &schematic, ResultStore &results);

    // Runs
    void RunSelected();
    void StopRun();
    RunAction GetRunAction() const;
    void PressRun();
    bool PressRunKey();
    void Update();
    bool IsRunning() const;
    bool IsStopping() const;
    bool IsBackendInUse() const;
    std::optional<Analysis> GetRunningAnalysis() const;
    double GetRunProgress() const;

    // Live mode
    bool IsLiveModeOn() const;
    void SetLiveMode(bool is_on);
    std::optional<std::string> FindLiveObstacle() const;
    bool IsLive() const;
    bool IsLivePaused() const;
    void ContinueLive();
    void DiscardLive();
    bool IsLiveResult() const;
    void SetSpeedMultiplier(double multiplier);
    double GetSpeedMultiplier() const;

    // Last run
    std::optional<Analysis> GetLastRun() const;
    std::size_t GetFinishedRunCount() const;
    RunOutcome GetOutcome(Analysis analysis) const;
    const std::optional<std::string> &GetError(Analysis analysis) const;
    std::optional<double> GetRunSeconds(Analysis analysis) const;
    const RunStatus &GetRunStatus(Analysis analysis) const;
    std::vector<std::string> GetResultWarnings(Analysis analysis) const;

private:
    Schematic &m_Schematic;
    ResultStore &m_Results;

    // Run in progress
    Core::SimulationService m_Service;
    std::optional<Analysis> m_RunningAnalysis;        ///< Analysis of the run in progress, if any.
    std::size_t m_RunCircuitVersion = 0;              ///< Version of the circuit the run in progress started from.
    std::chrono::steady_clock::time_point m_RunStart; ///< When the run in progress started, for the time it took.
    /// Run asked for while a paused live run held the worker, which starts once that run has ended.
    bool m_IsRunPending = false;

    // Live runs
    bool m_IsLiveModeOn = false; ///< Whether the next transient runs live.
    bool m_IsLiveRun = false;    ///< Whether the run in progress is a live one.
    bool m_IsLiveResult = false; ///< Whether the transient on display came from a live run.

    // Last runs
    RunReport m_Report;
    std::size_t m_ReportDocumentVersion = 0; ///< Version of the document the last runs were about.

    // Runs of one analysis
    void StartRun(Analysis analysis, Core::Job job);
    template <typename Work, typename Store>
    void StartSingleRun(Analysis analysis, const Core::Circuit &circuit, Work work, Store store);
    template <typename Result>
    void StartRepeatable(Analysis analysis,
                         const Core::Circuit &circuit,
                         const RunRepeat &repeat,
                         const RepeatableRun<Result> &run);
    template <typename Result>
    void StartSteppedRuns(Analysis analysis,
                          const Core::Circuit &circuit,
                          const StepPlan &plan,
                          const RepeatableRun<Result> &run);
    template <typename Result>
    void StartMonteCarloRuns(Analysis analysis,
                             const Core::Circuit &circuit,
                             const Core::MonteCarloSettings &monte_carlo,
                             const RepeatableRun<Result> &run);
    void Refuse(Analysis analysis, std::string message);
    void StartAnalysis(Analysis analysis, const Core::Circuit &circuit, const RunRepeat &repeat);
    void RunOperatingPointAnalysis(const Core::Circuit &circuit, const RunRepeat &repeat);
    void RunTransientAnalysis(const Core::Circuit &circuit, const RunRepeat &repeat);
    void StartSingleTransient(const Core::Circuit &circuit, const Core::TransientSettings &settings);
    std::function<void(Core::Transient)> MakeChunkSink(Core::RunControl &control, std::size_t circuit_version);
    void RunACSweepAnalysis(const Core::Circuit &circuit, const RunRepeat &repeat);
    void RunDCSweepAnalysis(const Core::Circuit &circuit, const RunRepeat &repeat);
    void RunNoiseAnalysis(const Core::Circuit &circuit);
    void RunTransferFunctionAnalysis(const Core::Circuit &circuit);
    void RunSensitivityAnalysis(const Core::Circuit &circuit);
    void RunACSensitivityAnalysis(const Core::Circuit &circuit);
    void RunPoleZeroAnalysis(const Core::Circuit &circuit);
    void RunSParameterAnalysis(const Core::Circuit &circuit);
    void RunTruthTableAnalysis(const Core::Circuit &circuit);
    void RunHarmonicBalanceAnalysis(const Core::Circuit &circuit);

    // Outcomes
    void
    RecordRunOutcome(Analysis analysis, std::optional<std::string> error, std::vector<Core::SimulatorMessage> messages);
};

} // namespace GUI

#endif // IMCSIM_ANALYSIS_CONTROLS_H
