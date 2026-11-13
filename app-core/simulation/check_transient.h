/**
 * @file    check_transient.h
 * @brief   Checks the settings of a transient before it runs.
 */

#ifndef IMCSIM_CHECK_TRANSIENT_H
#define IMCSIM_CHECK_TRANSIENT_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include <optional>
#include <string>

namespace Core {

/// Most time steps a transient may span, which keeps a mistyped setting from running for minutes or exhausting memory.
constexpr int MAX_TRANSIENT_POINTS = 1000000;

std::optional<std::string> CheckTransientSettings(const TransientSettings &settings);
std::optional<double> FindFinestSignalStep(const Circuit &circuit);
std::optional<std::string> CheckSourceSteps(const Circuit &circuit, const TransientSettings &settings);

} // namespace Core

#endif // IMCSIM_CHECK_TRANSIENT_H
