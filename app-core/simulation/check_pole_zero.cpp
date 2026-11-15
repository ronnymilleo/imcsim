/**
 * @file    check_pole_zero.cpp
 * @brief   Checks the settings of a pole-zero analysis before it runs.
 */

#include "simulation/check_pole_zero.h"

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/analysis_sources.h"
#include <format>
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
    if (settings.OutputNode < 1 || settings.OutputNode >= circuit.GetNodeCount()) {
        return "Pick the output node; ground has no poles or zeros";
    }
    if (settings.InputSource.empty()) {
        return std::format("A pole-zero analysis needs an input source; select {}", SWEEPABLE_SOURCES);
    }
    if (FindTransferFunctionInput(circuit, settings.InputSource) == nullptr) {
        return std::format("The input source {} is not {} of the circuit", settings.InputSource, SWEEPABLE_SOURCES);
    }
    return std::nullopt;
}

} // namespace Core
