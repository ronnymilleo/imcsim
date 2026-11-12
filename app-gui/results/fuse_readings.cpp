/**
 * @file    fuse_readings.cpp
 * @brief   Finds which fuses of the schematic have blown in a transient, and when.
 */

#include "results/fuse_readings.h"

#include "simulation/fuse_state.h"
#include <algorithm>
#include <array>

namespace GUI {

/**
 * @brief   Finds the instant each fuse of the schematic blew in a transient.
 * @param[in] elements      Elements of the schematic that was simulated.
 * @param[in] connectivity  Nodes of their terminals.
 * @param[in] transient     Result to read, whole or still growing.
 * @return  One entry per element: the time its fuse blew, or no value for a fuse that is whole so far, for a fuse
 *          whose terminals are not on a node, and for every other part.
 */
std::vector<std::optional<double>> FindFuseBlowTimes(const std::vector<std::unique_ptr<UIElement>> &elements,
                                                     const Connectivity &connectivity,
                                                     const Core::Transient &transient) {
    std::vector<std::optional<double>> blow_times(elements.size());
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const Core::Component &component = elements[index]->GetComponent();
        if (component.GetType() != Core::ComponentType::FUSE) {
            continue;
        }
        const std::vector<GridPoint> terminals = elements[index]->GetTerminals();
        const std::optional<int> first = connectivity.GetNode(terminals[0]);
        const std::optional<int> second = connectivity.GetNode(terminals[1]);
        if (!first || !second || static_cast<std::size_t>(std::max(*first, *second)) >= transient.NodeVoltages.size()) {
            continue;
        }
        const std::array<int, 2> nodes = {*first, *second};
        blow_times[index] = Core::FindFuseBlowTime(component, nodes, transient);
    }
    return blow_times;
}

/**
 * @brief   Tells whether a fuse is open at the instant on display.
 * @param[in] blow_time  When the fuse blew, or no value when it has not.
 * @param[in] instant    Instant of the transient on display, or no value for an operating point.
 * @return  True when the fuse blew at or before the instant; an operating point always sees a whole fuse.
 */
bool IsFuseBlownAt(const std::optional<double> blow_time, const std::optional<double> instant) {
    return blow_time && instant && *blow_time <= *instant;
}

} // namespace GUI
