/**
 * @file    check_noise.cpp
 * @brief   Checks the settings of a noise analysis before it runs.
 */

#include "simulation/check_noise.h"

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/check_analysis_ports.h"
#include "simulation/check_frequency_range.h"
#include <optional>
#include <string>

namespace Core {

/**
 * @brief   Checks the output node, input source and frequency range of a noise analysis.
 * @param[in] circuit   Circuit to analyze.
 * @param[in] settings  Output node, AC input source and frequency range.
 * @return  The reason the analysis cannot run; no value when it can.
 */
std::optional<std::string> CheckNoiseSettings(const Circuit &circuit, const NoiseSettings &settings) {
    if (std::optional<std::string> error =
            CheckFrequencyRange(settings.StartFrequency, settings.StopFrequency, settings.PointsPerDecade)) {
        return error;
    }
    if (std::optional<std::string> error = CheckOutputNode(circuit, settings.OutputNode, "noise to measure")) {
        return error;
    }
    return CheckAcInput(circuit, settings.InputSource, "A noise analysis");
}

} // namespace Core
