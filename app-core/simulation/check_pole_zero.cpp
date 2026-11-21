/**
 * @file    check_pole_zero.cpp
 * @brief   Checks the settings of a pole-zero analysis before it runs.
 */

#include "simulation/check_pole_zero.h"

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/check_analysis_ports.h"
#include <optional>
#include <string>

namespace Core {

/**
 * @brief   Checks the output node and the input source of a pole-zero analysis.
 * @param[in] circuit   Circuit to analyze.
 * @param[in] settings  Output node and input source.
 * @return  The reason the analysis cannot run; no value when it can.
 */
std::optional<std::string> CheckPoleZeroSettings(const Circuit &circuit, const PoleZeroSettings &settings) {
    if (std::optional<std::string> error = CheckOutputNode(circuit, settings.OutputNode, "poles or zeros")) {
        return error;
    }
    return CheckSweepableInput(circuit, settings.InputSource, "A pole-zero analysis");
}

} // namespace Core
