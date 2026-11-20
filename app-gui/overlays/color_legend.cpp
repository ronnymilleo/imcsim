/**
 * @file    color_legend.cpp
 * @brief   What the legend of the heat colors reads: the values at the ends of its bar, and what they measure.
 */

#include "overlays/color_legend.h"

#include "results/ac_sensitivity_view.h"
#include "results/current_flow.h"
#include "results/sensitivity_view.h"
#include "schematic/probing.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace GUI {

/**
 * @brief   Whether a coloring of the schematic has a legend.
 * @param[in] coloring  How the schematic is colored.
 * @return  True for voltage, current, power and sensitivity, which use the heat scale; false for plain and node colors.
 */
bool HasLegend(const WireColoring coloring) {
    return coloring == WireColoring::VOLTAGE || coloring == WireColoring::CURRENT || coloring == WireColoring::POWER
        || coloring == WireColoring::SENSITIVITY;
}

/**
 * @brief   The values at the ends of the legend of the heat scale, in the unit of what the schematic is colored by.
 * @param[in] coloring  Voltage, current or power; any other coloring reads as power.
 * @param[in] scale     The heat scale of the result on display.
 * @return  The lowest and highest voltage; or, as currents and powers are on a logarithmic scale, the largest one and
 *          the decades below it that the scale shows.
 */
LegendLabels GetHeatLegend(const WireColoring coloring, const HeatScale &scale) {
    if (coloring == WireColoring::VOLTAGE) {
        return LegendLabels{.Low = std::format("{}V", Core::FormatValue(scale.LowestVoltage)),
                            .High = std::format("{}V", Core::FormatValue(scale.HighestVoltage))};
    }
    if (coloring == WireColoring::CURRENT) {
        return LegendLabels{
            .Low = std::format("<{}A", Core::FormatValue(scale.LargestCurrent * std::pow(10.0, -CURRENT_DECADES))),
            .High = std::format("{}A", Core::FormatValue(scale.LargestCurrent))};
    }
    return LegendLabels{.Low =
                            std::format("<{}W", Core::FormatValue(scale.LargestPower * std::pow(10.0, -POWER_DECADES))),
                        .High = std::format("{}W", Core::FormatValue(scale.LargestPower))};
}

/**
 * @brief   The values at the ends of the legend of the sensitivity coloring, from the last AC sensitivity or, without
 *          one, the last DC sensitivity.
 * @param[in] results       The results, whose sensitivity the parts are colored by.
 * @param[in] connectivity  The nodes of the schematic, which name the output.
 * @return  The change of the output for 1 % of the value of a part, at the largest influence and the decades below it,
 *          titled with the output; none without a sensitivity analysis.
 */
std::optional<LegendLabels> GetSensitivityLegend(const ResultStore &results, const Connectivity &connectivity) {
    if (const auto &ac_sensitivity = results.GetACSensitivity()) {
        // The legend reads the change of the response for 1 % of the value, in percent of the response
        const std::size_t frequency_index =
            std::min(results.GetACSensitivityFrequency(), ac_sensitivity->Frequencies.size() - 1);
        const double largest_percent = GetLargestInfluence(*ac_sensitivity, frequency_index) * SENSITIVITY_PERCENT;
        return LegendLabels{
            .Low = std::format("<{} %", Core::FormatValue(largest_percent * std::pow(10.0, -AC_SENSITIVITY_DECADES))),
            .High = std::format("{} %", Core::FormatValue(largest_percent)),
            .Title = std::format("Change of {} at {}Hz per 1 %",
                                 GetVoltageLabel(connectivity, ac_sensitivity->OutputNode),
                                 Core::FormatValue(ac_sensitivity->Frequencies[frequency_index]))};
    }
    const auto &sensitivity = results.GetSensitivity();
    if (!sensitivity) {
        return std::nullopt;
    }
    // The legend reads the change for 1 % of the value, as the table does
    const double largest_per_percent = GetLargestSensitivity(*sensitivity) / SENSITIVITY_PERCENT;
    return LegendLabels{
        .Low = std::format("<{}V", Core::FormatValue(largest_per_percent * std::pow(10.0, -SENSITIVITY_DECADES))),
        .High = std::format("{}V", Core::FormatValue(largest_per_percent)),
        .Title = std::format("Change of {} per 1 %", GetVoltageLabel(connectivity, sensitivity->OutputNode))};
}

} // namespace GUI
