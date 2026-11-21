/**
 * @file    check_analysis_ports.cpp
 * @brief   Checks the output node and the input source that the small-signal analyses read, with the same words for
 *          every analysis.
 */

#include "simulation/check_analysis_ports.h"

#include "simulation/analysis_sources.h"
#include <format>

namespace Core {

/**
 * @brief   Checks that an analysis reads a node of the circuit other than ground.
 * @param[in] circuit       Circuit to analyze.
 * @param[in] node          Output node picked.
 * @param[in] ground_lacks  What ground has none of, for the reason, such as "noise to measure".
 * @return  The reason the node cannot be read; no value when it can.
 */
std::optional<std::string>
CheckOutputNode(const Circuit &circuit, const int node, const std::string_view ground_lacks) {
    if (node < 1 || node >= circuit.GetNodeCount()) {
        return std::format("Pick the output node; ground has no {}", ground_lacks);
    }
    return std::nullopt;
}

/**
 * @brief   Checks the AC source that a noise or AC sensitivity analysis takes as its input.
 * @param[in] circuit   Circuit to analyze.
 * @param[in] source    Name of the source picked, or empty.
 * @param[in] analysis  The analysis, as a sentence starts it, such as "A noise analysis".
 * @return  The reason the source cannot be the input; no value when it can.
 */
std::optional<std::string>
CheckAcInput(const Circuit &circuit, const std::string_view source, const std::string_view analysis) {
    if (source.empty()) {
        return std::format("{} needs an AC source as its input; select a voltage or current source and set it to AC",
                           analysis);
    }
    if (FindNoiseInput(circuit, source) == nullptr) {
        return std::format("The input source {} is not a voltage or current source set to AC", source);
    }
    return std::nullopt;
}

/**
 * @brief   Checks the source that a transfer function or pole-zero analysis takes as its input.
 * @param[in] circuit   Circuit to analyze.
 * @param[in] source    Name of the source picked, or empty.
 * @param[in] analysis  The analysis, as a sentence starts it, such as "A pole-zero analysis".
 * @return  The reason the source cannot be the input; no value when it can.
 */
std::optional<std::string>
CheckSweepableInput(const Circuit &circuit, const std::string_view source, const std::string_view analysis) {
    if (source.empty()) {
        return std::format("{} needs an input source; select {}", analysis, SWEEPABLE_SOURCES);
    }
    if (FindTransferFunctionInput(circuit, source) == nullptr) {
        return std::format("The input source {} is not {} of the circuit", source, SWEEPABLE_SOURCES);
    }
    return std::nullopt;
}

} // namespace Core
