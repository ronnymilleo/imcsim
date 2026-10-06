/**
 * @file    editor.cpp
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#include "editor.h"

#include "spice_value.h"
#include "ui_elements/ui_capacitor.h"
#include "ui_elements/ui_ground.h"
#include "ui_elements/ui_inductor.h"
#include "ui_elements/ui_resistor.h"
#include "ui_elements/ui_vcc.h"
#include "wire_editing.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <iterator>
#include <string>
#include <string_view>

namespace GUI {

namespace {

constexpr ImU32 BackgroundColor = IM_COL32(30, 30, 35, 255);
constexpr ImU32 GridDotColor = IM_COL32(90, 90, 100, 255);
constexpr ImU32 ElementColor = IM_COL32(220, 220, 220, 255);
constexpr ImU32 PreviewColor = IM_COL32(100, 180, 255, 160);
constexpr ImU32 WireColor = IM_COL32(120, 200, 120, 255);
constexpr ImU32 SelectedColor = IM_COL32(255, 200, 80, 255);
constexpr ImVec4 ErrorTextColor = {1.0f, 0.4f, 0.4f, 1.0f};
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

std::unique_ptr<UIElement> CreateElement(const Core::ComponentType type, const GridPoint position,
                                         const Rotation rotation) {
    switch (type) {
    case Core::ComponentType::Resistor:
        return std::make_unique<UIResistor>(position, rotation);
    case Core::ComponentType::Capacitor:
        return std::make_unique<UICapacitor>(position, rotation);
    case Core::ComponentType::Inductor:
        return std::make_unique<UIInductor>(position, rotation);
    case Core::ComponentType::Ground:
        return std::make_unique<UIGround>(position, rotation);
    case Core::ComponentType::VCC:
        return std::make_unique<UIVCC>(position, rotation);
    }
    return nullptr;
}

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
 * @brief   Draws the editor window and handles toolbar, pan, zoom, selection, placement and wiring input.
 *          Call once per frame.
 */
void Editor::Draw() {
    ImGui::Begin("Schematic");
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
    ImGui::End();

    DrawPropertiesWindow();
    if (m_ShowNetlist) {
        DrawNetlistWindow();
    }
}

void Editor::DrawToolbar() {
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
    ImGui::Checkbox("Netlist", &m_ShowNetlist);
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
}

void Editor::DrawWires(ImDrawList *draw_list, const ViewTransform &view) {
    if (!m_Connectivity) {
        m_Connectivity.emplace(m_Elements, m_Wires);
    }

    for (std::size_t index = 0; index < m_Wires.size(); ++index) {
        const UIWire &wire = m_Wires[index];
        // Every wire end is a connection point, so it always has a node
        const int node = m_Connectivity->GetNode(wire.GetStart()).value_or(0);
        ImU32 color = m_ShowNodes ? GetNodeColor(node) : WireColor;
        if (index == m_SelectedWire) {
            color = SelectedColor;
        }
        wire.Draw(draw_list, view, color);
    }

    const float junction_radius = std::max(m_Zoom * JunctionRadiusScale, MinJunctionRadius);
    for (const GridPoint junction : m_Connectivity->GetJunctions()) {
        const int node = m_Connectivity->GetNode(junction).value_or(0);
        draw_list->AddCircleFilled(view.ToScreen(ToVec2(junction)), junction_radius,
                                   m_ShowNodes ? GetNodeColor(node) : WireColor);
    }

    if (!m_ShowNodes) {
        return;
    }
    for (const UIWire &wire : m_Wires) {
        const int node = m_Connectivity->GetNode(wire.GetStart()).value_or(0);
        const ImVec2 middle = view.ToScreen((ToVec2(wire.GetStart()) + ToVec2(wire.GetEnd())) / 2.0f);
        draw_list->AddText(middle, NodeLabelColor, std::format("{}", node).c_str());
    }
}

