/**
 * @file    editor_window.cpp
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#include "editor_window.h"

#include "element_factory.h"
#include "schematic_file.h"
#include "wire_editing.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace GUI {

namespace {

constexpr ImU32 BackgroundColor = IM_COL32(30, 30, 35, 255);
constexpr ImU32 GridDotColor = IM_COL32(90, 90, 100, 255);
constexpr ImU32 ElementColor = IM_COL32(220, 220, 220, 255);
constexpr ImU32 PreviewColor = IM_COL32(100, 180, 255, 160);
constexpr ImU32 WireColor = IM_COL32(120, 200, 120, 255);
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
// Debug colors for "Nodes": ground (node 0) uses the first one, the others cycle through the rest
constexpr auto NodeColors = std::to_array<ImU32>({
    IM_COL32(160, 160, 160, 255),
    IM_COL32(230, 120, 100, 255),
    IM_COL32(110, 170, 240, 255),
    IM_COL32(240, 200, 90, 255),
    IM_COL32(190, 130, 230, 255),
    IM_COL32(100, 210, 190, 255),
    IM_COL32(240, 150, 200, 255),
});

ImU32 GetNodeColor(const int node) {
    if (node == 0) {
        return NodeColors[0];
    }
    const auto cycle_length = static_cast<int>(NodeColors.size()) - 1;
    return NodeColors[1 + (node - 1) % cycle_length];
}

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
    {"Ground", Core::ComponentType::Ground},
    {"VCC", Core::ComponentType::VCC},
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

} // namespace

/**
 * @brief   Creates the editor with an empty, untitled schematic.
 */
EditorWindow::EditorWindow() : AppWindow("Schematic", false) {
}

/**
 * @brief   Returns the selected element, for editing it from another window.
 * @return  The element, or nullptr when no element is selected. Valid until the next frame of the editor.
 */
UIElement *EditorWindow::GetSelectedElement() {
    return m_SelectedElement ? m_Elements[*m_SelectedElement].get() : nullptr;
}

/**
 * @brief   Tells whether a wire is selected.
 * @return  True when the selection is a wire.
 */
bool EditorWindow::IsWireSelected() const {
    return m_SelectedWire.has_value();
}

/**
 * @brief   Returns a number that changes every time the selection changes.
 * @return  The selection version; compare it with a stored one to know whether to reload selection data.
 */
std::size_t EditorWindow::GetSelectionVersion() const {
    return m_SelectionVersion;
}

/**
 * @brief   Records that the schematic changed, so it counts as unsaved and its nodes are recomputed.
 * @note    Call it after editing an element obtained from GetSelectedElement().
 */
void EditorWindow::MarkModified() {
    m_Modified = true;
    m_Connectivity.reset();
}

/**
 * @brief   Builds the SPICE netlist of the current schematic.
 * @return  The netlist text, as handed to ngspice.
 */
std::string EditorWindow::BuildSpiceNetlist() {
    return BuildCircuit(m_Elements, GetConnectivity()).ToSpiceNetlist();
}

// Window content only: AppWindow::Render() wraps it in Begin/End
void EditorWindow::Draw() {
    // File results open popups, which must belong to this window
    ProcessDialogResult();
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
    for (std::size_t index = 0; index < m_Elements.size(); ++index) {
        const ImU32 color = index == m_SelectedElement ? SelectedColor : ElementColor;
        m_Elements[index]->Draw(draw_list, view, color, m_SymbolStyle);
    }

    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_W)) {
        StartDrawingWires();
    }
    HandlePlacement(draw_list, view, hovered);
    HandleWireDrawing(draw_list, view, hovered);

    draw_list->PopClipRect();
    DrawFilePopups();
}

