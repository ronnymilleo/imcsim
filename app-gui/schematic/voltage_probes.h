/**
 * @file    voltage_probes.h
 * @brief   Measured nodes that follow their net when the schematic is edited.
 */

#ifndef IMCSIM_VOLTAGE_PROBES_H
#define IMCSIM_VOLTAGE_PROBES_H

#include "base/helpers.h"
#include "schematic/connectivity.h"
#include <optional>
#include <vector>

namespace GUI {

/**
 * @struct  VoltageProbe
 * @brief   A measured node, kept by the points of the node because its number changes with any edit.
 * @details Node is the number the probe has in the nodes as last built, or none while the points belong to no
 *          node a plot can show, such as while the net is joined to ground; the points stay, so the probe comes
 *          back when they are a node again.
 */
struct VoltageProbe {
    std::vector<GridPoint> Points;
    std::optional<int> Node;
};

// Following
std::vector<VoltageProbe> FollowVoltageProbes(std::vector<VoltageProbe> probes, const Connectivity &connectivity);

} // namespace GUI

#endif // IMCSIM_VOLTAGE_PROBES_H
