/**
 * @file    editor_window.cpp
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#include "editor_window.h"

#include "current_flow.h"
#include "element_factory.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "node_colors.h"
#include "probing.h"
#include "schematic_drawing.h"
#include "schematic_file.h"
#include "spice_value.h"
#include "theme.h"
#include "wire_editing.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <format>
#include <numbers>
#include <span>
#include <string>
#include <utility>

namespace GUI {

namespace {

constexpr float MinCanvasSize = 50.0f;
constexpr float MinZoom = 4.0f;
constexpr float MaxZoom = 200.0f;
constexpr float ZoomStep = 1.1f;
// Fit may zoom in this far on a small circuit; opening a file stops at the default zoom
constexpr float FitMaxZoom = 50.0f;
// Below this spacing the dots are thinned out, otherwise far zoom-out draws hundreds of thousands of them
constexpr float MinDotSpacing = 8.0f;
constexpr float WireCursorHalfSize = 4.0f;
// How close, in pixels, the cursor must be to a wire to select it
constexpr float WirePickDistance = 5.0f;
// Junction dot radius relative to the zoom, with a minimum so it stays visible when zoomed out
constexpr float JunctionRadiusScale = 0.15f;
constexpr float MinJunctionRadius = 2.5f;
// Voltage labels sit just above and right of their point, clear of the line
constexpr ImVec2 VoltageLabelOffset = {4.0f, -16.0f};
// Currents span decades, so the heat scale is logarithmic and shows this many below the largest current
constexpr double CurrentDecades = 4.0;
// Heat legend, in font sizes, at the bottom left of the canvas
constexpr float LegendWidth = 10.0f;
constexpr float LegendHeight = 0.6f;
// Null-terminated literals, so data() can go to ImGui
constexpr auto WireColoringNames = std::to_array<std::string_view>({"Plain", "Nodes", "Voltage", "Current"});
// Section of imgui.ini that keeps the View preferences
constexpr const char *SettingsTypeName = "Editor";
/**
 * @struct  PartInfo
 * @brief   How a kind of part is named to the user.
 */
struct PartInfo {
    Core::ComponentType Type;
    const char *Name;
};

constexpr auto Parts = std::to_array<PartInfo>({
    {Core::ComponentType::Resistor, "Resistor"},
    {Core::ComponentType::Capacitor, "Capacitor"},
    {Core::ComponentType::Inductor, "Inductor"},
    {Core::ComponentType::Ground, "Ground"},
    {Core::ComponentType::VCC, "VCC supply"},
    {Core::ComponentType::VoltageSource, "Voltage source"},
    {Core::ComponentType::CurrentSource, "Current source"},
    {Core::ComponentType::Diode, "Diode"},
    {Core::ComponentType::ZenerDiode, "Zener diode"},
    {Core::ComponentType::LED, "LED"},
    {Core::ComponentType::NPN, "NPN transistor"},
    {Core::ComponentType::PNP, "PNP transistor"},
    {Core::ComponentType::NMOS, "N-channel MOSFET"},
    {Core::ComponentType::PMOS, "P-channel MOSFET"},
    {Core::ComponentType::VCVS, "VCVS (E)"},
    {Core::ComponentType::VCCS, "VCCS (G)"},
    {Core::ComponentType::CCCS, "CCCS (F)"},
    {Core::ComponentType::CCVS, "CCVS (H)"},
    {Core::ComponentType::OpAmp, "Op-amp"},
});

const char *GetPartName(const Core::ComponentType type) {
    const auto part = std::ranges::find(Parts, type, &PartInfo::Type);
    return part != Parts.end() ? part->Name : Core::GetTypeName(type);
}

// Lowercase letters and digits only, so "opamp" finds "Op-amp" and "vcc" finds "VCC supply"
std::string NormalizeSearchText(const std::string_view text) {
    std::string normalized;
    for (const char character : text) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
            normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
    }
    return normalized;
}

// Parts whose user name or type name contains the query, in table order
std::vector<Core::ComponentType> FindParts(const std::string_view query) {
    const std::string normalized_query = NormalizeSearchText(query);
    std::vector<Core::ComponentType> found;
    for (const PartInfo &part : Parts) {
        if (NormalizeSearchText(part.Name).contains(normalized_query) ||
            NormalizeSearchText(Core::GetTypeName(part.Type)).contains(normalized_query)) {
            found.push_back(part.Type);
        }
    }
    return found;
}

/**
 * @struct  PartGroup
 * @brief   Related parts that share one toolbar button, with a dropdown to pick among them.
 */
struct PartGroup {
    const char *Name;
    std::span<const Core::ComponentType> Types;
};

constexpr auto SourceParts = std::to_array({Core::ComponentType::VoltageSource, Core::ComponentType::CurrentSource});
constexpr auto DiodeParts =
    std::to_array({Core::ComponentType::Diode, Core::ComponentType::ZenerDiode, Core::ComponentType::LED});
constexpr auto TransistorParts = std::to_array(
    {Core::ComponentType::NPN, Core::ComponentType::PNP, Core::ComponentType::NMOS, Core::ComponentType::PMOS});
constexpr auto ControlledParts = std::to_array(
    {Core::ComponentType::VCVS, Core::ComponentType::VCCS, Core::ComponentType::CCCS, Core::ComponentType::CCVS});
constexpr auto PartGroups = std::to_array<PartGroup>({
    {"Sources", SourceParts},
    {"Diodes", DiodeParts},
    {"Transistors", TransistorParts},
    {"Controlled sources", ControlledParts},
});

// Parts with a button of their own, in toolbar groups
constexpr auto PassiveParts =
    std::to_array({Core::ComponentType::Resistor, Core::ComponentType::Capacitor, Core::ComponentType::Inductor});
constexpr auto SupplyParts = std::to_array({Core::ComponentType::Ground, Core::ComponentType::VCC});

// Toolbar buttons are square, this many frame heights wide; icons fill this share of them
constexpr float ToolButtonScale = 1.4f;
constexpr float IconScale = 0.7f;
// The dropdown arrow beside a group button, in frame heights
constexpr float DropdownButtonScale = 0.6f;

/**
 * @enum    ToolIcon
 * @brief   Icons of the toolbar buttons that are not parts.
 */
enum class ToolIcon {
    Run,
    Select,
    Wire,
    Probe,
    Undo,
    Redo,
    Fit
};

