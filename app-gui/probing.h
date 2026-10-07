/**
 * @file    probing.h
 * @brief   Finds what the Probe tool measures at a point of the schematic: a node voltage or a current.
 */

#ifndef IMCSIM_PROBING_H
#define IMCSIM_PROBING_H

#include "connectivity.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  MeasurementTarget
 * @brief   Something the plots can show: the voltage of a node or a current, named as in the results.
 * @details Exactly one is set: Node for a voltage, or Current for a current such as "R1" or "Q1.C".
 */
struct MeasurementTarget {
    std::optional<int> Node;
    std::string Current;
};

std::optional<MeasurementTarget> FindMeasurementTarget(const std::vector<std::unique_ptr<UIElement>> &elements,
                                                       const std::vector<UIWire> &wires,
                                                       const Connectivity &connectivity, ImVec2 world_pos,
                                                       float wire_tolerance);
std::string GetMeasurementLabel(const MeasurementTarget &target);

} // namespace GUI

#endif // IMCSIM_PROBING_H
