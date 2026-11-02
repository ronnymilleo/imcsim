/**
 * @file    group_editing.cpp
 * @brief   Turns and mirrors the parts of a selection about a point, and finds that point.
 */

#include "group_editing.h"

#include <algorithm>
#include <numeric>

namespace GUI {

/**
 * @brief   Moves a part as a point transform moves the schematic around it.
 * @param[in,out] element    Part to move: its position, rotation and mirroring change.
 * @param[in] transform      What happens to the schematic.
 * @note    A mirror is a flip of the local axis of the part plus the rotation that turns that axis into the one
 *          being mirrored, so a mirror of a part turned a quarter keeps its terminals on the line they were on.
 */
void TransformElement(UIElement &element, const PointTransform &transform) {
    element.SetPosition(transform.Apply(element.GetPosition()));
    switch (transform.Type) {
    case PointTransform::Kind::TRANSLATE:
        break;
    case PointTransform::Kind::QUARTER_TURN:
        element.SetRotation(NextRotation(element.GetRotation()));
        break;
    case PointTransform::Kind::MIRROR_LEFT_RIGHT:
        element.SetMirrored(!element.IsMirrored());
        element.SetRotation(InverseRotation(element.GetRotation()));
        break;
    case PointTransform::Kind::MIRROR_TOP_BOTTOM:
        element.SetMirrored(!element.IsMirrored());
        element.SetRotation(NextRotation(NextRotation(InverseRotation(element.GetRotation()))));
        break;
    }
}

/**
 * @brief   Finds the point a selection turns or mirrors about.
 * @param[in] elements           Parts of the schematic.
 * @param[in] selected_elements  Indices of the selected parts.
 * @param[in] wires              Wires of the schematic.
 * @param[in] selected_wires     Indices of the selected wires.
 * @return  The position of the part when it is the only thing selected, so it turns in place; otherwise the middle
 *          of the box around the positions of the selected parts and the ends of the selected wires, on the grid.
 */
GridPoint FindPivot(const std::vector<std::unique_ptr<UIElement>> &elements,
                    const std::set<std::size_t> &selected_elements, const std::vector<UIWire> &wires,
                    const std::set<std::size_t> &selected_wires) {
    if (selected_elements.size() == 1 && selected_wires.empty()) {
        return elements[*selected_elements.begin()]->GetPosition();
    }
    std::vector<GridPoint> points;
    for (const std::size_t index : selected_elements) {
        points.push_back(elements[index]->GetPosition());
    }
    for (const std::size_t index : selected_wires) {
        points.push_back(wires[index].GetStart());
        points.push_back(wires[index].GetEnd());
    }
    if (points.empty()) {
        return {};
    }
    const auto [low_x, high_x] = std::ranges::minmax(points, {}, &GridPoint::X);
    const auto [low_y, high_y] = std::ranges::minmax(points, {}, &GridPoint::Y);
    return {std::midpoint(low_x.X, high_x.X), std::midpoint(low_y.Y, high_y.Y)};
}

} // namespace GUI
