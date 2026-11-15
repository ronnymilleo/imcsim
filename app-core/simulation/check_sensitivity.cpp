/**
 * @file    check_sensitivity.cpp
 * @brief   Checks the settings of a sensitivity analysis before it runs.
 */

#include "simulation/check_sensitivity.h"

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include <optional>
#include <string>

namespace Core {

/**
 * @brief   Checks the output node of a sensitivity analysis.
 * @param[in] circuit   Circuit to analyze.
 * @param[in] settings  Output node.
 * @return  The reason the analysis cannot run; no value when it can.
 * @note    ngspice answers a node that does not exist with zeros and no error, so the node is checked here.
 */
std::optional<std::string> CheckSensitivitySettings(const Circuit &circuit, const SensitivitySettings &settings) {
    if (settings.OutputNode < 1 || settings.OutputNode >= circuit.GetNodeCount()) {
        return "Pick the output node; ground has no sensitivity";
    }
    return std::nullopt;
}

} // namespace Core
