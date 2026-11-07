/**
 * @file    editor_toolbar.cpp
 * @brief   The icon toolbar above the schematic: tools, history, view, parts and the run controls.
 */

#include "windows/editor_toolbar.h"

#include "drawing/theme.h"
#include "drawing/tool_icons.h"
#include "editing/shortcuts.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <utility>

namespace GUI {

namespace {

/// Width of the analysis settings that hang below Run, in font sizes.
constexpr float SETTINGS_POPOVER_WIDTH = 20.0f;

/**
 * @class   ToolbarRow
 * @brief   Lays toolbar items out left to right, moving the next item to a new line when it does not fit.
 * @details Call Place() with the width of each item right before drawing it. A gap separates groups of items
 *          and disappears when the group starts a new line.
 */
class ToolbarRow {
public:
    ToolbarRow() : m_RightEdge(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x) {}

    void Place(const float width, const float gap = 0.0f) {
        if (std::exchange(m_First, false)) {
            return;
        }
        const float spacing = ButtonSpacing() + gap;
        if (ImGui::GetItemRectMax().x + spacing + width <= m_RightEdge) {
            ImGui::SameLine(0.0f, spacing);
        }
    }

    /**
     * @brief   Space between buttons, and the extra space between groups, in font sizes.
     */
    static float ButtonSpacing() { return ImGui::GetFontSize() * 0.25f; }
    static float GroupGap() { return ImGui::GetFontSize() * 0.6f; }

private:
    float m_RightEdge;
    bool m_First = true;
};

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
                                                  center + ImVec2(0.0f, arrow * 0.5f), GetIconColor(true));
}

/**
 * @brief   The button that names the selected analysis, with room for its dropdown arrow.
 */
float AnalysisButtonWidth() {
    float widest = 0.0f;
    for (const Analysis analysis :
         {Analysis::OPERATING_POINT, Analysis::TRANSIENT, Analysis::AC_SWEEP, Analysis::DC_SWEEP, Analysis::NOISE,
          Analysis::TRANSFER_FUNCTION, Analysis::SENSITIVITY, Analysis::POLE_ZERO, Analysis::S_PARAMETERS,
          Analysis::TRUTH_TABLE}) {
        widest = std::max(widest, ImGui::CalcTextSize(GetAnalysisName(analysis)).x);
    }
    return widest + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFontSize();
}

/**
 * @brief   The Live toggle beside the analysis button, which becomes Discard while a live run goes on.
 */
float LiveButtonWidth() {
    return std::max(ImGui::CalcTextSize("Live").x, ImGui::CalcTextSize("Discard").x) +
           ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFontSize();
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
        return "Continue the live run from where it stopped (F5)";
    }
    if (controls.IsLiveModeOn() && !controls.FindLiveObstacle()) {
        return std::format("Run {} live (F5)", GetAnalysisName(selected));
    }
    return std::format("Run {} (F5)", GetAnalysisName(selected));
}

/**
 * @brief   The label of the Run button at the foot of the analysis settings.
 */
const char *GetRunButtonLabel(const RunAction action) {
    switch (action) {
    case RunAction::STOP:
        return "Stop";
    case RunAction::CONTINUE:
        return "Continue (F5)";
    case RunAction::RUN:
    default:
        return "Run (F5)";
    }
}

} // namespace

/**
 * @brief   Creates the toolbar of the editor.
 * @param[in] controls        Runs of the analyses, for the Run button; they must outlive the toolbar.
 * @param[in] settings_panel  Analysis picker and settings, for the popover beside Run; it must outlive the toolbar.
 * @param[in] root_schematic  The document, whose settings name the analysis to run; it must outlive the toolbar.
 * @param[in] preferences     The symbol standard of the part icons; they must outlive the toolbar.
 */
EditorToolbar::EditorToolbar(AnalysisControls &controls, AnalysisSettingsPanel &settings_panel,
                             const Schematic &root_schematic, const Preferences &preferences)
    : m_Controls(controls), m_SettingsPanel(settings_panel), m_RootSchematic(root_schematic),
      m_Preferences(preferences) {
}

/**
 * @brief   Draws the toolbar at the cursor of the editor window.
 * @param[in] mode              The mode of the editor, whose tool shows as the one in use.
 * @param[in] schematic         The schematic being edited, for the state of Undo and Redo.
 * @param[in] is_editing_block  Whether a block is open, which disables the run controls.
 * @return  The mode or the command the buttons pressed this frame ask for, which the editor carries out.
 */