// Line drawings on a unit square centered on the icon, y pointing down
void DrawToolIcon(ImDrawList *draw_list, const ToolIcon icon, const ImVec2 center, const float size,
                  const ImU32 color) {
    const float flip = icon == ToolIcon::Redo ? -1.0f : 1.0f;
    const auto point = [&](const float x, const float y) { return center + ImVec2(x * flip, y) * size; };
    switch (icon) {
    case ToolIcon::Run: {
        draw_list->AddTriangleFilled(point(-0.25f, -0.35f), point(0.35f, 0.0f), point(-0.25f, 0.35f), color);
        break;
    }
    case ToolIcon::Select: {
        const std::array<ImVec2, 7> arrow = {point(-0.3f, -0.45f), point(-0.3f, 0.3f),  point(-0.12f, 0.13f),
                                             point(0.02f, 0.43f),  point(0.14f, 0.37f), point(0.0f, 0.08f),
                                             point(0.22f, 0.08f)};
        draw_list->AddPolyline(arrow.data(), static_cast<int>(arrow.size()), color, ImDrawFlags_Closed, LineThickness);
        break;
    }
    case ToolIcon::Wire: {
        const std::array<ImVec2, 4> wire = {point(-0.4f, 0.3f), point(0.0f, 0.3f), point(0.0f, -0.3f),
                                            point(0.4f, -0.3f)};
        draw_list->AddPolyline(wire.data(), static_cast<int>(wire.size()), color, ImDrawFlags_None, LineThickness);
        draw_list->AddCircleFilled(wire.front(), size * 0.08f, color);
        draw_list->AddCircleFilled(wire.back(), size * 0.08f, color);
        break;
    }
    case ToolIcon::Probe:
        draw_list->AddLine(point(-0.45f, 0.45f), point(-0.12f, 0.12f), color, LineThickness);
        draw_list->AddLine(point(-0.12f, 0.12f), point(0.3f, -0.3f), color, size * 0.22f);
        draw_list->AddLine(point(0.3f, -0.3f), point(0.45f, -0.45f), color, LineThickness);
        break;
    case ToolIcon::Fit: {
        // Four corners of a frame
        constexpr float Corner = 0.4f;
        constexpr float Arm = 0.18f;
        for (const float x : {-Corner, Corner}) {
            for (const float y : {-Corner, Corner}) {
                const float inward_x = x < 0.0f ? Arm : -Arm;
                const float inward_y = y < 0.0f ? Arm : -Arm;
                const std::array<ImVec2, 3> corner = {point(x + inward_x, y), point(x, y), point(x, y + inward_y)};
                draw_list->AddPolyline(corner.data(), static_cast<int>(corner.size()), color, ImDrawFlags_None,
                                       LineThickness);
            }
        }
        break;
    }
    case ToolIcon::Undo:
    case ToolIcon::Redo: {
        // An arc over the top that turns back, with the arrowhead at its left end, mirrored for Redo
        const float radius = 0.28f;
        const ImVec2 middle = {0.05f, 0.05f};
        for (int step = 0; step <= 12; ++step) {
            const float angle = std::numbers::pi_v<float> * (1.0f + static_cast<float>(step) / 12.0f);
            draw_list->PathLineTo(point(middle.x + radius * std::cos(angle), middle.y + radius * std::sin(angle)));
        }
        draw_list->PathLineTo(point(middle.x + radius, middle.y + 0.25f));
        draw_list->PathStroke(color, ImDrawFlags_None, LineThickness);
        draw_list->AddTriangleFilled(point(middle.x - radius, middle.y + 0.22f),
                                     point(middle.x - radius - 0.14f, middle.y - 0.02f),
                                     point(middle.x - radius + 0.14f, middle.y - 0.02f), color);
        break;
    }
    }
}

// Square toolbar button; the active one uses the primary colors, as the tool in use
bool ToolButton(const char *id, const bool active) {
    const float side = ImGui::GetFrameHeight() * ToolButtonScale;
    return active ? PrimaryButton(id, {side, side}) : ImGui::Button(id, {side, side});
}

// The button that names the selected analysis, with room for its dropdown arrow
float AnalysisButtonWidth() {
    float widest = 0.0f;
    for (const Analysis analysis :
         {Analysis::OperatingPoint, Analysis::Transient, Analysis::ACSweep, Analysis::DCSweep}) {
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
    return ImGui::GetFrameHeight() * ToolButtonScale * IconScale;
}

ImU32 IconColor(const bool enabled) {
    return ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
}

void DrawPartIcon(ImDrawList *draw_list, const Core::ComponentType type, const ImVec2 center, const float size,
                  const SymbolStyle style) {
    DrawListCanvas canvas(draw_list);
    CreateElement(type, {0, 0}, Rotation::R0)->DrawIcon(canvas, center, size, IconColor(true), style);
}

constexpr const char *DiscardPopup = "Discard changes?";
constexpr const char *FileMessagesPopup = "File messages";
constexpr const char *ExportPopup = "Export schematic";
constexpr const char *PartPickerPopup = "Find a part";
constexpr const char *PartPopover = "Part properties";
constexpr const char *ContextMenu = "Schematic actions";
// The properties popover opens this many grid units beside the part, and is about this many font sizes wide
constexpr float PopoverMargin = 1.0f;
constexpr float PopoverWidth = 18.0f;
// The part picker shows this many matches at once, and its top sits this far down the editor, as a share of it
constexpr std::size_t PartPickerRows = 5;
constexpr float PartPickerTop = 0.3f;
// Must stay valid until the dialog callback runs, so it lives for the whole program
constexpr std::array<SDL_DialogFileFilter, 1> SchematicFilters = {{{"imcsim schematic", "imcsim"}}};
constexpr std::array<SDL_DialogFileFilter, 1> SVGFilters = {{{"SVG image", "svg"}}};

void DrawGrid(ImDrawList *draw_list, const ViewTransform &view, const ImVec2 origin, const ImVec2 size,
              const float zoom) {
    int step = 1;
    while (zoom * static_cast<float>(step) < MinDotSpacing) {
        step *= 2;
    }
    const float step_size = static_cast<float>(step);

    const ImVec2 world_min = view.ToWorld(origin);
    const ImVec2 world_max = view.ToWorld(origin + size);
    const ImVec2 dot_half_size = {1.0f, 1.0f};
    for (float x = std::floor(world_min.x / step_size) * step_size; x <= world_max.x; x += step_size) {
        for (float y = std::floor(world_min.y / step_size) * step_size; y <= world_max.y; y += step_size) {
            const ImVec2 dot = view.ToScreen({x, y});
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

// "V(3) = 4.5V" once there is an operating point, or just "V(3)"
std::string DescribeMeasurement(const MeasurementTarget &target,
                                const std::optional<Core::OperatingPoint> &operating_point) {
    const std::optional<double> value =
        operating_point ? FindOperatingPointValue(target, *operating_point) : std::nullopt;
    if (!value) {
        return GetMeasurementLabel(target);
    }
    return std::format("{} = {}{}", GetMeasurementLabel(target), Core::FormatValue(*value), target.Node ? "V" : "A");
}

// Heat level of a current: 1 for the largest in the circuit, 0 for those CurrentDecades below it or less
float CurrentLevel(const double current, const double largest_current) {
    if (current == 0.0 || largest_current <= 0.0) {
        return 0.0f;
    }
    return static_cast<float>((std::log10(std::abs(current)) - std::log10(largest_current) + CurrentDecades) /
                              CurrentDecades);
}

double LargestCurrent(const Core::OperatingPoint &operating_point) {
    double largest = 0.0;
    for (const Core::ComponentCurrent &current : operating_point.Currents) {
        largest = std::max(largest, std::abs(current.Current));
    }
    return largest;
}

// Heat level of a node voltage, between the lowest and the highest node of the circuit, ground included
float VoltageLevel(const double voltage, const std::vector<double> &voltages) {
    const auto [lowest, highest] = std::ranges::minmax(voltages);
    return highest > lowest ? static_cast<float>((voltage - lowest) / (highest - lowest)) : 0.5f;
}

} // namespace

/**
 * @brief   Creates the editor for a schematic.
 * @param[in] schematic    Schematic to edit; it must outlive the window.
 * @param[in] controls     Analysis settings and runs, for the Run button; they must outlive the window.
 * @param[in] part_editor  Fields of the selected part, for the properties popover; it must outlive the window.
 */
EditorWindow::EditorWindow(Schematic &schematic, AnalysisControls &controls, PartEditor &part_editor)
    : AppWindow("Schematic", false), m_Schematic(schematic), m_Controls(controls), m_PartEditor(part_editor) {
    for (const PartGroup &group : PartGroups) {
        m_PartGroupChoices.push_back(group.Types.front());
    }
}

/**
 * @brief   Asks to quit, as when the user closes the main window. Call it between frames.
 * @note    Quitting is confirmed right away when there is nothing to lose; otherwise the next frame asks the user
 *          to discard the changes. Check IsQuitConfirmed() after drawing the frame.
 */
void EditorWindow::RequestQuit() {
    m_QuitRequested = true;
}

/**
 * @brief   Tells whether the application may quit.
 * @return  True once a quit request was confirmed, by the user or because there were no unsaved changes.
 */
bool EditorWindow::IsQuitConfirmed() const {
    return m_QuitConfirmed;
}

/**
 * @brief   Asks to open an example, as when the user picks one from the menu.
 * @param[in] example  One of GetExamples().
 * @note    Like quitting, the next frame opens it, after asking the user to discard unsaved changes if there are
 *          any. The example opens untitled, so saving it asks for a file.
 */
void EditorWindow::RequestExample(const Example &example) {
    m_RequestedExample = example;
}

/**
 * @brief   Draws the File menu items that act on the schematic, with their shortcuts.
 * @note    Call between BeginMenu() and EndMenu(); the command runs on the next frame of the editor.
 */
void EditorWindow::DrawFileMenuItems() {
    if (ImGui::MenuItem("New", "Ctrl+N")) {
        m_RequestedCommand = EditorCommand::New;
    }
    if (ImGui::MenuItem("Open...", "Ctrl+O")) {
        m_RequestedCommand = EditorCommand::Open;
    }
    if (ImGui::MenuItem("Save", "Ctrl+S")) {
        m_RequestedCommand = EditorCommand::Save;
    }
    if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
        m_RequestedCommand = EditorCommand::SaveAs;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Export Schematic...")) {
        m_RequestedCommand = EditorCommand::ExportSchematic;
    }
    ImGui::SetItemTooltip("Save the schematic as an SVG image, to show next to its plots");
}

/**
 * @brief   Draws the Edit menu items, with their shortcuts, enabled only when they apply.
 * @note    Call between BeginMenu() and EndMenu(); the command runs on the next frame of the editor.
 */
void EditorWindow::DrawEditMenuItems() {
    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_Schematic.CanUndo())) {
        m_RequestedCommand = EditorCommand::Undo;
    }
    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_Schematic.CanRedo())) {
        m_RequestedCommand = EditorCommand::Redo;
    }
    ImGui::Separator();
    const bool part_selected = m_Schematic.GetSelectedElement() != nullptr;
    if (ImGui::MenuItem("Rotate", "R", false, part_selected)) {
        m_RequestedCommand = EditorCommand::Rotate;
    }
    if (ImGui::MenuItem("Mirror", "M", false, part_selected)) {
        m_RequestedCommand = EditorCommand::Mirror;
    }
    if (ImGui::MenuItem("Flip", "Shift+M", false, part_selected)) {
        m_RequestedCommand = EditorCommand::Flip;
    }
    const bool anything_selected = part_selected || m_Schematic.GetSelectedWireIndex().has_value();
    if (ImGui::MenuItem("Delete", "Del", false, anything_selected)) {
        m_RequestedCommand = EditorCommand::Delete;
    }
}

