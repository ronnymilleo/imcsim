/**
 * @file    ngspice_read_transient.h
 * @brief   Reads the result of a transient from the current plot of ngspice, whole or from a point on.
 */

#ifndef IMCSIM_NGSPICE_READ_TRANSIENT_H
#define IMCSIM_NGSPICE_READ_TRANSIENT_H

#include "circuit/circuit.h"
#include "simulation/simulation_results.h"
#include <cstddef>
#include <expected>
#include <optional>
#include <string>

namespace Core {

std::expected<Transient, std::string> ReadTransient(const Circuit &circuit);
std::expected<Transient, std::string> ReadTransientPoints(const Circuit &circuit, std::size_t first_point);
std::optional<double> ReadTransientTime();

} // namespace Core

#endif // IMCSIM_NGSPICE_READ_TRANSIENT_H
