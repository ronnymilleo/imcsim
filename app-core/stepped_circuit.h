/**
 * @file    stepped_circuit.h
 * @brief   The circuit one run of a stepped analysis simulates: a part at its stepped value, or a temperature.
 * @details Independent of the simulation backend: every backend runs the circuit this builds.
 */

#ifndef IMCSIM_STEPPED_CIRCUIT_H
#define IMCSIM_STEPPED_CIRCUIT_H

#include "analysis_settings.h"
#include "circuit.h"
#include "components/component.h"
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Core {

/**
 * @struct  ParameterCircuit
 * @brief   A circuit as one run of a stepped analysis simulates it, and the temperature of the run, when it is stepped.
 * @details Simulated refers to Part, the copy of the stepped part at its value, so both live together.
 */
struct ParameterCircuit {
    std::unique_ptr<Component> Part;
    Circuit Simulated;
    // Temperature of the run in degrees Celsius when the stepped parameter is the temperature
    std::optional<double> Temperature;
};

std::expected<ParameterCircuit, std::string> ApplyParameter(const Circuit &circuit,
                                                            const std::optional<ParameterValue> &parameter);
std::vector<std::string> GetSteppableParts(const Circuit &circuit);

} // namespace Core

#endif // IMCSIM_STEPPED_CIRCUIT_H
