/**
 * @file    editor_window.cpp
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#include "editor_window.h"

#include "circuit_snapshot.h"
#include "components/net_label.h"
#include "current_flow.h"
#include "element_factory.h"
#include "group_editing.h"
#include "heat_glow.h"
#include "imgui_internal.h"
#include "led_glow.h"
#include "misc/cpp/imgui_stdlib.h"
#include "node_colors.h"
#include "probing.h"
#include "schematic_drawing.h"
#include "shortcuts.h"
#include "spice_value.h"
#include "theme.h"
#include "wire_editing.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <numbers>
#include <ranges>
#include <span>
#include <string>
#include <utility>

namespace GUI {

namespace {

constexpr float MIN_CANVAS_SIZE = 50.0f;
// Below this spacing the dots are thinned out, otherwise far zoom-out draws hundreds of thousands of them
constexpr float MIN_DOT_SPACING = 8.0f;
constexpr float WIRE_CURSOR_HALF_SIZE = 4.0f;
// How close, in pixels, the cursor must be to a wire to select it
constexpr float WIRE_PICK_DISTANCE = 5.0f;
// Junction dot radius relative to the zoom, with a minimum so it stays visible when zoomed out
constexpr float JUNCTION_RADIUS_SCALE = 0.15f;
constexpr float MIN_JUNCTION_RADIUS = 2.5f;
// Voltage labels sit just above and right of their point, clear of the line
constexpr ImVec2 VOLTAGE_LABEL_OFFSET = {4.0f, -16.0f};
// The time slider of a transient does not shrink below this, in pixels, so it stays usable in a narrow window
constexpr float MIN_TIME_SLIDER_WIDTH = 80.0f;
// Heat legend, in font sizes, at the bottom left of the canvas
constexpr float LEGEND_WIDTH = 10.0f;
constexpr float LEGEND_HEIGHT = 0.6f;
// LED glows, in grid units: a dim LED glows over its symbol (4 units long) and a fully lit one beyond it. Additive
// light can be stronger than a glow blended over a light background, which would hide the symbol
constexpr float GLOW_MIN_RADIUS = 2.0f;
constexpr float GLOW_MAX_RADIUS = 3.5f;
// Hot resistors glow smaller than LEDs, around their symbol, in a warm orange
constexpr float HEAT_GLOW_MIN_RADIUS = 1.5f;
constexpr float HEAT_GLOW_MAX_RADIUS = 2.5f;
constexpr ImVec4 HEAT_LIGHT = {1.0f, 0.45f, 0.1f, 1.0f};
constexpr float ADDITIVE_GLOW_INTENSITY = 0.9f;
constexpr float BLENDED_GLOW_INTENSITY = 0.5f;
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
    {Core::ComponentType::VCVS, Core::ComponentType::VCCS, Core::ComponentType::CCCS, Core::ComponentType::CCVS});
constexpr auto ADJUSTABLE_PARTS = std::to_array({Core::ComponentType::POTENTIOMETER, Core::ComponentType::TRANSFORMER});
constexpr auto SWITCH_PARTS = std::to_array({Core::ComponentType::SWITCH, Core::ComponentType::PUSH_BUTTON,
                                             Core::ComponentType::V_SWITCH, Core::ComponentType::RELAY});
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
    {"Switches", SWITCH_PARTS},
    {"Logic gates", GATE_PARTS},
    {"Flip-flops", FLIP_FLOP_PARTS},
});

// Parts with a button of their own, in toolbar groups
constexpr auto PASSIVE_PARTS =
    std::to_array({Core::ComponentType::RESISTOR, Core::ComponentType::CAPACITOR, Core::ComponentType::INDUCTOR});
constexpr auto SUPPLY_PARTS = std::to_array({Core::ComponentType::GROUND, Core::ComponentType::VCC,
                                             Core::ComponentType::NET_LABEL, Core::ComponentType::LOGIC_INPUT});

// Toolbar buttons are square, this many frame heights wide; icons fill this share of them
constexpr float TOOL_BUTTON_SCALE = 1.4f;
constexpr float ICON_SCALE = 0.7f;
// The dropdown arrow beside a group button, in frame heights
constexpr float DROPDOWN_BUTTON_SCALE = 0.6f;

/**
 * @enum    ToolIcon
 * @brief   Icons of the toolbar buttons that are not parts.
 */
enum class ToolIcon {
    RUN,
    STOP,
    SELECT,
    WIRE,
    PROBE,
    UNDO,
    REDO,
    FIT
};

/**
 * @struct  IconPlacement
 * @brief   Where and how large a toolbar icon is drawn, and the points of its unit square in screen space.
 */
struct IconPlacement {
    ImDrawList *DrawList;
    ImVec2 Center;
    float Size;
    ImU32 Color;
    // -1 mirrors the icon horizontally, for Redo
    float Flip;

    ImVec2 Point(const float x, const float y) const { return Center + ImVec2(x * Flip, y) * Size; }
};

void DrawSelectIcon(const IconPlacement &icon) {
    const std::array<ImVec2, 7> arrow = {icon.Point(-0.3f, -0.45f), icon.Point(-0.3f, 0.3f),  icon.Point(-0.12f, 0.13f),
                                         icon.Point(0.02f, 0.43f),  icon.Point(0.14f, 0.37f), icon.Point(0.0f, 0.08f),
                                         icon.Point(0.22f, 0.08f)};
    icon.DrawList->AddPolyline(arrow.data(), static_cast<int>(arrow.size()), icon.Color, ImDrawFlags_Closed,
                               LINE_THICKNESS);
}

void DrawWireIcon(const IconPlacement &icon) {
    const std::array<ImVec2, 4> wire = {icon.Point(-0.4f, 0.3f), icon.Point(0.0f, 0.3f), icon.Point(0.0f, -0.3f),
                                        icon.Point(0.4f, -0.3f)};
    icon.DrawList->AddPolyline(wire.data(), static_cast<int>(wire.size()), icon.Color, ImDrawFlags_None,
                               LINE_THICKNESS);
    icon.DrawList->AddCircleFilled(wire.front(), icon.Size * 0.08f, icon.Color);
    icon.DrawList->AddCircleFilled(wire.back(), icon.Size * 0.08f, icon.Color);
}

void DrawProbeIcon(const IconPlacement &icon) {
    icon.DrawList->AddLine(icon.Point(-0.45f, 0.45f), icon.Point(-0.12f, 0.12f), icon.Color, LINE_THICKNESS);
    icon.DrawList->AddLine(icon.Point(-0.12f, 0.12f), icon.Point(0.3f, -0.3f), icon.Color, icon.Size * 0.22f);
    icon.DrawList->AddLine(icon.Point(0.3f, -0.3f), icon.Point(0.45f, -0.45f), icon.Color, LINE_THICKNESS);
}

// Four corners of a frame
void DrawFitIcon(const IconPlacement &icon) {
    constexpr float CORNER = 0.4f;
    constexpr float ARM = 0.18f;
    for (const float x : {-CORNER, CORNER}) {
        for (const float y : {-CORNER, CORNER}) {
            const float inward_x = x < 0.0f ? ARM : -ARM;
            const float inward_y = y < 0.0f ? ARM : -ARM;
            const std::array<ImVec2, 3> corner = {icon.Point(x + inward_x, y), icon.Point(x, y),
                                                  icon.Point(x, y + inward_y)};
            icon.DrawList->AddPolyline(corner.data(), static_cast<int>(corner.size()), icon.Color, ImDrawFlags_None,
                                       LINE_THICKNESS);
        }
    }
}

// An arc over the top that turns back, with the arrowhead at its left end, mirrored for Redo
void DrawUndoIcon(const IconPlacement &icon) {
    const float radius = 0.28f;
    const ImVec2 middle = {0.05f, 0.05f};
    for (int step = 0; step <= 12; ++step) {
        const float angle = std::numbers::pi_v<float> * (1.0f + static_cast<float>(step) / 12.0f);
        icon.DrawList->PathLineTo(icon.Point(middle.x + radius * std::cos(angle), middle.y + radius * std::sin(angle)));
    }
    icon.DrawList->PathLineTo(icon.Point(middle.x + radius, middle.y + 0.25f));
    icon.DrawList->PathStroke(icon.Color, ImDrawFlags_None, LINE_THICKNESS);
    icon.DrawList->AddTriangleFilled(icon.Point(middle.x - radius, middle.y + 0.22f),
                                     icon.Point(middle.x - radius - 0.14f, middle.y - 0.02f),
                                     icon.Point(middle.x - radius + 0.14f, middle.y - 0.02f), icon.Color);
}

// Line drawings on a unit square centered on the icon, y pointing down
void DrawToolIcon(ImDrawList *draw_list, const ToolIcon icon, const ImVec2 center, const float size,
                  const ImU32 color) {
    const IconPlacement placement{.DrawList = draw_list,
                                  .Center = center,
                                  .Size = size,
                                  .Color = color,
                                  .Flip = icon == ToolIcon::REDO ? -1.0f : 1.0f};
    switch (icon) {
    case ToolIcon::RUN:
        draw_list->AddTriangleFilled(placement.Point(-0.25f, -0.35f), placement.Point(0.35f, 0.0f),
                                     placement.Point(-0.25f, 0.35f), color);
        break;
    case ToolIcon::STOP:
        draw_list->AddRectFilled(placement.Point(-0.28f, -0.28f), placement.Point(0.28f, 0.28f), color);
        break;
    case ToolIcon::SELECT:
        DrawSelectIcon(placement);
        break;
    case ToolIcon::WIRE:
        DrawWireIcon(placement);
        break;
    case ToolIcon::PROBE:
        DrawProbeIcon(placement);
        break;
    case ToolIcon::FIT:
        DrawFitIcon(placement);
        break;
    case ToolIcon::UNDO:
    case ToolIcon::REDO:
        DrawUndoIcon(placement);
        break;
    }
}

// Square toolbar button; the active one uses the primary colors, as the tool in use
bool ToolButton(const char *id, const bool active) {
    const float side = ImGui::GetFrameHeight() * TOOL_BUTTON_SCALE;
    return active ? PrimaryButton(id, {side, side}) : ImGui::Button(id, {side, side});
}

// "Transient running", with the share done when the run reports one
std::string FormatRunProgress(const char *analysis_name, const bool is_stopping, const double progress) {
    if (is_stopping) {
        return std::format("Stopping {}", analysis_name);
    }
    if (progress > 0.0) {
        return std::format("{} running, {:.0f}%", analysis_name, progress * 100.0);
    }
    return std::format("{} running", analysis_name);
}

// The button that names the selected analysis, with room for its dropdown arrow
float AnalysisButtonWidth() {
    float widest = 0.0f;
    for (const Analysis analysis :
         {Analysis::OPERATING_POINT, Analysis::TRANSIENT, Analysis::AC_SWEEP, Analysis::DC_SWEEP, Analysis::NOISE,
          Analysis::TRANSFER_FUNCTION, Analysis::TRUTH_TABLE}) {
        widest = std::max(widest, ImGui::CalcTextSize(GetAnalysisName(analysis)).x);
    }
    return widest + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFontSize();
}

// The character of a key that starts a value, from the main row or the keypad: a digit, a point or a minus
std::optional<char> FindTypedValueStart() {
    for (int digit = 0; digit <= 9; ++digit) {
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + digit), false) ||
            ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_Keypad0 + digit), false)) {
            return static_cast<char>('0' + digit);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Period, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false)) {
        return '.';
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) {
        return '-';
    }
    return std::nullopt;
}

// Middle and icon size of the last item, and the color its icon takes
ImVec2 ItemCenter() {
    return (ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) / 2.0f;
}

float IconSize() {
    return ImGui::GetFrameHeight() * TOOL_BUTTON_SCALE * ICON_SCALE;
}

ImU32 IconColor(const bool enabled) {
    return ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
}

void DrawPartIcon(ImDrawList *draw_list, const Core::ComponentType type, const ImVec2 center, const float size,
                  const SymbolStyle style) {
    DrawListCanvas canvas(draw_list);
    CreateElement(type, {0, 0}, Rotation::R0)->DrawIcon(canvas, center, size, IconColor(true), style);
}

