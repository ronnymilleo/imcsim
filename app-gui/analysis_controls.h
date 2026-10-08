/**
 * @file    analysis_controls.h
 * @brief   Settings and runs of the analyses, shared by the editor toolbar and the Analysis Settings window.
 */

#ifndef IMCSIM_ANALYSIS_CONTROLS_H
#define IMCSIM_ANALYSIS_CONTROLS_H

#include "schematic.h"
#include "simulation_settings.h"
#include "simulator.h"
#include "value_field.h"
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
 *          until the next run.
 */
enum class RunOutcome {
    None,
    Succeeded,
    SucceededWithWarnings,
    Failed
};

/**
 * @class   AnalysisControls
 * @brief   Picks the analysis Run starts, edits the settings of each analysis and runs them through ngspice.
 * @details The settings and the results live in the Schematic: the settings are saved with it, and the results
 *          disappear as soon as the circuit changes. The error and the ngspice output belong to the last run of
 *          each analysis and stay until its next run. The Application owns one instance, which the editor toolbar
 *          and the Analysis Settings window both draw, so the fields being typed are the same in both.
 */
class AnalysisControls {
public:
    explicit AnalysisControls(Schematic &schematic);

    // Settings
    void DrawAnalysisPicker();
    void DrawSettings();

    // Runs
    void RunSelected();

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

    Schematic &m_Schematic;

    // Settings fields, reloaded whenever the schematic replaces its settings
    std::size_t m_LoadedSettingsVersion = 0;
    ValueField m_StopTime;
    ValueField m_TimeStep;
    ValueField m_StartFrequency;
    ValueField m_StopFrequency;
    // Start, stop and step of each DC sweep range
    std::array<ValueField, 3> m_SweptFields;
    std::array<ValueField, 3> m_SteppedFields;

    // Last run of each analysis, in the order of the Analysis enum
    std::array<RunStatus, 4> m_Statuses;
    std::optional<Analysis> m_LastRun;

    void LoadSettingsFields();
    void DrawTransientSettings(SimulationSettings &settings);
    void DrawACSweepSettings(SimulationSettings &settings);
    void DrawDCSweepSettings(SimulationSettings &settings);
    void Finish(Analysis analysis, std::optional<std::string> error, std::vector<Core::SimulatorMessage> messages);
    std::vector<std::string> GetResultWarnings(Analysis analysis) const;
    bool HasResult(Analysis analysis) const;
};

const char *GetAnalysisName(Analysis analysis);

} // namespace GUI

#endif // IMCSIM_ANALYSIS_CONTROLS_H
