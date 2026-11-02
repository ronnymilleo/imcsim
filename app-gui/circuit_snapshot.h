/**
 * @file    circuit_snapshot.h
 * @brief   Turns an instant of a transient into an operating point and works out what the editor colors it with.
 */

#ifndef IMCSIM_CIRCUIT_SNAPSHOT_H
#define IMCSIM_CIRCUIT_SNAPSHOT_H

#include "connectivity.h"
#include "simulator.h"
#include "ui_elements/ui_element.h"
#include <memory>
#include <string>
#include <vector>

namespace GUI {

// Powers span decades like currents, so the heat scale is logarithmic and shows this many below the largest power
inline constexpr double POWER_DECADES = 4.0;

// The scale of a long transient reads the power of this many samples at most, evenly spread, since working out the
// power of a part at a sample costs far more than reading a voltage
inline constexpr std::size_t MAX_POWER_SAMPLES = 4000;

/**
 * @struct  HeatScale
 * @brief   The range of voltages, currents and powers the heat colors of the editor are spread over.
 * @details An operating point has a scale of its own. A transient has one for the whole run, so the same value keeps
 *          the same color at every instant.
 */
struct HeatScale {
    double LowestVoltage = 0.0;
    double HighestVoltage = 0.0;
    double LargestCurrent = 0.0;
    double LargestPower = 0.0;
};

/**
 * @struct  ComponentPower
 * @brief   The power one component absorbs, positive when it dissipates power and negative when it delivers it.
 */
struct ComponentPower {
    std::string Name;
    double Power = 0.0;
};

Core::OperatingPoint SampleTransient(const Core::Transient &transient, double time);
std::vector<ComponentPower> ComputePowers(const std::vector<std::unique_ptr<UIElement>> &elements,
                                          const Connectivity &connectivity,
                                          const Core::OperatingPoint &operating_point);
HeatScale ScaleOf(const Core::OperatingPoint &operating_point, const std::vector<std::unique_ptr<UIElement>> &elements,
                  const Connectivity &connectivity);
HeatScale ScaleOf(const Core::Transient &transient, const std::vector<std::unique_ptr<UIElement>> &elements,
                  const Connectivity &connectivity);
float VoltageLevel(double voltage, const HeatScale &scale);
float PowerLevel(double power, const HeatScale &scale);

} // namespace GUI

#endif // IMCSIM_CIRCUIT_SNAPSHOT_H
