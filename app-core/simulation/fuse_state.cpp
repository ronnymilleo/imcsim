/**
 * @file    fuse_state.cpp
 * @brief   Tells from the results of a transient whether and when a fuse blew.
 */

#include "simulation/fuse_state.h"

#include "components/fuse.h"
#include <algorithm>
#include <cmath>

namespace Core {

namespace {

/// Below this voltage across the fuse its resistance cannot be told from the noise of the current, so the point says
/// nothing; a fuse that carries so little is not blowing anyway.
constexpr double MIN_READABLE_VOLTAGE = 1e-3;
/// Floor for the current in the division, since a blown fuse in a quiet circuit carries next to nothing.
constexpr double MIN_CURRENT = 1e-15;

} // namespace

/**
 * @brief   Finds the first point of a transient where a fuse is open.
 * @param[in] part    A fuse of the circuit that was simulated.
 * @param[in] nodes   Nodes of its two terminals.
 * @param[in] result  Transient of that circuit, with the current of the fuse.
 * @return  The time of the first point whose resistance, the voltage across the fuse over its current, is above the
 *          geometric mean of its cold and blown resistances, so a fuse that heats up and still conducts is not taken
 *          for blown; no value when the fuse stays whole, is not a fuse or the results do not carry its current.
 * @note    A fuse cannot close again, so the first such point is the instant it blew, to the resolution of the points.
 */
std::optional<double> FindFuseBlowTime(const Component &part, const std::span<const int> nodes,
                                       const Transient &result) {
    if (part.GetType() != ComponentType::FUSE) {
        return std::nullopt;
    }
    const auto current = std::ranges::find(result.Currents, part.GetName(), &ComponentTrace::Name);
    if (current == result.Currents.end()) {
        return std::nullopt;
    }
    const FuseParameters &figures = static_cast<const Fuse &>(part).GetParameters();
    const double open_resistance = std::sqrt(figures.ColdResistance * figures.BlownResistance);
    const std::vector<double> &first = result.NodeVoltages[static_cast<std::size_t>(nodes[0])];
    const std::vector<double> &second = result.NodeVoltages[static_cast<std::size_t>(nodes[1])];
    for (std::size_t index = 0; index < result.Times.size(); ++index) {
        const double voltage = std::abs(first[index] - second[index]);
        const double current_magnitude = std::max(std::abs(current->Values[index]), MIN_CURRENT);
        if (voltage >= MIN_READABLE_VOLTAGE && voltage > open_resistance * current_magnitude) {
            return result.Times[index];
        }
    }
    return std::nullopt;
}

} // namespace Core
