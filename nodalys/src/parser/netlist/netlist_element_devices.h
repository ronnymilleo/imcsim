/**
 * @file    netlist_element_devices.h
 * @brief   Reads semiconductor, switch, and logic gate element lines of a SPICE netlist.
 */

#ifndef NODALYS_PARSER_NETLIST_ELEMENT_DEVICES_H
#define NODALYS_PARSER_NETLIST_ELEMENT_DEVICES_H

#include "parser/netlist/netlist_context.h"
#include "parser/netlist/netlist_deck.h"

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

// Shared option helper
[[nodiscard]] std::optional<double>
FindElementOptionValue(const std::vector<std::string> &tokens, std::size_t first, std::string_view key);

// Semiconductors and switches
[[nodiscard]] std::expected<void, std::string> ReadDiode(DeckContext &context, const DeckLine &line);
[[nodiscard]] std::expected<void, std::string> ReadBjt(DeckContext &context, const DeckLine &line);
[[nodiscard]] std::expected<void, std::string> ReadMosfet(DeckContext &context, const DeckLine &line);
[[nodiscard]] std::expected<void, std::string> ReadSwitch(DeckContext &context, const DeckLine &line);

// Logic gates
[[nodiscard]] std::vector<std::string> ListGateNodes(const DeckLine &line);
[[nodiscard]] std::expected<void, std::string> ReadLogicGate(DeckContext &context, const DeckLine &line);

} // namespace Nodalys

#endif // NODALYS_PARSER_NETLIST_ELEMENT_DEVICES_H
