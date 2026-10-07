/**
 * @file    editor_window.cpp
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#include "editor_window.h"

#include "element_factory.h"
#include "node_colors.h"
#include "schematic_file.h"
#include "spice_value.h"
#include "theme.h"
#include "wire_editing.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <utility>

namespace GUI {

namespace {

// Warm tones that match the theme; selection stays amber so it is not mistaken for the salmon of node 1
constexpr ImU32 BackgroundColor = IM_COL32(18, 15, 16, 255);
constexpr ImU32 GridDotColor = IM_COL32(78, 63, 66, 255);
constexpr ImU32 ElementColor = IM_COL32(220, 220, 220, 255);
constexpr ImU32 PreviewColor = IM_COL32(236, 96, 100, 170);
constexpr ImU32 WireColor = IM_COL32(158, 192, 120, 255);
constexpr ImU32 SelectedColor = IM_COL32(255, 200, 80, 255);
constexpr float MinCanvasSize = 50.0f;
constexpr float MinZoom = 4.0f;
constexpr float MaxZoom = 200.0f;
constexpr float ZoomStep = 1.1f;
// Below this spacing the dots are thinned out, otherwise far zoom-out draws hundreds of thousands of them
constexpr float MinDotSpacing = 8.0f;
constexpr float WireCursorHalfSize = 4.0f;
// How close, in pixels, the cursor must be to a wire to select it
constexpr float WirePickDistance = 5.0f;
// Junction dot radius relative to the zoom, with a minimum so it stays visible when zoomed out
constexpr float JunctionRadiusScale = 0.15f;
constexpr float MinJunctionRadius = 2.5f;
constexpr ImU32 NodeLabelColor = IM_COL32(255, 255, 255, 255);
constexpr ImU32 VoltageLabelColor = IM_COL32(255, 220, 120, 255);
// Voltage labels sit just above and right of their point, clear of the line
constexpr ImVec2 VoltageLabelOffset = {4.0f, -16.0f};
/**
 * @struct  ToolbarItem
 * @brief   A toolbar button that starts placing a component type.
 */
struct ToolbarItem {
    const char *Label;
    Core::ComponentType Type;
};

constexpr auto ToolbarComponents = std::to_array<ToolbarItem>({
    {"Resistor", Core::ComponentType::Resistor},
    {"Capacitor", Core::ComponentType::Capacitor},
    {"Inductor", Core::ComponentType::Inductor},
    {"Diode", Core::ComponentType::Diode},
    {"Zener", Core::ComponentType::ZenerDiode},
    {"LED", Core::ComponentType::LED},
    {"NPN", Core::ComponentType::NPN},
    {"PNP", Core::ComponentType::PNP},
    {"NMOS", Core::ComponentType::NMOS},
    {"PMOS", Core::ComponentType::PMOS},
    {"Ground", Core::ComponentType::Ground},
    {"VCC", Core::ComponentType::VCC},
    {"VSource", Core::ComponentType::VoltageSource},
    {"ISource", Core::ComponentType::CurrentSource},
});

constexpr const char *DiscardPopup = "Discard changes?";
constexpr const char *FileMessagesPopup = "File messages";
// Must stay valid until the dialog callback runs, so it lives for the whole program
constexpr std::array<SDL_DialogFileFilter, 1> SchematicFilters = {{{"imcsim schematic", "imcsim"}}};

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
            draw_list->AddRectFilled(dot - dot_half_size, dot + dot_half_size, GridDotColor);
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
        const float spacing = ImGui::GetStyle().ItemSpacing.x + gap;
        if (ImGui::GetItemRectMax().x + spacing + width <= m_RightEdge) {
            ImGui::SameLine(0.0f, spacing);
        }
    }

    // Widths of the kinds of items the toolbar has, as ImGui draws them
    static float ButtonWidth(const char *label) {
        return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    }
    static float CheckWidth(const char *label) {
        return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
    }
    static float TextWidth(const char *text) { return ImGui::CalcTextSize(text).x; }

    // Space between groups, in font sizes
    static float GroupGap() { return ImGui::GetFontSize(); }