// Shows the SPICE netlist that will be handed to ngspice
void Editor::DrawNetlistWindow() {
    if (!ImGui::Begin("Netlist", &m_ShowNetlist)) {
        ImGui::End();
        return;
    }
    if (!m_Connectivity) {
        m_Connectivity.emplace(m_Elements, m_Wires);
    }

    const std::string netlist = BuildCircuit(m_Elements, *m_Connectivity).ToSpiceNetlist();
    if (ImGui::Button("Copy")) {
        ImGui::SetClipboardText(netlist.c_str());
    }
    ImGui::Separator();
    ImGui::TextUnformatted(netlist.c_str());
    ImGui::End();
}

// Values are typed with SPICE suffixes and applied as soon as they are valid: the schematic window is handled
// before this one, so a click on the canvas would change the selection before a deferred edit was applied
void Editor::DrawPropertiesWindow() {
    if (!ImGui::Begin("Properties")) {
        ImGui::End();
        return;
    }
    if (m_SelectedWire) {
        ImGui::TextUnformatted("Wire");
        ImGui::End();
        return;
    }
    if (!m_SelectedElement) {
        ImGui::TextDisabled("Select a component to edit it");
        ImGui::End();
        return;
    }

    Core::Component &component = m_Elements[*m_SelectedElement]->GetComponent();
    ImGui::TextUnformatted(component.GetTypeName());
    if (!component.GetName().empty()) {
        ImGui::TextUnformatted(std::format("Name: {}", component.GetName()).c_str());
    }
    if (!component.HasValue()) {
        ImGui::End();
        return;
    }

    if (m_ValueTextElement != m_SelectedElement) {
        LoadValueText();
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    if (ImGui::InputText("##value", m_ValueText.data(), m_ValueText.size())) {
        const std::optional<double> value = Core::ParseValue(m_ValueText.data(), component.GetUnit());
        m_ValueTextInvalid = !value || !component.IsValidValue(*value);
        if (!m_ValueTextInvalid) {
            component.SetValue(*value);
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && !m_ValueTextInvalid) {
        // Rewrite the text in canonical form, so "4700" becomes "4.7k"
        LoadValueText();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(component.GetUnit());
    if (m_ValueTextInvalid) {
        ImGui::TextColored(ErrorTextColor, "Invalid value");
    }
    ImGui::TextDisabled("Suffixes: T G M k m u n p f (case sensitive)");
    ImGui::End();
}

void Editor::LoadValueText() {
    const Core::Component &component = m_Elements[*m_SelectedElement]->GetComponent();
    const std::string text = Core::FormatValue(component.GetValue());
    m_ValueText.fill('\0');
    text.copy(m_ValueText.data(), m_ValueText.size() - 1);
    m_ValueTextElement = m_SelectedElement;
    m_ValueTextInvalid = false;
}

// The next free number for the prefix, so names stay unique after deletions: R1, R2, R3...
void Editor::AssignName(Core::Component &component) const {
    const std::string_view prefix = component.GetNamePrefix();
    if (prefix.empty()) {
        return;
    }
    int highest = 0;
    for (const auto &element : m_Elements) {
        const std::string &name = element->GetComponent().GetName();
        if (!name.starts_with(prefix)) {
            continue;
        }
        int number = 0;
        const char *digits_end = name.data() + name.size();
        const auto [end, error] = std::from_chars(name.data() + prefix.size(), digits_end, number);
        if (error == std::errc{} && end == digits_end) {
            highest = std::max(highest, number);
        }
    }
    component.SetName(std::format("{}{}", prefix, highest + 1));
}

void Editor::StartPlacing(const Core::ComponentType type) {
    ClearSelection();
    m_DrawingWires = false;
    m_WireStart.reset();
    m_PlacingType = type;
    m_PlacingRotation = Rotation::R0;
}

void Editor::StartDrawingWires() {
    ClearSelection();
    m_PlacingType.reset();
    m_DrawingWires = true;
}

void Editor::HandlePanAndZoom(const ImVec2 origin, const bool hovered, const bool active) {
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
void Editor::HandlePlacement(ImDrawList *draw_list, const ViewTransform &view, const bool hovered) {
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
        m_Connectivity.reset();
    }
}

/**
 * @brief   Handles wire mode: the first click starts a wire, each following click adds an L-shaped bend up to the
 *          cursor and continues from there. F flips the bend, and the wire ends on a terminal, on a second click
 *          at the same point, or with Esc or right click. Esc or right click with no wire in progress leaves
 *          the mode.
 */
void Editor::HandleWireDrawing(ImDrawList *draw_list, const ViewTransform &view, const bool hovered) {
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
void Editor::StopWire() {
    if (m_WireStart) {
        m_WireStart.reset();
    } else {
        m_DrawingWires = false;
    }
}

// Zero-length segments appear when the bend lands on an end point, as with straight wires
void Editor::AddWire(const GridPoint start, const GridPoint end) {
    if (start != end) {
        m_Wires.emplace_back(start, end);
        m_Connectivity.reset();
    }
}

bool Editor::IsTerminal(const GridPoint point) const {
    return std::ranges::any_of(
        m_Elements, [point](const auto &element) { return std::ranges::contains(element->GetTerminals(), point); });
}

/**
 * @brief   Handles selection mode, active while not placing or wiring: click selects an element or a wire,
 *          dragging an element moves it with its wires following, R rotates the selected element, Delete removes
 *          the selection and Esc clears it.
 */
void Editor::HandleSelection(const ViewTransform &view, const bool hovered) {
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
            element.SetPosition(m_Drag->StartPosition + (Snap(cursor_world) - m_Drag->StartCursor));
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
void Editor::SelectAt(const ImVec2 world_pos) {
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

void Editor::ClearSelection() {
    EndDrag();
    m_SelectedElement.reset();
    m_SelectedWire.reset();
    // Indices are reused after a deletion, so the next selection must always reload the value text
    m_ValueTextElement.reset();
}

// While dragging, wires are rebuilt from the snapshot every frame; the cleanup waits until the drop
void Editor::EndDrag() {
    if (!m_Drag) {
        return;
    }
    m_Drag.reset();
    SimplifyAllWires();
}

// During a drag only the rotation changes here; the drag rebuilds the wires from its snapshot on the next frame
void Editor::RotateSelectedElement() {
    UIElement &element = *m_Elements[*m_SelectedElement];
    const std::vector<GridPoint> old_terminals = element.GetTerminals();
    element.SetRotation(NextRotation(element.GetRotation()));
    if (m_Drag) {
        return;
    }
    m_Wires = FollowTerminals(m_Wires, old_terminals, element.GetTerminals());
    SimplifyAllWires();
}

// Wires attached to a deleted element stay in place, like in LTspice
void Editor::DeleteSelection() {
    if (m_SelectedElement) {
        m_Elements.erase(m_Elements.begin() + static_cast<std::ptrdiff_t>(*m_SelectedElement));
    } else if (m_SelectedWire) {
        m_Wires.erase(m_Wires.begin() + static_cast<std::ptrdiff_t>(*m_SelectedWire));
    } else {
        return;
    }
    ClearSelection();
    m_Connectivity.reset();
}

// Simplifying merges and removes wires, so wire indices are no longer valid afterwards
void Editor::SimplifyAllWires() {
    m_Wires = SimplifyWires(m_Wires, CollectTerminals());
    m_SelectedWire.reset();
    m_Connectivity.reset();
}

std::vector<GridPoint> Editor::CollectTerminals() const {
    std::vector<GridPoint> terminals;
    for (const auto &element : m_Elements) {
        std::ranges::copy(element->GetTerminals(), std::back_inserter(terminals));
    }
    return terminals;
}

} // namespace GUI
