/**
 * @file    ui_transistor.h
 * @brief   Geometry and drawing shared by the schematic symbols of three-terminal transistors.
 */

#ifndef IMCSIM_UI_TRANSISTOR_H
#define IMCSIM_UI_TRANSISTOR_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UITransistor
 * @brief   A transistor symbol with its control terminal on the left and the other two on the right.
 * @details Terminals, in component order: collector or drain at (1, -2), base or gate at (-2, 0), and emitter or
 *          source at (1, 2). Leads run from each terminal one unit toward the body, so derived symbols draw between
 *          (-1, 0), (1, -1) and (1, 1). The name and model are labeled to the right of the body.
 */
class UITransistor : public UIElement {
public:
    UITransistor(std::unique_ptr<Core::Component> component, GridPoint position, Rotation rotation);
    ~UITransistor() override = default;

protected:
    void DrawTerminals(ImDrawList *draw_list, const ViewTransform &view, ImU32 color) const override;
    void DrawLabels(ImDrawList *draw_list, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    LocalBounds GetLocalBounds() const override;

    // Helpers for derived classes
    virtual const char *GetModelName() const = 0;
    void DrawArrowHead(ImDrawList *draw_list, const ViewTransform &view, ImVec2 tip, ImVec2 direction,
                       ImU32 color) const;
};

} // namespace GUI

#endif // IMCSIM_UI_TRANSISTOR_H
