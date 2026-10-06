/**
 * @file    ui_vcc.cpp
 * @brief   Schematic symbol of a VCC supply rail.
 */

#include "ui_vcc.h"

#include "components/vcc.h"
#include <cfloat>
#include <cmath>
#include <memory>

namespace GUI {

namespace {

constexpr float LeadLength = 1.0f;
constexpr float BarHalfWidth = 0.6f;
constexpr const char *Label = "VCC";
// Font size and gap between bar and label, relative to the zoom (pixels per grid unit)
constexpr float LabelScale = 0.8f;
constexpr float LabelGapScale = 0.2f;
// Below this font size the label is unreadable, so it is skipped
constexpr float MinLabelSize = 6.0f;

} // namespace

/**
 * @brief   Creates a VCC supply rail placed on the grid, together with its simulation component.
 * @param[in] position  Grid position of the terminal, in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIVCC::UIVCC(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::VCC>(), position, rotation) {
}

void UIVCC::DrawTerminals(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color) const {
    draw_list->AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, 0, -LeadLength), color, LineThickness);
}

std::vector<GridPoint> UIVCC::GetLocalTerminals() const {
    return {{0, 0}};
}

LocalBounds UIVCC::GetLocalBounds() const {
    return {{-0.6f, -1.0f}, {0.6f, 0.0f}};
}

// ImGui text cannot rotate, so the label stays upright and is pushed past the bar along the rotated lead direction
void UIVCC::DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color,
                       SymbolStyle /*style*/) const {
    const ImVec2 bar_center = LocalToScreen(view, 0, -LeadLength);
    draw_list->AddLine(LocalToScreen(view, -BarHalfWidth, -LeadLength), LocalToScreen(view, BarHalfWidth, -LeadLength),
                       color, LineThickness);

    // The lead spans one grid unit, so its length on screen is the current zoom
    const ImVec2 lead = bar_center - LocalToScreen(view, 0, 0);
    const float zoom = std::hypot(lead.x, lead.y);
    const float font_size = zoom * LabelScale;
    if (font_size < MinLabelSize) {
        return;
    }

    const ImVec2 direction = lead / zoom;
    ImFont *font = ImGui::GetFont();
    const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, Label);
    const float half_extent = (std::abs(direction.x) * text_size.x + std::abs(direction.y) * text_size.y) / 2.0f;
    const ImVec2 label_center = bar_center + direction * (zoom * LabelGapScale + half_extent);
    draw_list->AddText(font, font_size, label_center - text_size / 2.0f, color, Label);
}

} // namespace GUI
