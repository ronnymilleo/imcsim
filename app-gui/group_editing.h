/**
 * @file    group_editing.h
 * @brief   Turns and mirrors the parts of a selection about a point, and finds that point.
 */

#ifndef IMCSIM_GROUP_EDITING_H
#define IMCSIM_GROUP_EDITING_H

#include "helpers.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include "wire_editing.h"
#include <cstddef>
#include <memory>
#include <set>
#include <vector>

namespace GUI {

void TransformElement(UIElement &element, const PointTransform &transform);
GridPoint FindPivot(const std::vector<std::unique_ptr<UIElement>> &elements,
                    const std::set<std::size_t> &selected_elements, const std::vector<UIWire> &wires,
                    const std::set<std::size_t> &selected_wires);

} // namespace GUI

#endif // IMCSIM_GROUP_EDITING_H
