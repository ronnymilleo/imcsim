/**
 * @file    simulation_settings.h
 * @brief   Settings of every analysis, saved with the schematic.
 */

#ifndef IMCSIM_SIMULATION_SETTINGS_H
#define IMCSIM_SIMULATION_SETTINGS_H

#include "simulator.h"

namespace GUI {

/**
 * @enum    Analysis
 * @brief   The analyses imcsim runs, one at a time.
 */
enum class Analysis {
    OperatingPoint,
    Transient,
    ACSweep,
    DCSweep
};

/**
 * @struct  SimulationSettings
 * @brief   Which analysis Run starts, and how each one runs: transient times, AC range and DC sweep sources and
 *          ranges.
 * @details The operating point has no settings. A sweep source that is empty, or that left the circuit, is
 *          replaced by a default when the sweep runs, so the stored name is never rewritten behind the user.
 */
struct SimulationSettings {
    Analysis Selected = Analysis::Transient;
    Core::TransientSettings Transient;
    Core::ACSweepSettings ACSweep;
    Core::SweepRange SweptRange;
    // Keeps its values while stepping is off
    Core::SweepRange SteppedRange{.Start = 0.0, .Stop = 5.0, .Step = 1.0};
    bool StepSource = false;

    bool operator==(const SimulationSettings &) const = default;
};

} // namespace GUI

#endif // IMCSIM_SIMULATION_SETTINGS_H
