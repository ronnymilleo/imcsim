/**
 * @file    run_repeat.h
 * @brief   How a run of an analysis repeats: once, once per value of a stepped parameter, or once per Monte Carlo draw.
 */

#ifndef IMCSIM_RUN_REPEAT_H
#define IMCSIM_RUN_REPEAT_H

#include "circuit/circuit.h"
#include "schematic/parameter_step_settings.h"
#include "schematic/simulation_settings.h"
#include "simulation/monte_carlo.h"
#include <expected>
#include <string>
#include <variant>
#include <vector>

namespace GUI {

/**
 * @struct  StepPlan
 * @brief   The stepped parameter of a run, the unit of its values and the values, one run each.
 */
struct StepPlan {
    ParameterStepSettings Settings;
    std::string Unit;
    std::vector<double> Values;
};

/// A single run, the steps of a parameter or the draws of a Monte Carlo analysis, which exclude each other.
using RunRepeat = std::variant<std::monostate, StepPlan, Core::MonteCarloSettings>;

// Planning
[[nodiscard]] std::expected<RunRepeat, std::string> PlanRunRepeat(const SimulationSettings &settings,
                                                                  const Core::Circuit &circuit);
bool IsSingleRun(const RunRepeat &repeat);

} // namespace GUI

#endif // IMCSIM_RUN_REPEAT_H
