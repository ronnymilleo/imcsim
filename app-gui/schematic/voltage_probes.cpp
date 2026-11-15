/**
 * @file    voltage_probes.cpp
 * @brief   Measured nodes that follow their net when the schematic is edited.
 */

#include "schematic/voltage_probes.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace GUI {

/**
 * @brief   Puts each probe on the node most of its points belong to now, and gives it the points of that node.
 * @details With no such node, because its net is ground or its wires are gone, a probe waits with the points it had and
 *          comes back when they belong to a node again. Two probes that end up on one node are one.
 * @param[in] probes        The probes as they were, in the order they were added.
 * @param[in] connectivity  The nodes of the schematic as it is now.
 * @return  The probes that remain, in the same order.
 */
std::vector<VoltageProbe> FollowVoltageProbes(std::vector<VoltageProbe> probes, const Connectivity &connectivity) {
    std::set<int> taken;
    std::vector<VoltageProbe> followed;
    for (VoltageProbe &probe : probes) {
        std::map<int, int> votes;
        for (const GridPoint point : probe.Points) {
            if (const std::optional<int> node = connectivity.GetNode(point); node && *node != 0) {
                ++votes[*node];
            }
        }
        const auto best = std::ranges::max_element(votes, {}, &std::pair<const int, int>::second);
        if (best == votes.end()) {
            probe.Node.reset();
        } else if (taken.insert(best->first).second) {
            probe.Node = best->first;
            probe.Points = connectivity.GetPointsOfNode(best->first);
        } else {
            continue;
        }
        followed.push_back(std::move(probe));
    }
    return followed;
}

} // namespace GUI