constexpr const char *PART_PICKER_POPUP = "Find a part";
constexpr const char *PART_POPOVER = "Part properties";
constexpr const char *CONTEXT_MENU = "Schematic actions";
// The properties popover opens this many grid units beside the part, and is about this many font sizes wide
constexpr float POPOVER_MARGIN = 1.0f;
constexpr float POPOVER_WIDTH = 18.0f;
// The part picker shows this many matches at once, and its top sits this far down the editor, as a share of it
constexpr std::size_t PART_PICKER_ROWS = 5;
constexpr float PART_PICKER_TOP = 0.3f;

void DrawGrid(ImDrawList *draw_list, const ViewTransform &view, const ImVec2 origin, const ImVec2 size,
              const float zoom) {
    int step = 1;
    while (zoom * static_cast<float>(step) < MIN_DOT_SPACING) {
        step *= 2;
    }
    const float step_size = static_cast<float>(step);

    const ImVec2 world_min = view.ToWorld(origin);
    const ImVec2 world_max = view.ToWorld(origin + size);
    const ImVec2 dot_half_size = {1.0f, 1.0f};
    const int first_column = static_cast<int>(std::floor(world_min.x / step_size));
    const int last_column = static_cast<int>(std::floor(world_max.x / step_size));
    const int first_row = static_cast<int>(std::floor(world_min.y / step_size));
    const int last_row = static_cast<int>(std::floor(world_max.y / step_size));
    for (int column = first_column; column <= last_column; ++column) {
        for (int row = first_row; row <= last_row; ++row) {
            const ImVec2 dot =
                view.ToScreen({static_cast<float>(column) * step_size, static_cast<float>(row) * step_size});
            draw_list->AddRectFilled(dot - dot_half_size, dot + dot_half_size, GetPalette().GridDot);
        }
    }
}

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

    // Space between buttons, and the extra space between groups, in font sizes
    static float ButtonSpacing() { return ImGui::GetFontSize() * 0.25f; }
    static float GroupGap() { return ImGui::GetFontSize() * 0.6f; }

private:
    float m_RightEdge;
    bool m_First = true;
};

// Operating point value of a measurement, or no value before a simulation or for a current it lacks
std::optional<double> FindOperatingPointValue(const MeasurementTarget &target,
                                              const Core::OperatingPoint &operating_point) {
    if (target.Node) {
        const auto node = static_cast<std::size_t>(*target.Node);
        return node < operating_point.NodeVoltages.size() ? std::optional(operating_point.NodeVoltages[node])
                                                          : std::nullopt;
    }
    const auto current = std::ranges::find(operating_point.Currents, target.Current, &Core::ComponentCurrent::Name);
    return current != operating_point.Currents.end() ? std::optional(current->Current) : std::nullopt;
}

// "V(3) = 4.5V" once there is an operating point, or just "V(3)"; a node a net label named shows the name, V(VOUT)
std::string DescribeMeasurement(const MeasurementTarget &target, const Connectivity &connectivity,
                                const Core::OperatingPoint *operating_point) {
    const std::optional<double> value =
        operating_point != nullptr ? FindOperatingPointValue(target, *operating_point) : std::nullopt;
    const std::string label = GetMeasurementLabel(target, connectivity);
    if (!value) {
        return label;
    }
    return std::format("{} = {}{}", label, Core::FormatValue(*value), target.Node ? "V" : "A");
}

// Light of an LED color, kept bright so it reads on any background; an LED with custom parameters glows warm white
ImVec4 GetLEDLight(const Core::LEDColor color) {
    switch (color) {
    case Core::LEDColor::RED:
        return {1.0f, 0.15f, 0.08f, 1.0f};
    case Core::LEDColor::GREEN:
        return {0.2f, 1.0f, 0.25f, 1.0f};
    case Core::LEDColor::BLUE:
        return {0.2f, 0.4f, 1.0f, 1.0f};
    case Core::LEDColor::NONE:
        break;
    }
    return {1.0f, 0.9f, 0.7f, 1.0f};
}

// A selected wire keeps its heat color and gets this much wider halo behind it, in pixels, partly see-through
constexpr float SELECTION_HALO_EXTRA = 6.0f;
constexpr ImU32 SELECTION_HALO_ALPHA = 150;
constexpr ImU32 SELECTION_BOX_FILL_ALPHA = 40;

ImU32 WithAlpha(const ImU32 color, const ImU32 alpha) {
    return (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

// Power a part absorbs, looked up by the name of the part; a current name such as "Q1.C" belongs to part "Q1"
std::optional<ComponentPower> FindPower(const std::vector<ComponentPower> &powers, const std::string &name) {
    const std::string part = name.substr(0, name.find('.'));
    const auto found = std::ranges::find(powers, part, &ComponentPower::Name);
    return found != powers.end() ? std::optional(*found) : std::nullopt;
}

} // namespace

/**
 * @brief   Creates the editor for a schematic.
 * @param[in] schematic    Schematic to edit; it must outlive the window.
 * @param[in] controls     Analysis settings and runs, for the Run button; they must outlive the window.
 * @param[in] part_editor  Fields of the selected part, for the properties popover; it must outlive the window.
 * @param[in] glow_renderer  Draws the glow of lit LEDs; it must outlive the window.
 * @param[in] playback     Instant of the transient to show, which the time bar moves; it must outlive the window.
 */
EditorWindow::EditorWindow(Schematic &schematic, AnalysisControls &controls, PartEditor &part_editor,
                           GlowRenderer &glow_renderer, TransientPlayback &playback)
    : AppWindow("Schematic", false), m_Schematic(schematic), m_Controls(controls), m_PartEditor(part_editor),
      m_GlowRenderer(glow_renderer), m_Playback(playback), m_Preferences(part_editor),
      m_Files(schematic, m_Preferences,
              {.StopEditing = [this] { StopEditing(); },
               .FrameSchematic = [this] { m_View.RequestDefaultFrame(); },
               .PlaceGroup = [this](PastedGroup group) { StartPastingGroup(std::move(group)); }}) {
    for (const PartGroup &group : PART_GROUPS) {
        m_PartGroupChoices.push_back(group.Types.front());
    }
}

/**
 * @brief   Runs what does not depend on the editor being shown: the transient playback, file dialog results, quit,
 *          example and menu requests, the global shortcuts and the file popups.
 * @note    Call it every frame before Render() and outside any window: a docked editor behind another tab skips
 *          Draw(), and the popups opened here are drawn here too, so they show whichever window is in front.
 */
void EditorWindow::Update() {
    // The playhead of the Output window moves with the time too, so playing goes on while this window is hidden
    if (const auto &transient = m_Schematic.GetTransient(); transient && !transient->Times.empty()) {
        m_Playback.Advance(ImGui::GetIO().DeltaTime, transient->Times.back());
    }
    m_Files.Update();
    HandleCommandRequest();
    HandleGlobalShortcuts();
    m_Files.DrawPopups();
}

/**
 * @brief   Asks to quit, as when the user closes the main window. Call it between frames.
 * @note    Quitting is confirmed right away when there is nothing to lose; otherwise the next frame asks the user
 *          to discard the changes. Check IsQuitConfirmed() after drawing the frame.
 */
void EditorWindow::RequestQuit() {
    m_Files.RequestQuit();
}

/**
 * @brief   Tells whether the application may quit.
 * @return  True once a quit request was confirmed, by the user or because there were no unsaved changes.
 */
bool EditorWindow::IsQuitConfirmed() const {
    return m_Files.IsQuitConfirmed();
}

/**
 * @brief   Asks to open an example, as when the user picks one from the menu.
 * @param[in] example  One of GetExamples().
 * @note    Like quitting, the next frame opens it, after asking the user to discard unsaved changes if there are
 *          any. The example opens untitled, so saving it asks for a file.
 */
void EditorWindow::RequestExample(const Example &example) {
    m_Files.RequestExample(example);
}

/**
 * @brief   Asks to insert an example into the schematic, as when the user picks it from the insert menu.
 * @param[in] example  One of GetExamples().
 * @note    The next frame starts placing it like an import. Nothing is replaced, so nothing is asked.
 */
void EditorWindow::RequestInsertExample(const Example &example) {
    m_Files.RequestInsertExample(example);
}

/**
 * @brief   Asks to import a file into the schematic, as when the user drops it on the window.
 * @param[in] path  A schematic file.
 * @note    The next frame starts placing it, as File > Import does after its dialog.
 */
void EditorWindow::RequestImport(std::filesystem::path path) {
    m_Files.RequestImport(std::move(path));
}

/**
 * @brief   Draws the File menu items that act on the schematic, with their shortcuts.
 * @note    Call between BeginMenu() and EndMenu(); the command runs on the next frame of the editor.
 */
void EditorWindow::DrawFileMenuItems() {
    if (ImGui::MenuItem("New", IMCSIM_SHORTCUT "N")) {
        m_RequestedCommand = EditorCommand::NEW;
    }
    if (ImGui::MenuItem("Open...", IMCSIM_SHORTCUT "O")) {
        m_RequestedCommand = EditorCommand::OPEN;
    }
    if (ImGui::MenuItem("Save", IMCSIM_SHORTCUT "S")) {
        m_RequestedCommand = EditorCommand::SAVE;
    }
    if (ImGui::MenuItem("Save As...", IMCSIM_SHORTCUT "Shift+S")) {
        m_RequestedCommand = EditorCommand::SAVE_AS;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Import...")) {
        m_RequestedCommand = EditorCommand::IMPORT;
    }
    ImGui::SetItemTooltip("Add the circuit of a file to this schematic: it follows the cursor until a click places it");
    if (ImGui::MenuItem("Export Schematic...")) {
        m_RequestedCommand = EditorCommand::EXPORT_SCHEMATIC;
    }
    ImGui::SetItemTooltip("Save the schematic as an SVG image, to show next to its plots");
}

/**
 * @brief   Draws the Edit menu items, with their shortcuts, enabled only when they apply.
 * @note    Call between BeginMenu() and EndMenu(); the command runs on the next frame of the editor.
 */
void EditorWindow::DrawEditMenuItems() {
    if (ImGui::MenuItem("Undo", IMCSIM_SHORTCUT "Z", false, m_Schematic.CanUndo())) {
        m_RequestedCommand = EditorCommand::UNDO;
    }
    if (ImGui::MenuItem("Redo", IMCSIM_SHORTCUT "Y", false, m_Schematic.CanRedo())) {
        m_RequestedCommand = EditorCommand::REDO;
    }
    ImGui::Separator();
    const bool part_selected = !m_Schematic.GetSelectedElements().empty();
    if (ImGui::MenuItem("Rotate", "R", false, part_selected)) {
        m_RequestedCommand = EditorCommand::ROTATE;
    }
    if (ImGui::MenuItem("Mirror", "M", false, part_selected)) {
        m_RequestedCommand = EditorCommand::MIRROR;
    }
    if (ImGui::MenuItem("Flip", "Shift+M", false, part_selected)) {
        m_RequestedCommand = EditorCommand::FLIP;
    }
    if (ImGui::BeginMenu("Keep Wires in Place", part_selected)) {
        if (ImGui::MenuItem("Rotate", "Alt+R")) {
            m_RequestedCommand = EditorCommand::ROTATE_KEEP_WIRES;
        }
        if (ImGui::MenuItem("Mirror", "Alt+M")) {
            m_RequestedCommand = EditorCommand::MIRROR_KEEP_WIRES;
        }
        if (ImGui::MenuItem("Flip", "Alt+Shift+M")) {
            m_RequestedCommand = EditorCommand::FLIP_KEEP_WIRES;
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Cut", IMCSIM_SHORTCUT "X", false, m_Schematic.HasSelection())) {
        m_RequestedCommand = EditorCommand::CUT;
    }
    if (ImGui::MenuItem("Copy", IMCSIM_SHORTCUT "C", false, m_Schematic.HasSelection())) {
        m_RequestedCommand = EditorCommand::COPY;
    }
    const char *clipboard_text = ImGui::GetClipboardText();
    const bool can_paste = clipboard_text != nullptr && LooksLikeCopiedSchematic(clipboard_text);
    if (ImGui::MenuItem("Paste", IMCSIM_SHORTCUT "V", false, can_paste)) {
        m_RequestedCommand = EditorCommand::PASTE;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Select All", IMCSIM_SHORTCUT "A")) {
        m_RequestedCommand = EditorCommand::SELECT_ALL;
    }
    if (ImGui::MenuItem("Delete", "Del", false, m_Schematic.HasSelection())) {
        m_RequestedCommand = EditorCommand::DELETE_SELECTION;
    }
}

/**
 * @brief   Draws the View menu items that set how the schematic looks; they apply right away and are remembered.
 * @note    Call between BeginMenu() and EndMenu().
 */
void EditorWindow::DrawViewMenuItems() {
    if (ImGui::MenuItem("Fit Schematic", "Home")) {
        m_View.RequestFit();
    }
    ImGui::Separator();
    m_Preferences.DrawMenuItems();
}

/**
 * @brief   Keeps the View preferences (wire colors, symbol style, terminal numbers, standard values) in imgui.ini,
 *          next to the window layout, so they survive a restart.
 */
void EditorWindow::RegisterSettingsHandler() {
    m_Preferences.RegisterSettingsHandler();
}

// Window content only: AppWindow::Render() wraps it in Begin/End
void EditorWindow::Draw() {
    // Only while the editor has the focus, since text fields use Home
    if (ImGui::Shortcut(ImGuiKey_Home)) {
        m_View.RequestFit();
    }
    DrawPartPicker();
    DrawToolbar();
    UpdateDisplayedState();
    DrawTimeBar();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    // The status bar takes one line below the canvas
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.y -= ImGui::GetTextLineHeightWithSpacing();
    size.x = std::max(size.x, MIN_CANVAS_SIZE);
    size.y = std::max(size.y, MIN_CANVAS_SIZE);
    m_View.FrameIfRequested(m_Schematic, size, m_Preferences.GetSymbolStyle(), m_Preferences.ShowsTerminalNumbers());

    ImGui::InvisibleButton("canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    m_View.HandlePanAndZoom(origin, hovered, active);

    const ViewTransform view = m_View.GetTransform(origin);
    // Selection runs before drawing so a dragged element is drawn where the cursor is this frame
    HandleSelection(view, hovered);
    HandleContextMenu(view, hovered);
    HandleValueTyping(view);
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(origin, origin + size, true);
    draw_list->AddRectFilled(origin, origin + size, GetPalette().CanvasBackground);
    DrawGrid(draw_list, view, origin, size, m_View.GetZoom());

    DrawListCanvas canvas(draw_list);
    DrawWires(canvas, view);
    const auto &elements = m_Schematic.GetElements();
    for (std::size_t index = 0; index < elements.size(); ++index) {
        elements[index]->Draw(canvas, view, GetElementColor(index), m_Preferences.GetSymbolStyle());
        if (m_Schematic.IsElementSelected(index) && ArePartsHeatColored()) {
            elements[index]->DrawOutline(canvas, view, GetPalette().Selected);
        }
    }
    // Over the symbols, so a lit LED looks lit through its own lines
    DrawGlows(draw_list, view);
    if (m_Preferences.ShowsTerminalNumbers()) {
        DrawTerminalNumbers(canvas, view, m_Schematic, SCREEN_TERMINAL_RING_SCALE);
    }
    DrawNodeVoltages(draw_list, view);
    DrawMeasurementMarkers(canvas, view, m_Schematic);
    DrawSelectionBox(draw_list, view);

    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_W)) {
        StartDrawingWires();
    }
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_P)) {
        StartProbing();
    }
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_L) && !ImGui::GetIO().KeyCtrl) {
        StartPlacing(Core::ComponentType::NET_LABEL);
    }
    HandlePlacement(canvas, view, hovered);
    HandlePasting(canvas, view, hovered);
    HandleWireDrawing(draw_list, view, hovered);
    HandleProbing(draw_list, view, hovered);
    HandleClipboardShortcuts();
    DrawHoveredValue(view, hovered);
    DrawColorLegend(draw_list, origin, size);

    draw_list->PopClipRect();
    DrawStatusBar(view, hovered);
    DrawContextMenu();
    DrawPartPopover();
    m_PopupOpenLastFrame = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