/**
 * @brief   Draws the View menu items that set how the schematic looks; they apply right away and are remembered.
 * @note    Call between BeginMenu() and EndMenu().
 */
void EditorWindow::DrawViewMenuItems() {
    if (ImGui::MenuItem("Fit Schematic", "Home")) {
        m_FrameMaxZoom = FitMaxZoom;
    }
    ImGui::Separator();
    bool changed = false;
    if (ImGui::BeginMenu("Wire Colors")) {
        for (std::size_t index = 0; index < WireColoringNames.size(); ++index) {
            const auto coloring = static_cast<WireColoring>(index);
            if (ImGui::MenuItem(WireColoringNames[index].data(), nullptr, m_WireColoring == coloring)) {
                m_WireColoring = coloring;
                changed = true;
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Symbols")) {
        if (ImGui::MenuItem("IEC", nullptr, m_SymbolStyle == SymbolStyle::IEC)) {
            m_SymbolStyle = SymbolStyle::IEC;
            changed = true;
        }
        if (ImGui::MenuItem("ANSI", nullptr, m_SymbolStyle == SymbolStyle::ANSI)) {
            m_SymbolStyle = SymbolStyle::ANSI;
            changed = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Terminal Numbers", nullptr, &m_ShowTerminalNumbers)) {
        changed = true;
    }
    ImGui::SetItemTooltip("Number the terminals of each part; terminal 1 is where a positive current enters");
    if (changed) {
        ImGui::MarkIniSettingsDirty();
    }
}

/**
 * @brief   Keeps the View preferences (wire colors, symbol style, terminal numbers) in imgui.ini, next to the
 *          window layout, so they survive a restart.
 */
void EditorWindow::RegisterSettingsHandler() {
    ImGuiSettingsHandler handler;
    handler.TypeName = SettingsTypeName;
    handler.TypeHash = ImHashStr(SettingsTypeName);
    handler.UserData = this;
    handler.ReadOpenFn = [](ImGuiContext *, ImGuiSettingsHandler *settings, const char *) -> void * {
        return settings->UserData;
    };
    handler.ReadLineFn = [](ImGuiContext *, ImGuiSettingsHandler *, void *entry, const char *line) {
        auto *editor = static_cast<EditorWindow *>(entry);
        const std::string_view text(line);
        const auto value_of = [&text](const std::string_view key) -> std::optional<std::string_view> {
            if (!text.starts_with(key) || text.size() <= key.size() || text[key.size()] != '=') {
                return std::nullopt;
            }
            return text.substr(key.size() + 1);
        };
        if (const auto colors = value_of("WireColors")) {
            const auto found = std::ranges::find(WireColoringNames, *colors);
            if (found != WireColoringNames.end()) {
                editor->m_WireColoring = static_cast<WireColoring>(found - WireColoringNames.begin());
            }
        } else if (const auto symbols = value_of("Symbols")) {
            editor->m_SymbolStyle = *symbols == "ANSI" ? SymbolStyle::ANSI : SymbolStyle::IEC;
        } else if (const auto numbers = value_of("TerminalNumbers")) {
            editor->m_ShowTerminalNumbers = *numbers == "1";
        }
    };
    handler.WriteAllFn = [](ImGuiContext *, ImGuiSettingsHandler *settings, ImGuiTextBuffer *buffer) {
        const auto *editor = static_cast<const EditorWindow *>(settings->UserData);
        buffer->appendf("[%s][Preferences]\n", SettingsTypeName);
        buffer->appendf("WireColors=%s\n", WireColoringNames[static_cast<std::size_t>(editor->m_WireColoring)].data());
        buffer->appendf("Symbols=%s\n", editor->m_SymbolStyle == SymbolStyle::ANSI ? "ANSI" : "IEC");
        buffer->appendf("TerminalNumbers=%d\n\n", editor->m_ShowTerminalNumbers ? 1 : 0);
    };
    ImGui::AddSettingsHandler(&handler);
}

// Window content only: AppWindow::Render() wraps it in Begin/End
void EditorWindow::Draw() {
    // File results and the quit request open popups, which must belong to this window
    ProcessDialogResult();
    HandleQuitRequest();
    HandleExampleRequest();
    HandleCommandRequest();
    HandleFileShortcuts();
    DrawPartPicker();
    DrawToolbar();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    // The status bar takes one line below the canvas
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.y -= ImGui::GetTextLineHeightWithSpacing();
    size.x = std::max(size.x, MinCanvasSize);
    size.y = std::max(size.y, MinCanvasSize);
    if (const std::optional<float> max_zoom = std::exchange(m_FrameMaxZoom, std::nullopt)) {
        FrameSchematic(size, *max_zoom);
    }

    ImGui::InvisibleButton("canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    HandlePanAndZoom(origin, hovered, active);

    const ViewTransform view(origin, m_Pan, m_Zoom);
    // Selection runs before drawing so a dragged element is drawn where the cursor is this frame
    HandleSelection(view, hovered);
    HandleContextMenu(view, hovered);
    HandleValueTyping(view);
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(origin, origin + size, true);
    draw_list->AddRectFilled(origin, origin + size, GetPalette().CanvasBackground);
    DrawGrid(draw_list, view, origin, size, m_Zoom);

    DrawListCanvas canvas(draw_list);
    DrawWires(canvas, view);
    const auto &elements = m_Schematic.GetElements();
    for (std::size_t index = 0; index < elements.size(); ++index) {
        elements[index]->Draw(canvas, view, GetElementColor(index), m_SymbolStyle);
    }
    if (m_ShowTerminalNumbers) {
        DrawTerminalNumbers(canvas, view, m_Schematic, ScreenTerminalRingScale);
    }
    DrawNodeVoltages(draw_list, view);
    DrawMeasurementMarkers(canvas, view, m_Schematic);

    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_W)) {
        StartDrawingWires();
    }
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_P)) {
        StartProbing();
    }
    HandlePlacement(canvas, view, hovered);
    HandleWireDrawing(draw_list, view, hovered);
    HandleProbing(draw_list, view, hovered);
    DrawHoveredValue(view, hovered);
    DrawColorLegend(draw_list, origin, size);

    draw_list->PopClipRect();
    DrawStatusBar(view, hovered);
    DrawContextMenu();
    DrawPartPopover();
    DrawFilePopups();
    m_PopupOpenLastFrame = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

// One row of icon buttons, whose groups wrap onto new lines when the window is narrow instead of being cut off
void EditorWindow::DrawToolbar() {
    ToolbarRow row;
    const float gap = ToolbarRow::GroupGap();
    const float side = ImGui::GetFrameHeight() * ToolButtonScale;
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
    const bool selecting = !m_PlacingType && !m_DrawingWires && !m_Probing;
    if (tool_button("##select", ToolIcon::Select, selecting, true, "Select (Esc)", 0.0f)) {
        StartSelecting();
    }
    if (tool_button("##wire", ToolIcon::Wire, m_DrawingWires, true, "Wire (W)", 0.0f)) {
        StartDrawingWires();
    }
    if (tool_button("##probe", ToolIcon::Probe, m_Probing, true, "Probe (P): pick what the plots measure", 0.0f)) {
        StartProbing();
    }
    if (tool_button("##undo", ToolIcon::Undo, false, m_Schematic.CanUndo(), "Undo (Ctrl+Z)", 0.0f)) {
        Undo();
    }
    if (tool_button("##redo", ToolIcon::Redo, false, m_Schematic.CanRedo(), "Redo (Ctrl+Y)", 0.0f)) {
        Redo();
    }
    if (tool_button("##fit", ToolIcon::Fit, false, true, "Fit the schematic in view (Home)", 0.0f)) {
        m_FrameMaxZoom = FitMaxZoom;
    }

    // Parts
    for (const auto parts :
         {std::span<const Core::ComponentType>(PassiveParts), std::span<const Core::ComponentType>(SupplyParts)}) {
        float part_gap = gap;
        for (const Core::ComponentType type : parts) {
            row.Place(side, std::exchange(part_gap, 0.0f));
            DrawPartButton(type);
        }
    }
    for (std::size_t index = 0; index < PartGroups.size(); ++index) {
        row.Place(side + ImGui::GetFrameHeight() * DropdownButtonScale, index == 0 ? gap : 0.0f);
        DrawPartGroup(index);
    }
    row.Place(side);
    DrawPartButton(Core::ComponentType::OpAmp);

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
    DrawPartIcon(ImGui::GetWindowDrawList(), type, ItemCenter(), IconSize(), m_SymbolStyle);
    ImGui::SetItemTooltip("%s", GetPartName(type));
}

// Run starts the selected analysis; the button beside it names that analysis and opens its settings, where another
// one can be picked
void EditorWindow::DrawRunControls() {
    const float side = ImGui::GetFrameHeight() * ToolButtonScale;
    const Analysis selected = m_Schematic.GetSimulationSettings().Selected;
    if (PrimaryButton("##run", {side, side})) {
        m_Controls.RunSelected();
    }
    const float run_left = ImGui::GetItemRectMin().x;
    DrawToolIcon(ImGui::GetWindowDrawList(), ToolIcon::Run, ItemCenter(), IconSize(), IconColor(true));
    ImGui::SetItemTooltip("Run %s (F5)", GetAnalysisName(selected));

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
        if (PrimaryButton("Run (F5)") || ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
            m_Controls.RunSelected();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// The button places the part last picked in the group; the arrow beside it, or a right click, lists the others
void EditorWindow::DrawPartGroup(const std::size_t index) {
    const PartGroup &group = PartGroups[index];
    Core::ComponentType &choice = m_PartGroupChoices[index];
    ImGui::PushID(group.Name);
    DrawPartButton(choice);
    const bool open_list = ImGui::IsItemClicked(ImGuiMouseButton_Right);

    ImGui::SameLine(0.0f, 0.0f);
    const float side = ImGui::GetFrameHeight() * ToolButtonScale;
    const bool open_arrow = ImGui::Button("##more", {ImGui::GetFrameHeight() * DropdownButtonScale, side});
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
        const float row_height = ImGui::GetFrameHeight() * ToolButtonScale;
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
            DrawPartIcon(draw_list, type, start + ImVec2(row_height, row_height) / 2.0f, row_height * IconScale,
                         m_SymbolStyle);
            const float text_y = (row_height - ImGui::GetTextLineHeight()) / 2.0f;
            draw_list->AddText(start + ImVec2(row_height + ImGui::GetStyle().ItemSpacing.x, text_y), IconColor(true),
                               GetPartName(type));
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

// Voltage and Current need an operating point; without one, wires stay plain and the legend asks for it
void EditorWindow::DrawWires(SchematicCanvas &canvas, const ViewTransform &view) {
    const Connectivity &connectivity = m_Schematic.GetConnectivity();
    const auto &operating_point = m_Schematic.GetOperatingPoint();
    const auto &wires = m_Schematic.GetWires();
    const bool by_nodes = m_WireColoring == WireColoring::Nodes;
    const bool by_voltage = m_WireColoring == WireColoring::Voltage && operating_point;
    if (m_WireColoring == WireColoring::Current && operating_point) {
        DrawWireCurrents(canvas, view, *operating_point);
    }
    const auto node_color = [&](const int node) {
        if (by_nodes) {
            return GetNodeColor(node);
        }
        if (by_voltage && static_cast<std::size_t>(node) < operating_point->NodeVoltages.size()) {
            const std::vector<double> &voltages = operating_point->NodeVoltages;
            return GetHeatColor(VoltageLevel(voltages[static_cast<std::size_t>(node)], voltages));
        }
        return GetPalette().Wire;
    };

    const bool drawn_by_current = m_WireColoring == WireColoring::Current && operating_point;
    for (std::size_t index = 0; index < wires.size(); ++index) {
        const UIWire &wire = wires[index];
        const bool selected = index == m_Schematic.GetSelectedWireIndex();
        if (drawn_by_current && !selected) {
            continue;
        }
        // Every wire end is a connection point, so it always has a node
        const int node = connectivity.GetNode(wire.GetStart()).value_or(0);
        wire.Draw(canvas, view, selected ? GetPalette().Selected : node_color(node));
    }

    const float junction_radius = std::max(m_Zoom * JunctionRadiusScale, MinJunctionRadius);
    for (const GridPoint junction : connectivity.GetJunctions()) {
        const int node = connectivity.GetNode(junction).value_or(0);
        canvas.AddCircleFilled(view.ToScreen(ToVec2(junction)), junction_radius, node_color(node));
    }

    if (!by_nodes) {
        return;
    }
    for (const UIWire &wire : wires) {
        const int node = connectivity.GetNode(wire.GetStart()).value_or(0);
        const ImVec2 middle = view.ToScreen((ToVec2(wire.GetStart()) + ToVec2(wire.GetEnd())) / 2.0f);
        canvas.AddText(middle, GetPalette().CanvasText, std::format("{}", node).c_str());
    }
}

// Each piece of wire takes the heat color of its own current, so the path of the current shows along the wires
void EditorWindow::DrawWireCurrents(SchematicCanvas &canvas, const ViewTransform &view,
                                    const Core::OperatingPoint &operating_point) {
    const double largest = LargestCurrent(operating_point);
    for (const WireCurrent &piece :
         ComputeWireCurrents(m_Schematic.GetElements(), m_Schematic.GetWires(), operating_point.Currents)) {
        UIWire(piece.Start, piece.End).Draw(canvas, view, GetHeatColor(CurrentLevel(piece.Current, largest)));
    }
}

// Selection wins; coloring by current heats each part by the largest current through its terminals
ImU32 EditorWindow::GetElementColor(const std::size_t index) const {
    if (index == m_Schematic.GetSelectedElementIndex()) {
        return GetPalette().Selected;
    }
    const auto &operating_point = m_Schematic.GetOperatingPoint();
    if (m_WireColoring != WireColoring::Current || !operating_point) {
        return GetPalette().Element;
    }
    const UIElement &element = *m_Schematic.GetElements()[index];
    const Core::Component &component = element.GetComponent();
    if (component.GetType() == Core::ComponentType::Ground) {
        return GetPalette().Element;
    }
    double current = 0.0;
    for (std::size_t terminal = 0; terminal < element.GetTerminals().size(); ++terminal) {
        current = std::max(current,
                           std::abs(GetTerminalCurrent(component, terminal, operating_point->Currents).value_or(0.0)));
    }
    return GetHeatColor(CurrentLevel(current, LargestCurrent(*operating_point)));
}

// What the current mode does and which keys it takes, so the shortcuts can be learned while working, and the grid
// point under the cursor on the right
void EditorWindow::DrawStatusBar(const ViewTransform &view, const bool hovered) {
    std::string hint;
    if (m_PlacingType) {
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
                        "Del delete, right click for more",
                        name);
    } else if (m_Schematic.GetSelectedWireIndex()) {
        hint = "Wire selected: Del delete, Esc deselect";
    } else {
        hint =
            "Click a part or wire to select it, middle drag to pan, wheel to zoom, Space find a part, W wire, P probe";
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
    const RunOutcome outcome = last_run ? m_Controls.GetOutcome(*last_run) : RunOutcome::None;
    std::string status;
    ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    switch (outcome) {
    case RunOutcome::None:
        break;
    case RunOutcome::Succeeded:
        status = std::format("{} done", GetAnalysisName(*last_run));
        break;
    case RunOutcome::SucceededWithWarnings:
        status = std::format("{} done, with warnings", GetAnalysisName(*last_run));
        color = GetWarningTextColor();
        break;
    case RunOutcome::Failed:
        status = std::format("{} failed", GetAnalysisName(*last_run));
        color = GetErrorTextColor();
        break;
    }
    const ImVec2 line_start = ImGui::GetCursorScreenPos();
    if (!status.empty()) {
        const ImVec2 status_size = ImGui::CalcTextSize(status.c_str());
        right_edge -= status_size.x;
        ImGui::SetCursorScreenPos({right_edge, line_start.y});
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        if (ImGui::Selectable(status.c_str(), false, ImGuiSelectableFlags_None, status_size)) {
            ImGui::OpenPopup("run_report");
        }
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Show what the run reported, with the ngspice output");
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

// A gradient with the values at its ends, or a reminder to run an operating point
void EditorWindow::DrawColorLegend(ImDrawList *draw_list, const ImVec2 origin, const ImVec2 size) const {
    if (m_WireColoring != WireColoring::Voltage && m_WireColoring != WireColoring::Current) {
        return;
    }
    const float font_size = ImGui::GetFontSize();
    const ImVec2 corner = {origin.x + font_size, origin.y + size.y - font_size * 2.0f};
    const auto &operating_point = m_Schematic.GetOperatingPoint();
    if (!operating_point) {
        draw_list->AddText(corner, GetPalette().CanvasText,
                           "Run an operating point (.op) to color by voltage or current");
        return;
    }

    std::string low_label;
    std::string high_label;
    if (m_WireColoring == WireColoring::Voltage) {
        const auto [lowest, highest] = std::ranges::minmax(operating_point->NodeVoltages);
        low_label = std::format("{}V", Core::FormatValue(lowest));
        high_label = std::format("{}V", Core::FormatValue(highest));
    } else {
        const double largest = LargestCurrent(*operating_point);
        low_label = std::format("<{}A", Core::FormatValue(largest * std::pow(10.0, -CurrentDecades)));
        high_label = std::format("{}A", Core::FormatValue(largest));
    }
    const ImVec2 bar_size = {font_size * LegendWidth, font_size * LegendHeight};
    constexpr int Steps = 32;
    for (int step = 0; step < Steps; ++step) {
        const float left = corner.x + bar_size.x * static_cast<float>(step) / Steps;
        const float right = corner.x + bar_size.x * static_cast<float>(step + 1) / Steps;
        draw_list->AddRectFilled({left, corner.y}, {right, corner.y + bar_size.y},
                                 GetHeatColor((static_cast<float>(step) + 0.5f) / Steps));
    }
    const float text_y = corner.y + bar_size.y + 2.0f;
    draw_list->AddText({corner.x, text_y}, GetPalette().CanvasText, low_label.c_str());
    const float high_width = ImGui::CalcTextSize(high_label.c_str()).x;
    draw_list->AddText({corner.x + bar_size.x - high_width, text_y}, GetPalette().CanvasText, high_label.c_str());
}

// One label per node, on its first wire or, for terminals joined without wires, on a terminal. Ground is
// always 0 V, so it is left out
void EditorWindow::DrawNodeVoltages(ImDrawList *draw_list, const ViewTransform &view) {
    const auto &operating_point = m_Schematic.GetOperatingPoint();
    if (!operating_point) {
        return;
    }
    const Connectivity &connectivity = m_Schematic.GetConnectivity();
    const std::vector<double> &voltages = operating_point->NodeVoltages;
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
        draw_list->AddText(view.ToScreen(world_pos) + VoltageLabelOffset, GetPalette().VoltageLabel, text.c_str());
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

// In selection mode, hovering a wire or a part shows its operating point value, once there is one
void EditorWindow::DrawHoveredValue(const ViewTransform &view, const bool hovered) {
    const auto &operating_point = m_Schematic.GetOperatingPoint();
    if (!hovered || !operating_point || m_PlacingType || m_DrawingWires || m_Probing || m_Drag) {
        return;
    }
    const std::optional<MeasurementTarget> target =
        FindMeasurementTarget(m_Schematic.GetElements(), m_Schematic.GetWires(), m_Schematic.GetConnectivity(),
                              view.ToWorld(ImGui::GetIO().MousePos), WirePickDistance / m_Zoom);
    if (target && FindOperatingPointValue(*target, *operating_point)) {
        ImGui::SetTooltip("%s", DescribeMeasurement(*target, operating_point).c_str());
    }
}

void EditorWindow::HandlePanAndZoom(const ImVec2 origin, const bool hovered, const bool active) {
    const ImGuiIO &io = ImGui::GetIO();
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        m_Pan += io.MouseDelta;
    }
    if (hovered && io.MouseWheel != 0.0f) {
        // Keep the world point under the cursor fixed while zooming
        const ImVec2 before = ViewTransform(origin, m_Pan, m_Zoom).ToWorld(io.MousePos);
        m_Zoom = std::clamp(m_Zoom * (io.MouseWheel > 0 ? ZoomStep : 1 / ZoomStep), MinZoom, MaxZoom);
        const ImVec2 after = ViewTransform(origin, m_Pan, m_Zoom).ToWorld(io.MousePos);
        m_Pan += (after - before) * m_Zoom;
    }
}

// Shows the whole schematic centered, never zoomed in past max_zoom, so a small circuit is not blown up
void EditorWindow::FrameSchematic(const ImVec2 canvas_size, const float max_zoom) {
    std::vector<GridPoint> points = m_Schematic.CollectTerminals();
    for (const auto &element : m_Schematic.GetElements()) {
        points.push_back(element->GetPosition());
    }
    for (const UIWire &wire : m_Schematic.GetWires()) {
        points.push_back(wire.GetStart());
        points.push_back(wire.GetEnd());
    }
    const ViewFrame frame = FramePoints(points, canvas_size, MinZoom, max_zoom);
    m_Pan = frame.Pan;
    m_Zoom = frame.Zoom;
}

// Leaves every other mode, so clicks select and drag
void EditorWindow::StartSelecting() {
    m_PlacingType.reset();
    m_DrawingWires = false;
    m_WireStart.reset();
    m_Probing = false;
}

void EditorWindow::StartPlacing(const Core::ComponentType type) {
    ClearSelection();
    m_Probing = false;
    m_DrawingWires = false;
    m_WireStart.reset();
    m_PlacingType = type;
    m_PlacingRotation = Rotation::R0;
    m_PlacingMirrored = false;
}

void EditorWindow::StartDrawingWires() {
    ClearSelection();
    m_PlacingType.reset();
    m_Probing = false;
    m_DrawingWires = true;
}

void EditorWindow::StartProbing() {
    ClearSelection();
    m_PlacingType.reset();
    m_DrawingWires = false;
    m_WireStart.reset();
    m_Probing = true;
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
        ImGui::OpenPopup(PartPickerPopup);
    }
    // Hangs from its top edge, a third of the way down the editor, so it stays put as the list filters
    const ImVec2 anchor = {ImGui::GetWindowPos().x + ImGui::GetWindowWidth() / 2.0f,
                           ImGui::GetWindowPos().y + ImGui::GetWindowHeight() * PartPickerTop};
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Appearing, {0.5f, 0.0f});
    // Without navigation the arrow keys move the highlight below instead of taking focus from the query
    if (!ImGui::BeginPopup(PartPickerPopup, ImGuiWindowFlags_NoNav)) {
        return;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    const float row_height = ImGui::GetFrameHeight() * ToolButtonScale;
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
    const std::size_t visible_rows = std::min(found.size(), PartPickerRows);
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
        DrawPartIcon(draw_list, type, start + ImVec2(row_height, row_height) / 2.0f, row_height * IconScale,
                     m_SymbolStyle);
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
    preview->Draw(canvas, view, GetPreviewColor(), m_SymbolStyle);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_Schematic.AddElement(std::move(preview));
    }
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
        const ImVec2 half_size = {WireCursorHalfSize, WireCursorHalfSize};
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
                              view.ToWorld(cursor), WirePickDistance / m_Zoom);
    if (!target) {
        ImGui::SetTooltip("Click a wire for its voltage, or a part for its current");
        return;
    }
    const bool measured =
        target->Node ? m_Schematic.IsVoltageMeasured(*target->Node) : m_Schematic.IsCurrentMeasured(target->Current);
    draw_list->AddCircle(cursor, MeasurementMarkerRadius * 2.0f, GetHighlightColor(), 0, LineThickness);
    ImGui::SetTooltip("%s", std::format("{}\nClick to {} the plots",
                                        DescribeMeasurement(*target, m_Schematic.GetOperatingPoint()),
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
 * @brief   Handles selection mode, active while not placing, wiring or probing: click selects an element or a wire,
 *          dragging an element moves it with its wires following, R rotates the selected element, M mirrors it left
 *          to right and Shift+M top to bottom, Delete removes the selection and Esc clears it.
 */
void EditorWindow::HandleSelection(const ViewTransform &view, const bool hovered) {
    if (m_PlacingType || m_DrawingWires || m_Probing) {
        return;
    }

    const ImVec2 cursor_world = view.ToWorld(ImGui::GetIO().MousePos);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        SelectAt(cursor_world);
        if (const UIElement *element = m_Schematic.GetSelectedElement()) {
            m_Drag = ElementDrag{Snap(cursor_world), element->GetPosition(), element->GetTerminals(),
                                 m_Schematic.GetWires()};
        }
    }

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_Schematic.GetSelectedElement() != nullptr) {
        EndDrag();
        PlacePartPopover(view);
        m_PartPopoverRequested = true;
    }

    if (m_Drag) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            UIElement &element = *m_Schematic.GetSelectedElement();
            const GridPoint position = m_Drag->StartPosition + (Snap(cursor_world) - m_Drag->StartCursor);
            if (position != element.GetPosition()) {
                element.SetPosition(position);
                m_Schematic.MarkModified();
            }
            // Rebuilt every frame, not only on moves, so a rotation during the drag is followed too
            m_Schematic.SetWires(FollowTerminals(m_Drag->StartWires, m_Drag->StartTerminals, element.GetTerminals()));
        } else {
            EndDrag();
        }
    }

    if (ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_R) && m_Schematic.GetSelectedElement() != nullptr) {
            RotateSelectedElement();
        }
        const bool enter =
            ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        if (enter && m_Schematic.GetSelectedElement() != nullptr && !ImGui::IsAnyItemActive()) {
            PlacePartPopover(view);
            m_PartPopoverRequested = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_M) && m_Schematic.GetSelectedElement() != nullptr) {
            MirrorSelectedElement(ImGui::GetIO().KeyShift);
        }
        // Deleting mid-drag would leave the drag pointing at a removed element
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_Drag) {
            m_Schematic.DeleteSelection();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !m_PopupOpenLastFrame) {
            ClearSelection();
        }
    }
}

