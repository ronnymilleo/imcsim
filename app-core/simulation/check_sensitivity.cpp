/**
 * @file    check_sensitivity.cpp
 * @brief   Checks the settings of a DC or AC sensitivity analysis before it runs.
 */

#include "simulation/check_sensitivity.h"

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/check_analysis_ports.h"
#include "simulation/check_frequency_range.h"
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
    return CheckOutputNode(circuit, settings.OutputNode, "sensitivity");
}

/**
 * @brief   Checks the output node, input source and frequency range of an AC sensitivity analysis.
 * @param[in] circuit   Circuit to analyze.
 * @param[in] settings  Output node, AC input source and frequency range.
 * @return  The reason the analysis cannot run; no value when it can.
 */
std::optional<std::string> CheckACSensitivitySettings(const Circuit &circuit, const ACSensitivitySettings &settings) {
    if (std::optional<std::string> error =
            CheckFrequencyRange(settings.StartFrequency, settings.StopFrequency, settings.PointsPerDecade)) {
        return error;
    }
    if (std::optional<std::string> error = CheckOutputNode(circuit, settings.OutputNode, "sensitivity")) {
        return error;
    }
    return CheckAcInput(circuit, settings.InputSource, "An AC sensitivity analysis");
}

} // namespace Core