// One row of icon buttons, whose groups wrap onto new lines when the window is narrow instead of being cut off
void EditorWindow::DrawToolbar() {
    ToolbarRow row;
    const float gap = ToolbarRow::GroupGap();
    const float side = ImGui::GetFrameHeight() * TOOL_BUTTON_SCALE;
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const auto tool_button = [&](const char *id, const ToolIcon icon, const bool active, const bool enabled,
                                 const char *tooltip, const float group_gap) {
        row.Place(side, group_gap);
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ToolButton(id, active);
        ImGui::EndDisabled();
        DrawToolIcon(draw_list, icon, ItemCenter(), IconSize(), IconColor(enabled));
        ImGui::SetItemTooltip("%s", tooltip);
        return clicked;
    };

    // Tools, history and view
    const bool selecting = IsSelecting();
    if (tool_button("##select", ToolIcon::SELECT, selecting, true, "Select (Esc)", 0.0f)) {
        StartSelecting();
    }
    if (tool_button("##wire", ToolIcon::WIRE, m_DrawingWires, true, "Wire (W)", 0.0f)) {
        StartDrawingWires();
    }
    if (tool_button("##probe", ToolIcon::PROBE, m_Probing, true, "Probe (P): pick what the plots measure", 0.0f)) {
        StartProbing();
    }
    if (tool_button("##undo", ToolIcon::UNDO, false, m_Schematic.CanUndo(), "Undo (" IMCSIM_SHORTCUT "Z)", 0.0f)) {
        Undo();
    }
    if (tool_button("##redo", ToolIcon::REDO, false, m_Schematic.CanRedo(), "Redo (" IMCSIM_SHORTCUT "Y)", 0.0f)) {
        Redo();
    }
    if (tool_button("##fit", ToolIcon::FIT, false, true, "Fit the schematic in view (Home)", 0.0f)) {
        m_View.RequestFit();
    }

    // Parts
    for (const auto parts :
         {std::span<const Core::ComponentType>(PASSIVE_PARTS), std::span<const Core::ComponentType>(SUPPLY_PARTS)}) {
        float part_gap = gap;
        for (const Core::ComponentType type : parts) {
            row.Place(side, std::exchange(part_gap, 0.0f));
            DrawPartButton(type);
        }
    }
    for (std::size_t index = 0; index < PART_GROUPS.size(); ++index) {
        row.Place(side + ImGui::GetFrameHeight() * DROPDOWN_BUTTON_SCALE, index == 0 ? gap : 0.0f);
        DrawPartGroup(index);
    }
    row.Place(side);
    DrawPartButton(Core::ComponentType::OP_AMP);

    // Simulation
    row.Place(side + ToolbarRow::ButtonSpacing() + AnalysisButtonWidth(), gap);
    DrawRunControls();
}

void EditorWindow::DrawPartButton(const Core::ComponentType type) {
    ImGui::PushID(static_cast<int>(type));
    if (ToolButton("##part", m_PlacingType == type)) {
        StartPlacing(type);
    }
    ImGui::PopID();
    DrawPartIcon(ImGui::GetWindowDrawList(), type, ItemCenter(), IconSize(), m_Preferences.GetSymbolStyle());
    ImGui::SetItemTooltip("%s", GetPartName(type));
}

