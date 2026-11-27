/**
 * @file    sensitivity_view.cpp
 * @brief   What the editor and the Output window show of a sensitivity result: the heat level of each part and the
 *          rows and texts of the table.
 */

#include "results/sensitivity_view.h"

#include "results/logarithmic_level.h"
#include "simulation/worst_case.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace GUI {

namespace {

std::string FormatUnit(const std::string &unit, const std::string &prefix) {
    return unit.empty() ? prefix : std::format("{}/{}", prefix, unit);
}

std::string FormatPartTolerance(const Core::PartSensitivity &part) {
    const std::optional<double> delta = Core::GetPartWorstCaseChange(part);
    if (!delta || !part.TolerancePercent) {
        return "-";
    }
    return std::format("±{}V ({}%)", Core::FormatValue(*delta), Core::FormatValue(*part.TolerancePercent));
}

} // namespace

/**
 * @brief   Names a part as the schematic labels it: a supply rail shows its voltage, "VCC 12V", and has no name there;
 *          the others show their own, and a block shows the element inside it that the row stands for.
 * @param[in] type     Kind of the part.
 * @param[in] name     Name of the part in the circuit.
 * @param[in] element  The element inside a block that the row stands for; empty for other parts.
 * @param[in] value    Value of the part, or none when the results do not carry it.
 * @return  Such as "R1", "VCC 12V" or "X2.R1".
 */
std::string FormatSensitivityPartLabel(const Core::ComponentType type,
                                       const std::string &name,
                                       const std::string &element,
                                       const std::optional<double> value) {
    if (type == Core::ComponentType::VCC && value) {
        return std::format("VCC {}V", Core::FormatValue(*value));
    }
    return element.empty() ? name : std::format("{}.{}", name, element);
}

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
    return std::clamp(LogarithmicLevel(found->Normalized, largest, SENSITIVITY_DECADES), 0.0f, 1.0f);
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
        row.Part = FormatSensitivityPartLabel(part.Type, part.Name, part.Element, part.Value);
        row.PartName = part.Name;
        if (part.Value) {
            row.Value = std::format("{}{}", Core::FormatValue(*part.Value), part.Unit);
        }
        row.Absolute = std::format("{}{}", Core::FormatValue(part.Absolute), FormatUnit(part.Unit, "V"));
        row.Normalized = std::format("{}V", Core::FormatValue(part.Normalized / SENSITIVITY_PERCENT));
        row.Tolerance = FormatPartTolerance(part);
        row.Share = largest > 0.0 ? std::abs(part.Normalized) / largest : 0.0;
        rows.push_back(std::move(row));
    }
    return rows;
}

/**
 * @brief   Formats the analytical worst-case summary line above the DC sensitivity table.
 * @param[in] sensitivity  The result.
 * @return  Summary of EVA and RSS bounds when at least one part has a tolerance, or none otherwise.
 */
std::optional<std::string> FormatWorstCaseSummary(const Core::Sensitivity &sensitivity) {
    const Core::DCWorstCase worst_case = Core::ComputeWorstCase(sensitivity);
    if (worst_case.TolerancedPartCount == 0) {
        return std::nullopt;
    }
    const char *part_word = worst_case.TolerancedPartCount == 1 ? "part" : "parts";
    return std::format("Worst case from {} toleranced {}: ±{}V (extreme) · ±{}V (RSS)",
                       worst_case.TolerancedPartCount,
                       part_word,
                       Core::FormatValue(worst_case.ExtremeVolts),
                       Core::FormatValue(worst_case.RssVolts));
}

} // namespace GUI