// Elements win over wires, and the most recently added item wins because it is drawn on top
void EditorWindow::SelectAt(const ImVec2 world_pos) {
    ClearSelection();
    const auto &elements = m_Schematic.GetElements();
    for (std::size_t index = elements.size(); index-- > 0;) {
        if (elements[index]->Contains(world_pos)) {
            m_Schematic.SelectElement(index);
            return;
        }
    }
    const float tolerance = WirePickDistance / m_Zoom;
    const auto &wires = m_Schematic.GetWires();
    for (std::size_t index = wires.size(); index-- > 0;) {
        if (wires[index].IsNear(world_pos, tolerance)) {
            m_Schematic.SelectWire(index);
            return;
        }
    }
}

// A drag belongs to the selected element, so it ends with the selection
void EditorWindow::ClearSelection() {
    EndDrag();
    m_Schematic.ClearSelection();
}

// While dragging, wires are rebuilt from the snapshot every frame; the cleanup waits until the drop
void EditorWindow::EndDrag() {
    if (!m_Drag) {
        return;
    }
    m_Drag.reset();
    m_Schematic.SimplifyAllWires();
}

// During a drag only the rotation changes here; the drag rebuilds the wires from its snapshot on the next frame
void EditorWindow::RotateSelectedElement() {
    UIElement &element = *m_Schematic.GetSelectedElement();
    const std::vector<GridPoint> old_terminals = element.GetTerminals();
    element.SetRotation(NextRotation(element.GetRotation()));
    m_Schematic.MarkModified();
    if (m_Drag) {
        return;
    }
    m_Schematic.SetWires(FollowTerminals(m_Schematic.GetWires(), old_terminals, element.GetTerminals()));
    m_Schematic.SimplifyAllWires();
}

