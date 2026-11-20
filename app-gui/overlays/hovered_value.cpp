/**
 * @file    hovered_value.cpp
 * @brief   The text of the tooltip over a wire or a part: its value on display, the power of a part and when a fuse
 *          blew.
 */

#include "overlays/hovered_value.h"

#include "overlays/part_heat.h"
#include "results/fuse_readings.h"
#include "spice/spice_value.h"
#include <format>

namespace GUI {

namespace {

/**
 * @brief   The lines a part adds under its current: the power it absorbs, and when it blew for a fuse open at the
 *          instant on display.
 */
std::string DescribePart(const std::string &current_name,
                         const std::vector<std::unique_ptr<UIElement>> &elements,
                         const DisplayedState &displayed) {
    std::string text;
    if (const std::optional<ComponentPower> power = FindPower(displayed.Powers, current_name)) {
        text += std::format("\nP({}) = {}W", power->Name, Core::FormatValue(power->Power));
    }
    for (std::size_t index = 0; index < elements.size() && index < displayed.FuseBlowTimes.size(); ++index) {
        const std::optional<double> blow_time = displayed.FuseBlowTimes[index];
        if (blow_time && elements[index]->GetComponent().GetName() == current_name
            && IsFuseBlownAt(blow_time, displayed.Time)) {
            text += std::format("\n{} blew at t = {}s", current_name, Core::FormatValue(*blow_time));
        }
    }
    return text;
}

} // namespace

/**
 * @brief   Describes the value on display of the wire or the part under the cursor.
 * @param[in] target        What is under the cursor.
 * @param[in] connectivity  The nodes of the schematic, for the names of nets.
 * @param[in] elements      The parts of the schematic, for the fuses.
 * @param[in] displayed     The values on display.
 * @return  The text of the tooltip, such as "I(R1) = 2mA" with "P(R1) = 4mW" under it, or none for a value the result
 *          lacks.
 */
std::optional<std::string> DescribeHoveredValue(const MeasurementTarget &target,
                                                const Connectivity &connectivity,
                                                const std::vector<std::unique_ptr<UIElement>> &elements,
                                                const DisplayedState &displayed) {
    if (!FindMeasuredValue(target, displayed.Point)) {
        return std::nullopt;
    }
    std::string text = DescribeMeasurement(target, connectivity, &displayed.Point);
    if (!target.Node) {
        text += DescribePart(target.Current, elements, displayed);
    }
    return text;
}

} // namespace GUI