private:
    float m_RightEdge;
    bool m_First = true;
};

} // namespace

/**
 * @brief   Creates the editor for a schematic.
 * @param[in] schematic  Schematic to edit; it must outlive the window.
 */
EditorWindow::EditorWindow(Schematic &schematic) : AppWindow("Schematic", false), m_Schematic(schematic) {
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

// Window content only: AppWindow::Render() wraps it in Begin/End
void EditorWindow::Draw() {
    // File results and the quit request open popups, which must belong to this window
    ProcessDialogResult();
    HandleQuitRequest();
    HandleFileShortcuts();
    DrawToolbar();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, MinCanvasSize);
    size.y = std::max(size.y, MinCanvasSize);

    ImGui::InvisibleButton("canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    HandlePanAndZoom(origin, hovered, active);

    const ViewTransform view(origin, m_Pan, m_Zoom);
    // Selection runs before drawing so a dragged element is drawn where the cursor is this frame
    HandleSelection(view, hovered);
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(origin, origin + size, true);
    draw_list->AddRectFilled(origin, origin + size, BackgroundColor);
    DrawGrid(draw_list, view, origin, size, m_Zoom);

    DrawWires(draw_list, view);
    const auto &elements = m_Schematic.GetElements();
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const ImU32 color = index == m_Schematic.GetSelectedElementIndex() ? SelectedColor : ElementColor;
        elements[index]->Draw(draw_list, view, color, m_SymbolStyle);
    }
    DrawNodeVoltages(draw_list, view);

    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_W)) {
        StartDrawingWires();
    }
    HandlePlacement(draw_list, view, hovered);
    HandleWireDrawing(draw_list, view, hovered);

    draw_list->PopClipRect();
    DrawFilePopups();
}

// Groups of buttons wrap onto new lines when the window is narrow, instead of being cut off
void EditorWindow::DrawToolbar() {
    ToolbarRow row;
    const float gap = ToolbarRow::GroupGap();

    // File
    row.Place(ToolbarRow::ButtonWidth("New"));
    if (ImGui::Button("New")) {
        RequestNew();
    }
    row.Place(ToolbarRow::ButtonWidth("Open"));
    if (ImGui::Button("Open")) {
        RequestOpen();
    }
    row.Place(ToolbarRow::ButtonWidth("Save"));
    if (PrimaryButton("Save")) {
        Save();
    }
    row.Place(ToolbarRow::ButtonWidth("Save As"));
    if (ImGui::Button("Save As")) {
        ShowFileDialog(FileAction::Save);
    }

    // History
    row.Place(ToolbarRow::ButtonWidth("Undo"), gap);
    ImGui::BeginDisabled(!m_Schematic.CanUndo());
    if (ImGui::Button("Undo")) {
        Undo();
    }
    ImGui::EndDisabled();
    row.Place(ToolbarRow::ButtonWidth("Redo"));
    ImGui::BeginDisabled(!m_Schematic.CanRedo());
    if (ImGui::Button("Redo")) {
        Redo();
    }
    ImGui::EndDisabled();

    // Components and wires
    float component_gap = gap;
    for (const auto &[label, type] : ToolbarComponents) {
        row.Place(ToolbarRow::ButtonWidth(label), std::exchange(component_gap, 0.0f));
        if (ImGui::Button(label)) {
            StartPlacing(type);
        }
    }
    row.Place(ToolbarRow::ButtonWidth("Wire"));
    if (ImGui::Button("Wire")) {
        StartDrawingWires();
    }

    // View
    row.Place(ToolbarRow::CheckWidth("Nodes"), gap);
    ImGui::Checkbox("Nodes", &m_ShowNodes);
    row.Place(ToolbarRow::TextWidth("Symbols:"), gap);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Symbols:");
    row.Place(ToolbarRow::CheckWidth("IEC"));
    if (ImGui::RadioButton("IEC", m_SymbolStyle == SymbolStyle::IEC)) {
        m_SymbolStyle = SymbolStyle::IEC;
    }
    row.Place(ToolbarRow::CheckWidth("ANSI"));
    if (ImGui::RadioButton("ANSI", m_SymbolStyle == SymbolStyle::ANSI)) {
        m_SymbolStyle = SymbolStyle::ANSI;
    }

    // Document
    const auto &file_path = m_Schematic.GetFilePath();
    const std::string title = std::format("{}{}", file_path ? file_path->filename().string() : "Untitled",
                                          m_Schematic.IsModified() ? " *" : "");
    row.Place(ToolbarRow::TextWidth(title.c_str()), gap);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", title.c_str());
}

