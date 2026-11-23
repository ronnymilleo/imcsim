/**
 * @file    netlist_directive_settings.h
 * @brief   Shared directive helpers (output nodes, frequency sweeps) and settings directives (.options, .temp, .ic,
 *          .nodeset).
 */

#ifndef NODALYS_PARSER_NETLIST_DIRECTIVE_SETTINGS_H
#define NODALYS_PARSER_NETLIST_DIRECTIVE_SETTINGS_H

#include "nodalys/types.h"
#include "parser/netlist/netlist_context.h"
#include "parser/netlist/netlist_deck.h"

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

/**
 * @struct  FrequencySweepWords
 * @brief   The sweep of an .ac, .noise or .sens ... ac directive as it is written.
 */
struct FrequencySweepWords {
    FrequencySweepType Type = FrequencySweepType::DECADE;
    int Points = 0;
    double StartFrequency = 0.0;
    double StopFrequency = 0.0;
};

/**
 * @struct  OutputNodes
 * @brief   The two nodes of an output written V(node) or V(positive, negative), by name.
 */
struct OutputNodes {
    std::string Positive;
    std::string Negative;
};

// Shared directive parsers
[[nodiscard]] OutputNodes ReadOutputNodes(std::string_view text);
[[nodiscard]] std::optional<FrequencySweepType> FindSweepType(std::string_view word);
[[nodiscard]] std::expected<FrequencySweepWords, std::string> ReadFrequencySweep(const std::vector<std::string> &tokens,
                                                                                 std::size_t first_token,
                                                                                 bool is_type_required,
                                                                                 std::string_view directive,
                                                                                 std::string_view line);

// Settings and node-voltage directives
[[nodiscard]] std::expected<void, std::string> ReadOptions(DeckContext &context, const DeckLine &line);
[[nodiscard]] std::expected<void, std::string> ReadTemperature(DeckContext &context, const DeckLine &line);
[[nodiscard]] std::expected<void, std::string> ReadInitialConditions(DeckContext &context, const DeckLine &line);
[[nodiscard]] std::expected<void, std::string> ReadNodesets(DeckContext &context, const DeckLine &line);

} // namespace Nodalys

#endif // NODALYS_PARSER_NETLIST_DIRECTIVE_SETTINGS_H
