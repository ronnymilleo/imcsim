/**
 * @file    check_analysis_ports.h
 * @brief   Checks the output node and the input source that the small-signal analyses read, with the same words for
 *          every analysis.
 */

#ifndef IMCSIM_CHECK_ANALYSIS_PORTS_H
#define IMCSIM_CHECK_ANALYSIS_PORTS_H

#include "circuit/circuit.h"
#include <optional>
#include <string>
#include <string_view>

namespace Core {

// Output
std::optional<std::string> CheckOutputNode(const Circuit &circuit, int node, std::string_view ground_lacks);

// Input
std::optional<std::string> CheckAcInput(const Circuit &circuit, std::string_view source, std::string_view analysis);
std::optional<std::string>
CheckSweepableInput(const Circuit &circuit, std::string_view source, std::string_view analysis);

} // namespace Core

#endif // IMCSIM_CHECK_ANALYSIS_PORTS_H
