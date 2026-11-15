/**
 * @file    check_transfer_function.h
 * @brief   Checks the settings of a transfer function analysis before it runs.
 */

#ifndef IMCSIM_CHECK_TRANSFER_FUNCTION_H
#define IMCSIM_CHECK_TRANSFER_FUNCTION_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include <optional>
#include <string>

namespace Core {

std::optional<std::string> CheckTransferFunctionSettings(const Circuit &circuit,
                                                         const TransferFunctionSettings &settings);

} // namespace Core

#endif // IMCSIM_CHECK_TRANSFER_FUNCTION_H
