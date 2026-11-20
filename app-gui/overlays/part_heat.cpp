/**
 * @file    part_heat.cpp
 * @brief   What the heat color of a part reads: the largest current through its terminals or the power it absorbs.
 */

#include "overlays/part_heat.h"

#include "results/current_flow.h"
#include <algorithm>
#include <cmath>

namespace GUI {

/**
 * @brief   Whether a part has a current or power of its own to color it by.
 * @param[in] type  Kind of part.
 * @return  False for ground, net labels, initial voltages, nodesets, logic displays and subcircuit pin markers, which
 *          have none; true for the other parts.
 */
bool CarriesCurrentOrPower(const Core::ComponentType type) {
    return type != Core::ComponentType::GROUND && type != Core::ComponentType::NET_LABEL
        && type != Core::ComponentType::INITIAL_VOLTAGE && type != Core::ComponentType::NODESET
        && !Core::IsLogicDisplay(type) && !Core::IsSubcircuitPin(type);
}

/**
 * @brief   The largest current through any terminal of a part, which its heat color by current reads.
 * @param[in] element   The part.
 * @param[in] currents  The currents on display.
 * @return  The largest magnitude, in amperes; 0 when the currents name none of its terminals.
 */
double GetLargestTerminalCurrent(const UIElement &element, const std::vector<Core::ComponentCurrent> &currents) {
    double current = 0.0;
    for (std::size_t terminal = 0; terminal < element.GetTerminals().size(); ++terminal) {
        current =
            std::max(current, std::abs(GetTerminalCurrent(element.GetComponent(), terminal, currents).value_or(0.0)));
    }
    return current;
}

/**
 * @brief   The power a part absorbs, looked up by the name of the part.
 * @param[in] powers  The powers on display.
 * @param[in] name    The name of the part, or of one of its currents: a current name such as "Q1.C" belongs to part
 *                    "Q1".
 * @return  The power of the part, or none when the powers do not list it.
 */
std::optional<ComponentPower> FindPower(const std::vector<ComponentPower> &powers, const std::string &name) {
    const std::string part = name.substr(0, name.find('.'));
    const auto found = std::ranges::find(powers, part, &ComponentPower::Name);
    return found != powers.end() ? std::optional(*found) : std::nullopt;
}

} // namespace GUI
