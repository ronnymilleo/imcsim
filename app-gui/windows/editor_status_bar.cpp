/**
 * @file    editor_status_bar.cpp
 * @brief   The line under the canvas of the editor: the state of the last run, the hint of the mode and the cursor.
 */

#include "windows/editor_status_bar.h"

#include "analysis/run_status.h"
#include "base/text_fitting.h"
#include "base/theme.h"
#include "imgui.h"
#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace GUI {

namespace {

/// The middle dot between the run state and its reason, encoded in UTF-8.
constexpr std::string_view MIDDLE_DOT = "\xC2\xB7";
/// Space between the parts of the status bar, in font sizes.
constexpr float STATUS_GAP_FONT_SIZES = 1.5f;
/// Share of the status bar the run state may take, so a long failure reason leaves room for the hint.
constexpr float MAX_RUN_STATUS_SHARE = 0.6f;

ImVec4 GetToneColor(const RunStatusTone tone) {
    switch (tone) {
    case RunStatusTone::WARNING:
        return GetWarningTextColor();
    case RunStatusTone::ERROR:
        return GetErrorTextColor();
    case RunStatusTone::PLAIN:
    default:
        return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    }
}

/**
 * @brief   "Transient done", in the warning color when something needs a look and in the error color after a failure,
 *          followed by the reason; a click opens the report of the run, with the ngspice output.
 * @param[in] line       State of the run, from DescribeRunStatus().
 * @param[in] controls   Runs, whose report a click opens.
 * @param[in] max_right  Where the state must end, shortened at a word to fit.
 * @return  Where the state ends, or where the line starts when there is none.
 */
float DrawRunStatus(const RunStatusLine &line, const AnalysisControls &controls, const float max_right) {
    const std::optional<Analysis> last_run = controls.GetLastRun();
    const ImVec2 line_start = ImGui::GetCursorScreenPos();
    if (line.Text.empty()) {
        return line_start.x;
    }
    const auto measure = [](const std::string_view text) {
        return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    };
    const float max_width = max_right - line_start.x;
    const std::string text = ShortenToWidth(line.Text, max_width, measure);
    const float text_width = measure(text);
    // The reason gets what the state leaves
    const std::string detail = line.Detail.empty() ? std::string()
                                                   : ShortenToWidth(std::format(" {} {}", MIDDLE_DOT, line.Detail),
                                                                    max_width - text_width, measure);
    // A run in progress keeps the width of its widest text, so the hint does not shift as its figures change
    const float width = std::min(max_width, std::max(text_width + measure(detail), measure(line.WidestText)));
    if (line.IsRunning) {
        ImGui::TextDisabled("%s", text.c_str());
    } else {
        if (ImGui::Selectable("##run_status", false, ImGuiSelectableFlags_None, {width, 0.0f})) {
            ImGui::OpenPopup("run_report");
        }
        ImGui::SetItemTooltip("Show what the run reported, with the ngspice output");
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        draw_list->AddText(line_start, ImGui::GetColorU32(GetToneColor(line.Tone)), text.c_str());
        draw_list->AddText({line_start.x + text_width, line_start.y}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                           detail.c_str());
    }
    if (ImGui::BeginPopup("run_report")) {
        if (last_run) {
            controls.DrawRunReport(*last_run);
        }
        ImGui::EndPopup();
    }
    return line_start.x + width;
}

} // namespace

/**
 * @brief   The state of the last run first, then what the current mode does and which keys it takes, so the shortcuts
 *          can be learned while working, and the grid point under the cursor on the right.
 * @details The run state leads, so a finished run and a failed one tell apart at a glance; the hint takes the space
 *          left and is shortened at a word, never cut mid-word.
 * @param[in]     mode       The mode of the editor.
 * @param[in,out] schematic  Schematic on show, whose selection the hint talks about.
 * @param[in]     controls   Runs, which say what the last one reported and whether one is live.
 * @param[in]     transient  The transient of the results on show, which a run status counts the steps of.
 * @param[in]     view       Pan and zoom of the canvas, to read the grid point under the cursor.
 * @param[in]     hovered    Whether the cursor is over the canvas, which shows its position.
 */
void DrawStatusBar(const EditorMode &mode, Schematic &schematic, const AnalysisControls &controls,
                   const std::optional<Core::Transient> &transient, const ViewTransform &view, const bool hovered) {
    const std::string hint = DescribeEditorHint(mode, schematic, controls.IsLive());
    const ImVec2 line_start = ImGui::GetCursorScreenPos();
    const float gap = ImGui::GetFontSize() * STATUS_GAP_FONT_SIZES;
    float right_edge = line_start.x + ImGui::GetContentRegionAvail().x;
    if (hovered) {
        const GridPoint point = Snap(view.ToWorld(ImGui::GetIO().MousePos));
        const std::string position = std::format("x {}, y {}", point.X, point.Y);
        right_edge -= ImGui::CalcTextSize(position.c_str()).x;
        ImGui::SetCursorScreenPos({right_edge, line_start.y});
        ImGui::TextDisabled("%s", position.c_str());
        right_edge -= gap;
    }
    ImGui::SetCursorScreenPos(line_start);
    // The reason of a failure is worth more than the hint, so it takes the whole line
    const RunStatusLine line = DescribeRunStatus(controls, transient);
    if (line.TakesWholeLine) {
        DrawRunStatus(line, controls, right_edge);
        return;
    }
    float hint_start = DrawRunStatus(line, controls, line_start.x + (right_edge - line_start.x) * MAX_RUN_STATUS_SHARE);
    if (hint_start > line_start.x) {
        hint_start += gap;
    }
    const std::string fitted = ShortenToWidth(hint, right_edge - hint_start, [](const std::string_view text) {
        return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    });
    ImGui::SetCursorScreenPos({hint_start, line_start.y});
    ImGui::TextDisabled("%s", fitted.c_str());
}

} // namespace GUI
