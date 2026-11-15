/**
 * @file    ngspice_read_transfer_function.h
 * @brief   Reads the result of a transfer function analysis from the plot ngspice leaves.
 */

#ifndef IMCSIM_NGSPICE_READ_TRANSFER_FUNCTION_H
#define IMCSIM_NGSPICE_READ_TRANSFER_FUNCTION_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/simulation_results.h"
#include <expected>
#include <string>

namespace Core {

std::expected<TransferFunction, std::string> ReadTransferFunction(const Circuit &circuit,
                                                                  const TransferFunctionSettings &settings);

} // namespace Core

#endif // IMCSIM_NGSPICE_READ_TRANSFER_FUNCTION_H