ToolbarRequest EditorToolbar::Draw(const EditorMode &mode, const Schematic &schematic, const bool is_editing_block) {
    ToolbarRequest request;
    ToolbarRow row;
    const float gap = ToolbarRow::GroupGap();
    const float side = GetToolButtonSide();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const auto tool_button = [&](const char *id, const ToolIcon icon, const bool active, const bool enabled,
                                 const char *tooltip) {
        row.Place(side);
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ToolButton(id, active);
        ImGui::EndDisabled();
        DrawToolIcon(draw_list, icon, GetItemCenter(), GetIconSize(), GetIconColor(enabled));
        ImGui::SetItemTooltip("%s", tooltip);
        return clicked;
    };

    // Tools, history and view
    if (tool_button("##select", ToolIcon::SELECT, IsSelecting(mode), true, "Select (Esc)")) {
        request.Mode = SelectingMode{};
    }
    if (tool_button("##wire", ToolIcon::WIRE, std::holds_alternative<WiringMode>(mode), true, "Wire (W)")) {
        request.Mode = WiringMode{};
    }
    if (tool_button("##probe", ToolIcon::PROBE, std::holds_alternative<ProbingMode>(mode), true,
                    "Probe (P): pick what the plots measure")) {
        request.Mode = ProbingMode{};
    }
    if (tool_button("##undo", ToolIcon::UNDO, false, schematic.CanUndo(), "Undo (" IMCSIM_SHORTCUT "Z)")) {
        request.Command = ToolbarCommand::UNDO;
    }
    if (tool_button("##redo", ToolIcon::REDO, false, schematic.CanRedo(), "Redo (" IMCSIM_SHORTCUT "Y)")) {
        request.Command = ToolbarCommand::REDO;
    }
    if (tool_button("##fit", ToolIcon::FIT, false, true, "Fit the schematic in view (Home)")) {
        request.Command = ToolbarCommand::FIT;
    }

    // Simulation
    row.Place(side + ToolbarRow::ButtonSpacing() * 2.0f + AnalysisButtonWidth() + LiveButtonWidth(), gap);
    ImGui::BeginDisabled(is_editing_block);
    DrawRunControls();
    ImGui::SameLine(0.0f, ToolbarRow::ButtonSpacing());
    DrawLiveToggle();
    ImGui::EndDisabled();
    return request;
}

/**
 * @brief   Run starts the selected analysis; the button beside it names that analysis and opens its settings, where
 *          another one can be picked.
 */
void EditorToolbar::DrawRunControls() {
    const float side = GetToolButtonSide();
    const Analysis selected = m_RootSchematic.GetSimulationSettings().Selected;
    const RunAction action = m_Controls.GetRunAction();
    if (PrimaryButton("##run", {side, side})) {
        m_Controls.PressRun();
    }
    const float run_left = ImGui::GetItemRectMin().x;
    DrawToolIcon(ImGui::GetWindowDrawList(), action == RunAction::STOP ? ToolIcon::STOP : ToolIcon::RUN,
                 GetItemCenter(), GetIconSize(), GetIconColor(true));
    ImGui::SetItemTooltip("%s", DescribeRunAction(m_Controls, selected).c_str());
    ImGui::SameLine(0.0f, ToolbarRow::ButtonSpacing());
    DrawAnalysisButton(run_left);
}

/**
 * @brief   The analysis picker beside Run, whose popover holds the settings of the analysis and its own Run button.
 */
void EditorToolbar::DrawAnalysisButton(const float run_left) {
    const float side = GetToolButtonSide();
    const Analysis selected = m_RootSchematic.GetSimulationSettings().Selected;
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0.0f, 0.5f});
    const std::string label = std::format("{}##analysis", GetAnalysisName(selected));
    const bool open = ImGui::Button(label.c_str(), {AnalysisButtonWidth(), side});
    ImGui::PopStyleVar();
    ImGui::SetItemTooltip("Pick the analysis and set it up");
    DrawDropdownArrow({ImGui::GetItemRectMax().x - ImGui::GetFontSize() * 0.75f, GetItemCenter().y});
    if (open) {
        // Hangs below Run, moved left when the settings would not fit before the right edge of the editor
        const float settings_width = ImGui::GetFontSize() * SETTINGS_POPOVER_WIDTH;
        const float right_edge = ImGui::GetWindowPos().x + ImGui::GetWindowWidth();
        ImGui::SetNextWindowPos({std::min(run_left, right_edge - settings_width), ImGui::GetItemRectMax().y});
        ImGui::OpenPopup("analysis");
    }
    if (ImGui::BeginPopup("analysis")) {
        m_SettingsPanel.DrawAnalysisPicker();
        ImGui::Separator();
        m_SettingsPanel.DrawSettings();
        ImGui::Separator();
        // The popup has the focus, so F5 is checked here too; like the shortcut, it never stops a run
        const RunAction action = m_Controls.GetRunAction();
        if (PrimaryButton(GetRunButtonLabel(action)) ||
            (action != RunAction::STOP && ImGui::IsKeyPressed(ImGuiKey_F5, false))) {
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
