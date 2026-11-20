/**
 * @file    ui_single_terminal_element.h
 * @brief   Geometry shared by the schematic symbols with one terminal, such as ground, supplies and labels.
 */

#ifndef IMCSIM_UI_SINGLE_TERMINAL_ELEMENT_H
#define IMCSIM_UI_SINGLE_TERMINAL_ELEMENT_H

#include "ui_elements/ui_element.h"
#include <cstddef>
#include <memory>
#include <vector>

namespace GUI {

/**
 * @class   UISingleTerminalElement
 * @brief   A symbol with one terminal at its origin and a straight lead from it to the body.
 * @details Derived symbols say where the lead ends, along one local axis; the terminal, its lead and the way into the
 *          part all follow from it.
 */
class UISingleTerminalElement : public UIElement {
public:
    UISingleTerminalElement(std::unique_ptr<Core::Component> component, GridPoint position, Rotation rotation);
    ~UISingleTerminalElement() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    GridPoint GetLocalTerminalInward(std::size_t terminal) const override;

    // Helpers for derived classes
    /**
     * @brief   Tells where the lead from the terminal reaches the body of the symbol.
     * @return  A point on one local axis, in local grid units before rotation.
     */
    virtual ImVec2 GetLeadEnd() const = 0;
};

} // namespace GUI

#endif // IMCSIM_UI_SINGLE_TERMINAL_ELEMENT_H