void EditorWindow::DrawToolbar() {
    DrawFileButtons();
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    for (const auto &[label, type] : ToolbarComponents) {
        if (ImGui::Button(label)) {
            StartPlacing(type);
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Wire")) {
        StartDrawingWires();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    ImGui::Checkbox("Nodes", &m_ShowNodes);
    ImGui::SameLine();
    ImGui::TextUnformatted("|  Symbols:");
    ImGui::SameLine();
    if (ImGui::RadioButton("IEC", m_SymbolStyle == SymbolStyle::IEC)) {
        m_SymbolStyle = SymbolStyle::IEC;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("ANSI", m_SymbolStyle == SymbolStyle::ANSI)) {
        m_SymbolStyle = SymbolStyle::ANSI;
    }
    ImGui::SameLine();
    const std::string file_name = m_FilePath ? m_FilePath->filename().string() : "Untitled";
    ImGui::TextDisabled("|  %s%s", file_name.c_str(), m_Modified ? " *" : "");
}

void EditorWindow::DrawFileButtons() {
    if (ImGui::Button("New")) {
        RequestNew();
    }
    ImGui::SameLine();
    if (ImGui::Button("Open")) {
        RequestOpen();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        Save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As")) {
        ShowFileDialog(FileAction::Save);
    }
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
}

void EditorWindow::DrawFilePopups() {
    if (ImGui::BeginPopupModal(DiscardPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The schematic has unsaved changes. Discard them?");
        if (ImGui::Button("Discard")) {
            if (m_ActionToConfirm == FileAction::New) {
                NewSchematic();
            } else if (m_ActionToConfirm == FileAction::Open) {
                ShowFileDialog(FileAction::Open);
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
        if (ImGui::Button("OK")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorWindow::RequestNew() {
    if (m_Modified) {
        m_ActionToConfirm = FileAction::New;
        ImGui::OpenPopup(DiscardPopup);
    } else {
        NewSchematic();
    }
}

void EditorWindow::RequestOpen() {
    if (m_Modified) {
        m_ActionToConfirm = FileAction::Open;
        ImGui::OpenPopup(DiscardPopup);
    } else {
        ShowFileDialog(FileAction::Open);
    }
}

void EditorWindow::Save() {
    if (m_FilePath) {
        SaveFile(*m_FilePath);
    } else {
        ShowFileDialog(FileAction::Save);
    }
}

// The dialog runs asynchronously: its result is picked up by ProcessDialogResult() on a later frame
void EditorWindow::ShowFileDialog(const FileAction action) {
    if (m_DialogAction) {
        return;
    }
    m_DialogAction = action;
    m_DialogLocation = m_FilePath ? m_FilePath->string() : "";
    const char *location = m_DialogLocation.empty() ? nullptr : m_DialogLocation.c_str();
    const int filter_count = static_cast<int>(SchematicFilters.size());
    if (action == FileAction::Open) {
        SDL_ShowOpenFileDialog(HandleFileDialogResult, this, nullptr, SchematicFilters.data(), filter_count, location,
                               false);
    } else {
        SDL_ShowSaveFileDialog(HandleFileDialogResult, this, nullptr, SchematicFilters.data(), filter_count, location);
    }
}

// SDL may call this from another thread, so it only stores the result for the main thread to handle
void SDLCALL EditorWindow::HandleFileDialogResult(void *userdata, const char *const *file_list, int /*filter*/) {
    auto *editor = static_cast<EditorWindow *>(userdata);
    DialogResult result;
    // A null list means an error and an empty list means the user canceled
    if (file_list != nullptr && file_list[0] != nullptr) {
        result.Path = std::filesystem::path(file_list[0]);
    }
    const std::scoped_lock lock(editor->m_DialogMutex);
    editor->m_DialogResult = std::move(result);
}

void EditorWindow::ProcessDialogResult() {
    std::optional<DialogResult> result;
    {
        const std::scoped_lock lock(m_DialogMutex);
        result = std::exchange(m_DialogResult, std::nullopt);
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

void EditorWindow::NewSchematic() {
    ClearSelection();
    m_PlacingType.reset();
    m_DrawingWires = false;
    m_WireStart.reset();
    m_Elements.clear();
    m_Wires.clear();
    m_Connectivity.reset();
    m_FilePath.reset();
    m_Modified = false;
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
    m_Elements = std::move(loaded->Elements);
    m_Wires = std::move(loaded->Wires);
    m_FilePath = path;
    m_Modified = !loaded->Warnings.empty();
    if (m_Modified) {
        ShowFileMessages(std::format("{} was opened, but some parts were changed or skipped", path.filename().string()),
                         std::move(loaded->Warnings));
    }
}

// Dialogs do not always add the extension, so it is added here when missing
void EditorWindow::SaveFile(std::filesystem::path path) {
    if (path.extension() != SchematicExtension) {
        path += SchematicExtension;
    }
    const auto written = WriteTextFile(path, SaveSchematic(m_Elements, m_Wires));
    if (!written) {
        ShowFileMessages("Could not save the schematic", {written.error()});
        return;
    }
    m_FilePath = std::move(path);
    m_Modified = false;
}

void EditorWindow::ShowFileMessages(std::string title, std::vector<std::string> messages) {
    m_FileMessagesTitle = std::move(title);
    m_FileMessages = std::move(messages);
    ImGui::OpenPopup(FileMessagesPopup);
}

// Built on first use after a change and kept until MarkModified() or another change resets it
const Connectivity &EditorWindow::GetConnectivity() {
    if (!m_Connectivity) {
        m_Connectivity.emplace(m_Elements, m_Wires);
    }
    return *m_Connectivity;
}

void EditorWindow::DrawWires(ImDrawList *draw_list, const ViewTransform &view) {
    const Connectivity &connectivity = GetConnectivity();

    for (std::size_t index = 0; index < m_Wires.size(); ++index) {
        const UIWire &wire = m_Wires[index];
        // Every wire end is a connection point, so it always has a node
        const int node = connectivity.GetNode(wire.GetStart()).value_or(0);
        ImU32 color = m_ShowNodes ? GetNodeColor(node) : WireColor;
        if (index == m_SelectedWire) {
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
    for (const UIWire &wire : m_Wires) {
        const int node = connectivity.GetNode(wire.GetStart()).value_or(0);
        const ImVec2 middle = view.ToScreen((ToVec2(wire.GetStart()) + ToVec2(wire.GetEnd())) / 2.0f);
        draw_list->AddText(middle, NodeLabelColor, std::format("{}", node).c_str());
    }
}

void EditorWindow::AssignName(Core::Component &component) const {
    const std::string_view prefix = component.GetNamePrefix();
    if (prefix.empty()) {
        return;
    }
    std::vector<std::string> names;
    for (const auto &element : m_Elements) {
        names.push_back(element->GetComponent().GetName());
    }
    component.SetName(Core::NextComponentName(prefix, names));
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
        AssignName(preview->GetComponent());
        m_Elements.push_back(std::move(preview));
        MarkModified();
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
    AddWire(start, corner);
    AddWire(corner, cursor);
    if (IsTerminal(cursor)) {
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

// Zero-length segments appear when the bend lands on an end point, as with straight wires
void EditorWindow::AddWire(const GridPoint start, const GridPoint end) {
    if (start != end) {
        m_Wires.emplace_back(start, end);
        MarkModified();
    }
}

bool EditorWindow::IsTerminal(const GridPoint point) const {
    return std::ranges::any_of(
        m_Elements, [point](const auto &element) { return std::ranges::contains(element->GetTerminals(), point); });
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
        if (m_SelectedElement) {
            const UIElement &element = *m_Elements[*m_SelectedElement];
            m_Drag = ElementDrag{Snap(cursor_world), element.GetPosition(), element.GetTerminals(), m_Wires};
        }
    }

    if (m_Drag) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            UIElement &element = *m_Elements[*m_SelectedElement];
            const GridPoint position = m_Drag->StartPosition + (Snap(cursor_world) - m_Drag->StartCursor);
            if (position != element.GetPosition()) {
                element.SetPosition(position);
                MarkModified();
            }
            // Rebuilt every frame, not only on moves, so a rotation during the drag is followed too
            m_Wires = FollowTerminals(m_Drag->StartWires, m_Drag->StartTerminals, element.GetTerminals());
            m_Connectivity.reset();
        } else {
            EndDrag();
        }
    }

    if (ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_R) && m_SelectedElement) {
            RotateSelectedElement();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_Drag) {
            DeleteSelection();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ClearSelection();
        }
    }
}

// Elements win over wires, and the most recently added item wins because it is drawn on top
void EditorWindow::SelectAt(const ImVec2 world_pos) {
    ClearSelection();
    for (std::size_t index = m_Elements.size(); index-- > 0;) {
        if (m_Elements[index]->Contains(world_pos)) {
            m_SelectedElement = index;
            return;
        }
    }
    const float tolerance = WirePickDistance / m_Zoom;
    for (std::size_t index = m_Wires.size(); index-- > 0;) {
        if (m_Wires[index].IsNear(world_pos, tolerance)) {
            m_SelectedWire = index;
            return;
        }
    }
}

void EditorWindow::ClearSelection() {
    EndDrag();
    m_SelectedElement.reset();
    m_SelectedWire.reset();
    ++m_SelectionVersion;
}

// While dragging, wires are rebuilt from the snapshot every frame; the cleanup waits until the drop
void EditorWindow::EndDrag() {
    if (!m_Drag) {
        return;
    }
    m_Drag.reset();
    SimplifyAllWires();
}

// During a drag only the rotation changes here; the drag rebuilds the wires from its snapshot on the next frame
void EditorWindow::RotateSelectedElement() {
    UIElement &element = *m_Elements[*m_SelectedElement];
    const std::vector<GridPoint> old_terminals = element.GetTerminals();
    element.SetRotation(NextRotation(element.GetRotation()));
    MarkModified();
    if (m_Drag) {
        return;
    }
    m_Wires = FollowTerminals(m_Wires, old_terminals, element.GetTerminals());
    SimplifyAllWires();
}

// Wires attached to a deleted element stay in place, like in LTspice
void EditorWindow::DeleteSelection() {
    if (m_SelectedElement) {
        m_Elements.erase(m_Elements.begin() + static_cast<std::ptrdiff_t>(*m_SelectedElement));
    } else if (m_SelectedWire) {
        m_Wires.erase(m_Wires.begin() + static_cast<std::ptrdiff_t>(*m_SelectedWire));
    } else {
        return;
    }
    ClearSelection();
    MarkModified();
}

// Simplifying merges and removes wires, so wire indices are no longer valid afterwards
void EditorWindow::SimplifyAllWires() {
    m_Wires = SimplifyWires(m_Wires, CollectTerminals());
    m_SelectedWire.reset();
    m_Connectivity.reset();
}

std::vector<GridPoint> EditorWindow::CollectTerminals() const {
    std::vector<GridPoint> terminals;
    for (const auto &element : m_Elements) {
        std::ranges::copy(element->GetTerminals(), std::back_inserter(terminals));
    }
    return terminals;
}

} // namespace GUI
