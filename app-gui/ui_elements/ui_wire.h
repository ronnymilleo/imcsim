/**
 * @file    ui_wire.h
 * @brief   Straight wire segment between two grid points of the schematic.
 */

#ifndef IMCSIM_UI_WIRE_H
#define IMCSIM_UI_WIRE_H

#include "helpers.h"
#include "imgui.h"
#include "schematic_canvas.h"

namespace GUI {

/**
 * @class   UIWire
 * @brief   A horizontal or vertical wire segment between two grid points.
 * @details Bent wires are stored as several segments. Wires carry no simulation component: they only connect
 *          the points they touch.
 */
class UIWire {
public:
    UIWire(GridPoint start, GridPoint end);

    void Draw(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const;

    bool operator==(const UIWire &) const = default;

    GridPoint GetStart() const;
    GridPoint GetEnd() const;
    bool PassesThrough(GridPoint point) const;
    bool IsNear(ImVec2 world_pos, float tolerance) const;

private:
    GridPoint m_Start;
    GridPoint m_End;
};

} // namespace GUI

#endif // IMCSIM_UI_WIRE_H
