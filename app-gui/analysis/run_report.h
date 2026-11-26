/**
 * @file    run_report.h
 * @brief   What the last run of each analysis reported besides its result: its error, the ngspice output, the time it
 *          took, and how it went.
 */

#ifndef IMCSIM_RUN_REPORT_H
#define IMCSIM_RUN_REPORT_H

#include "results/result_store.h"
#include "schematic/simulation_settings.h"
#include "simulation/simulator.h"
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
 * @struct  RunStatus
 * @brief   What the last run of one analysis left besides its result.
 */
struct RunStatus {
    std::optional<std::string> Error;
    std::vector<Core::SimulatorMessage> Messages;
    /// Wall time from Run to the result, none for a live run, which keeps pace with the clock, and for a run refused
    /// before it started.
    std::optional<double> Seconds;

    bool HasNgspiceWarnings() const;
};

/**
 * @class   RunReport
 * @brief   The status of the last run of each analysis, which analysis ran last and how many runs have finished.
 * @details A status stays until the next run of its analysis; the result it goes with lives in the ResultStore, which
 *          drops it when the circuit changes, so how a run went depends on both.
 */
class RunReport {
public:
    // Recording
    void Record(Analysis analysis, RunStatus status);
    void Clear(Analysis analysis);
    void Reset();

    // Reading
    const RunStatus &GetStatus(Analysis analysis) const;
    RunOutcome
    GetOutcome(Analysis analysis, const ResultStore &results, const std::vector<std::string> &result_warnings) const;
    std::optional<Analysis> GetLastRun() const;
    std::size_t GetFinishedRunCount() const;

private:
    /// Last run of each analysis, in the order of the Analysis enum.
    std::array<RunStatus, ANALYSIS_COUNT> m_Statuses;
    std::optional<Analysis> m_LastRun;
    /// Runs finished so far, which tells a new outcome from the same one seen again.
    std::size_t m_FinishedRunCount = 0;
};

} // namespace GUI

#endif // IMCSIM_RUN_REPORT_H