void EditorWindow::DrawWires(ImDrawList *draw_list, const ViewTransform &view) {
    const Connectivity &connectivity = m_Schematic.GetConnectivity();
    const auto &wires = m_Schematic.GetWires();
    for (std::size_t index = 0; index < wires.size(); ++index) {
        const UIWire &wire = wires[index];
        // Every wire end is a connection point, so it always has a node
        const int node = connectivity.GetNode(wire.GetStart()).value_or(0);
        ImU32 color = m_ShowNodes ? GetNodeColor(node) : WireColor;
        if (index == m_Schematic.GetSelectedWireIndex()) {
            color = SelectedColor;
        }
        wire.Draw(draw_list, view, color);
    }

    const float junction_radius = std::max(m_Zoom * JunctionRadiusScale, MinJunctionRadius);
    for (const GridPoint junction : connectivity.GetJunctions()) {
        const int node = connectivity.GetNode(junction).value_or(0);
        draw_list->AddCircleFilled(view.ToScreen(ToVec2(junction)), junction_radius,
                                   m_ShowNodes ? GetNodeColor(node) : WireColor);
    }

    if (!m_ShowNodes) {
        return;
    }
    for (const UIWire &wire : wires) {
        const int node = connectivity.GetNode(wire.GetStart()).value_or(0);
        const ImVec2 middle = view.ToScreen((ToVec2(wire.GetStart()) + ToVec2(wire.GetEnd())) / 2.0f);
        draw_list->AddText(middle, NodeLabelColor, std::format("{}", node).c_str());
    }
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
        draw_list->AddText(view.ToScreen(world_pos) + VoltageLabelOffset, VoltageLabelColor, text.c_str());
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

void EditorWindow::StartPlacing(const Core::ComponentType type) {
    ClearSelection();
    m_DrawingWires = false;
    m_WireStart.reset();
    m_PlacingType = type;
    m_PlacingRotation = Rotation::R0;
}

void EditorWindow::StartDrawingWires() {
    ClearSelection();
    m_PlacingType.reset();
    m_DrawingWires = true;
}

/**
 * @brief   Handles placement mode: R rotates, Esc or right click cancels, left click places and keeps the mode
 *          active so several components can be placed in a row.
 */
void EditorWindow::HandlePlacement(ImDrawList *draw_list, const ViewTransform &view, const bool hovered) {
    if (!m_PlacingType) {
        return;
    }

    // Keyboard shortcuts only apply while the editor has focus, so typing elsewhere does not trigger them
    if (ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            m_PlacingRotation = NextRotation(m_PlacingRotation);
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
    preview->Draw(draw_list, view, PreviewColor, m_SymbolStyle);
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
        draw_list->AddRect(cursor_on_screen - half_size, cursor_on_screen + half_size, PreviewColor);
        if (clicked) {
            m_WireStart = cursor;
        }
        return;
    }

    const GridPoint start = *m_WireStart;
    const GridPoint corner = m_WireVerticalFirst ? GridPoint{start.X, cursor.Y} : GridPoint{cursor.X, start.Y};
    UIWire(start, corner).Draw(draw_list, view, PreviewColor);
    UIWire(corner, cursor).Draw(draw_list, view, PreviewColor);
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
 * @brief   Handles selection mode, active while not placing or wiring: click selects an element or a wire,
 *          dragging an element moves it with its wires following, R rotates the selected element, Delete removes
 *          the selection and Esc clears it.
 */
void EditorWindow::HandleSelection(const ViewTransform &view, const bool hovered) {
    if (m_PlacingType || m_DrawingWires) {
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
        // Deleting mid-drag would leave the drag pointing at a removed element
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_Drag) {
            m_Schematic.DeleteSelection();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
    m_Schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires));
    m_Schematic.MarkSaved(path);
    if (!loaded->Warnings.empty()) {
        m_Schematic.MarkModified();
        ShowFileMessages(std::format("{} was opened, but some parts were changed or skipped", path.filename().string()),
                         std::move(loaded->Warnings));
    }
}

// Dialogs do not always add the extension, so it is added here when missing
void EditorWindow::SaveFile(std::filesystem::path path) {
    if (path.extension() != SchematicExtension) {
        path += SchematicExtension;
    }
    const auto written = WriteTextFile(path, SaveSchematic(m_Schematic.GetElements(), m_Schematic.GetWires()));
    if (!written) {
        ShowFileMessages("Could not save the schematic", {written.error()});
        return;
    }
    m_Schematic.MarkSaved(std::move(path));
}

// The dialog runs asynchronously: its result is picked up by ProcessDialogResult() on a later frame
void EditorWindow::ShowFileDialog(const FileAction action) {
    if (m_DialogAction) {
        return;
    }
    m_DialogAction = action;
    const auto &file_path = m_Schematic.GetFilePath();
    m_DialogLocation = file_path ? file_path->string() : "";
    const char *location = m_DialogLocation.empty() ? nullptr : m_DialogLocation.c_str();
    const int filter_count = static_cast<int>(SchematicFilters.size());
    // The callback owns this copy of the channel and deletes it, so the channel outlives the editor if needed
    auto *channel = new std::shared_ptr<DialogChannel>(m_DialogChannel);
    if (action == FileAction::Open) {
        SDL_ShowOpenFileDialog(HandleFileDialogResult, channel, nullptr, SchematicFilters.data(), filter_count,
                               location, false);
    } else {
        SDL_ShowSaveFileDialog(HandleFileDialogResult, channel, nullptr, SchematicFilters.data(), filter_count,
                               location);
    }
}

// SDL may call this from another thread, and after the editor is gone, so it only stores the result in the
// channel for the main thread to handle. SDL calls it exactly once per dialog
void SDLCALL EditorWindow::HandleFileDialogResult(void *userdata, const char *const *file_list, int /*filter*/) {
    const std::unique_ptr<std::shared_ptr<DialogChannel>> channel(
        static_cast<std::shared_ptr<DialogChannel> *>(userdata));
    DialogResult result;
    // A null list means an error and an empty list means the user canceled
    if (file_list != nullptr && file_list[0] != nullptr) {
        result.Path = std::filesystem::path(file_list[0]);
    }
    const std::scoped_lock lock((*channel)->Mutex);
    (*channel)->Result = std::move(result);
}

void EditorWindow::ProcessDialogResult() {
    std::optional<DialogResult> result;
    {
        const std::scoped_lock lock(m_DialogChannel->Mutex);
        result = std::exchange(m_DialogChannel->Result, std::nullopt);
    }
    if (!result) {
        return;
    }
    const std::optional<FileAction> action = std::exchange(m_DialogAction, std::nullopt);
    if (!result->Path) {
        return;
    }
    if (action == FileAction::Open) {
        OpenFile(*result->Path);
    } else if (action == FileAction::Save) {
        SaveFile(*result->Path);
    }
}

void EditorWindow::DrawFilePopups() {
    if (ImGui::BeginPopupModal(DiscardPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The schematic has unsaved changes. Discard them?");
        if (ImGui::Button("Discard")) {
            if (m_ActionToConfirm == FileAction::New) {
                NewSchematic();
            } else if (m_ActionToConfirm == FileAction::Open) {
                ShowFileDialog(FileAction::Open);
            } else if (m_ActionToConfirm == FileAction::Quit) {
                m_QuitConfirmed = true;
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

void EditorWindow::ShowFileMessages(std::string title, std::vector<std::string> messages) {
    m_FileMessagesTitle = std::move(title);
    m_FileMessages = std::move(messages);
    ImGui::OpenPopup(FileMessagesPopup);
}

} // namespace GUI
