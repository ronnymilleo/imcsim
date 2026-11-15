/**
 * @file    ngspice_read_operating_point.h
 * @brief   Reads the result of an operating point from the current plot of ngspice.
 */

#ifndef IMCSIM_NGSPICE_READ_OPERATING_POINT_H
#define IMCSIM_NGSPICE_READ_OPERATING_POINT_H

#include "circuit/circuit.h"
#include "simulation/simulation_results.h"
#include <expected>
#include <string>

namespace Core {

std::expected<OperatingPoint, std::string> ReadOperatingPoint(const Circuit &circuit);

} // namespace Core

#endif // IMCSIM_NGSPICE_READ_OPERATING_POINT_H