// A vertical flip is a mirror plus half a turn, so it needs no state of its own. Like a rotation, the wires follow
// the terminals, and a drag rebuilds them from its snapshot on the next frame
void EditorWindow::MirrorSelectedElement(const bool vertically) {
    UIElement &element = *m_Schematic.GetSelectedElement();
    const std::vector<GridPoint> old_terminals = element.GetTerminals();
    element.SetMirrored(!element.IsMirrored());
    if (vertically) {
        element.SetRotation(NextRotation(NextRotation(element.GetRotation())));
    }
    m_Schematic.MarkModified();
    if (m_Drag) {
        return;
    }
    m_Schematic.SetWires(FollowTerminals(m_Schematic.GetWires(), old_terminals, element.GetTerminals()));
    m_Schematic.SimplifyAllWires();
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
    const float margin = m_Zoom * PopoverMargin;
    const float right_edge = ImGui::GetWindowPos().x + ImGui::GetWindowWidth();
    const bool fits_right = max.x + margin + ImGui::GetFontSize() * PopoverWidth <= right_edge;
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
        ImGui::OpenPopup(PartPopover);
    }
    ImGui::SetNextWindowPos(m_PartPopoverAnchor, ImGuiCond_Appearing, {m_PartPopoverPivotX, 0.0f});
    if (!ImGui::BeginPopup(PartPopover)) {
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
    if (m_PlacingType || m_DrawingWires || m_Probing || !hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return;
    }
    const ImVec2 cursor_world = view.ToWorld(ImGui::GetIO().MousePos);
    SelectAt(cursor_world);
    m_ContextTarget = FindMeasurementTarget(m_Schematic.GetElements(), m_Schematic.GetWires(),
                                            m_Schematic.GetConnectivity(), cursor_world, WirePickDistance / m_Zoom);
    PlacePartPopover(view);
    ImGui::OpenPopup(ContextMenu);
}

