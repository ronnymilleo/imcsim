/**
 * @file    group_editing.h
 * @brief   Turns and mirrors the parts of a selection about a point, and finds that point.
 */

#ifndef IMCSIM_GROUP_EDITING_H
#define IMCSIM_GROUP_EDITING_H

#include "base/helpers.h"
#include "schematic/selection.h"
#include "schematic/wire_editing.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <memory>
#include <vector>

namespace GUI {

void TransformElement(UIElement &element, const PointTransform &transform);
GridPoint FindPivot(const std::vector<std::unique_ptr<UIElement>> &elements,
                    const std::vector<UIWire> &wires,
                    const Selection &selection);

} // namespace GUI

#endif // IMCSIM_GROUP_EDITING_H
