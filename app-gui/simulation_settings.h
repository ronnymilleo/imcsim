/**
 * @file    simulation_settings.h
 * @brief   Settings of every analysis, saved with the schematic.
 */

#ifndef IMCSIM_SIMULATION_SETTINGS_H
#define IMCSIM_SIMULATION_SETTINGS_H

#include "helpers.h"
#include "parameter_step.h"
#include "simulator.h"
#include <optional>
#include <string>

namespace GUI {

/**
 * @enum    Analysis
 * @brief   The analyses imcsim runs, one at a time.
 */
enum class Analysis {
    OperatingPoint,
    Transient,
    ACSweep,
    DCSweep,
    Noise,
    TransferFunction
};

/**
 * @struct  SimulationSettings
 * @brief   Which analysis Run starts, and how each one runs: transient times, AC range, DC sweep sources and
 *          ranges, and the input source and output node of the noise and transfer function analyses.
 * @details The operating point has no settings. A sweep, noise or transfer function source that is empty, or that
 *          left the circuit, is replaced by a default when the analysis runs, so the stored name is never rewritten
 *          behind the user. The noise and transfer function outputs are kept as points of their node, as the voltage
 *          probes are, since node numbers change with any edit; one that no longer lies on a node also falls back to
 *          a default.
 */
struct SimulationSettings {
    Analysis Selected = Analysis::Transient;
    Core::TransientSettings Transient;
    Core::ACSweepSettings ACSweep;
    Core::SweepRange SweptRange;
    // Keeps its values while stepping is off
    Core::SweepRange SteppedRange{.Start = 0.0, .Stop = 5.0, .Step = 1.0};
    bool StepSource = false;
    Core::ACSweepSettings NoiseRange{.StartFrequency = 10.0, .StopFrequency = 100e3, .PointsPerDecade = 20};
    std::string NoiseSource;
    std::optional<GridPoint> NoiseOutput;
    std::string TransferFunctionSource;
    std::optional<GridPoint> TransferFunctionOutput;
    // Runs the transient, AC sweep or DC sweep once per value of a part or of the temperature
    ParameterStepSettings ParameterStep;

    bool operator==(const SimulationSettings &) const = default;
};

} // namespace GUI

#endif // IMCSIM_SIMULATION_SETTINGS_H