/**
 * @brief   Draws the context menu: properties, orientation, measuring and deletion for a part, measuring and
 *          deletion for a wire, and finding a part, running and fitting the view on empty canvas.
 */
void EditorWindow::DrawContextMenu() {
    if (!ImGui::BeginPopup(ContextMenu)) {
        return;
    }
    const bool on_element = m_Schematic.GetSelectedElement() != nullptr;
    const bool on_wire = m_Schematic.GetSelectedWireIndex().has_value();
    if (on_element) {
        if (ImGui::MenuItem("Properties...", "Enter")) {
            m_PartPopoverRequested = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rotate", "R")) {
            RotateSelectedElement();
        }
        if (ImGui::MenuItem("Mirror", "M")) {
            MirrorSelectedElement(false);
        }
        if (ImGui::MenuItem("Flip", "Shift+M")) {
            MirrorSelectedElement(true);
        }
    }
    if ((on_element || on_wire) && m_ContextTarget) {
        if (on_element) {
            ImGui::Separator();
        }
        const MeasurementTarget &target = *m_ContextTarget;
        const bool measured =
            target.Node ? m_Schematic.IsVoltageMeasured(*target.Node) : m_Schematic.IsCurrentMeasured(target.Current);
        const std::string label =
            std::format("{} {}", measured ? "Stop measuring" : "Measure", GetMeasurementLabel(target));
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
        if (ImGui::MenuItem(
                std::format("Run {}", GetAnalysisName(m_Schematic.GetSimulationSettings().Selected)).c_str(), "F5")) {
            m_Controls.RunSelected();
        }
        if (ImGui::MenuItem("Fit Schematic", "Home")) {
            m_FrameMaxZoom = FitMaxZoom;
        }
    }
    ImGui::EndPopup();
}