// Run starts the selected analysis; the button beside it names that analysis and opens its settings, where another
// one can be picked
void EditorWindow::DrawRunControls() {
    const float side = ImGui::GetFrameHeight() * TOOL_BUTTON_SCALE;
    const Analysis selected = m_Schematic.GetSimulationSettings().Selected;
    const bool is_running = m_Controls.IsRunning();
    if (PrimaryButton("##run", {side, side})) {
        if (is_running) {
            m_Controls.StopRun();
        } else {
            m_Controls.RunSelected();
        }
    }
    const float run_left = ImGui::GetItemRectMin().x;
    DrawToolIcon(ImGui::GetWindowDrawList(), is_running ? ToolIcon::STOP : ToolIcon::RUN, ItemCenter(), IconSize(),
                 IconColor(true));
    if (const std::optional<Analysis> running = m_Controls.GetRunningAnalysis()) {
        ImGui::SetItemTooltip("Stop the %s run", GetAnalysisName(*running));
    } else {
        ImGui::SetItemTooltip("Run %s (F5)", GetAnalysisName(selected));
    }

    ImGui::SameLine(0.0f, ToolbarRow::ButtonSpacing());
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0.0f, 0.5f});
    const std::string label = std::format("{}##analysis", GetAnalysisName(selected));
    const bool open = ImGui::Button(label.c_str(), {AnalysisButtonWidth(), side});
    ImGui::PopStyleVar();
    ImGui::SetItemTooltip("Pick the analysis and set it up");
    const ImVec2 arrow_center = {ImGui::GetItemRectMax().x - ImGui::GetFontSize() * 0.75f, ItemCenter().y};
    const float arrow = ImGui::GetFontSize() * 0.25f;
    ImGui::GetWindowDrawList()->AddTriangleFilled(arrow_center + ImVec2(-arrow, -arrow * 0.5f),
                                                  arrow_center + ImVec2(arrow, -arrow * 0.5f),
                                                  arrow_center + ImVec2(0.0f, arrow * 0.5f), IconColor(true));
    if (open) {
        // Hangs below Run, moved left when the settings would not fit before the right edge of the editor
        const float settings_width = ImGui::GetFontSize() * 20.0f;
        const float right_edge = ImGui::GetWindowPos().x + ImGui::GetWindowWidth();
        ImGui::SetNextWindowPos({std::min(run_left, right_edge - settings_width), ImGui::GetItemRectMax().y});
        ImGui::OpenPopup("analysis");
    }
    if (ImGui::BeginPopup("analysis")) {
        m_Controls.DrawAnalysisPicker();
        ImGui::Separator();
        m_Controls.DrawSettings();
        ImGui::Separator();
        // The popup has the focus, so F5 is checked here too
        if (PrimaryButton(is_running ? "Stop" : "Run (F5)") || ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
            if (is_running) {
                m_Controls.StopRun();
            } else {
                m_Controls.RunSelected();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// The button places the part last picked in the group; the arrow beside it, or a right click, lists the others
void EditorWindow::DrawPartGroup(const std::size_t index) {
    const PartGroup &group = PART_GROUPS[index];
    Core::ComponentType &choice = m_PartGroupChoices[index];
    ImGui::PushID(group.Name);
    DrawPartButton(choice);
    const bool open_list = ImGui::IsItemClicked(ImGuiMouseButton_Right);

    ImGui::SameLine(0.0f, 0.0f);
    const float side = ImGui::GetFrameHeight() * TOOL_BUTTON_SCALE;
    const bool open_arrow = ImGui::Button("##more", {ImGui::GetFrameHeight() * DROPDOWN_BUTTON_SCALE, side});
    ImGui::SetItemTooltip("More %s", group.Name);
    const ImVec2 center = ItemCenter();
    const float arrow = ImGui::GetFontSize() * 0.25f;
    ImGui::GetWindowDrawList()->AddTriangleFilled(center + ImVec2(-arrow, -arrow * 0.5f),
                                                  center + ImVec2(arrow, -arrow * 0.5f),
                                                  center + ImVec2(0.0f, arrow * 0.5f), IconColor(true));
    if (open_list || open_arrow) {
        ImGui::SetNextWindowPos({ImGui::GetItemRectMin().x - side, ImGui::GetItemRectMax().y});
        ImGui::OpenPopup("parts");
    }

    if (ImGui::BeginPopup("parts")) {
        const float row_height = ImGui::GetFrameHeight() * TOOL_BUTTON_SCALE;
        float width = 0.0f;
        for (const Core::ComponentType type : group.Types) {
            width = std::max(width, ImGui::CalcTextSize(GetPartName(type)).x);
        }
        width += row_height + ImGui::GetStyle().ItemSpacing.x;
        for (const Core::ComponentType type : group.Types) {
            const ImVec2 start = ImGui::GetCursorScreenPos();
            ImGui::PushID(static_cast<int>(type));
            if (ImGui::Selectable("##part", m_PlacingType == type, ImGuiSelectableFlags_None, {width, row_height})) {
                choice = type;
                StartPlacing(type);
            }
            ImGui::PopID();
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            DrawPartIcon(draw_list, type, start + ImVec2(row_height, row_height) / 2.0f, row_height * ICON_SCALE,
                         m_Preferences.GetSymbolStyle());
            const float text_y = (row_height - ImGui::GetTextLineHeight()) / 2.0f;
            draw_list->AddText(start + ImVec2(row_height + ImGui::GetStyle().ItemSpacing.x, text_y), IconColor(true),
                               GetPartName(type));
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

// The last operating point, unless a transient was run after it: then the transient at the instant of the playback,
// on a scale that covers the whole run, so a value keeps its color while the time moves
void EditorWindow::UpdateDisplayedState() {
    m_Displayed.reset();
    const auto &operating_point = m_Schematic.GetOperatingPoint();
    const auto &transient = m_Schematic.GetTransient();
    const bool show_transient = transient && !transient->Times.empty() &&
                                (!operating_point || m_Controls.GetLastRun() != Analysis::OPERATING_POINT);
    if (!show_transient && !operating_point) {
        return;
    }
    const auto &elements = m_Schematic.GetElements();
    const Connectivity &connectivity = m_Schematic.GetConnectivity();
    DisplayedState state;
    if (show_transient) {
        if (m_ScaledTransientVersion != m_Schematic.GetTransientVersion()) {
            m_TransientScale = ScaleOf(*transient, elements, connectivity);
            m_ScaledTransientVersion = m_Schematic.GetTransientVersion();
        }
        state.Point = SampleTransient(*transient, m_Playback.Time);
        state.Scale = m_TransientScale;
        state.Time = m_Playback.Time;
    } else {
        state.Point = *operating_point;
        state.Scale = ScaleOf(state.Point, elements, connectivity);
    }
    state.Powers = ComputePowers(elements, connectivity, state.Point);
    m_Displayed = std::move(state);
}

// A row above the canvas with the playback of the transient on display: play, loop, a slider over the whole run and
// the instant. Moving the slider stops the playback, so it holds where the user put it
void EditorWindow::DrawTimeBar() {
    if (!m_Displayed || !m_Displayed->Time) {
        return;
    }
    const std::optional<Core::Transient> &transient = m_Schematic.GetTransient();
    if (!transient) {
        return;
    }
    const double stop_time = transient->Times.back();
    if (ImGui::Button(m_Playback.Playing ? "Pause" : "Play")) {
        if (!m_Playback.Playing && m_Playback.Time >= stop_time) {
            m_Playback.Time = 0.0;
        }
        m_Playback.Playing = !m_Playback.Playing;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &m_Playback.Loop);
    ImGui::SameLine();
    const std::string readout =
        std::format("t = {}s / {}s", Core::FormatValue(m_Playback.Time), Core::FormatValue(stop_time));
    const float readout_width = ImGui::CalcTextSize(readout.c_str()).x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - readout_width, MIN_TIME_SLIDER_WIDTH));
    constexpr double START = 0.0;
    ImGui::SliderScalar("##time", ImGuiDataType_Double, &m_Playback.Time, &START, &stop_time, "");
    if (ImGui::IsItemActive()) {
        m_Playback.Playing = false;
    }
    ImGui::SetItemTooltip("Instant of the transient shown on the schematic");
    ImGui::SameLine();
    ImGui::TextUnformatted(readout.c_str());
}

// Voltage, Current and Power need a result on display; without one, wires stay plain and the legend asks for it.
// Wires carry no power, so they stay plain by power. Over heat colors a selected wire is marked by a halo instead of
// a color of its own, which could pass for a value on the scale
void EditorWindow::DrawWires(SchematicCanvas &canvas, const ViewTransform &view) {
    const Connectivity &connectivity = m_Schematic.GetConnectivity();
    const auto &wires = m_Schematic.GetWires();
    const bool drawn_by_current = m_Preferences.GetWireColoring() == WireColoring::CURRENT && m_Displayed;
    DrawWireSelectionHalos(canvas, view);
    if (drawn_by_current) {
        DrawWireCurrents(canvas, view);
    }

    for (std::size_t index = 0; index < wires.size(); ++index) {
        const UIWire &wire = wires[index];
        const bool selected = m_Schematic.IsWireSelected(index) && !AreWiresHeatColored();
        if (drawn_by_current && !selected) {
            continue;
        }
        // Every wire end is a connection point, so it always has a node
        const int node = connectivity.GetNode(wire.GetStart()).value_or(0);
        wire.Draw(canvas, view, selected ? GetPalette().Selected : GetWireColor(node));
    }

    const float junction_radius = std::max(m_View.GetZoom() * JUNCTION_RADIUS_SCALE, MIN_JUNCTION_RADIUS);
    for (const GridPoint junction : connectivity.GetJunctions()) {
        const int node = connectivity.GetNode(junction).value_or(0);
        canvas.AddCircleFilled(view.ToScreen(ToVec2(junction)), junction_radius, GetWireColor(node));
    }

    if (m_Preferences.IsEffectOn(LiveEffect::CURRENT_FLOW) && m_Displayed) {
        DrawLiveCurrent(canvas, view, m_Schematic, m_Displayed->Point, m_Displayed->Scale, ImGui::GetTime());
    }

    if (m_Preferences.GetWireColoring() == WireColoring::NODES) {
        DrawNodeNumbers(canvas, view);
    }
}

// Over heat colors a selected wire has a halo behind it
void EditorWindow::DrawWireSelectionHalos(SchematicCanvas &canvas, const ViewTransform &view) {
    if (!AreWiresHeatColored()) {
        return;
    }
    for (const std::size_t selected : m_Schematic.GetSelectedWires()) {
        const UIWire &wire = m_Schematic.GetWires()[selected];
        canvas.AddLine(view.ToScreen(ToVec2(wire.GetStart())), view.ToScreen(ToVec2(wire.GetEnd())),
                       WithAlpha(GetPalette().Selected, SELECTION_HALO_ALPHA), LINE_THICKNESS + SELECTION_HALO_EXTRA);
    }
}

// The number of the node on the middle of each wire
void EditorWindow::DrawNodeNumbers(SchematicCanvas &canvas, const ViewTransform &view) {
    const Connectivity &connectivity = m_Schematic.GetConnectivity();
    for (const UIWire &wire : m_Schematic.GetWires()) {
        const int node = connectivity.GetNode(wire.GetStart()).value_or(0);
        const ImVec2 middle = view.ToScreen((ToVec2(wire.GetStart()) + ToVec2(wire.GetEnd())) / 2.0f);
        canvas.AddText(middle, GetPalette().CanvasText, std::format("{}", node).c_str());
    }
}

// The color of the wires of a node: its own by node, its heat color by voltage, otherwise the color of the theme
ImU32 EditorWindow::GetWireColor(const int node) const {
    if (m_Preferences.GetWireColoring() == WireColoring::NODES) {
        return GetNodeColor(node);
    }
    const bool by_voltage = m_Preferences.GetWireColoring() == WireColoring::VOLTAGE && m_Displayed;
    if (by_voltage && static_cast<std::size_t>(node) < m_Displayed->Point.NodeVoltages.size()) {
        const double voltage = m_Displayed->Point.NodeVoltages[static_cast<std::size_t>(node)];
        return GetHeatColor(VoltageLevel(voltage, m_Displayed->Scale));
    }
    return GetPalette().Wire;
}

// Each piece of wire takes the heat color of its own current, so the path of the current shows along the wires
void EditorWindow::DrawWireCurrents(SchematicCanvas &canvas, const ViewTransform &view) {
    if (!m_Displayed) {
        return;
    }
    for (const WireCurrent &piece :
         ComputeWireCurrents(m_Schematic.GetElements(), m_Schematic.GetWires(), m_Displayed->Point.Currents)) {
        UIWire(piece.Start, piece.End)
            .Draw(canvas, view, GetHeatColor(CurrentLevel(piece.Current, m_Displayed->Scale.LargestCurrent)));
    }
}

// Every lit LED glows in its color, bigger and stronger with its current, and every resistor that dissipates power
// glows orange with it. Additive light needs a dark background, so light themes blend the glows instead
void EditorWindow::DrawGlows(ImDrawList *draw_list, const ViewTransform &view) {
    const bool leds = m_Preferences.IsEffectOn(LiveEffect::LED_GLOW);
    const bool resistors = m_Preferences.IsEffectOn(LiveEffect::HEAT_GLOW);
    if (!m_Displayed || (!leds && !resistors)) {
        return;
    }
    const bool additive = !GetPalette().Light;
    const float intensity = additive ? ADDITIVE_GLOW_INTENSITY : BLENDED_GLOW_INTENSITY;
    std::vector<Glow> glows;
    const auto add_glow = [&](const GridPoint center, ImVec4 light, const float brightness, const float min_radius,
                              const float max_radius) {
        light.w = intensity * brightness;
        const float radius = min_radius + (max_radius - min_radius) * brightness;
        glows.push_back(
            {view.ToScreen(ToVec2(center)), radius * m_View.GetZoom(), ImGui::ColorConvertFloat4ToU32(light)});
    };
    if (leds) {
        for (const LEDGlow &led : ComputeLEDGlows(m_Schematic.GetElements(), m_Displayed->Point.Currents)) {
            add_glow(led.Center, GetLEDLight(led.Color), led.Brightness, GLOW_MIN_RADIUS, GLOW_MAX_RADIUS);
        }
    }
    if (resistors) {
        for (const HeatGlow &resistor :
             ComputeHeatGlows(m_Schematic.GetElements(), m_Displayed->Powers, m_Displayed->Scale)) {
            add_glow(resistor.Center, HEAT_LIGHT, resistor.Brightness, HEAT_GLOW_MIN_RADIUS, HEAT_GLOW_MAX_RADIUS);
        }
    }
    m_GlowRenderer.AddGlows(draw_list, glows, additive);
}

// Selection wins, unless the parts are heat colored, where an outline marks it instead; coloring by current heats each
// part by the largest current through its terminals, and coloring by power by the power it absorbs
ImU32 EditorWindow::GetElementColor(const std::size_t index) const {
    if (m_Schematic.IsElementSelected(index) && !ArePartsHeatColored()) {
        return GetPalette().Selected;
    }
    const bool by_current = m_Preferences.GetWireColoring() == WireColoring::CURRENT;
    if ((!by_current && m_Preferences.GetWireColoring() != WireColoring::POWER) || !m_Displayed) {
        return GetPalette().Element;
    }
    const UIElement &element = *m_Schematic.GetElements()[index];
    const Core::Component &component = element.GetComponent();
    // Ground and net labels carry no current or power of their own
    if (component.GetType() == Core::ComponentType::GROUND || component.GetType() == Core::ComponentType::NET_LABEL) {
        return GetPalette().Element;
    }
    if (!by_current) {
        const auto power = FindPower(m_Displayed->Powers, component.GetName());
        return power ? GetHeatColor(PowerLevel(power->Power, m_Displayed->Scale)) : GetPalette().Element;
    }
    double current = 0.0;
    for (std::size_t terminal = 0; terminal < element.GetTerminals().size(); ++terminal) {
        current = std::max(
            current, std::abs(GetTerminalCurrent(component, terminal, m_Displayed->Point.Currents).value_or(0.0)));
    }
    return GetHeatColor(CurrentLevel(current, m_Displayed->Scale.LargestCurrent));
}

// What the current mode does and which keys it takes, so the shortcuts can be learned while working, and the grid
// point under the cursor on the right
void EditorWindow::DrawStatusBar(const ViewTransform &view, const bool hovered) {
    std::string hint;
    if (m_Pasting) {
        hint = "Pasting: click to place, R rotate, M mirror, Shift+M flip, Esc or right click to cancel";
    } else if (m_PlacingType) {
        hint = std::format("Placing {}: click to place, R rotate, M mirror, Shift+M flip, Esc or right click to stop",
                           GetPartName(*m_PlacingType));
    } else if (m_DrawingWires) {
        hint = m_WireStart ? "Wire: click to bend, a terminal or the same point twice to end, F flip the bend, Esc "
                             "to stop"
                           : "Wire: click to start, Esc or right click to stop";
    } else if (m_Probing) {
        hint = "Probe: click a wire for its voltage or a part for its current, Esc or right click to stop";
    } else if (const UIElement *element = m_Schematic.GetSelectedElement()) {
        const Core::Component &component = element->GetComponent();
        const std::string name =
            component.GetName().empty() ? std::string(GetPartName(component.GetType())) : component.GetName();
        hint =
            std::format("{} selected: Enter or double click to edit, type a value, drag to move, R rotate, M mirror, "
                        "Alt keeps the wires, Del delete, right click for more",
                        name);
    } else if (m_Schematic.HasSelection()) {
        const std::size_t count = m_Schematic.GetSelectedElements().size() + m_Schematic.GetSelectedWires().size();
        hint = count == 1 ? "Wire selected: drag to slide it, double click for its whole node, Del delete, Esc deselect"
                          : std::format("{} items selected: drag to move, R rotate, M mirror, Alt keeps the wires, "
                                        "Shift+click to add or remove, Del delete, Esc deselect",
                                        count);
    } else {
        hint = "Click a part or wire to select it, drag to select an area, middle drag to pan, wheel to zoom, Space "
               "find a part, W wire, P probe";
    }
    // The right end holds the result of the last run, which stays put, and the grid position before it; the hint is
    // cut short before them
    const ImVec2 line_start = ImGui::GetCursorScreenPos();
    float right_edge = DrawRunStatus(line_start.x + ImGui::GetContentRegionAvail().x);
    if (hovered) {
        const GridPoint point = Snap(view.ToWorld(ImGui::GetIO().MousePos));
        const std::string position = std::format("x {}, y {}", point.X, point.Y);
        right_edge -= ImGui::CalcTextSize(position.c_str()).x;
        ImGui::SetCursorScreenPos({right_edge, line_start.y});
        ImGui::TextDisabled("%s", position.c_str());
        right_edge -= ImGui::GetFontSize() * 1.5f;
    }
    ImGui::SetCursorScreenPos(line_start);
    ImGui::PushClipRect(line_start, {right_edge, line_start.y + ImGui::GetTextLineHeightWithSpacing()}, true);
    ImGui::TextDisabled("%s", hint.c_str());
    ImGui::PopClipRect();
}

// "Transient done", in the warning color when something needs a look and in the error color after a failure; a click
// opens the report of the run, with the ngspice output. Returns where the space left for the hint ends
float EditorWindow::DrawRunStatus(float right_edge) {
    const std::optional<Analysis> last_run = m_Controls.GetLastRun();
    const RunOutcome outcome = last_run ? m_Controls.GetOutcome(*last_run) : RunOutcome::NONE;
    const std::string_view analysis_name = last_run ? GetAnalysisName(*last_run) : std::string_view();
    std::string status;
    ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    switch (outcome) {
    case RunOutcome::NONE:
        break;
    case RunOutcome::SUCCEEDED:
        status = std::format("{} done", analysis_name);
        break;
    case RunOutcome::SUCCEEDED_WITH_WARNINGS:
        status = std::format("{} done, with warnings", analysis_name);
        color = GetWarningTextColor();
        break;
    case RunOutcome::FAILED:
        status = std::format("{} failed", analysis_name);
        color = GetErrorTextColor();
        break;
    }
    // A run in progress takes the place of the report of the last one
    const std::optional<Analysis> running = m_Controls.GetRunningAnalysis();
    if (running) {
        status = FormatRunProgress(GetAnalysisName(*running), m_Controls.IsStopping(), m_Controls.GetRunProgress());
        color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    }
    const ImVec2 line_start = ImGui::GetCursorScreenPos();
    if (!status.empty()) {
        const ImVec2 status_size = ImGui::CalcTextSize(status.c_str());
        right_edge -= status_size.x;
        ImGui::SetCursorScreenPos({right_edge, line_start.y});
        if (running) {
            ImGui::TextDisabled("%s", status.c_str());
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            if (ImGui::Selectable(status.c_str(), false, ImGuiSelectableFlags_None, status_size)) {
                ImGui::OpenPopup("run_report");
            }
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("Show what the run reported, with the ngspice output");
        }
        right_edge -= ImGui::GetFontSize() * 1.5f;
    }
    if (ImGui::BeginPopup("run_report")) {
        if (last_run) {
            m_Controls.DrawRunReport(*last_run);
        }
        ImGui::EndPopup();
    }
    return right_edge;
}

// Voltage and Current color the wires with the heat scale, once there is a result to color them by
bool EditorWindow::AreWiresHeatColored() const {
    return m_Displayed && (m_Preferences.GetWireColoring() == WireColoring::VOLTAGE ||
                           m_Preferences.GetWireColoring() == WireColoring::CURRENT);
}

// Current and Power color the parts with the heat scale, once there is a result to color them by
bool EditorWindow::ArePartsHeatColored() const {
    return m_Displayed && (m_Preferences.GetWireColoring() == WireColoring::CURRENT ||
                           m_Preferences.GetWireColoring() == WireColoring::POWER);
}

// A gradient with the values at its ends, or a reminder to run an operating point or a transient
void EditorWindow::DrawColorLegend(ImDrawList *draw_list, const ImVec2 origin, const ImVec2 size) const {
    if (m_Preferences.GetWireColoring() != WireColoring::VOLTAGE &&
        m_Preferences.GetWireColoring() != WireColoring::CURRENT &&
        m_Preferences.GetWireColoring() != WireColoring::POWER) {
        return;
    }
    const float font_size = ImGui::GetFontSize();
    const ImVec2 corner = {origin.x + font_size, origin.y + size.y - font_size * 2.0f};
    if (!m_Displayed) {
        draw_list->AddText(corner, GetPalette().CanvasText,
                           "Run an operating point (.op) or a transient to color by voltage, current or power");
        return;
    }

    const HeatScale &scale = m_Displayed->Scale;
    std::string low_label;
    std::string high_label;
    if (m_Preferences.GetWireColoring() == WireColoring::VOLTAGE) {
        low_label = std::format("{}V", Core::FormatValue(scale.LowestVoltage));
        high_label = std::format("{}V", Core::FormatValue(scale.HighestVoltage));
    } else if (m_Preferences.GetWireColoring() == WireColoring::CURRENT) {
        low_label = std::format("<{}A", Core::FormatValue(scale.LargestCurrent * std::pow(10.0, -CURRENT_DECADES)));
        high_label = std::format("{}A", Core::FormatValue(scale.LargestCurrent));
    } else {
        low_label = std::format("<{}W", Core::FormatValue(scale.LargestPower * std::pow(10.0, -POWER_DECADES)));
        high_label = std::format("{}W", Core::FormatValue(scale.LargestPower));
    }
    const ImVec2 bar_size = {font_size * LEGEND_WIDTH, font_size * LEGEND_HEIGHT};
    constexpr int STEPS = 32;
    for (int step = 0; step < STEPS; ++step) {
        const float left = corner.x + bar_size.x * static_cast<float>(step) / STEPS;
        const float right = corner.x + bar_size.x * static_cast<float>(step + 1) / STEPS;
        draw_list->AddRectFilled({left, corner.y}, {right, corner.y + bar_size.y},
                                 GetHeatColor((static_cast<float>(step) + 0.5f) / STEPS));
    }
    const float text_y = corner.y + bar_size.y + 2.0f;
    draw_list->AddText({corner.x, text_y}, GetPalette().CanvasText, low_label.c_str());
    const float high_width = ImGui::CalcTextSize(high_label.c_str()).x;
    draw_list->AddText({corner.x + bar_size.x - high_width, text_y}, GetPalette().CanvasText, high_label.c_str());
}

// One label per node, on its first wire or, for terminals joined without wires, on a terminal. Ground is
// always 0 V, so it is left out
void EditorWindow::DrawNodeVoltages(ImDrawList *draw_list, const ViewTransform &view) {
    if (!m_Displayed) {
        return;
    }
    const Connectivity &connectivity = m_Schematic.GetConnectivity();
    const std::vector<double> &voltages = m_Displayed->Point.NodeVoltages;
    // The theme color of the labels is yellow, which gets lost over the yellow of the heat scale
    const ImU32 label_color = AreWiresHeatColored() ? GetPalette().CanvasText : GetPalette().VoltageLabel;
    std::vector<bool> labeled(voltages.size(), false);
    const auto draw_label = [&](const GridPoint point, const ImVec2 world_pos) {
        const std::optional<int> node = connectivity.GetNode(point);
        if (!node || *node <= 0 || static_cast<std::size_t>(*node) >= voltages.size()) {
            return;
        }
        const auto index = static_cast<std::size_t>(*node);
        if (labeled[index]) {
            return;
        }
        labeled[index] = true;
        const std::string text = std::format("{}V", Core::FormatValue(voltages[index]));
        draw_list->AddText(view.ToScreen(world_pos) + VOLTAGE_LABEL_OFFSET, label_color, text.c_str());
    };

    for (const UIWire &wire : m_Schematic.GetWires()) {
        draw_label(wire.GetStart(), (ToVec2(wire.GetStart()) + ToVec2(wire.GetEnd())) / 2.0f);
    }
    for (const auto &element : m_Schematic.GetElements()) {
        for (const GridPoint terminal : element->GetTerminals()) {
            draw_label(terminal, ToVec2(terminal));
        }
    }
}

// In selection mode, hovering a wire or a part shows its value on display, once there is one, and the power of a part
void EditorWindow::DrawHoveredValue(const ViewTransform &view, const bool hovered) {
    if (!hovered || !m_Displayed || !IsSelecting() || m_Drag) {
        return;
    }
    const std::optional<MeasurementTarget> target =
        FindMeasurementTarget(m_Schematic.GetElements(), m_Schematic.GetWires(), m_Schematic.GetConnectivity(),
                              view.ToWorld(ImGui::GetIO().MousePos), WIRE_PICK_DISTANCE / m_View.GetZoom());
    if (!target || !FindOperatingPointValue(*target, m_Displayed->Point)) {
        return;
    }
    std::string text = DescribeMeasurement(*target, m_Schematic.GetConnectivity(), &m_Displayed->Point);
    if (!target->Node) {
        if (const std::optional<ComponentPower> power = FindPower(m_Displayed->Powers, target->Current)) {
            text += std::format("\nP({}) = {}W", power->Name, Core::FormatValue(power->Power));
        }
    }
    ImGui::SetTooltip("%s", text.c_str());
}

// Leaves every other mode, so clicks select and drag
void EditorWindow::StartSelecting() {
    m_Pasting.reset();
    m_PlacingType.reset();
    m_DrawingWires = false;
    m_WireStart.reset();
    m_Probing = false;
}

void EditorWindow::StartPlacing(const Core::ComponentType type) {
    ClearSelection();
    m_Pasting.reset();
    m_Probing = false;
    m_DrawingWires = false;
    m_WireStart.reset();
    m_PlacingType = type;
    m_PlacingRotation = Rotation::R0;
    m_PlacingMirrored = false;
}

void EditorWindow::StartDrawingWires() {
    ClearSelection();
    m_Pasting.reset();
    m_PlacingType.reset();
    m_Probing = false;
    m_DrawingWires = true;
}

void EditorWindow::StartProbing() {
    ClearSelection();
    m_Pasting.reset();
    m_PlacingType.reset();
    m_DrawingWires = false;
    m_WireStart.reset();
    m_Probing = true;
}

// Before the schematic is cleared or replaced: a drag or paste in progress would point at parts that are gone
void EditorWindow::StopEditing() {
    EndDrag();
    m_Pasting.reset();
    m_PlacingType.reset();
    m_DrawingWires = false;
    m_WireStart.reset();
}

/**
 * @brief   Opens, on Space, a list of every part filtered by what is typed; Up and Down move through it and Enter or
 *          a click starts placing the highlighted part.
 */
void EditorWindow::DrawPartPicker() {
    const bool space_pressed =
        ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Space, false);
    if (std::exchange(m_PartPickerRequested, false) || space_pressed) {
        m_PartQuery.clear();
        m_PartPickerIndex = 0;
        ImGui::OpenPopup(PART_PICKER_POPUP);
    }
    // Hangs from its top edge, a third of the way down the editor, so it stays put as the list filters
    const ImVec2 anchor = {ImGui::GetWindowPos().x + ImGui::GetWindowWidth() / 2.0f,
                           ImGui::GetWindowPos().y + ImGui::GetWindowHeight() * PART_PICKER_TOP};
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Appearing, {0.5f, 0.0f});
    // Without navigation the arrow keys move the highlight below instead of taking focus from the query
    if (!ImGui::BeginPopup(PART_PICKER_POPUP, ImGuiWindowFlags_NoNav)) {
        return;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    const float row_height = ImGui::GetFrameHeight() * TOOL_BUTTON_SCALE;
    const float width = ImGui::GetFontSize() * 16.0f;
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputTextWithHint("##query", "Find a part", &m_PartQuery)) {
        m_PartPickerIndex = 0;
    }
    const std::vector<Core::ComponentType> found = FindParts(m_PartQuery);
    // The list follows the highlight only when the keys move it, so the wheel still scrolls it
    bool moved = false;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && m_PartPickerIndex + 1 < found.size()) {
        ++m_PartPickerIndex;
        moved = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && m_PartPickerIndex > 0) {
        --m_PartPickerIndex;
        moved = true;
    }
    m_PartPickerIndex = std::min(m_PartPickerIndex, found.empty() ? 0 : found.size() - 1);

    std::optional<Core::ComponentType> picked;
    if (!found.empty() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
        picked = found[m_PartPickerIndex];
    }
    if (found.empty()) {
        ImGui::TextDisabled("No part matches");
    }
    // A short list that scrolls, so the picker stays small
    const float row_spacing = ImGui::GetStyle().ItemSpacing.y;
    const std::size_t visible_rows = std::min(found.size(), PART_PICKER_ROWS);
    const float list_height = static_cast<float>(visible_rows) * (row_height + row_spacing) - row_spacing;
    if (visible_rows > 0) {
        ImGui::BeginChild("##matches", {width + ImGui::GetStyle().ScrollbarSize, list_height}, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoNav);
    }
    for (std::size_t index = 0; index < found.size(); ++index) {
        const Core::ComponentType type = found[index];
        const ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::PushID(static_cast<int>(type));
        if (ImGui::Selectable("##part", index == m_PartPickerIndex, ImGuiSelectableFlags_None, {width, row_height})) {
            picked = type;
        }
        ImGui::PopID();
        if (moved && index == m_PartPickerIndex) {
            ImGui::SetScrollHereY();
        }
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        DrawPartIcon(draw_list, type, start + ImVec2(row_height, row_height) / 2.0f, row_height * ICON_SCALE,
                     m_Preferences.GetSymbolStyle());
        const float text_y = (row_height - ImGui::GetTextLineHeight()) / 2.0f;
        draw_list->AddText(start + ImVec2(row_height + ImGui::GetStyle().ItemSpacing.x, text_y), IconColor(true),
                           GetPartName(type));
    }
    if (visible_rows > 0) {
        ImGui::EndChild();
    }

    if (picked) {
        StartPlacing(*picked);
        ImGui::CloseCurrentPopup();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        // Esc only closes the list; the lock keeps the editor below from also leaving its mode this frame
        ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetCurrentWindow()->ID, ImGuiInputFlags_LockThisFrame);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

/**
 * @brief   Handles placement mode: R rotates, M mirrors left to right and Shift+M top to bottom, Esc or right click
 *          cancels, left click places and keeps the mode active so several components can be placed in a row.
 */
void EditorWindow::HandlePlacement(SchematicCanvas &canvas, const ViewTransform &view, const bool hovered) {
    if (!m_PlacingType) {
        return;
    }

    // Keyboard shortcuts only apply while the editor has focus, so typing elsewhere does not trigger them
    if (ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            m_PlacingRotation = NextRotation(m_PlacingRotation);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_M)) {
            m_PlacingMirrored = !m_PlacingMirrored;
            if (ImGui::GetIO().KeyShift) {
                m_PlacingRotation = NextRotation(NextRotation(m_PlacingRotation));
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_PlacingType.reset();
            return;
        }
    }
    if (!hovered) {
        return;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_PlacingType.reset();
        return;
    }

    const GridPoint position = Snap(view.ToWorld(ImGui::GetIO().MousePos));
    auto preview = CreateElement(*m_PlacingType, position, m_PlacingRotation);
    preview->SetMirrored(m_PlacingMirrored);
    const bool is_label = *m_PlacingType == Core::ComponentType::NET_LABEL;
    if (is_label) {
        // A new label starts with the name of the last one, so the same net is a click away
        static_cast<Core::NetLabel &>(preview->GetComponent()).SetNet(FindLastNet());
    }
    preview->Draw(canvas, view, GetPreviewColor(), m_Preferences.GetSymbolStyle());
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return;
    }
    m_Schematic.AddElement(std::move(preview));
    if (is_label) {
        // The name is what a label needs next: placing ends and its popover opens on the name
        m_PlacingType.reset();
        m_Schematic.SelectElement(m_Schematic.GetElements().size() - 1);
        m_PartEditor.FocusNetName();
        PlacePartPopover(view);
        m_PartPopoverRequested = true;
    }
}

