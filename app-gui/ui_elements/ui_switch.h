/**
 * @file    ui_switch.h
 * @brief   Schematic symbol of a switch and of a push button.
 */

#ifndef IMCSIM_UI_SWITCH_H
#define IMCSIM_UI_SWITCH_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UISwitch
 * @brief   Two contacts between the leads, with a lever across them that is up when the switch is open, or a bar
 *          on a stem that is raised until a push button is pressed.
 * @details The terminals are at (-2, 0) and (2, 0), like a resistor. The state is drawn, so the schematic shows
 *          whether the circuit is open, and written under the part.
 */
class UISwitch : public UIElement {
public:
    UISwitch(Core::ComponentType type, GridPoint position, Rotation rotation);
    ~UISwitch() override = default;

protected:
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    LocalBounds GetLocalBounds() const override;

private:
    bool IsPushButton() const;
    bool IsClosed() const;
};

} // namespace GUI

#endif // IMCSIM_UI_SWITCH_H
