/**
 * @file    sensitivity_view.cpp
 * @brief   What the editor and the Output window show of a sensitivity result: the heat level of each part and the
 *          rows and texts of the table.
 */

#include "results/sensitivity_view.h"

#include "spice/spice_value.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace GUI {

namespace {

std::string FormatUnit(const std::string &unit, const std::string &prefix) {
    return unit.empty() ? prefix : std::format("{}/{}", prefix, unit);
}

/**
 * @brief   Names a part as the schematic labels it: a supply rail shows its voltage, "VCC 12V", and has no name there;
 *          the others show their own, and a block shows the element inside it that the row stands for.
 */
std::string FormatPartLabel(const Core::PartSensitivity &part) {
    if (part.Type == Core::ComponentType::VCC && part.Value) {
        return std::format("VCC {}V", Core::FormatValue(*part.Value));
    }
    return part.Element.empty() ? part.Name : std::format("{}.{}", part.Name, part.Element);
}

} // namespace

/**
 * @brief   Finds the size of the largest normalized sensitivity, which the heat scale and the shares are relative to.
 * @param[in] sensitivity  The result.
 * @return  The largest absolute normalized sensitivity in volts, or 0 when the result has no part.
 */
double GetLargestSensitivity(const Core::Sensitivity &sensitivity) {
    double largest = 0.0;
    for (const Core::PartSensitivity &part : sensitivity.Parts) {
        largest = std::max(largest, std::abs(part.Normalized));
    }
    return largest;
}

/**
 * @brief   Places a part on the logarithmic heat scale of the sensitivities.
 * @param[in] sensitivity  The result.
 * @param[in] part_name    Name of the part, such as "R1".
 * @return  1 for the part with the largest sensitivity, 0 for one SENSITIVITY_DECADES below it or with none, linear in
 *          between on a logarithmic scale, or no value for a part the result does not list.
 */
std::optional<float> FindSensitivityLevel(const Core::Sensitivity &sensitivity, const std::string_view part_name) {
    const auto found = std::ranges::find(sensitivity.Parts, part_name, &Core::PartSensitivity::Name);
    if (found == sensitivity.Parts.end()) {
        return std::nullopt;
    }
    const double largest = GetLargestSensitivity(sensitivity);
    if (found->Normalized == 0.0 || largest <= 0.0) {
        return 0.0f;
    }
    const double level =
        (std::log10(std::abs(found->Normalized)) - std::log10(largest) + SENSITIVITY_DECADES) / SENSITIVITY_DECADES;
    return static_cast<float>(std::clamp(level, 0.0, 1.0));
}

/**
 * @brief   Writes the table of a result, one row per part in the order of the result.
 * @param[in] sensitivity  The result.
 * @return  The rows, with the normalized sensitivity per 1 % of the value, which is easier to read than per 100 %.
 */
std::vector<SensitivityRow> MakeSensitivityRows(const Core::Sensitivity &sensitivity) {
    const double largest = GetLargestSensitivity(sensitivity);
    std::vector<SensitivityRow> rows;
    for (const Core::PartSensitivity &part : sensitivity.Parts) {
        SensitivityRow row;
        row.Part = FormatPartLabel(part);
        row.PartName = part.Name;
        if (part.Value) {
            row.Value = std::format("{}{}", Core::FormatValue(*part.Value), part.Unit);
        }
        row.Absolute = std::format("{}{}", Core::FormatValue(part.Absolute), FormatUnit(part.Unit, "V"));
        row.Normalized = std::format("{}V", Core::FormatValue(part.Normalized / SENSITIVITY_PERCENT));
        row.Share = largest > 0.0 ? std::abs(part.Normalized) / largest : 0.0;
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace GUI