// The name of the newest label that has one, or none
std::string EditorWindow::FindLastNet() const {
    for (const auto &element : std::views::reverse(m_Schematic.GetElements())) {
        if (element->GetComponent().GetType() != Core::ComponentType::NET_LABEL) {
            continue;
        }
        if (const std::string &net = static_cast<const Core::NetLabel &>(element->GetComponent()).GetNet();
            !net.empty()) {
            return net;
        }
    }
    return {};
}

// Selecting is the mode where clicks pick, drag and select: none of placing, pasting, wiring and probing is active
bool EditorWindow::IsSelecting() const {
    return !m_PlacingType && !m_Pasting && !m_DrawingWires && !m_Probing;
}

// Ctrl+C, Ctrl+X and Ctrl+V while the editor has the focus and no text field wants the keys
void EditorWindow::HandleClipboardShortcuts() {
    if (!ImGui::IsWindowFocused() || ImGui::GetIO().WantTextInput) {
        return;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
        CopySelected();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_X)) {
        CutSelected();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V)) {
        StartPasting();
    }
}

// The selection goes to the clipboard as text in the format of the files, so another copy of the program can paste it
void EditorWindow::CopySelected() {
    if (!IsSelecting() || !m_Schematic.HasSelection()) {
        return;
    }
    const Selection selection = {m_Schematic.GetSelectedElements(), m_Schematic.GetSelectedWires()};
    ImGui::SetClipboardText(CopySelection(m_Schematic.GetElements(), m_Schematic.GetWires(), selection).c_str());
}

