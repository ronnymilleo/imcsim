/**
 * @file    check_truth_table.cpp
 * @brief   Checks the settings of a truth table before it runs.
 */

#include "simulation/check_truth_table.h"

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/analysis_sources.h"
#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <string>

namespace Core {

/**
 * @brief   Checks the inputs and outputs of a truth table.
 * @param[in] circuit   Circuit to run.
 * @param[in] settings  The logic inputs to vary and the nodes to read.
 * @return  The reason the table cannot be built, such as no inputs, more than the limit, a name that is not a logic
 * input or an output node that is ground; no value when it can.
 */
std::optional<std::string> CheckTruthTableSettings(const Circuit &circuit, const TruthTableSettings &settings) {
    if (settings.Inputs.empty()) {
        return "A truth table needs at least one logic input to vary; add a Logic input";
    }
    if (settings.Inputs.size() > MAX_TRUTH_TABLE_INPUTS) {
        return std::format("A truth table varies at most {} inputs, which is {} runs; clear some",
                           MAX_TRUTH_TABLE_INPUTS, 1u << MAX_TRUTH_TABLE_INPUTS);
    }
    for (std::size_t index = 0; index < settings.Inputs.size(); ++index) {
        const std::string &name = settings.Inputs[index];
        if (FindLogicInput(circuit, name) == nullptr) {
            return std::format("The input {} is not a logic input in the circuit", name);
        }
        if (std::ranges::count(settings.Inputs, name) > 1) {
            return std::format("The input {} is listed twice", name);
        }
    }
    if (settings.OutputNodes.empty()) {
        return "Measure at least one node, so the truth table has an output to read";
    }
    for (const int node : settings.OutputNodes) {
        if (node < 1 || node >= circuit.GetNodeCount()) {
            return "Pick output nodes of the circuit; ground has no logic level to read";
        }
    }
    return std::nullopt;
}

} // namespace Core
