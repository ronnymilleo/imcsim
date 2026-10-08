/**
 * @file    ui_controlled_source.h
 * @brief   Schematic symbol of a controlled source: VCVS, VCCS, CCCS or CCVS.
 */

#ifndef IMCSIM_UI_CONTROLLED_SOURCE_H
#define IMCSIM_UI_CONTROLLED_SOURCE_H

#include "components/controlled_source.h"
#include "ui_element.h"

namespace GUI {

/**
 * @class   UIControlledSource
 * @brief   A diamond, the usual mark of a dependent source, with + and - inside when it sets a voltage and an
 *          arrow when it sets a current.
 * @details The output runs vertically: terminal 1 at (0, -2) on top and terminal 2 at (0, 2) below. A
 *          voltage-controlled source also has its control terminals on the left, + at (-2, -1) and - at (-2, 1).
 *          The name and the gain are labeled to the right; a current-controlled source adds the current it follows.
 */
class UIControlledSource : public UIElement {
public:
    UIControlledSource(Core::ComponentType type, GridPoint position, Rotation rotation);
    ~UIControlledSource() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    LocalBounds GetLocalBounds() const override;

private:
    const Core::ControlledSource &GetSource() const;
    void DrawSign(SchematicCanvas &canvas, const ViewTransform &view, ImVec2 center, bool plus, ImU32 color) const;
};

} // namespace GUI

#endif // IMCSIM_UI_CONTROLLED_SOURCE_H
