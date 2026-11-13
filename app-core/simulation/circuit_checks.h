/**
 * @file    circuit_checks.h
 * @brief   Checks of a circuit before any analysis runs on it.
 * @details Independent of the simulation backend: whatever runs the circuit, these say whether it can be asked to.
 */

#ifndef IMCSIM_CIRCUIT_CHECKS_H
#define IMCSIM_CIRCUIT_CHECKS_H

#include "circuit/circuit.h"
#include <optional>
#include <string>

namespace Core {

std::optional<std::string> CheckCircuit(const Circuit &circuit);
std::optional<std::string> CheckBehavioralExpression(const Circuit &circuit, const Component &source);

} // namespace Core

#endif // IMCSIM_CIRCUIT_CHECKS_H
