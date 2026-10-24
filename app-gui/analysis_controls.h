/**
 * @file    analysis_controls.h
 * @brief   Runs of the analyses on the worker thread and what they leave in the schematic, shared by the editor
 *          toolbar and the Analysis Settings window.
 */

#ifndef IMCSIM_ANALYSIS_CONTROLS_H
#define IMCSIM_ANALYSIS_CONTROLS_H

#include "parameter_step.h"
#include "schematic.h"
#include "simulation_service.h"
#include "simulation_settings.h"
#include "simulator.h"
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @enum    RunOutcome
 * @brief   How the last run of an analysis went, as the editor status bar reports it.
 * @details Succeeded turns back into None when the result is dropped, as when the circuit changes; Failed stays
 *          until the next run. Stopped is a transient that ended early and kept the points made until then.
 */
enum class RunOutcome {
    NONE,
    SUCCEEDED,
    SUCCEEDED_WITH_WARNINGS,
    STOPPED,
    FAILED
};

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
 * @details The settings and the results live in the Schematic: the settings are saved with it, and the results
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
    explicit AnalysisControls(Schematic &schematic);

    // Runs
    void RunSelected();
    void StopRun();
    RunAction GetRunAction() const;
    void PressRun();
    void Update();
    bool IsRunning() const;
    bool IsStopping() const;
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

    // Last run
    std::optional<Analysis> GetLastRun() const;
    RunOutcome GetOutcome(Analysis analysis) const;
    const std::optional<std::string> &GetError(Analysis analysis) const;
    void DrawRunReport(Analysis analysis) const;

private:
    /**
     * @struct  RunStatus
     * @brief   What the last run of one analysis left besides its result.
     */
    struct RunStatus {
        std::optional<std::string> Error;
        std::vector<Core::SimulatorMessage> Messages;
    };

    /**
     * @struct  StepPlan
     * @brief   The stepped parameter of a run, the unit of its values and the values, one run each.
     */
    struct StepPlan {
        ParameterStepSettings Settings;
        std::string Unit;
        std::vector<double> Values;
    };

    Schematic &m_Schematic;

    // The run in progress: the analysis, and the version of the circuit it started from
    Core::SimulationService m_Service;
    std::optional<Analysis> m_RunningAnalysis;
    std::size_t m_RunCircuitVersion = 0;
    // Run asked for while a paused live run held the worker, which starts once that run has ended
    bool m_IsRunPending = false;

    // Whether the next transient runs live, whether the run in progress does, and whether the transient on display
    // came from a live run
    bool m_IsLiveModeOn = false;
    bool m_IsLiveRun = false;
    bool m_IsLiveResult = false;

    // Last run of each analysis, in the order of the Analysis enum
    std::array<RunStatus, ANALYSIS_COUNT> m_Statuses;
    std::optional<Analysis> m_LastRun;

    // Runs of one analysis
    void StartRun(Analysis analysis, Core::Job job);
    void StartAnalysis(Analysis analysis, const Core::Circuit &circuit, const std::optional<StepPlan> &steps);
    void RunOperatingPointAnalysis(const Core::Circuit &circuit);
    void RunTransientAnalysis(const Core::Circuit &circuit, const std::optional<StepPlan> &steps);
    void RunACSweepAnalysis(const Core::Circuit &circuit, const std::optional<StepPlan> &steps);
    void RunDCSweepAnalysis(const Core::Circuit &circuit, const std::optional<StepPlan> &steps);
    void RunNoiseAnalysis(const Core::Circuit &circuit);
    void RunTransferFunctionAnalysis(const Core::Circuit &circuit);
    void RunTruthTableAnalysis(const Core::Circuit &circuit);

    // Results
    void ClearResult(Analysis analysis);
    void Finish(Analysis analysis, std::optional<std::string> error, std::vector<Core::SimulatorMessage> messages);
    std::vector<std::string> GetResultWarnings(Analysis analysis) const;
    bool HasResult(Analysis analysis) const;
};

} // namespace GUI

#endif // IMCSIM_ANALYSIS_CONTROLS_H
