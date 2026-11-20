/**
 * @file    ui_flip_flop.h
 * @brief   Schematic symbol of a flip-flop.
 */

#ifndef IMCSIM_UI_FLIP_FLOP_H
#define IMCSIM_UI_FLIP_FLOP_H

#include "ui_elements/ui_element.h"
#include <vector>

namespace GUI {

/**
 * @class   UIFlipFlop
 * @brief   A flip-flop drawn as a box with its inputs on the left, its outputs on the right and SET and RESET above and
 *          below.
 * @details A D or T flip-flop has its data input at (-3, -1), CLK at (-3, 1), Q at (3, -1) and QN at (3, 1), SET at
 *          (0, -3) and RESET at (0, 3); a JK or SR flip-flop has its two data inputs at (-3, -2) and (-3, 2), CLK at
 *          (-3, 0), Q at (3, -2), QN at (3, 2), SET at (0, -4) and RESET at (0, 4).
 */
class UIFlipFlop : public UIElement {
public:
    UIFlipFlop(Core::ComponentType type, GridPoint position, Rotation rotation);
    ~UIFlipFlop() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    LocalBounds GetLocalBounds() const override;

private:
    float GetHalfHeight() const;
};

} // namespace GUI

#endif // IMCSIM_UI_FLIP_FLOP_H
