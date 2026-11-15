/**
 * @file    check_transfer_function.cpp
 * @brief   Checks the settings of a transfer function analysis before it runs.
 */

#include "simulation/check_transfer_function.h"

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/analysis_sources.h"
#include <format>
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
    if (settings.OutputNode < 1 || settings.OutputNode >= circuit.GetNodeCount()) {
        return "Pick the output node; ground has no transfer function";
    }
    if (settings.InputSource.empty()) {
        return std::format("A transfer function analysis needs an input source; select {}", SWEEPABLE_SOURCES);
    }
    if (FindTransferFunctionInput(circuit, settings.InputSource) == nullptr) {
        return std::format("The input source {} is not {} of the circuit", settings.InputSource, SWEEPABLE_SOURCES);
    }
    return std::nullopt;
}

} // namespace Core
