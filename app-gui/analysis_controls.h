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
 * @class   AnalysisControls
 * @brief   Runs the selected analysis on a worker thread and keeps what the last run of each analysis reported.
 * @details The settings and the results live in the Schematic: the settings are saved with it, and the results
 *          disappear as soon as the circuit changes. The error and the ngspice output belong to the last run of
 *          each analysis and stay until its next run. The Application owns one instance, which the editor toolbar
 *          and the Analysis Settings window both use. A run works on a copy of the circuit, so the user keeps editing
 * while it goes on; an edit of the circuit stops it, as it drops the results of the last one. Call Update() every
 * frame, since the results arrive there.
 */
class AnalysisControls {
public:
    explicit AnalysisControls(Schematic &schematic);

    // Runs
    void RunSelected();
    void StopRun();
    void Update();
    bool IsRunning() const;
    bool IsStopping() const;
    std::optional<Analysis> GetRunningAnalysis() const;
    double GetRunProgress() const;

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

    // Last run of each analysis, in the order of the Analysis enum
    std::array<RunStatus, ANALYSIS_COUNT> m_Statuses;
    std::optional<Analysis> m_LastRun;

    // Runs of one analysis
    void StartRun(Analysis analysis, Core::Job job);
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
