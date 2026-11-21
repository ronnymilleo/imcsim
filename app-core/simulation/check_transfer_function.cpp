/**
 * @file    check_transfer_function.cpp
 * @brief   Checks the settings of a transfer function analysis before it runs.
 */

#include "simulation/check_transfer_function.h"

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/check_analysis_ports.h"
#include <optional>
#include <string>

namespace Core {

/**
 * @brief   Checks the output node and input source of a transfer function analysis.
 * @param[in] circuit   Circuit to analyze.
 * @param[in] settings  Output node and input source.
 * @return  The reason the analysis cannot run; no value when it can.
 */
std::optional<std::string> CheckTransferFunctionSettings(const Circuit &circuit,
                                                         const TransferFunctionSettings &settings) {
    if (std::optional<std::string> error = CheckOutputNode(circuit, settings.OutputNode, "transfer function")) {
        return error;
    }
    return CheckSweepableInput(circuit, settings.InputSource, "A transfer function analysis");
}

} // namespace Core
