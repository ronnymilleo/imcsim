/**
 * @file    editor_toolbar.cpp
 * @brief   The icon toolbar above the schematic: tools, history, view, parts and the run controls.
 */

#include "windows/editor_toolbar.h"

#include "document/ready_blocks.h"
#include "drawing/schematic_canvas.h"
#include "drawing/theme.h"
#include "drawing/tool_icons.h"
#include "editing/element_factory.h"
#include "editing/shortcuts.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <span>
#include <string>
#include <utility>

namespace GUI {

namespace {

/**
 * @struct  PartGroup
 * @brief   Related parts that share one toolbar button, with a dropdown to pick among them.
 */
struct PartGroup {
    const char *Name;
    std::span<const Core::ComponentType> Types;
};

constexpr auto SOURCE_PARTS = std::to_array(
    {Core::ComponentType::VOLTAGE_SOURCE, Core::ComponentType::CURRENT_SOURCE, Core::ComponentType::CLOCK});
constexpr auto DIODE_PARTS =
    std::to_array({Core::ComponentType::DIODE, Core::ComponentType::ZENER_DIODE, Core::ComponentType::LED});
constexpr auto TRANSISTOR_PARTS = std::to_array(
    {Core::ComponentType::NPN, Core::ComponentType::PNP, Core::ComponentType::NMOS, Core::ComponentType::PMOS});
constexpr auto CONTROLLED_PARTS = std::to_array(
    {Core::ComponentType::VCVS, Core::ComponentType::VCCS, Core::ComponentType::CCCS, Core::ComponentType::CCVS,
     Core::ComponentType::BEHAVIORAL_VOLTAGE, Core::ComponentType::BEHAVIORAL_CURRENT});
constexpr auto ADJUSTABLE_PARTS = std::to_array({Core::ComponentType::POTENTIOMETER, Core::ComponentType::TRANSFORMER});
constexpr auto SWITCH_PARTS =
    std::to_array({Core::ComponentType::SWITCH, Core::ComponentType::PUSH_BUTTON, Core::ComponentType::V_SWITCH,
                   Core::ComponentType::RELAY, Core::ComponentType::FUSE});
constexpr auto GATE_PARTS = std::to_array(
    {Core::ComponentType::AND, Core::ComponentType::OR, Core::ComponentType::NOT, Core::ComponentType::NAND,
     Core::ComponentType::NOR, Core::ComponentType::XOR, Core::ComponentType::XNOR, Core::ComponentType::BUFFER});
constexpr auto FLIP_FLOP_PARTS = std::to_array({Core::ComponentType::D_FLIP_FLOP, Core::ComponentType::JK_FLIP_FLOP,
                                                Core::ComponentType::T_FLIP_FLOP, Core::ComponentType::SR_FLIP_FLOP});
constexpr auto PART_GROUPS = std::to_array<PartGroup>({
    {"Sources", SOURCE_PARTS},
    {"Diodes", DIODE_PARTS},
    {"Transistors", TRANSISTOR_PARTS},
    {"Controlled sources", CONTROLLED_PARTS},
    {"Adjustable parts", ADJUSTABLE_PARTS},
    {"Switches and fuses", SWITCH_PARTS},
    {"Logic gates", GATE_PARTS},
    {"Flip-flops", FLIP_FLOP_PARTS},
});

// Parts with a button of their own, in toolbar groups
constexpr auto PASSIVE_PARTS =
    std::to_array({Core::ComponentType::RESISTOR, Core::ComponentType::CAPACITOR, Core::ComponentType::INDUCTOR});
constexpr auto SUPPLY_PARTS = std::to_array(
    {Core::ComponentType::GROUND, Core::ComponentType::VCC, Core::ComponentType::NET_LABEL, Core::ComponentType::PIN,
     Core::ComponentType::LOGIC_INPUT, Core::ComponentType::LOGIC_OUTPUT, Core::ComponentType::SEVEN_SEGMENT});

/// The dropdown arrow beside a group button, in frame heights.
constexpr float DROPDOWN_BUTTON_SCALE = 0.6f;
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
    for (const PartGroup &group : PART_GROUPS) {
        m_PartGroupChoices.push_back(group.Types.front());
    }
    for (const ReadyBlock &block : GetReadyBlocks()) {
        auto group = MakeReadyBlock(block);
        m_ReadyBlockIcons.push_back(group && !group->Elements.empty() ? std::move(group->Elements.front()) : nullptr);
    }
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

