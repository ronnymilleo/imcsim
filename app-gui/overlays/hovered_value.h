/**
 * @file    hovered_value.h
 * @brief   The text of the tooltip over a wire or a part: its value on display, the power of a part and when a fuse
 *          blew.
 */

#ifndef IMCSIM_HOVERED_VALUE_H
#define IMCSIM_HOVERED_VALUE_H

#include "overlays/displayed_state.h"
#include "schematic/connectivity.h"
#include "schematic/probing.h"
#include "ui_elements/ui_element.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

// Tooltip
std::optional<std::string> DescribeHoveredValue(const MeasurementTarget &target,
                                                const Connectivity &connectivity,
                                                const std::vector<std::unique_ptr<UIElement>> &elements,
                                                const DisplayedState &displayed);

} // namespace GUI

#endif // IMCSIM_HOVERED_VALUE_H
