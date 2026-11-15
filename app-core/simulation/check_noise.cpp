/**
 * @file    check_noise.cpp
 * @brief   Checks the settings of a noise analysis before it runs.
 */

#include "simulation/check_noise.h"

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/analysis_sources.h"
#include "simulation/check_frequency_range.h"
#include <format>
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
    if (settings.OutputNode < 1 || settings.OutputNode >= circuit.GetNodeCount()) {
        return "Pick the output node; ground has no noise to measure";
    }
    if (settings.InputSource.empty()) {
        return "A noise analysis needs an AC source as its input; select a voltage or current source and set it to AC";
    }
    if (FindNoiseInput(circuit, settings.InputSource) == nullptr) {
        return std::format("The input source {} is not a voltage or current source set to AC", settings.InputSource);
    }
    return std::nullopt;
}

} // namespace Core
