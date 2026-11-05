/**
 * @file    ui_logic_box.h
 * @brief   The box on a lead that logic inputs and logic outputs share, with the digit of their level in it.
 */

#ifndef IMCSIM_UI_LOGIC_BOX_H
#define IMCSIM_UI_LOGIC_BOX_H

#include "ui_elements/ui_element.h"
#include <memory>

namespace GUI {

/**
 * @class   UILogicBox
 * @brief   A box above its terminal, joined to it by a lead, that shows a logic level as a digit and can be lit.
 * @details Logic inputs and outputs derive from it, so the two read as a pair. A lit box is filled with the accent of
 *          the theme, as the Logic Lights effect draws a level that is 1.
 */
class UILogicBox : public UIElement {
public:
    ~UILogicBox() override = default;

    void DrawLit(SchematicCanvas &canvas, const ViewTransform &view, const char *digit) const;

protected:
    // Box in local grid units, above the terminal at the origin
    static constexpr float LEAD_LENGTH = 0.6f;
    static constexpr float BOX_HALF_WIDTH = 0.8f;
    static constexpr float BOX_TOP = -2.0f;
    static constexpr float BOX_BOTTOM = -LEAD_LENGTH;

    UILogicBox(std::unique_ptr<Core::Component> component, GridPoint position, Rotation rotation);

    // Parts of the symbol
    void DrawLead(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const;
    void DrawBox(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const;
    void DrawDigit(SchematicCanvas &canvas, const ViewTransform &view, const char *digit, ImU32 color) const;

    // Geometry
    std::vector<GridPoint> GetLocalTerminals() const override;
    GridPoint GetLocalTerminalInward(std::size_t terminal) const override;
    LocalBounds GetLocalBounds() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_LOGIC_BOX_H
