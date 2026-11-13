/**
 * @file    run_status.h
 * @brief   The text of the run status in the status bar of the editor: what the last run reported, or how far a run in
 *          progress is.
 */

#ifndef IMCSIM_RUN_STATUS_H
#define IMCSIM_RUN_STATUS_H

#include "analysis/analysis_controls.h"
#include "schematic/simulation_settings.h"
#include "simulation/simulation_results.h"
#include <optional>
#include <string>
#include <string_view>

namespace GUI {

/**
 * @enum    RunStatusTone
 * @brief   How much attention the run status asks for, which the drawing turns into a color.
 */
enum class RunStatusTone {
    PLAIN,
    WARNING,
    ERROR
};

/**
 * @struct  RunStatusLine
 * @brief   The text of the run status in the status bar, its tone, and whether it is a run in progress.
 */
struct RunStatusLine {
    std::string Text;
    /// What follows the state in a muted color, such as the time a run took or the reason it failed, or empty.
    std::string Detail;
    RunStatusTone Tone = RunStatusTone::PLAIN;
    bool IsRunning = false;
    /// Text as wide as the line can get, whose width it keeps so it does not shift as its figures change, or empty.
    std::string_view WidestText;
    /// Whether the detail is worth more than the hint, as the reason of a failure is, and takes the whole line.
    bool TakesWholeLine = false;
};

// Texts
std::string FormatRunProgress(std::string_view analysis_name, bool is_stopping, double progress);
std::string FormatLiveProgress(bool is_paused, const std::optional<Core::Transient> &transient);
std::string DescribeRunCost(std::optional<double> seconds, Analysis analysis,
                            const std::optional<Core::Transient> &transient);

// Status
RunStatusLine DescribeRunStatus(const AnalysisControls &controls, const std::optional<Core::Transient> &transient);

} // namespace GUI

#endif // IMCSIM_RUN_STATUS_H
