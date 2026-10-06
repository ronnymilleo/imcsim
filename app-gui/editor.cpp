/**
 * @file    editor.cpp
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#include "editor.h"

#include "ui_elements/ui_capacitor.h"
#include "ui_elements/ui_ground.h"
#include "ui_elements/ui_inductor.h"
#include "ui_elements/ui_resistor.h"
#include "ui_elements/ui_vcc.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr ImU32 BackgroundColor = IM_COL32(30, 30, 35, 255);
constexpr ImU32 GridDotColor = IM_COL32(90, 90, 100, 255);
constexpr ImU32 ElementColor = IM_COL32(220, 220, 220, 255);
constexpr ImU32 PreviewColor = IM_COL32(100, 180, 255, 160);
constexpr ImU32 WireColor = IM_COL32(120, 200, 120, 255);
constexpr float MinCanvasSize = 50.0f;
constexpr float MinZoom = 4.0f;
constexpr float MaxZoom = 200.0f;
constexpr float ZoomStep = 1.1f;
// Below this spacing the dots are thinned out, otherwise far zoom-out draws hundreds of thousands of them
constexpr float MinDotSpacing = 8.0f;
constexpr float WireCursorHalfSize = 4.0f;

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

std::unique_ptr<GUI::UIElement> CreateElement(const Core::ComponentType type, const GUI::GridPoint position,
                                              const GUI::Rotation rotation) {
    switch (type) {
    case Core::ComponentType::Resistor:
        return std::make_unique<GUI::UIResistor>(position, rotation);
    case Core::ComponentType::Capacitor:
        return std::make_unique<GUI::UICapacitor>(position, rotation);
    case Core::ComponentType::Inductor:
        return std::make_unique<GUI::UIInductor>(position, rotation);
    case Core::ComponentType::Ground:
        return std::make_unique<GUI::UIGround>(position, rotation);
    case Core::ComponentType::VCC:
        return std::make_unique<GUI::UIVCC>(position, rotation);
    }
    return nullptr;
}

void DrawGrid(ImDrawList *draw_list, const GUI::ViewTransform &view, const ImVec2 origin, const ImVec2 size,
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

namespace GUI {

/**
 * @brief   Draws the editor window and handles toolbar, pan, zoom, placement and wiring input. Call once per frame.
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
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(origin, origin + size, true);
    draw_list->AddRectFilled(origin, origin + size, BackgroundColor);
    DrawGrid(draw_list, view, origin, size, m_Zoom);

    for (const UIWire &wire : m_Wires) {
        wire.Draw(draw_list, view, WireColor);
    }
    for (const auto &element : m_Elements) {
        element->Draw(draw_list, view, ElementColor, m_SymbolStyle);
    }

    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_W)) {
        StartDrawingWires();
    }
    HandlePlacement(draw_list, view, hovered);
    HandleWireDrawing(draw_list, view, hovered);

    draw_list->PopClipRect();
    ImGui::End();
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

void Editor::StartPlacing(const Core::ComponentType type) {
    m_DrawingWires = false;
    m_WireStart.reset();
    m_PlacingType = type;
    m_PlacingRotation = Rotation::R0;
}

void Editor::StartDrawingWires() {
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
        m_Elements.push_back(std::move(preview));
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
    }
}

bool Editor::IsTerminal(const GridPoint point) const {
    return std::ranges::any_of(
        m_Elements, [point](const auto &element) { return std::ranges::contains(element->GetTerminals(), point); });
}

} // namespace GUI
