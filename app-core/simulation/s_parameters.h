/**
 * @file    s_parameters.h
 * @brief   The netlist lines of an S-parameter analysis, the quantities read from a reflection, and what went wrong
 *          when the solver gave no matrix.
 * @details Independent of the solver: the backend asks for the lines and the explanations, and the views and the tests
 *          share the formulas for impedance, standing wave ratio and return loss.
 */

#ifndef IMCSIM_S_PARAMETERS_H
#define IMCSIM_S_PARAMETERS_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/simulation_results.h"
#include <complex>
#include <optional>
#include <string>
#include <vector>

namespace Core {

/// A magnitude below this reads as this value in decibels, which is -300 dB, so a zero never gives minus infinity.
constexpr double MIN_S_MAGNITUDE = 1e-15;

// Netlist
std::string FormatPortSources(const std::vector<CircuitPort> &ports);
std::string FormatSParameterAnalysis(const SParameterSettings &settings);

// Quantities of a reflection coefficient
double ToDecibels(std::complex<double> value);
double ToDegrees(std::complex<double> value);
std::optional<std::complex<double>> ToImpedance(std::complex<double> reflection, double reference_ohms);
std::optional<double> ComputeVswr(std::complex<double> reflection);
std::optional<double> ComputeReturnLossDecibels(std::complex<double> reflection);

// Solver messages
std::optional<std::string> DescribeSParameterFailure(const std::vector<SimulatorMessage> &messages);

} // namespace Core

#endif // IMCSIM_S_PARAMETERS_H
