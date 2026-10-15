/**
 * @file    circuit_snapshot.cpp
 * @brief   Turns an instant of a transient into an operating point and works out what the editor colors it with.
 */

#include "circuit_snapshot.h"

#include "current_flow.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace GUI {

namespace {

// The two samples around an instant and how far the instant is from the first to the second
struct Bracket {
    std::size_t Before = 0;
    std::size_t After = 0;
    double Fraction = 0.0;
};

// Instants outside the run take its first or last sample
Bracket FindBracket(const std::vector<double> &times, const double time) {
    if (time <= times.front()) {
        return {};
    }
    if (time >= times.back()) {
        return {.Before = times.size() - 1, .After = times.size() - 1, .Fraction = 0.0};
    }
    const auto after = static_cast<std::size_t>(std::ranges::upper_bound(times, time) - times.begin());
    const std::size_t before = after - 1;
    return {.Before = before, .After = after, .Fraction = (time - times[before]) / (times[after] - times[before])};
}

// A trace shorter than the time axis reads as zero where it has no sample
double Interpolate(const std::vector<double> &values, const Bracket &bracket) {
    if (bracket.After >= values.size()) {
        return 0.0;
    }
    return std::lerp(values[bracket.Before], values[bracket.After], bracket.Fraction);
}

} // namespace

/**
 * @brief   Reads a transient at one instant, as the operating point the editor already knows how to show.
 * @param[in] transient  Result of a transient simulation.
 * @param[in] time       Instant in seconds; an instant outside the run takes its first or last sample.
 * @return  Node voltages and component currents interpolated linearly between the two samples around the instant, or
 *          an empty operating point for a transient without samples.
 */
Core::OperatingPoint SampleTransient(const Core::Transient &transient, const double time) {
    Core::OperatingPoint point;
    point.Warnings = transient.Warnings;
    if (transient.Times.empty()) {
        return point;
    }
    const Bracket bracket = FindBracket(transient.Times, time);
    for (const std::vector<double> &voltages : transient.NodeVoltages) {
        point.NodeVoltages.push_back(Interpolate(voltages, bracket));
    }
    for (const Core::ComponentTrace &current : transient.Currents) {
        point.Currents.push_back({current.Name, Interpolate(current.Values, bracket)});
    }
    return point;
}

/**
 * @brief   Works out the power every part absorbs at an operating point.
 * @param[in] elements          Parts of the schematic.
 * @param[in] connectivity      Nodes of the schematic, which number the voltages of the operating point.
 * @param[in] operating_point   Voltages and currents to read.
 * @return  One entry per part with currents in the results, named like the part: the sum over its terminals of the
 *          voltage of the terminal times the current into it. A resistor absorbs positive power and a source that
 *          delivers power negative power. Ground has no power. An op-amp has a current only at its output, so its
 *          power is the one of its output stage.
 */
std::vector<ComponentPower> ComputePowers(const std::vector<std::unique_ptr<UIElement>> &elements,
                                          const Connectivity &connectivity,
                                          const Core::OperatingPoint &operating_point) {
    std::vector<ComponentPower> powers;
    for (const auto &element : elements) {
        const Core::Component &component = element->GetComponent();
        const std::vector<GridPoint> terminals = element->GetTerminals();
        bool has_current = false;
        double power = 0.0;
        for (std::size_t index = 0; index < terminals.size(); ++index) {
            const std::optional<double> current = GetTerminalCurrent(component, index, operating_point.Currents);
            const std::optional<int> node = connectivity.GetNode(terminals[index]);
            if (!current || !node || *node < 0 ||
                static_cast<std::size_t>(*node) >= operating_point.NodeVoltages.size()) {
                continue;
            }
            has_current = true;
            power += operating_point.NodeVoltages[static_cast<std::size_t>(*node)] * *current;
        }
        if (has_current) {
            powers.push_back({component.GetName(), power});
        }
    }
    return powers;
}

/**
 * @brief   Works out the heat scale of one operating point.
 * @param[in] operating_point   Voltages and currents to scale.
 * @param[in] elements          Parts of the schematic, for their powers.
 * @param[in] connectivity      Nodes of the schematic.
 * @return  The lowest and highest node voltage, ground included, and the largest current and power in absolute value.
 */
HeatScale ScaleOf(const Core::OperatingPoint &operating_point, const std::vector<std::unique_ptr<UIElement>> &elements,
                  const Connectivity &connectivity) {
    HeatScale scale;
    if (!operating_point.NodeVoltages.empty()) {
        const auto [lowest, highest] = std::ranges::minmax(operating_point.NodeVoltages);
        scale.LowestVoltage = lowest;
        scale.HighestVoltage = highest;
    }
    scale.LargestCurrent = LargestCurrent(operating_point);
    for (const ComponentPower &power : ComputePowers(elements, connectivity, operating_point)) {
        scale.LargestPower = std::max(scale.LargestPower, std::abs(power.Power));
    }
    return scale;
}

/**
 * @brief   Works out the heat scale of a whole transient, so colors mean the same at every instant.
 * @param[in] transient     Result of a transient simulation.
 * @param[in] elements      Parts of the schematic, for their powers.
 * @param[in] connectivity  Nodes of the schematic.
 * @return  The lowest and highest node voltage, and the largest current and power in absolute value, over all the
 *          samples; an empty scale for a transient without samples.
 */
HeatScale ScaleOf(const Core::Transient &transient, const std::vector<std::unique_ptr<UIElement>> &elements,
                  const Connectivity &connectivity) {
    HeatScale scale;
    for (std::size_t index = 0; index < transient.Times.size(); ++index) {
        const HeatScale sample = ScaleOf(SampleTransient(transient, transient.Times[index]), elements, connectivity);
        if (index == 0) {
            scale = sample;
            continue;
        }
        scale.LowestVoltage = std::min(scale.LowestVoltage, sample.LowestVoltage);
        scale.HighestVoltage = std::max(scale.HighestVoltage, sample.HighestVoltage);
        scale.LargestCurrent = std::max(scale.LargestCurrent, sample.LargestCurrent);
        scale.LargestPower = std::max(scale.LargestPower, sample.LargestPower);
    }
    return scale;
}

/**
 * @brief   Returns where a node voltage falls on the heat scale the editor colors wires with.
 * @param[in] voltage  Voltage to place.
 * @param[in] scale    Scale of the circuit or of the whole simulation.
 * @return  0 at the lowest voltage and 1 at the highest, linear in between, and 0.5 when they are equal.
 */
float VoltageLevel(const double voltage, const HeatScale &scale) {
    return scale.HighestVoltage > scale.LowestVoltage
               ? static_cast<float>((voltage - scale.LowestVoltage) / (scale.HighestVoltage - scale.LowestVoltage))
               : 0.5f;
}

/**
 * @brief   Returns where a power falls on the heat scale the editor colors parts with.
 * @param[in] power  Power to place, of either sign.
 * @param[in] scale  Scale of the circuit or of the whole simulation.
 * @return  The LogarithmicLevel() of the power over PowerDecades.
 */
float PowerLevel(const double power, const HeatScale &scale) {
    return LogarithmicLevel(power, scale.LargestPower, PowerDecades);
}

} // namespace GUI
