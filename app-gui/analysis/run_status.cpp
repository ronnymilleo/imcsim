/**
 * @file    run_status.cpp
 * @brief   The text of the run status in the status bar of the editor, what the last run reported, or how far a run in
 *          progress is, the label of the Run button, and the full report of the last run of an analysis.
 */

#include "analysis/run_status.h"

#include "base/text_fitting.h"
#include "base/theme.h"
#include "imgui.h"
#include "schematic/shortcuts.h"
#include "spice/spice_value.h"
#include <format>
#include <string>

namespace GUI {

/**
 * @brief   "Transient running", with the share done when the run reports one.
 */
std::string FormatRunProgress(const std::string_view analysis_name, const bool is_stopping, const double progress) {
    if (is_stopping) {
        return std::format("Stopping {}", analysis_name);
    }
    if (progress > 0.0) {
        return std::format("{} running, {:.0f}%", analysis_name, progress * 100.0);
    }
    return std::format("{} running", analysis_name);
}

/**
 * @brief   Formats a simulated instant with fixed decimals per engineering decade so live readouts do not jitter.
 * @param[in] seconds  Simulated time in seconds.
 * @return  Formatted time with unit, such as "12.34ms".
 */
std::string FormatLiveTime(const double seconds) {
    return std::format("{}s", Core::FormatMeasurement(seconds));
}

/**
 * @brief   "Transient live at 12.50ms", or paused: a live run has no end to count toward, so it tells how far it has
 *          got.
 */
std::string FormatLiveProgress(const bool is_paused, const std::optional<Core::Transient> &transient) {
    const char *state = is_paused ? "paused" : "live";
    if (!transient || transient->Times.empty()) {
        return std::format("Transient {}", state);
    }
    return std::format("Transient {} at {}", state, FormatLiveTime(transient->Times.back()));
}

/**
 * @brief   "2000 time steps in 41ms" for a transient, "41ms" for another analysis: how much the last run made and how
 *          long it took, or empty when the run was live and kept pace with the clock.
 */
std::string DescribeRunCost(const std::optional<double> seconds,
                            const Analysis analysis,
                            const std::optional<Core::Transient> &transient) {
    if (!seconds) {
        return {};
    }
    const std::string time = std::format("{}s", Core::FormatValue(*seconds));
    if (analysis == Analysis::TRANSIENT && transient) {
        return std::format("{} time steps in {}", transient->Times.size(), time);
    }
    return time;
}

/**
 * @brief   The label of the Run button at the foot of the analysis settings, with the key that presses it.
 * @param[in] action  What the button does now.
 * @return  "Run" or "Continue" with the key of the Run command, such as "Run (F5)"; "Stop" alone, since the key never
 *          stops a run.
 */
std::string GetRunButtonLabel(const RunAction action) {
    switch (action) {
    case RunAction::STOP:
        return "Stop";
    case RunAction::CONTINUE:
        return NameWithShortcut("Continue", EditorCommand::RUN);
    case RunAction::RUN:
    default:
        return NameWithShortcut("Run", EditorCommand::RUN);
    }
}

/**
 * @brief   What the last run reported, in the tone its outcome asks for; a run in progress takes the place of that
 *          report.
 */
RunStatusLine DescribeRunStatus(const AnalysisControls &controls, const std::optional<Core::Transient> &transient) {
    if (controls.IsLive() && !controls.IsStopping()) {
        return {.Text = FormatLiveProgress(controls.IsLivePaused(), transient),
                .IsRunning = true,
                .WidestText = "Transient paused at 888.8ms"};
    }
    if (const std::optional<Analysis> running = controls.GetRunningAnalysis()) {
        return {.Text = FormatRunProgress(GetAnalysisName(*running), controls.IsStopping(), controls.GetRunProgress()),
                .IsRunning = true};
    }
    const std::optional<Analysis> last_run = controls.GetLastRun();
    if (!last_run) {
        return {};
    }
    const std::string_view name = GetAnalysisName(*last_run);
    switch (controls.GetOutcome(*last_run)) {
    case RunOutcome::SUCCEEDED:
        return {.Text = std::format("{} done", name),
                .Detail = DescribeRunCost(controls.GetRunSeconds(*last_run), *last_run, transient)};
    case RunOutcome::SUCCEEDED_WITH_WARNINGS:
        return {.Text = std::format("{} done, with warnings", name),
                .Detail = DescribeRunCost(controls.GetRunSeconds(*last_run), *last_run, transient),
                .Tone = RunStatusTone::WARNING};
    case RunOutcome::STOPPED:
        return {.Text = std::format("{} stopped", name)};
    case RunOutcome::FAILED: {
        const std::optional<std::string> &error = controls.GetError(*last_run);
        return {.Text = std::format("{} failed", name),
                .Detail = error ? std::string(GetFirstLine(*error)) : std::string(),
                .Tone = RunStatusTone::ERROR,
                .TakesWholeLine = true};
    }
    case RunOutcome::NONE:
    default:
        return {};
    }
}

/**
 * @brief   Draws what the last run of an analysis reported: its error or its warnings, and the ngspice output.
 * @param[in] controls  Controls that ran the analysis.
 * @param[in] analysis  Analysis whose last run is shown.
 */
void DrawRunReport(const AnalysisControls &controls, const Analysis analysis) {
    const RunStatus &status = controls.GetRunStatus(analysis);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 30.0f);
    if (status.Error) {
        ImGui::TextColored(GetErrorTextColor(), "%s", status.Error->c_str());
    } else if (controls.GetOutcome(analysis) != RunOutcome::NONE) {
        // Without an error, the outcome is None exactly when the analysis has no result
        ImGui::Text("%s done", GetAnalysisName(analysis));
        for (const std::string &warning : controls.GetResultWarnings(analysis)) {
            ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
        }
        if (status.HasNgspiceWarnings()) {
            ImGui::TextColored(GetWarningTextColor(),
                               "ngspice reported warnings; check its output before trusting "
                               "the results");
        }
    } else {
        ImGui::TextDisabled("No results for the current circuit");
    }
    ImGui::PopTextWrapPos();
    if (status.Messages.empty() || !ImGui::CollapsingHeader("ngspice output")) {
        return;
    }
    for (const Core::SimulatorMessage &message : status.Messages) {
        if (message.FromErrorStream) {
            ImGui::TextColored(GetWarningTextColor(), "%s", message.Text.c_str());
        } else {
            ImGui::TextUnformatted(message.Text.c_str());
        }
    }
}

} // namespace GUI