// Global routing makes the shortcuts work while another editor window, such as Properties, has focus
void EditorWindow::HandleFileShortcuts() {
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, ImGuiInputFlags_RouteGlobal)) {
        RequestNew();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) {
        RequestOpen();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
        Save();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
        ShowFileDialog(FileAction::Save);
    }
    // Only while the editor has the focus, since text fields use Home
    if (ImGui::Shortcut(ImGuiKey_Home)) {
        m_FrameMaxZoom = FitMaxZoom;
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

void EditorWindow::RequestNew() {
    if (m_Schematic.IsModified()) {
        m_ActionToConfirm = FileAction::New;
        ImGui::OpenPopup(DiscardPopup);
    } else {
        NewSchematic();
    }
}

void EditorWindow::RequestOpen() {
    if (m_Schematic.IsModified()) {
        m_ActionToConfirm = FileAction::Open;
        ImGui::OpenPopup(DiscardPopup);
    } else {
        ShowFileDialog(FileAction::Open);
    }
}

void EditorWindow::HandleQuitRequest() {
    if (!std::exchange(m_QuitRequested, false)) {
        return;
    }
    if (m_Schematic.IsModified()) {
        m_ActionToConfirm = FileAction::Quit;
        ImGui::OpenPopup(DiscardPopup);
    } else {
        m_QuitConfirmed = true;
    }
}

void EditorWindow::HandleExampleRequest() {
    const std::optional<Example> example = std::exchange(m_RequestedExample, std::nullopt);
    if (!example) {
        return;
    }
    if (m_Schematic.IsModified()) {
        m_ExampleToOpen = example;
        m_ActionToConfirm = FileAction::Example;
        ImGui::OpenPopup(DiscardPopup);
    } else {
        OpenExample(*example);
    }
}

// Menu commands run here, inside the editor window, where their popups and file dialogs belong
void EditorWindow::HandleCommandRequest() {
    const std::optional<EditorCommand> command = std::exchange(m_RequestedCommand, std::nullopt);
    if (!command) {
        return;
    }
    switch (*command) {
    case EditorCommand::New:
        RequestNew();
        break;
    case EditorCommand::Open:
        RequestOpen();
        break;
    case EditorCommand::Save:
        Save();
        break;
    case EditorCommand::SaveAs:
        ShowFileDialog(FileAction::Save);
        break;
    case EditorCommand::ExportSchematic:
        ImGui::OpenPopup(ExportPopup);
        break;
    case EditorCommand::Undo:
        Undo();
        break;
    case EditorCommand::Redo:
        Redo();
        break;
    case EditorCommand::Rotate:
        RotateSelectedElement();
        break;
    case EditorCommand::Mirror:
    case EditorCommand::Flip:
        MirrorSelectedElement(*command == EditorCommand::Flip);
        break;
    case EditorCommand::Delete:
        // Deleting mid-drag would leave the drag pointing at a removed element
        if (!m_Drag) {
            m_Schematic.DeleteSelection();
        }
        break;
    }
}

void EditorWindow::Save() {
    if (const auto &file_path = m_Schematic.GetFilePath()) {
        SaveFile(*file_path);
    } else {
        ShowFileDialog(FileAction::Save);
    }
}

void EditorWindow::NewSchematic() {
    EndDrag();
    m_PlacingType.reset();
    m_DrawingWires = false;
    m_WireStart.reset();
    m_Schematic.Clear();
    m_Schematic.ClearMeasurements();
}

// Skipped parts are reported, and the schematic counts as modified because saving it would drop them
void EditorWindow::OpenFile(const std::filesystem::path &path) {
    const auto text = ReadTextFile(path);
    if (!text) {
        ShowFileMessages("Could not open the schematic", {text.error()});
        return;
    }
    auto loaded = LoadSchematic(*text);
    if (!loaded) {
        ShowFileMessages(std::format("Could not open {}", path.filename().string()), {loaded.error()});
        return;
    }

    NewSchematic();
    m_Schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings));
    m_Schematic.RestoreMeasurements(loaded->Measurements);
    m_Schematic.MarkSaved(path);
    m_FrameMaxZoom = DefaultZoom;
    if (!loaded->Warnings.empty()) {
        m_Schematic.MarkModified();
        ShowFileMessages(std::format("{} was opened, but some parts were changed or skipped", path.filename().string()),
                         std::move(loaded->Warnings));
    }
}

