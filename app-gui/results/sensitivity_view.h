/**
 * @file    sensitivity_view.h
 * @brief   What the editor and the Output window show of a sensitivity result: the heat level of each part and the
 *          rows and texts of the table.
 * @details Pure functions with no ImGui, so the tests run them; the drawing is in the overlays and the result tables.
 */

#ifndef IMCSIM_SENSITIVITY_VIEW_H
#define IMCSIM_SENSITIVITY_VIEW_H

#include "simulation/simulation_results.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/// Percent of the value that the table and the legend give the normalized sensitivity for.
inline constexpr double SENSITIVITY_PERCENT = 100.0;

/// Sensitivities span decades, so the heat scale is logarithmic and shows this many below the largest one.
inline constexpr double SENSITIVITY_DECADES = 4.0;

/**
 * @struct  SensitivityRow
 * @brief   One line of the sensitivity table, written for reading.
 */
struct SensitivityRow {
    /// The part as the schematic names it, such as "R1", "VCC 12V", or "X2.R1" for the element inside a block.
    std::string Part;
    /// The name of the part in the circuit, which a click on the row selects.
    std::string PartName;
    /// Such as "4.7kOhm", or empty when the results do not carry it.
    std::string Value;
    /// Volts per unit of the value, such as "-1.1mV/Ohm".
    std::string Absolute;
    /// Volts for a 1 % change of the value, signed.
    std::string Normalized;
    /// The size of the normalized sensitivity as a share of the largest, from 0 to 1.
    double Share = 0.0;
};

// Heat colors
double GetLargestSensitivity(const Core::Sensitivity &sensitivity);
std::optional<float> FindSensitivityLevel(const Core::Sensitivity &sensitivity, std::string_view part_name);

// Table
std::vector<SensitivityRow> MakeSensitivityRows(const Core::Sensitivity &sensitivity);

} // namespace GUI

#endif // IMCSIM_SENSITIVITY_VIEW_H
