/**
 * @file    sensitivity.h
 * @brief   Turns the sensitivity vectors a solver reports for each element into the sensitivity of each part.
 * @details Independent of the solver: it takes the values by element name, so any backend that reports them, and the
 *          tests with made-up numbers, share the rules for potentiometers, blocks and which parts are listed.
 */

#ifndef IMCSIM_SENSITIVITY_H
#define IMCSIM_SENSITIVITY_H

#include "model/circuit.h"
#include "simulation/simulation_results.h"
#include <map>
#include <string>

namespace Core {

/// Values by lower case vector name, as ngspice names them: "r1" holds the change of the output in volts per ohm of R1,
/// "r1_scale" the change for a 100 % change of it, "e1_gain" the change per unit of the gain of E1, and "r.x2.r1" the
/// resistor R1 inside the block X2.
using SensitivityVectors = std::map<std::string, double>;

// Parts
bool HasSensitivity(ComponentType type);

// Results
Sensitivity BuildSensitivity(const Circuit &circuit, int output_node, const SensitivityVectors &vectors);

} // namespace Core

#endif // IMCSIM_SENSITIVITY_H
