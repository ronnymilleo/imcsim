/**
 * @file    logic_outputs.cpp
 * @brief   Reads the logic levels the logic outputs and 7-segment displays show from the result on display.
 */

#include "analysis/logic_outputs.h"

#include "circuit/components/logic_output.h"
#include "circuit/components/seven_segment.h"

namespace GUI {

namespace {

std::optional<LogicState> ReadTerminal(const GridPoint terminal, const double logic_level,
                                       const Connectivity &connectivity, const Core::OperatingPoint &point) {
    const std::optional<int> node = connectivity.GetNode(terminal);
    if (!node || *node < 0 || static_cast<std::size_t>(*node) >= point.NodeVoltages.size()) {
        return std::nullopt;
    }
    return ReadLogicState(point.NodeVoltages[static_cast<std::size_t>(*node)], logic_level);
}

} // namespace

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
    const double level = static_cast<const Core::LogicOutput &>(output.GetComponent()).GetParameters().LogicLevel;
    return ReadTerminal(output.GetTerminals().front(), level, connectivity, point);
}

/**
 * @brief   Reads every input of a logic display, a logic output or a 7-segment display, against its logic level.
 * @param[in] display       An element for which Core::IsLogicDisplay() is true.
 * @param[in] connectivity  The nodes of the schematic the result was made from.
 * @param[in] point         The voltages on display.
 * @return  What each terminal reads, in terminal order, none for one the result has no voltage for; nothing for an
 *          element of another kind.
 */
std::vector<std::optional<LogicState>> ReadLogicDisplay(const UIElement &display, const Connectivity &connectivity,
                                                        const Core::OperatingPoint &point) {
    const Core::Component &component = display.GetComponent();
    double level = 0.0;
    if (component.GetType() == Core::ComponentType::LOGIC_OUTPUT) {
        level = static_cast<const Core::LogicOutput &>(component).GetParameters().LogicLevel;
    } else if (component.GetType() == Core::ComponentType::SEVEN_SEGMENT) {
        level = static_cast<const Core::SevenSegmentDisplay &>(component).GetParameters().LogicLevel;
    } else {
        return {};
    }
    std::vector<std::optional<LogicState>> readings;
    for (const GridPoint terminal : display.GetTerminals()) {
        readings.push_back(ReadTerminal(terminal, level, connectivity, point));
    }
    return readings;
}

} // namespace GUI