// Deleting in the middle of a drag would leave the drag pointing at removed items
void EditorWindow::CutSelected() {
    if (m_Drag || !IsSelecting() || !m_Schematic.HasSelection()) {
        return;
    }
    CopySelected();
    m_Schematic.DeleteSelection();
}

// Reads the clipboard and, when it holds a schematic, places it. Text that is not a schematic is left alone
void EditorWindow::StartPasting() {
    const char *text = ImGui::GetClipboardText();
    if (text == nullptr) {
        return;
    }
    auto group = ParseCopiedSchematic(text);
    if (!group) {
        return;
    }
    StartPastingGroup(std::move(*group));
}

// Leaves every other mode, so the group is the only thing to place
void EditorWindow::StartPastingGroup(PastedGroup group) {
    ClearSelection();
    m_PlacingType.reset();
    m_DrawingWires = false;
    m_WireStart.reset();
    m_Probing = false;
    m_Pasting = std::move(group);
}

/**
 * @brief   Handles paste mode: the pasted group follows the cursor, half see-through, R rotates it, M mirrors it left
 *          to right and Shift+M top to bottom, Esc or right click cancels, and a left click places it and selects what
 *          was placed. Parts get the next free names of the schematic.
 */
void EditorWindow::HandlePasting(SchematicCanvas &canvas, const ViewTransform &view, const bool hovered) {
    if (!m_Pasting) {
        return;
    }
    if (ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            TransformGroup(*m_Pasting, {.Type = PointTransform::Kind::QUARTER_TURN, .Delta = {}, .Pivot = {}});
        }
        if (ImGui::IsKeyPressed(ImGuiKey_M)) {
            const bool top_to_bottom = ImGui::GetIO().KeyShift;
            TransformGroup(*m_Pasting, {.Type = top_to_bottom ? PointTransform::Kind::MIRROR_TOP_BOTTOM
                                                              : PointTransform::Kind::MIRROR_LEFT_RIGHT,
                                        .Delta = {},
                                        .Pivot = {}});
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_Pasting.reset();
            return;
        }
    }
    if (!hovered) {
        return;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_Pasting.reset();
        return;
    }

    MovePivotTo(*m_Pasting, Snap(view.ToWorld(ImGui::GetIO().MousePos)));
    for (const UIWire &wire : m_Pasting->Wires) {
        wire.Draw(canvas, view, GetPreviewColor());
    }
    for (const auto &element : m_Pasting->Elements) {
        element->Draw(canvas, view, GetPreviewColor(), m_Preferences.GetSymbolStyle());
    }
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return;
    }

    const std::size_t first_element = m_Schematic.GetElements().size();
    const std::size_t first_wire = m_Schematic.GetWires().size();
    for (auto &element : m_Pasting->Elements) {
        m_Schematic.AddElement(std::move(element));
    }
    for (const UIWire &wire : m_Pasting->Wires) {
        m_Schematic.AddWire(wire.GetStart(), wire.GetEnd());
    }
    std::set<std::size_t> elements;
    for (std::size_t index = first_element; index < m_Schematic.GetElements().size(); ++index) {
        elements.insert(index);
    }
    std::set<std::size_t> wires;
    for (std::size_t index = first_wire; index < m_Schematic.GetWires().size(); ++index) {
        wires.insert(index);
    }
    m_Pasting.reset();
    m_Schematic.SetSelection(std::move(elements), std::move(wires));
}

/**
 * @brief   Handles wire mode: the first click starts a wire, each following click adds an L-shaped bend up to the
 *          cursor and continues from there. F flips the bend, and the wire ends on a terminal, on a second click
 *          at the same point, or with Esc or right click. Esc or right click with no wire in progress leaves
 *          the mode.
 */
void EditorWindow::HandleWireDrawing(ImDrawList *draw_list, const ViewTransform &view, const bool hovered) {
    if (!m_DrawingWires) {
        return;
    }

    if (ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_F)) {
            m_WireVerticalFirst = !m_WireVerticalFirst;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            StopWire();
            return;
        }
    }
    if (!hovered) {
        return;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        StopWire();
        return;
    }

    const GridPoint cursor = Snap(view.ToWorld(ImGui::GetIO().MousePos));
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (!m_WireStart) {
        const ImVec2 cursor_on_screen = view.ToScreen(ToVec2(cursor));
        const ImVec2 half_size = {WIRE_CURSOR_HALF_SIZE, WIRE_CURSOR_HALF_SIZE};
        draw_list->AddRect(cursor_on_screen - half_size, cursor_on_screen + half_size, GetPreviewColor());
        if (clicked) {
            m_WireStart = cursor;
        }
        return;
    }

    const GridPoint start = *m_WireStart;
    const GridPoint corner = m_WireVerticalFirst ? GridPoint{start.X, cursor.Y} : GridPoint{cursor.X, start.Y};
    DrawListCanvas canvas(draw_list);
    UIWire(start, corner).Draw(canvas, view, GetPreviewColor());
    UIWire(corner, cursor).Draw(canvas, view, GetPreviewColor());
    if (!clicked) {
        return;
    }
    if (cursor == start) {
        m_WireStart.reset();
        return;
    }
    m_Schematic.AddWire(start, corner);
    m_Schematic.AddWire(corner, cursor);
    if (m_Schematic.IsTerminal(cursor)) {
        m_WireStart.reset();
    } else {
        m_WireStart = cursor;
    }
}