void EditorWindow::OpenExample(const Example &example) {
    auto loaded = LoadExample(example);
    if (!loaded) {
        ShowFileMessages(std::format("Could not open the example {}", example.Title), {loaded.error()});
        return;
    }

    NewSchematic();
    m_Schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings));
    m_Schematic.RestoreMeasurements(loaded->Measurements);
    m_FrameMaxZoom = DefaultZoom;
}

// Dialogs do not always add the extension, so it is added here when missing
void EditorWindow::SaveFile(std::filesystem::path path) {
    if (path.extension() != SchematicExtension) {
        path += SchematicExtension;
    }
    const auto written =
        WriteTextFile(path, SaveSchematic(m_Schematic.GetElements(), m_Schematic.GetWires(),
                                          m_Schematic.GetSimulationSettings(), m_Schematic.SaveMeasurements()));
    if (!written) {
        ShowFileMessages("Could not save the schematic", {written.error()});
        return;
    }
    m_Schematic.MarkSaved(std::move(path));
}

// The dialog runs asynchronously: its result is picked up by ProcessDialogResult() on a later frame
void EditorWindow::ShowFileDialog(const FileAction action) {
    if (m_FileDialog.IsPending()) {
        return;
    }
    m_DialogAction = action;
    const auto &file_path = m_Schematic.GetFilePath();
    if (action == FileAction::ExportSchematic) {
        // Next to the schematic and named after it, like the exported plots
        const std::string stem = file_path ? file_path->stem().string() : "circuit";
        const std::filesystem::path name = std::format("{}-schematic.svg", stem);
        m_FileDialog.ShowSave(SVGFilters, (file_path ? file_path->parent_path() / name : name).string());
        return;
    }
    std::string location = file_path ? file_path->string() : "";
    if (action == FileAction::Open) {
        m_FileDialog.ShowOpen(SchematicFilters, std::move(location));
    } else {
        m_FileDialog.ShowSave(SchematicFilters, std::move(location));
    }
}

void EditorWindow::ProcessDialogResult() {
    const std::optional<std::optional<std::filesystem::path>> result = m_FileDialog.TakeResult();
    if (!result) {
        return;
    }
    const std::optional<FileAction> action = std::exchange(m_DialogAction, std::nullopt);
    if (!*result) {
        return;
    }
    if (action == FileAction::Open) {
        OpenFile(**result);
    } else if (action == FileAction::Save) {
        SaveFile(**result);
    } else if (action == FileAction::ExportSchematic) {
        ExportSchematic(**result);
    }
}

void EditorWindow::DrawFilePopups() {
    DrawExportPopup();
    if (ImGui::BeginPopupModal(DiscardPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The schematic has unsaved changes. Discard them?");
        if (ImGui::Button("Discard")) {
            if (m_ActionToConfirm == FileAction::New) {
                NewSchematic();
            } else if (m_ActionToConfirm == FileAction::Open) {
                ShowFileDialog(FileAction::Open);
            } else if (m_ActionToConfirm == FileAction::Quit) {
                m_QuitConfirmed = true;
            } else if (m_ActionToConfirm == FileAction::Example) {
                OpenExample(*m_ExampleToOpen);
            }
            m_ActionToConfirm.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_ActionToConfirm.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal(FileMessagesPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(m_FileMessagesTitle.c_str());
        ImGui::Separator();
        for (const std::string &message : m_FileMessages) {
            ImGui::BulletText("%s", message.c_str());
        }
        if (PrimaryButton("OK")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// The image follows the editor: the symbol style chosen in the View menu, names, values and probes
void EditorWindow::DrawExportPopup() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(ExportPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextDisabled("As shown: symbols, names, values, terminal numbers and the probes of the plots");
    ImGui::Separator();
    if (ImGui::RadioButton("Light, for print", !m_ExportDark)) {
        m_ExportDark = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("As on screen", m_ExportDark)) {
        m_ExportDark = true;
    }
    ImGui::Separator();
    const bool empty = m_Schematic.GetElements().empty() && m_Schematic.GetWires().empty();
    if (empty) {
        ImGui::TextDisabled("Place some parts to export the schematic");
    }
    ImGui::BeginDisabled(empty || m_FileDialog.IsPending());
    if (PrimaryButton("Save SVG...")) {
        ShowFileDialog(FileAction::ExportSchematic);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Dialogs do not always add the extension, so it is added here when missing
void EditorWindow::ExportSchematic(std::filesystem::path path) {
    if (path.extension() != ".svg") {
        path += ".svg";
    }
    const auto svg = RenderSchematicSvg(m_Schematic, m_SymbolStyle, m_ShowTerminalNumbers, m_ExportDark);
    const auto written =
        svg ? WriteTextFile(path, *svg) : std::expected<void, std::string>(std::unexpected(svg.error()));
    if (!written) {
        ShowFileMessages("Could not export the schematic", {written.error()});
    }
}

void EditorWindow::ShowFileMessages(std::string title, std::vector<std::string> messages) {
    m_FileMessagesTitle = std::move(title);
    m_FileMessages = std::move(messages);
    ImGui::OpenPopup(FileMessagesPopup);
}

} // namespace GUI
