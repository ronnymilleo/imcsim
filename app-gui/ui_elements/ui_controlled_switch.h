/**
 * @file    ui_controlled_switch.h
 * @brief   Schematic symbol of a voltage-controlled switch and of a relay.
 */

#ifndef IMCSIM_UI_CONTROLLED_SWITCH_H
#define IMCSIM_UI_CONTROLLED_SWITCH_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UIControlledSwitch
 * @brief   A contact on the right with a lever, and on the left what moves it: a bar with + and - leads for a
 *          switch that follows a voltage, a coil for a relay.
 * @details The contact runs vertically, terminal 1 at (0, -2) on top and terminal 2 at (0, 2) below. The control
 *          pins are on the left, + at (-2, -1) and - at (-2, 1). A dashed line from the control to the lever shows
 *          which one moves the other. The name and the threshold, or the pull-in voltage, are labeled to the right.
 */
class UIControlledSwitch : public UIElement {
public:
    UIControlledSwitch(Core::ComponentType type, GridPoint position, Rotation rotation);
    ~UIControlledSwitch() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    LocalBounds GetLocalBounds() const override;

private:
    bool IsRelay() const;
    void DrawSign(SchematicCanvas &canvas, const ViewTransform &view, ImVec2 center, bool plus, ImU32 color) const;
    void DrawDashedLine(SchematicCanvas &canvas, const ViewTransform &view, float from_x, float to_x, float y,
                        ImU32 color) const;
};

} // namespace GUI

#endif // IMCSIM_UI_CONTROLLED_SWITCH_H