// Esc and right click first drop the wire in progress, and only leave wire mode when there is none
void EditorWindow::StopWire() {
    if (m_WireStart) {
        m_WireStart.reset();
    } else {
        m_DrawingWires = false;
    }
}

/**
 * @brief   Handles probe mode: the item under the cursor is outlined with what it measures, a node voltage on a
 *          wire or a current on a part, and a left click adds it to the plots or removes it. Esc or right click
 *          leaves the mode.
 */
void EditorWindow::HandleProbing(ImDrawList *draw_list, const ViewTransform &view, const bool hovered) {
    if (!m_Probing) {
        return;
    }
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_Probing = false;
        return;
    }
    if (!hovered) {
        return;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_Probing = false;
        return;
    }

    const ImVec2 cursor = ImGui::GetIO().MousePos;
    const std::optional<MeasurementTarget> target =
        FindMeasurementTarget(m_Schematic.GetElements(), m_Schematic.GetWires(), m_Schematic.GetConnectivity(),
                              view.ToWorld(cursor), WIRE_PICK_DISTANCE / m_View.GetZoom());
    if (!target) {
        ImGui::SetTooltip("Click a wire for its voltage, or a part for its current");
        return;
    }
    const bool measured =
        target->Node ? m_Schematic.IsVoltageMeasured(*target->Node) : m_Schematic.IsCurrentMeasured(target->Current);
    draw_list->AddCircle(cursor, MEASUREMENT_MARKER_RADIUS * 2.0f, GetHighlightColor(), 0, LINE_THICKNESS);
    ImGui::SetTooltip("%s", std::format("{}\nClick to {} the plots",
                                        DescribeMeasurement(*target, m_Schematic.GetConnectivity(),
                                                            m_Displayed ? &m_Displayed->Point : nullptr),
                                        measured ? "remove it from" : "add it to")
                                .c_str());
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return;
    }
    if (target->Node) {
        m_Schematic.SetVoltageMeasured(*target->Node, !measured);
    } else {
        m_Schematic.SetCurrentMeasured(target->Current, !measured);
    }
}

/**
 * @brief   Handles selection mode, active while not placing, wiring or probing: a click selects an element or a wire,
 *          Shift+click adds or removes one, a drag on empty canvas selects what the box touches, and a double click on
 *          a wire selects its whole node. Dragging the selection moves it, a single wire sliding across itself, with
 *          the wires around it following. R rotates the selection, M mirrors it left to right and Shift+M top to
 *          bottom, both about its center, and holding Alt leaves the wires where they are; Delete removes the
 *          selection and Esc clears it.
 */
void EditorWindow::HandleSelection(const ViewTransform &view, const bool hovered) {
    if (!IsSelecting()) {
        m_Box.reset();
        return;
    }

    const ImVec2 cursor_world = view.ToWorld(ImGui::GetIO().MousePos);
    const bool shift = ImGui::GetIO().KeyShift;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const Selection picked = PickAt(m_Schematic.GetElements(), m_Schematic.GetWires(), cursor_world,
                                        WIRE_PICK_DISTANCE / m_View.GetZoom());
        const Selection current = {m_Schematic.GetSelectedElements(), m_Schematic.GetSelectedWires()};
        if (picked.Empty()) {
            // Empty canvas starts a box; with Shift it adds to what is selected, otherwise it starts over
            if (!shift) {
                ClearSelection();
            }
            m_Box = SelectionBox{.Start = cursor_world, .Base = shift ? current : Selection{}};
        } else if (shift) {
            ApplySelection(Toggled(current, picked));
        } else if (Joined(current, picked) == current) {
            // Part of a group: keep the group so it can be dragged, and collapse to the item if it is only a click
            m_Drag = MakeDrag(Snap(cursor_world), picked);
        } else {
            ApplySelection(picked);
            m_Drag = MakeDrag(Snap(cursor_world), std::nullopt);
        }
    }

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (m_Schematic.GetSelectedElement() != nullptr) {
            EndDrag();
            PlacePartPopover(view);
            m_PartPopoverRequested = true;
        } else if (m_Schematic.GetSelectedElements().empty() && m_Schematic.GetSelectedWires().size() == 1) {
            const Selection node = PickNode(m_Schematic.GetWires(), m_Schematic.GetConnectivity(),
                                            *m_Schematic.GetSelectedWires().begin());
            EndDrag();
            ApplySelection(node);
        }
    }

    if (m_Box) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const Selection inside =
                PickInRectangle(m_Schematic.GetElements(), m_Schematic.GetWires(), m_Box->Start, cursor_world);
            ApplySelection(Joined(m_Box->Base, inside));
        } else {
            m_Box.reset();
        }
    }

    if (m_Drag) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            UpdateDrag(Snap(cursor_world));
        } else {
            EndDrag();
        }
    }

    if (ImGui::IsWindowFocused()) {
        const bool keep_wires = ImGui::GetIO().KeyAlt;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            TransformSelection(PointTransform::Kind::QUARTER_TURN, keep_wires);
        }
        const bool enter =
            ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        if (enter && m_Schematic.GetSelectedElement() != nullptr && !ImGui::IsAnyItemActive()) {
            PlacePartPopover(view);
            m_PartPopoverRequested = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_M)) {
            TransformSelection(
                shift ? PointTransform::Kind::MIRROR_TOP_BOTTOM : PointTransform::Kind::MIRROR_LEFT_RIGHT, keep_wires);
        }
        // Deleting mid-drag would leave the drag pointing at removed items
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_Drag) {
            m_Schematic.DeleteSelection();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !m_PopupOpenLastFrame) {
            ClearSelection();
        }
    }
}

// Replaces the selection; a drag belongs to the items it was started with, so it ends
void EditorWindow::ApplySelection(const Selection &selection) {
    EndDrag();
    m_Schematic.SetSelection(selection.Elements, selection.Wires);
}

// A drag belongs to the selection, so it ends with it, and so does a selection box
void EditorWindow::ClearSelection() {
    EndDrag();
    m_Box.reset();
    m_Schematic.ClearSelection();
}

void EditorWindow::SelectAll() {
    if (!IsSelecting()) {
        return;
    }
    ApplySelection(PickAll(m_Schematic.GetElements(), m_Schematic.GetWires()));
}

// Captures what a drag of the current selection needs, so every frame can be computed from the start. A click on one
// item of a group only keeps the group while the mouse moves: without a move it leaves just that item selected
EditorWindow::GroupDrag EditorWindow::MakeDrag(const GridPoint cursor, std::optional<Selection> collapse_to) const {
    GroupDrag drag;
    drag.StartCursor = cursor;
    drag.Elements = m_Schematic.GetSelectedElements();
    drag.Wires = m_Schematic.GetSelectedWires();
    drag.StartWires = m_Schematic.GetWires();
    drag.CollapseTo = std::move(collapse_to);
    const auto &elements = m_Schematic.GetElements();
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const std::vector<GridPoint> terminals = elements[index]->GetTerminals();
        if (drag.Elements.contains(index)) {
            drag.StartPositions.push_back(elements[index]->GetPosition());
            drag.StartTerminals.insert(drag.StartTerminals.end(), terminals.begin(), terminals.end());
        } else {
            drag.FixedTerminals.insert(drag.FixedTerminals.end(), terminals.begin(), terminals.end());
        }
    }
    if (drag.Elements.empty() && drag.Wires.size() == 1) {
        const UIWire &wire = drag.StartWires[*drag.Wires.begin()];
        drag.SlidesVertically = wire.GetStart().Y == wire.GetEnd().Y;
    }
    return drag;
}

// Moves the selection by how far the cursor went from where the drag started, rebuilding the wires from the wires the
// drag started with so moving back and forth does not pile up bends
void EditorWindow::UpdateDrag(const GridPoint cursor) {
    if (!m_Drag) {
        return;
    }
    GroupDrag &drag = *m_Drag;
    drag.LastCursor = cursor;
    GridPoint delta = cursor - drag.StartCursor;
    if (drag.SlidesVertically) {
        // A wire only slides across itself: along its own line, moving it would change nothing
        (*drag.SlidesVertically ? delta.X : delta.Y) = 0;
    }
    if (delta == drag.LastDelta) {
        return;
    }
    drag.LastDelta = delta;
    drag.Moved = delta != GridPoint{};
    if (drag.Moved) {
        drag.CollapseTo.reset();
    }

    std::vector<GridPoint> new_terminals;
    std::size_t position_index = 0;
    for (const std::size_t index : drag.Elements) {
        UIElement &element = m_Schematic.GetElement(index);
        element.SetPosition(drag.StartPositions[position_index++] + delta);
        const std::vector<GridPoint> terminals = element.GetTerminals();
        new_terminals.insert(new_terminals.end(), terminals.begin(), terminals.end());
    }
    const PointTransform translation = {.Type = PointTransform::Kind::TRANSLATE, .Delta = delta, .Pivot = {}};
    MovedWires moved =
        MoveGroup(drag.StartWires, drag.Wires, drag.StartTerminals, new_terminals, drag.FixedTerminals, translation);
    m_Schematic.SetWires(std::move(moved.Wires));
    m_Schematic.SetSelection(drag.Elements, std::move(moved.Selected));
    m_Schematic.MarkModified();
}

// What the drag left selected: just the clicked item when the mouse never moved over a group. The cleanup of the
// wires is done by every frame of the drag already, so it only matters when the drag changed nothing
void EditorWindow::EndDrag() {
    if (!m_Drag) {
        return;
    }
    const std::optional<Selection> collapse_to = std::move(m_Drag->CollapseTo);
    m_Drag.reset();
    if (collapse_to) {
        m_Schematic.SetSelection(collapse_to->Elements, collapse_to->Wires);
    }
    m_Schematic.SimplifyAllWires();
}

// The box and the items it takes in, drawn over the schematic
void EditorWindow::DrawSelectionBox(ImDrawList *draw_list, const ViewTransform &view) const {
    if (!m_Box) {
        return;
    }
    const ImVec2 start = view.ToScreen(m_Box->Start);
    const ImVec2 end = ImGui::GetIO().MousePos;
    const ImU32 color = GetPalette().Selected;
    draw_list->AddRectFilled(start, end, WithAlpha(color, SELECTION_BOX_FILL_ALPHA));
    draw_list->AddRect(start, end, color, 0.0f, 0, 1.0f);
}

// Turns or mirrors every selected part about the center of the selection, and the selected wires with them. The wires
// that meet them follow, unless Alt is held: then the parts turn alone and every wire stays where it was. A turn
// during a drag ends that move and goes on dragging from the new position
void EditorWindow::TransformSelection(const PointTransform::Kind kind, const bool keep_wires) {
    if (m_Schematic.GetSelectedElements().empty()) {
        return;
    }
    const std::optional<GridPoint> drag_cursor = m_Drag ? std::optional(m_Drag->LastCursor) : std::nullopt;
    EndDrag();

    const auto &elements = m_Schematic.GetElements();
    const std::set<std::size_t> selected_elements = m_Schematic.GetSelectedElements();
    const std::set<std::size_t> selected_wires = m_Schematic.GetSelectedWires();
    const PointTransform transform = {
        .Type = kind,
        .Delta = {},
        .Pivot = FindPivot(elements, selected_elements, m_Schematic.GetWires(), selected_wires)};

    std::vector<GridPoint> old_terminals;
    std::vector<GridPoint> new_terminals;
    std::vector<GridPoint> fixed_terminals;
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const std::vector<GridPoint> before = elements[index]->GetTerminals();
        if (!selected_elements.contains(index)) {
            fixed_terminals.insert(fixed_terminals.end(), before.begin(), before.end());
            continue;
        }
        old_terminals.insert(old_terminals.end(), before.begin(), before.end());
        TransformElement(m_Schematic.GetElement(index), transform);
        const std::vector<GridPoint> after = elements[index]->GetTerminals();
        new_terminals.insert(new_terminals.end(), after.begin(), after.end());
    }
    if (!keep_wires) {
        MovedWires moved =
            MoveGroup(m_Schematic.GetWires(), selected_wires, old_terminals, new_terminals, fixed_terminals, transform);
        m_Schematic.SetWires(std::move(moved.Wires));
        m_Schematic.SetSelection(selected_elements, std::move(moved.Selected));
    }
    m_Schematic.MarkModified();
    if (drag_cursor) {
        m_Drag = MakeDrag(*drag_cursor, std::nullopt);
    }
}

