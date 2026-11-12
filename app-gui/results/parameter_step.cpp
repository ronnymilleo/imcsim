/**
 * @file    parameter_step.cpp
 * @brief   The results of an analysis run once per value of a stepped parameter, and how they are labeled and joined.
 */

#include "results/parameter_step.h"

#include "spice/spice_value.h"
#include <algorithm>
#include <format>
#include <limits>
#include <utility>

namespace GUI {

/**
 * @brief   Writes one value of a stepped parameter, as the labels of its curves show it.
 * @param[in] parameter  Name of the parameter, from GetParameterLabel().
 * @param[in] value      Value of the step.
 * @param[in] unit       Unit of the value, such as "Ohm" or "°C".
 * @return  Such as "R1=2.2kOhm" or "T=85°C"; for a Monte Carlo run, "Nominal" or "Run 17".
 */
std::string FormatStepValue(const std::string &parameter, const double value, const std::string &unit) {
    if (parameter == MONTE_CARLO_PARAMETER) {
        return value == 0.0 ? "Nominal" : std::format("Run {}", static_cast<int>(value));
    }
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

/**
 * @brief   Lays the operating points of a stepped run out as a sweep of the parameter, so each node voltage and each
 *          current reads against the step value, as a DC sweep reads against its source.
 * @param[in] family  One operating point per step, all of the same circuit, so the first one names the nodes and the
 *                    currents.
 * @return  One curve with a value per step, the parameter as the swept source and the step values as the X axis;
 *          a node or current a step lacks reads NaN there.
 */
Core::DCSweep JoinOperatingPointSteps(const StepFamily<Core::OperatingPoint> &family) {
    Core::DCSweep sweep{.SweptSource = family.Parameter,
                        .SweptUnit = family.Unit,
                        .SweptValues = family.Values,
                        .SteppedSource = "",
                        .SteppedUnit = "",
                        .Curves = {}};
    if (family.Results.empty()) {
        return sweep;
    }
    const Core::OperatingPoint &first = family.Results.front();
    const std::size_t step_count = family.Results.size();
    const double missing = std::numeric_limits<double>::quiet_NaN();
    Core::DCSweepCurve curve;
    curve.NodeVoltages.assign(first.NodeVoltages.size(), std::vector<double>(step_count, missing));
    for (const Core::ComponentCurrent &current : first.Currents) {
        curve.Currents.push_back({.Name = current.Name, .Values = std::vector<double>(step_count, missing)});
    }
    for (std::size_t step = 0; step < step_count; ++step) {
        const Core::OperatingPoint &point = family.Results[step];
        for (std::size_t node = 0; node < curve.NodeVoltages.size() && node < point.NodeVoltages.size(); ++node) {
            curve.NodeVoltages[node][step] = point.NodeVoltages[node];
        }
        for (Core::ComponentTrace &trace : curve.Currents) {
            const auto found = std::ranges::find(point.Currents, trace.Name, &Core::ComponentCurrent::Name);
            if (found != point.Currents.end()) {
                trace.Values[step] = found->Current;
            }
        }
    }
    sweep.Curves.push_back(std::move(curve));
    return sweep;
}

} // namespace GUI
