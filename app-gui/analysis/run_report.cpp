/**
 * @file    run_report.cpp
 * @brief   What the last run of each analysis reported besides its result: its error, the ngspice output, the time it
 *          took, and how it went.
 */

#include "analysis/run_report.h"

#include <algorithm>
#include <utility>

namespace GUI {

namespace {

std::size_t ToIndex(const Analysis analysis) {
    return static_cast<std::size_t>(analysis);
}

} // namespace

/**
 * @brief   Tells whether ngspice printed warnings during the run.
 * @return  True when any of its messages came from its error stream.
 */
bool RunStatus::HasNgspiceWarnings() const {
    return std::ranges::any_of(Messages, [](const Core::SimulatorMessage &message) { return message.FromErrorStream; });
}

/**
 * @brief   Records how a run of an analysis ended, in place of its last one, and makes it the last run.
 * @param[in] analysis  Analysis that ran, or that was refused.
 * @param[in] status    Its error, the ngspice output and the time it took.
 */
void RunReport::Record(const Analysis analysis, RunStatus status) {
    m_Statuses[ToIndex(analysis)] = std::move(status);
    m_LastRun = analysis;
    ++m_FinishedRunCount;
}

/**
 * @brief   Forgets the last run of an analysis, as when the user discards it; which analysis ran last stays.
 * @param[in] analysis  Analysis whose status goes.
 */
void RunReport::Clear(const Analysis analysis) {
    m_Statuses[ToIndex(analysis)] = {};
}

/**
 * @brief   Returns what the last run of an analysis left besides its result.
 * @param[in] analysis  Analysis to read.
 * @return  Its status, empty before its first run.
 */
const RunStatus &RunReport::GetStatus(const Analysis analysis) const {
    return m_Statuses[ToIndex(analysis)];
}

/**
 * @brief   Tells how the last run of an analysis went.
 * @param[in] analysis         Analysis to check.
 * @param[in] results          Where its result is, if it still is.
 * @param[in] result_warnings  The warnings its result carries, Monte Carlo ones included.
 * @return  Failed while its last run has an error; Stopped for a transient that ended early; with warnings when the
 *          result or ngspice reported some; None when it has no result, such as after the circuit changed.
 */
RunOutcome RunReport::GetOutcome(const Analysis analysis,
                                 const ResultStore &results,
                                 const std::vector<std::string> &result_warnings) const {
    const RunStatus &status = GetStatus(analysis);
    if (status.Error) {
        return RunOutcome::FAILED;
    }
    if (!results.HasResult(analysis)) {
        return RunOutcome::NONE;
    }
    if (analysis == Analysis::TRANSIENT && results.GetTransient() && !results.GetTransient()->IsComplete) {
        return RunOutcome::STOPPED;
    }
    return status.HasNgspiceWarnings() || !result_warnings.empty() ? RunOutcome::SUCCEEDED_WITH_WARNINGS
                                                                   : RunOutcome::SUCCEEDED;
}

/**
 * @brief   Returns the analysis that ran last.
 * @return  The analysis, or no value before the first run.
 */
std::optional<Analysis> RunReport::GetLastRun() const {
    return m_LastRun;
}

/**
 * @brief   Counts the runs that finished, so a caller can tell a new outcome from one it has already seen.
 * @return  The number of runs finished since the start, failed and refused ones included.
 */
std::size_t RunReport::GetFinishedRunCount() const {
    return m_FinishedRunCount;
}

} // namespace GUI
