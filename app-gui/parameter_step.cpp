/**
 * @file    parameter_step.cpp
 * @brief   Settings and values of a stepped parameter, and the results of an analysis run once per value.
 */

#include "parameter_step.h"

#include "spice_value.h"
#include <cmath>
#include <format>

namespace GUI {

namespace {

// Separators between the values of a list: commas, semicolons and blanks, in any mix
bool IsListSeparator(const char character) {
    return character == ',' || character == ';' || character == ' ' || character == '\t';
}

std::expected<std::vector<double>, std::string> ParseList(const std::string &list, const std::string &unit) {
    std::vector<double> values;
    std::size_t position = 0;
    while (position < list.size()) {
        if (IsListSeparator(list[position])) {
            ++position;
            continue;
        }
        std::size_t end = position;
        while (end < list.size() && !IsListSeparator(list[end])) {
            ++end;
        }
        const std::string token = list.substr(position, end - position);
        const std::optional<double> value = Core::ParseValue(token, unit);
        if (!value) {
            return std::unexpected(std::format("\"{}\" is not a value", token));
        }
        values.push_back(*value);
        position = end;
    }
    if (values.empty()) {
        return std::unexpected("Type the values to step through, such as 1k, 2.2k, 4.7k");
    }
    return values;
}

std::expected<std::vector<double>, std::string> ListRange(const ParameterStepSettings &settings) {
    if (settings.Step == 0.0 || (settings.Stop - settings.Start) / settings.Step < 0.0) {
        return std::unexpected("The step must go from the start value toward the stop value");
    }
    // Values from start to stop, both included, as the DC sweep counts them
    const double count = std::floor((settings.Stop - settings.Start) / settings.Step + 1e-9) + 1.0;
    if (count > static_cast<double>(MaxParameterSteps)) {
        return std::unexpected(
            std::format("The step is too small: it would take more than {} runs", MaxParameterSteps));
    }
    std::vector<double> values;
    for (int index = 0; index < static_cast<int>(count); ++index) {
        values.push_back(settings.Start + index * settings.Step);
    }
    return values;
}

} // namespace

/**
 * @brief   Works out the values a stepped parameter takes, one run each.
 * @param[in] settings  The range or the list of values.
 * @param[in] unit      Unit a typed list may follow its values with, such as "Ohm".
 * @return  The values in order, or why they cannot be stepped through: a list entry that is not a value, a step
 *          that goes the wrong way, or more than MaxParameterSteps values.
 * @note    Whether a value suits the parameter, such as a positive resistance, is checked when the analysis runs.
 */
std::expected<std::vector<double>, std::string> ListStepValues(const ParameterStepSettings &settings,
                                                               const std::string &unit) {
    auto values = settings.UsesList ? ParseList(settings.List, unit) : ListRange(settings);
    if (values && values->size() > MaxParameterSteps) {
        return std::unexpected(std::format("Step through at most {} values", MaxParameterSteps));
    }
    return values;
}

/**
 * @brief   Names the stepped parameter, as plots and messages show it.
 * @param[in] settings  Settings of the step.
 * @return  The part name, or "T" for the temperature.
 */
std::string GetParameterLabel(const ParameterStepSettings &settings) {
    return settings.Part.empty() ? "T" : settings.Part;
}

/**
 * @brief   Writes one value of a stepped parameter, as the labels of its curves show it.
 * @param[in] parameter  Name of the parameter, from GetParameterLabel().
 * @param[in] value      Value of the step.
 * @param[in] unit       Unit of the value, such as "Ohm" or "°C".
 * @return  Such as "R1=2.2kOhm" or "T=85°C".
 */
std::string FormatStepValue(const std::string &parameter, const double value, const std::string &unit) {
    return std::format("{}={}{}", parameter, Core::FormatValue(value), unit);
}

/**
 * @brief   Joins the DC sweeps of a stepped run into one family of curves, as a second stepped source makes.
 * @param[in] family  One sweep per step, each with a single curve; every run sweeps the same source over the same
 *                    values, so the first one gives the X axis.
 * @return  The sweep of the first step with one curve per step, each carrying its step value, and the parameter as
 *          the stepped source.
 */
Core::DCSweep JoinDCSweepSteps(StepFamily<Core::DCSweep> family) {
    std::vector<Core::DCSweepCurve> curves;
    for (std::size_t index = 0; index < family.Results.size(); ++index) {
        Core::DCSweepCurve curve = std::move(family.Results[index].Curves.front());
        curve.StepValue = family.Values[index];
        curves.push_back(std::move(curve));
    }
    Core::DCSweep joined = std::move(family.Results.front());
    joined.SteppedSource = family.Parameter;
    joined.SteppedUnit = family.Unit;
    joined.Curves = std::move(curves);
    return joined;
}

} // namespace GUI