// Beside the part, on its right, or on its left when the right side lacks room
void EditorWindow::PlacePartPopover(const ViewTransform &view) {
    const UIElement *element = m_Schematic.GetSelectedElement();
    if (element == nullptr) {
        return;
    }
    ImVec2 min = {FLT_MAX, FLT_MAX};
    ImVec2 max = {-FLT_MAX, -FLT_MAX};
    for (const GridPoint terminal : element->GetTerminals()) {
        const ImVec2 point = view.ToScreen(ToVec2(terminal));
        min = {std::min(min.x, point.x), std::min(min.y, point.y)};
        max = {std::max(max.x, point.x), std::max(max.y, point.y)};
    }
    const float margin = m_View.GetZoom() * POPOVER_MARGIN;
    const float right_edge = ImGui::GetWindowPos().x + ImGui::GetWindowWidth();
    const bool fits_right = max.x + margin + ImGui::GetFontSize() * POPOVER_WIDTH <= right_edge;
    m_PartPopoverAnchor = {fits_right ? max.x + margin : min.x - margin, min.y - margin};
    m_PartPopoverPivotX = fits_right ? 0.0f : 1.0f;
}

// A digit, a point or a minus typed on a selected part starts editing its value, as in LTspice: "4k7" and Enter.
// Keys are read rather than characters, since SDL sends characters only while a text field is active
void EditorWindow::HandleValueTyping(const ViewTransform &view) {
    const ImGuiIO &io = ImGui::GetIO();
    if (!ImGui::IsWindowFocused() || ImGui::IsAnyItemActive() || io.KeyCtrl || io.KeyAlt || io.KeyShift ||
        !m_PartEditor.CanTypeValue()) {
        return;
    }
    const std::optional<char> first = FindTypedValueStart();
    if (!first) {
        return;
    }
    m_PartEditor.StartTypingValue(*first);
    PlacePartPopover(view);
    m_PartPopoverRequested = true;
}

/**
 * @brief   Draws the properties of the selected part in a popover beside it. Edits apply as they are typed; Enter or
 *          Esc closes it, and so does a click outside.
 */
void EditorWindow::DrawPartPopover() {
    if (std::exchange(m_PartPopoverRequested, false)) {
        ImGui::OpenPopup(PART_POPOVER);
    }
    ImGui::SetNextWindowPos(m_PartPopoverAnchor, ImGuiCond_Appearing, {m_PartPopoverPivotX, 0.0f});
    if (!ImGui::BeginPopup(PART_POPOVER)) {
        return;
    }
    // Undo or a deletion may take the part away while the popover is open
    if (m_Schematic.GetSelectedElement() == nullptr) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    m_PartEditor.Draw();
    // The Enter that opened it is ignored; Esc closes only the popover, not the selection below
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    if (enter && !ImGui::IsWindowAppearing()) {
        ImGui::CloseCurrentPopup();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetCurrentWindow()->ID, ImGuiInputFlags_LockThisFrame);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// A right click while selecting selects what is under the cursor and opens the menu of what it can do
void EditorWindow::HandleContextMenu(const ViewTransform &view, const bool hovered) {
    if (!IsSelecting() || !hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return;
    }
    const ImVec2 cursor_world = view.ToWorld(ImGui::GetIO().MousePos);
    // Right click keeps a group when it lands on one of its items; anywhere else it selects what is under the cursor
    const Selection picked =
        PickAt(m_Schematic.GetElements(), m_Schematic.GetWires(), cursor_world, WIRE_PICK_DISTANCE / m_View.GetZoom());
    const Selection current = {m_Schematic.GetSelectedElements(), m_Schematic.GetSelectedWires()};
    if (picked.Empty() || Joined(current, picked) != current) {
        ApplySelection(picked);
    }
    m_ContextTarget =
        FindMeasurementTarget(m_Schematic.GetElements(), m_Schematic.GetWires(), m_Schematic.GetConnectivity(),
                              cursor_world, WIRE_PICK_DISTANCE / m_View.GetZoom());
    PlacePartPopover(view);
    ImGui::OpenPopup(CONTEXT_MENU);
}

/**
 * @brief   Draws the context menu: properties, orientation, measuring and deletion for a part, measuring and
 *          deletion for a wire, and finding a part, running and fitting the view on empty canvas.
 */
void EditorWindow::DrawContextMenu() {
    if (!ImGui::BeginPopup(CONTEXT_MENU)) {
        return;
    }
    const bool on_element = !m_Schematic.GetSelectedElements().empty();
    const bool on_wire = !m_Schematic.GetSelectedWires().empty();
    const bool single = m_Schematic.GetSelectedElements().size() + m_Schematic.GetSelectedWires().size() == 1;
    if (m_Schematic.GetSelectedElement() != nullptr) {
        if (ImGui::MenuItem("Properties...", "Enter")) {
            m_PartPopoverRequested = true;
        }
        ImGui::Separator();
    }
    if (on_element) {
        if (ImGui::MenuItem("Rotate", "R")) {
            TransformSelection(PointTransform::Kind::QUARTER_TURN, false);
        }
        if (ImGui::MenuItem("Mirror", "M")) {
            TransformSelection(PointTransform::Kind::MIRROR_LEFT_RIGHT, false);
        }
        if (ImGui::MenuItem("Flip", "Shift+M")) {
            TransformSelection(PointTransform::Kind::MIRROR_TOP_BOTTOM, false);
        }
        if (ImGui::BeginMenu("Keep Wires in Place")) {
            if (ImGui::MenuItem("Rotate", "Alt+R")) {
                TransformSelection(PointTransform::Kind::QUARTER_TURN, true);
            }
            if (ImGui::MenuItem("Mirror", "Alt+M")) {
                TransformSelection(PointTransform::Kind::MIRROR_LEFT_RIGHT, true);
            }
            if (ImGui::MenuItem("Flip", "Alt+Shift+M")) {
                TransformSelection(PointTransform::Kind::MIRROR_TOP_BOTTOM, true);
            }
            ImGui::EndMenu();
        }
    }
    if (single && m_ContextTarget) {
        if (on_element) {
            ImGui::Separator();
        }
        const MeasurementTarget &target = *m_ContextTarget;
        const bool measured =
            target.Node ? m_Schematic.IsVoltageMeasured(*target.Node) : m_Schematic.IsCurrentMeasured(target.Current);
        const std::string label = std::format("{} {}", measured ? "Stop measuring" : "Measure",
                                              GetMeasurementLabel(target, m_Schematic.GetConnectivity()));
        if (ImGui::MenuItem(label.c_str())) {
            if (target.Node) {
                m_Schematic.SetVoltageMeasured(*target.Node, !measured);
            } else {
                m_Schematic.SetCurrentMeasured(target.Current, !measured);
            }
        }
    }
    if (on_element || on_wire) {
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del")) {
            m_Schematic.DeleteSelection();
        }
    } else {
        if (ImGui::MenuItem("Find a Part...", "Space")) {
            m_PartPickerRequested = true;
        }
        if (m_Controls.IsRunning()) {
            if (ImGui::MenuItem("Stop the Run")) {
                m_Controls.StopRun();
            }
        } else if (ImGui::MenuItem(
                       std::format("Run {}", GetAnalysisName(m_Schematic.GetSimulationSettings().Selected)).c_str(),
                       "F5")) {
            m_Controls.RunSelected();
        }
        if (ImGui::MenuItem("Fit Schematic", "Home")) {
            m_View.RequestFit();
        }
    }
    ImGui::EndPopup();
}

// Global routing makes the shortcuts work while another editor window, such as Properties, has focus
void EditorWindow::HandleGlobalShortcuts() {
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, ImGuiInputFlags_RouteGlobal)) {
        m_Files.RequestNew();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) {
        m_Files.RequestOpen();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
        m_Files.Save();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
        m_Files.SaveAs();
    }
    if (ImGui::Shortcut(ImGuiKey_F5, ImGuiInputFlags_RouteGlobal)) {
        m_Controls.RunSelected();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, ImGuiInputFlags_RouteGlobal)) {
        RequestQuit();
    }
    // A focused text field keeps Ctrl+Z for undoing its own typing
    if (ImGui::GetIO().WantTextInput) {
        return;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A, ImGuiInputFlags_RouteGlobal)) {
        SelectAll();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) {
        Undo();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) {
        Redo();
    }
}

// A drag keeps pointers into the elements, so undoing in the middle of one waits until it ends
void EditorWindow::Undo() {
    if (m_Drag) {
        return;
    }
    m_Schematic.Undo();
}

void EditorWindow::Redo() {
    if (m_Drag) {
        return;
    }
    m_Schematic.Redo();
}

// Menu commands run here, from Update(), where their popups are opened and drawn whichever window is in front
void EditorWindow::HandleCommandRequest() {
    const std::optional<EditorCommand> command = std::exchange(m_RequestedCommand, std::nullopt);
    if (command && !RunFileCommand(*command) && !RunClipboardCommand(*command)) {
        RunEditCommand(*command);
    }
}

// The commands of the File menu; false for any other
bool EditorWindow::RunFileCommand(const EditorCommand command) {
    switch (command) {
    case EditorCommand::NEW:
        m_Files.RequestNew();
        return true;
    case EditorCommand::OPEN:
        m_Files.RequestOpen();
        return true;
    case EditorCommand::SAVE:
        m_Files.Save();
        return true;
    case EditorCommand::SAVE_AS:
        m_Files.SaveAs();
        return true;
    case EditorCommand::IMPORT:
        m_Files.Import();
        return true;
    case EditorCommand::EXPORT_SCHEMATIC:
        m_Files.OpenExportPopup();
        return true;
    default:
        return false;
    }
}

// Cut, Copy and Paste; false for any other
bool EditorWindow::RunClipboardCommand(const EditorCommand command) {
    switch (command) {
    case EditorCommand::CUT:
        CutSelected();
        return true;
    case EditorCommand::COPY:
        CopySelected();
        return true;
    case EditorCommand::PASTE:
        StartPasting();
        return true;
    default:
        return false;
    }
}

// The commands of the Edit menu: history, selecting all, deleting and turning the selection
void EditorWindow::RunEditCommand(const EditorCommand command) {
    switch (command) {
    case EditorCommand::UNDO:
        Undo();
        break;
    case EditorCommand::REDO:
        Redo();
        break;
    case EditorCommand::SELECT_ALL:
        SelectAll();
        break;
    case EditorCommand::DELETE_SELECTION:
        // Deleting mid-drag would leave the drag pointing at a removed element
        if (!m_Drag) {
            m_Schematic.DeleteSelection();
        }
        break;
    default:
        RunTurnCommand(command);
        break;
    }
}

// Rotate, Mirror and Flip, and their variants that keep the wires where they are
void EditorWindow::RunTurnCommand(const EditorCommand command) {
    switch (command) {
    case EditorCommand::ROTATE:
        TransformSelection(PointTransform::Kind::QUARTER_TURN, false);
        break;
    case EditorCommand::ROTATE_KEEP_WIRES:
        TransformSelection(PointTransform::Kind::QUARTER_TURN, true);
        break;
    case EditorCommand::MIRROR:
        TransformSelection(PointTransform::Kind::MIRROR_LEFT_RIGHT, false);
        break;
    case EditorCommand::MIRROR_KEEP_WIRES:
        TransformSelection(PointTransform::Kind::MIRROR_LEFT_RIGHT, true);
        break;
    case EditorCommand::FLIP:
        TransformSelection(PointTransform::Kind::MIRROR_TOP_BOTTOM, false);
        break;
    case EditorCommand::FLIP_KEEP_WIRES:
        TransformSelection(PointTransform::Kind::MIRROR_TOP_BOTTOM, true);
        break;
    default:
        break;
    }
}

} // namespace GUI
