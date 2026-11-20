/**
 * @file    editor_toolbar.cpp
 * @brief   The command bar under the menu: tools, history and view, the part search, and the run controls at its right.
 */

#include "windows/editor_toolbar.h"

#include "analysis/run_status.h"
#include "base/theme.h"
#include "schematic/shortcuts.h"
#include "spice/spice_value.h"
#include "windows/tool_icons.h"
#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <utility>

namespace GUI {

namespace {

/// Width of the analysis settings that hang below Run, in font sizes.
constexpr float SETTINGS_POPOVER_WIDTH = 20.0f;

// Spacing of the bar, in font sizes
constexpr float BUTTON_SPACING = 0.25f;
constexpr float GROUP_GAP = 0.6f;
/// Widest the part search grows, in font sizes; it shrinks first when the bar is short of room.
constexpr float SEARCH_MAX_WIDTH = 18.0f;
/// Narrowest the part search gets, in font sizes.
constexpr float SEARCH_MIN_WIDTH = 8.0f;

/**
 * @brief   A thin upright line between two groups of buttons, centered in the gap.
 */
void DrawGroupSeparator() {
    const float gap = ImGui::GetFontSize() * GROUP_GAP;
    ImGui::SameLine(0.0f, gap / 2.0f);
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const float side = GetToolButtonSide();
    ImGui::GetWindowDrawList()->AddLine({top.x, top.y + side * 0.2f},
                                        {top.x, top.y + side * 0.8f},
                                        ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy({1.0f, side});
    ImGui::SameLine(0.0f, gap / 2.0f);
}

/**
 * @brief   Square toolbar button; the active one uses the primary colors, as the tool in use.
 */
bool ToolButton(const char *id, const bool active) {
    const float side = GetToolButtonSide();
    return active ? PrimaryButton(id, {side, side}) : ImGui::Button(id, {side, side});
}

/**
 * @brief   A small triangle pointing down, which tells a button opens a list.
 */
void DrawDropdownArrow(const ImVec2 center) {
    const float arrow = ImGui::GetFontSize() * 0.25f;
    ImGui::GetWindowDrawList()->AddTriangleFilled(center + ImVec2(-arrow, -arrow * 0.5f),
                                                  center + ImVec2(arrow, -arrow * 0.5f),
                                                  center + ImVec2(0.0f, arrow * 0.5f),
                                                  GetIconColor(true));
}

/**
 * @brief   The button that names the selected analysis, with room for its dropdown arrow.
 */
float AnalysisButtonWidth() {
    float widest = 0.0f;
    for (const Analysis analysis : ALL_ANALYSES) {
        widest = std::max(widest, ImGui::CalcTextSize(GetAnalysisName(analysis)).x);
    }
    return widest + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFontSize();
}

/**
 * @brief   The Live toggle beside the analysis button, which becomes Discard while a live run goes on.
 */
float LiveButtonWidth() {
    return std::max(ImGui::CalcTextSize("Live").x, ImGui::CalcTextSize("Discard").x)
         + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFontSize();
}

/**
 * @brief   The analysis button, Live and Run, pinned to the right end of the bar.
 */
float GetRunControlsWidth() {
    const float spacing = ImGui::GetFontSize() * BUTTON_SPACING;
    return AnalysisButtonWidth() + LiveButtonWidth() + GetToolButtonSide() + spacing * 2.0f;
}

/**
 * @struct  ToolButtonLook
 * @brief   What one square button of the bar shows: its icon, whether it is the tool in use or can be pressed, and its
 *          tooltip.
 */
struct ToolButtonLook {
    const char *Id;
    ToolIcon Icon;
    bool IsActive = false;
    bool IsEnabled = true;
    std::string Tooltip;
};

/**
 * @class   ToolButtonRow
 * @brief   A group of square buttons side by side, spaced alike, each with its icon and its tooltip.
 */
class ToolButtonRow {
public:
    /**
     * @brief   Draws the next button of the group.
     * @param[in] look  What the button shows.
     * @return  True in the frame it is clicked.
     */
    bool Draw(const ToolButtonLook &look) {
        if (!std::exchange(m_IsFirst, false)) {
            ImGui::SameLine(0.0f, ImGui::GetFontSize() * BUTTON_SPACING);
        }
        ImGui::BeginDisabled(!look.IsEnabled);
        const bool clicked = ToolButton(look.Id, look.IsActive);
        ImGui::EndDisabled();
        DrawToolIcon(ImGui::GetWindowDrawList(),
                     look.Icon,
                     GetItemCenter(),
                     GetIconSize(),
                     GetIconColor(look.IsEnabled));
        ImGui::SetItemTooltip("%s", look.Tooltip.c_str());
        return clicked;
    }

private:
    /// The first button of a group starts the bar or follows a separator, which makes its own gap.
    bool m_IsFirst = true;
};

/**
 * @brief   Select, Wire and Probe, with the tool in use shown as active.
 * @return  The mode of the tool clicked this frame, or none.
 */
std::optional<EditorMode> DrawToolButtons(const EditorMode &mode) {
    ToolButtonRow row;
    std::optional<EditorMode> picked;
    if (row.Draw(
            {.Id = "##select", .Icon = ToolIcon::SELECT, .IsActive = IsSelecting(mode), .Tooltip = "Select (Esc)"})) {
        picked = SelectingMode{};
    }
    if (row.Draw({.Id = "##wire",
                  .Icon = ToolIcon::WIRE,
                  .IsActive = std::holds_alternative<WiringMode>(mode),
                  .Tooltip = "Wire (W)"})) {
        picked = WiringMode{};
    }
    if (row.Draw({.Id = "##probe",
                  .Icon = ToolIcon::PROBE,
                  .IsActive = std::holds_alternative<ProbingMode>(mode),
                  .Tooltip = "Probe (P): pick what the plots measure"})) {
        picked = ProbingMode{};
    }
    return picked;
}

/**
 * @brief   Undo and Redo, enabled when the schematic has a step to take, and Fit.
 * @return  The command of the button clicked this frame, or none.
 */
std::optional<EditorCommand> DrawHistoryButtons(const Schematic &schematic) {
    ToolButtonRow row;
    std::optional<EditorCommand> command;
    if (row.Draw({.Id = "##undo",
                  .Icon = ToolIcon::UNDO,
                  .IsEnabled = schematic.CanUndo(),
                  .Tooltip = NameWithShortcut("Undo", EditorCommand::UNDO)})) {
        command = EditorCommand::UNDO;
    }
    if (row.Draw({.Id = "##redo",
                  .Icon = ToolIcon::REDO,
                  .IsEnabled = schematic.CanRedo(),
                  .Tooltip = NameWithShortcut("Redo", EditorCommand::REDO)})) {
        command = EditorCommand::REDO;
    }
    if (row.Draw({.Id = "##fit",
                  .Icon = ToolIcon::FIT,
                  .Tooltip = NameWithShortcut("Fit the schematic in view", EditorCommand::FIT)})) {
        command = EditorCommand::FIT;
    }
    return command;
}

/**
 * @brief   The part search, as wide as the room before the run controls allows, between its narrowest and widest.
 * @param[in,out] search    The part search.
 * @param[in]     blocks    The blocks that can be placed where the editor is, which the search finds too.
 * @param[in]     run_left  Where the run controls start, on the screen.
 * @return  The mode of the part picked this frame, or none.
 */
std::optional<EditorMode>
DrawPartSearch(PartPicker &search, const std::vector<const SavedSubcircuit *> &blocks, const float run_left) {
    const float font_size = ImGui::GetFontSize();
    const float room = run_left - font_size * GROUP_GAP - ImGui::GetCursorScreenPos().x;
    const float search_width = std::clamp(room, font_size * SEARCH_MIN_WIDTH, font_size * SEARCH_MAX_WIDTH);
    // The field is a frame tall and the buttons a little more, so it sits in the middle of the row
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (GetToolButtonSide() - ImGui::GetFrameHeight()) / 2.0f);
    return search.Draw(blocks, search_width);
}

/**
 * @brief   What the Run button does now, for its tooltip.
 */
std::string DescribeRunAction(const AnalysisControls &controls, const Analysis selected) {
    const RunAction action = controls.GetRunAction();
    const std::optional<Analysis> running = controls.GetRunningAnalysis();
    if (action == RunAction::STOP && controls.IsLive()) {
        return "Stop the live run, keeping what it made; Continue then takes it on from there";
    }
    if (action == RunAction::STOP && running) {
        return std::format("Stop the {} run", GetAnalysisName(*running));
    }
    if (action == RunAction::CONTINUE) {
        return NameWithShortcut("Continue the live run from where it stopped", EditorCommand::RUN);
    }
    if (controls.IsLiveModeOn() && !controls.FindLiveObstacle()) {
        return NameWithShortcut(std::format("Run {} live", GetAnalysisName(selected)), EditorCommand::RUN);
    }
    return NameWithShortcut(std::format("Run {}", GetAnalysisName(selected)), EditorCommand::RUN);
}

} // namespace

/**
 * @brief   Creates the command bar of the editor.
 * @param[in] controls        Runs of the analyses, for the Run button; they must outlive the toolbar.
 * @param[in] settings_panel  Analysis picker and settings, for the popover beside Run; it must outlive the toolbar.
 * @param[in] root_schematic  The document, whose settings name the analysis to run; it must outlive the toolbar.
 */
EditorToolbar::EditorToolbar(AnalysisControls &controls,
                             AnalysisSettingsPanel &settings_panel,
                             const Schematic &root_schematic)
    : m_Controls(controls), m_SettingsPanel(settings_panel), m_RootSchematic(root_schematic) {
}

/**
 * @brief   Draws the bar on one row that never wraps: the tools and the history on the left, the part search after
 * them, and the analysis, Live and Run at the right end, where Run stays whatever the width of the window.
 * @param[in]     mode              The mode of the editor, whose tool shows as the one in use.
 * @param[in]     schematic         The schematic being edited, for the state of Undo and Redo.
 * @param[in]     is_editing_block  Whether a block is open, which disables the run controls.
 * @param[in,out] search            The part search, drawn in the bar.
 * @param[in]     blocks            The blocks that can be placed where the editor is, which the search finds too.
 * @return  The mode or the command the buttons pressed this frame ask for, which the editor carries out.
 */
ToolbarRequest EditorToolbar::Draw(const EditorMode &mode,
                                   const Schematic &schematic,
                                   const bool is_editing_block,
                                   PartPicker &search,
                                   const std::vector<const SavedSubcircuit *> &blocks) {
    const float row_right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const float run_left = row_right - GetRunControlsWidth();
    ToolbarRequest request = {.Mode = DrawToolButtons(mode), .Command = std::nullopt};
    DrawGroupSeparator();
    request.Command = DrawHistoryButtons(schematic);
    DrawGroupSeparator();
    const float row_top = ImGui::GetCursorPosY();
    if (std::optional<EditorMode> picked = DrawPartSearch(search, blocks, run_left)) {
        request.Mode = std::move(picked);
    }
    DrawRunControls(run_left, row_top, is_editing_block);
    return request;
}

/**
 * @brief   The analysis button, Live and Run, from where the room before them ends or, when the bar is short of room,
 *          right after the part search.
 * @param[in] run_left          Where they start when the bar has room, so Run ends at the right end.
 * @param[in] row_top           Top of the row of the bar, in window coordinates.
 * @param[in] is_editing_block  Whether a block is open, which disables them.
 */
void EditorToolbar::DrawRunControls(const float run_left, const float row_top, const bool is_editing_block) {
    const float spacing = ImGui::GetFontSize() * BUTTON_SPACING;
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorScreenPos({std::max(run_left, ImGui::GetCursorScreenPos().x + ImGui::GetFontSize() * GROUP_GAP),
                               ImGui::GetWindowPos().y + row_top});
    ImGui::BeginDisabled(is_editing_block);
    DrawAnalysisButton();
    ImGui::SameLine(0.0f, spacing);
    DrawLiveToggle();
    ImGui::SameLine(0.0f, spacing);
    DrawRunButton();
    ImGui::EndDisabled();
}

/**
 * @brief   Run starts the selected analysis, Stop ends the one going on and Continue takes a stopped live run on; the
 *          one primary button of the bar.
 */
void EditorToolbar::DrawRunButton() {
    const float side = GetToolButtonSide();
    const Analysis selected = m_RootSchematic.GetSimulationSettings().Selected;
    const RunAction action = m_Controls.GetRunAction();
    if (PrimaryButton("##run", {side, side})) {
        m_Controls.PressRun();
    }
    DrawToolIcon(ImGui::GetWindowDrawList(),
                 action == RunAction::STOP ? ToolIcon::STOP : ToolIcon::RUN,
                 GetItemCenter(),
                 GetIconSize(),
                 GetIconColor(true));
    ImGui::SetItemTooltip("%s", DescribeRunAction(m_Controls, selected).c_str());
}

/**
 * @brief   The analysis picker before Live and Run, whose popover holds the settings of the analysis and its own Run
 *          button.
 */
void EditorToolbar::DrawAnalysisButton() {
    const float side = GetToolButtonSide();
    const Analysis selected = m_RootSchematic.GetSimulationSettings().Selected;
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0.0f, 0.5f});
    const std::string label = std::format("{}##analysis", GetAnalysisName(selected));
    const bool open = ImGui::Button(label.c_str(), {AnalysisButtonWidth(), side});
    ImGui::PopStyleVar();
    ImGui::SetItemTooltip("Pick the analysis and set it up");
    DrawDropdownArrow({ImGui::GetItemRectMax().x - ImGui::GetFontSize() * 0.75f, GetItemCenter().y});
    if (open) {
        // Hangs below the picker, moved left when the settings would not fit before the right edge of the window
        const float settings_width = ImGui::GetFontSize() * SETTINGS_POPOVER_WIDTH;
        const float right_edge = ImGui::GetWindowPos().x + ImGui::GetWindowWidth();
        ImGui::SetNextWindowPos(
            {std::min(ImGui::GetItemRectMin().x, right_edge - settings_width), ImGui::GetItemRectMax().y});
        ImGui::OpenPopup("analysis");
    }
    if (ImGui::BeginPopup("analysis")) {
        m_SettingsPanel.DrawAnalysisPicker();
        ImGui::Separator();
        m_SettingsPanel.DrawSettings();
        ImGui::Separator();
        // The popup has the focus, so F5 is checked here too; like the shortcut, it never stops a run
        const RunAction action = m_Controls.GetRunAction();
        if (PrimaryButton(GetRunButtonLabel(action).c_str())
            || (action != RunAction::STOP && ImGui::IsKeyPressed(ImGuiKey_F5, false))) {
            m_Controls.PressRun();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

/**
 * @brief   Live makes Run start the transient paced against the clock, so the user sets switches, push buttons, logic
 *          inputs and potentiometers on the schematic while it runs; it is disabled, with the reason, for any other
 *          analysis.
 * @details While a live run goes on, paused or not, the mode cannot change it, so the button drops the run instead.
 */
void EditorToolbar::DrawLiveToggle() {
    const ImVec2 size = {LiveButtonWidth(), GetToolButtonSide()};
    if (m_Controls.IsLive()) {
        if (ImGui::Button("Discard", size)) {
            m_Controls.DiscardLive();
        }
        ImGui::SetItemTooltip("End the live run and drop what it made; Run then starts a new one from zero");
        return;
    }
    const std::optional<std::string> obstacle = m_Controls.FindLiveObstacle();
    const bool is_on = m_Controls.IsLiveModeOn();
    ImGui::BeginDisabled(obstacle.has_value());
    if (is_on && !obstacle ? PrimaryButton("Live", size) : ImGui::Button("Live", size)) {
        m_Controls.SetLiveMode(!is_on);
    }
    ImGui::EndDisabled();
    if (obstacle) {
        ImGui::SetItemTooltip("%s", obstacle->c_str());
    } else {
        ImGui::SetItemTooltip(
            "Live: Run plays the transient on the schematic, a stop time every %s s, until you stop it, and a click "
            "flips a switch or a logic input, holds a push button or, with the wheel, turns a potentiometer on the way",
            Core::FormatValue(m_RootSchematic.GetSimulationSettings().LiveSeconds).c_str());
    }
}

} // namespace GUI
