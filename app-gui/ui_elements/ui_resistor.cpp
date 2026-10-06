/**
 * @file    ui_resistor.cpp
 * @brief   Schematic symbol of a resistor.
 */

#include "ui_resistor.h"

#include "components/resistor.h"
#include <memory>

namespace GUI {

/**
 * @brief   Creates a resistor placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIResistor::UIResistor(const ImVec2 position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Resistor>(), position, rotation) {
}

void UIResistor::DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color) const {
    const ImVec2 rectangle[4] = {LocalToScreen(view, -1, -0.4f), LocalToScreen(view, 1, -0.4f),
                                 LocalToScreen(view, 1, 0.4f), LocalToScreen(view, -1, 0.4f)};
    draw_list->AddPolyline(rectangle, 4, color, ImDrawFlags_Closed, LineThickness);
}

} // namespace GUI
