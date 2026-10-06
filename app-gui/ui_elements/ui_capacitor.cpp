/**
 * @file    ui_capacitor.cpp
 * @brief   Schematic symbol of a capacitor.
 */

#include "ui_capacitor.h"

#include "components/capacitor.h"
#include <memory>

namespace GUI {

/**
 * @brief   Creates a capacitor placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UICapacitor::UICapacitor(const ImVec2 position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Capacitor>(), position, rotation) {
}

void UICapacitor::DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color) const {
    draw_list->AddLine(LocalToScreen(view, -1, 0), LocalToScreen(view, -0.25f, 0), color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, 0.25f, 0), LocalToScreen(view, 1, 0), color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, -0.25f, -0.8f), LocalToScreen(view, -0.25f, 0.8f), color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, 0.25f, -0.8f), LocalToScreen(view, 0.25f, 0.8f), color, LineThickness);
}

} // namespace GUI