    // Parts
    if (std::optional<EditorMode> placing =
            DrawParts(mode, [&row](const float width, const float part_gap) { row.Place(width, part_gap); })) {
        request.Mode = std::move(placing);
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
 * @brief   The part buttons: the passive parts and supplies with a button each, the groups with a list, the ready logic
 *          blocks and the op-amp.
 * @details place lays out the next button, with its width and the gap before it. Returns the mode that places the part
 *          or block clicked this frame, or none.
 */
std::optional<EditorMode> EditorToolbar::DrawParts(const EditorMode &mode,
                                                   const std::function<void(float, float)> &place) {
    const float gap = ToolbarRow::GroupGap();
    const float side = GetToolButtonSide();
    std::optional<Core::ComponentType> picked;
    for (const auto parts :
         {std::span<const Core::ComponentType>(PASSIVE_PARTS), std::span<const Core::ComponentType>(SUPPLY_PARTS)}) {
        float part_gap = gap;
        for (const Core::ComponentType type : parts) {
            place(side, std::exchange(part_gap, 0.0f));
            if (DrawPartButton(type, mode)) {
                picked = type;
            }
        }
    }
    for (std::size_t index = 0; index < PART_GROUPS.size(); ++index) {
        place(side + ImGui::GetFrameHeight() * DROPDOWN_BUTTON_SCALE, index == 0 ? gap : 0.0f);
        if (const std::optional<Core::ComponentType> type = DrawPartGroup(index, mode)) {
            picked = type;
        }
    }
    place(side + ImGui::GetFrameHeight() * DROPDOWN_BUTTON_SCALE, 0.0f);
    std::optional<EditorMode> block = DrawReadyBlockGroup();
    place(side, 0.0f);
    if (DrawPartButton(Core::ComponentType::OP_AMP, mode)) {
        picked = Core::ComponentType::OP_AMP;
    }
    if (picked) {
        return PlacingMode{.Type = *picked};
    }
    return block;
}

/**
 * @brief   The button of one part, shown as the tool in use while that part is being placed.
 * @details Returns whether it was clicked.
 */
bool EditorToolbar::DrawPartButton(const Core::ComponentType type, const EditorMode &mode) {
    ImGui::PushID(static_cast<int>(type));
    const bool clicked = ToolButton("##part", GetPlacingType(mode) == type);
    ImGui::PopID();
    DrawPartIcon(ImGui::GetWindowDrawList(), type, GetItemCenter(), GetIconSize(), m_Preferences.Symbols);
    ImGui::SetItemTooltip("%s", GetPartName(type));
    return clicked;
}

/**
 * @brief   The button places the part last picked in the group; the arrow beside it, or a right click, lists the
 *          others.
 * @details Returns the part to place, or none.
 */
std::optional<Core::ComponentType> EditorToolbar::DrawPartGroup(const std::size_t index, const EditorMode &mode) {
    const PartGroup &group = PART_GROUPS[index];
    Core::ComponentType &choice = m_PartGroupChoices[index];
    std::optional<Core::ComponentType> picked;
    ImGui::PushID(group.Name);
    if (DrawPartButton(choice, mode)) {
        picked = choice;
    }
    const bool open_list = ImGui::IsItemClicked(ImGuiMouseButton_Right);

    ImGui::SameLine(0.0f, 0.0f);
    const float side = GetToolButtonSide();
    const bool open_arrow = ImGui::Button("##more", {ImGui::GetFrameHeight() * DROPDOWN_BUTTON_SCALE, side});
    ImGui::SetItemTooltip("More %s", group.Name);
    DrawDropdownArrow(GetItemCenter());
    if (open_list || open_arrow) {
        ImGui::SetNextWindowPos({ImGui::GetItemRectMin().x - side, ImGui::GetItemRectMax().y});
        ImGui::OpenPopup("parts");
    }

    if (ImGui::BeginPopup("parts")) {
        float width = 0.0f;
        for (const Core::ComponentType type : group.Types) {
            width = std::max(width, ImGui::CalcTextSize(GetPartName(type)).x);
        }
        width += side + ImGui::GetStyle().ItemSpacing.x;
        for (const Core::ComponentType type : group.Types) {
            if (SelectablePartRow(type, GetPlacingType(mode) == type, width, m_Preferences.Symbols)) {
                choice = type;
                picked = type;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return picked;
}

/**
 * @brief   The button places the ready logic block last picked; the arrow beside it, or a right click, lists them all,
 *          each telling what it does when hovered.
 * @details Returns the paste of the block to place, which brings its definition along, or none.
 */
std::optional<EditorMode> EditorToolbar::DrawReadyBlockGroup() {
    const std::span<const ReadyBlock> blocks = GetReadyBlocks();
    std::optional<std::size_t> picked;
    ImGui::PushID("ready blocks");
    const float side = GetToolButtonSide();
    if (ToolButton("##block", false)) {
        picked = m_ReadyBlockChoice;
    }
    DrawReadyBlockIcon(m_ReadyBlockChoice, GetItemCenter());
    ImGui::SetItemTooltip("%.*s", static_cast<int>(blocks[m_ReadyBlockChoice].Title.size()),
                          blocks[m_ReadyBlockChoice].Title.data());
    const bool open_list = ImGui::IsItemClicked(ImGuiMouseButton_Right);

    ImGui::SameLine(0.0f, 0.0f);
    const bool open_arrow = ImGui::Button("##more", {ImGui::GetFrameHeight() * DROPDOWN_BUTTON_SCALE, side});
    ImGui::SetItemTooltip("More logic blocks");
    DrawDropdownArrow(GetItemCenter());
    if (open_list || open_arrow) {
        ImGui::SetNextWindowPos({ImGui::GetItemRectMin().x - side, ImGui::GetItemRectMax().y});
        ImGui::OpenPopup("blocks");
    }
    if (ImGui::BeginPopup("blocks")) {
        float width = 0.0f;
        for (const ReadyBlock &block : blocks) {
            width = std::max(width, ImGui::CalcTextSize(block.Title.data(), block.Title.data() + block.Title.size()).x);
        }
        width += side + ImGui::GetStyle().ItemSpacing.x;
        for (std::size_t index = 0; index < blocks.size(); ++index) {
            const ImVec2 start = ImGui::GetCursorScreenPos();
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable("##block", index == m_ReadyBlockChoice, ImGuiSelectableFlags_None, {width, side})) {
                picked = index;
            }
            ImGui::PopID();
            ImGui::SetItemTooltip("%.*s", static_cast<int>(blocks[index].Description.size()),
                                  blocks[index].Description.data());
            DrawReadyBlockIcon(index, start + ImVec2(side, side) / 2.0f);
            ImGui::GetWindowDrawList()->AddText(
                start + ImVec2(side + ImGui::GetStyle().ItemSpacing.x, (side - ImGui::GetTextLineHeight()) / 2.0f),
                GetIconColor(true), blocks[index].Title.data(),
                blocks[index].Title.data() + blocks[index].Title.size());
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    if (!picked) {
        return std::nullopt;
    }
    m_ReadyBlockChoice = *picked;
    auto group = MakeReadyBlock(blocks[*picked]);
    if (!group) {
        return std::nullopt;
    }
    return PastingMode{.Group = std::move(*group)};
}

/**
 * @brief   Draws a ready block as its icon: its box with the leads of its pins, scaled to a button.
 */
void EditorToolbar::DrawReadyBlockIcon(const std::size_t index, const ImVec2 center) const {
    if (index >= m_ReadyBlockIcons.size() || !m_ReadyBlockIcons[index]) {
        return;
    }
    DrawListCanvas canvas(ImGui::GetWindowDrawList());
    m_ReadyBlockIcons[index]->DrawIcon(canvas, center, GetIconSize(), GetIconColor(true), m_Preferences.Symbols);
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
