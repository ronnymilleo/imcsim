/**
 * @file    logic_outputs.cpp
 * @brief   Reads the logic level a logic output shows from the result on display.
 */

#include "analysis/logic_outputs.h"

#include "components/logic_output.h"

namespace GUI {

/**
 * @brief   Reads the node of a logic output as a gate would, against the output's logic level.
 * @param[in] output        A logic output of the schematic.
 * @param[in] connectivity  The nodes of the schematic the result was made from.
 * @param[in] point         The voltages on display: an operating point, or a transient at the instant shown.
 * @return  0, 1 or unknown; none when the result has no voltage for its node, as for an output wired to nothing that
 *          was simulated.
 */
std::optional<LogicState> ReadLogicOutput(const UIElement &output, const Connectivity &connectivity,
                                          const Core::OperatingPoint &point) {
    const std::optional<int> node = connectivity.GetNode(output.GetTerminals().front());
    if (!node || *node < 0 || static_cast<std::size_t>(*node) >= point.NodeVoltages.size()) {
        return std::nullopt;
    }
    const double level = static_cast<const Core::LogicOutput &>(output.GetComponent()).GetParameters().LogicLevel;
    return ReadLogicState(point.NodeVoltages[static_cast<std::size_t>(*node)], level);
}

} // namespace GUI
