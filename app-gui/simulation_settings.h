/**
 * @file    simulation_settings.h
 * @brief   Settings of every analysis, saved with the schematic.
 */

#ifndef IMCSIM_SIMULATION_SETTINGS_H
#define IMCSIM_SIMULATION_SETTINGS_H

#include "helpers.h"
#include "parameter_step.h"
#include "simulator.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @enum    Analysis
 * @brief   The analyses imcsim runs, one at a time.
 */
enum class Analysis {
    OPERATING_POINT,
    TRANSIENT,
    AC_SWEEP,
    DC_SWEEP,
    NOISE,
    TRANSFER_FUNCTION,
    TRUTH_TABLE
};

constexpr std::size_t ANALYSIS_COUNT = 7;

// Wall seconds a live transient takes to cover its stop time, at first and at the least and most the user may set
constexpr double DEFAULT_LIVE_SECONDS = 10.0;
constexpr double MIN_LIVE_SECONDS = 2.0;
constexpr double MAX_LIVE_SECONDS = 60.0;
// A live transient goes on until it is stopped, up to an hour on the clock or the most time steps a transient may
// span, whichever comes first: ngspice keeps every point it makes and cannot drop the old ones, so its memory grows
// with the run
constexpr double LIVE_RUN_LIMIT_SECONDS = 3600.0;

/**
 * @struct  SimulationSettings
 * @brief   Which analysis Run starts, and how each one runs: transient times, AC range, DC sweep sources and
 *          ranges, the input source and output node of the noise and transfer function analyses, and the logic
 *          inputs of the truth table, and how long a live transient lasts.
 * @details The operating point has no settings. A sweep, noise or transfer function source that is empty, or that
 *          left the circuit, is replaced by a default when the analysis runs, so the stored name is never rewritten
 *          behind the user. The noise and transfer function outputs are kept as points of their node, as the voltage
 *          probes are, since node numbers change with any edit; one that no longer lies on a node also falls back to
 *          a default. The truth table reads the voltages the schematic measures, so it keeps no outputs of its own.
 */
struct SimulationSettings {
    Analysis Selected = Analysis::TRANSIENT;
    Core::TransientSettings Transient;
    // Wall seconds a live transient takes to cover the stop time, which is the stretch it shows, whatever the circuit
    double LiveSeconds = DEFAULT_LIVE_SECONDS;
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
    // Logic inputs the truth table varies; none listed, or none left in the circuit, means every logic input
    std::vector<std::string> TruthTableInputs;
    // Runs the transient, AC sweep or DC sweep once per value of a part or of the temperature
    ParameterStepSettings ParameterStep;

    bool operator==(const SimulationSettings &) const = default;
};

// Names
const char *GetAnalysisName(Analysis analysis);

// Live runs
bool IsValidLiveSeconds(double seconds);

} // namespace GUI

#endif // IMCSIM_SIMULATION_SETTINGS_H
