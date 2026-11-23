/**
 * @file    netlist_element_sources.h
 * @brief   Reads independent, controlled, behavioral, and port element lines of a SPICE netlist.
 */

#ifndef NODALYS_PARSER_NETLIST_ELEMENT_SOURCES_H
#define NODALYS_PARSER_NETLIST_ELEMENT_SOURCES_H

#include "parser/netlist/netlist_context.h"
#include "parser/netlist/netlist_deck.h"

#include <expected>
#include <string>
#include <vector>

namespace Nodalys {

// Independent sources and ports
[[nodiscard]] std::expected<void, std::string> ReadSource(DeckContext &context, const DeckLine &line);
[[nodiscard]] std::expected<void, std::string> ReadPort(DeckContext &context, const DeckLine &line);

// Controlled sources
[[nodiscard]] std::expected<void, std::string> ReadVcvs(DeckContext &context, const DeckLine &line);
[[nodiscard]] std::expected<void, std::string> ReadVccs(DeckContext &context, const DeckLine &line);
[[nodiscard]] std::expected<void, std::string> ReadCcvs(DeckContext &context, const DeckLine &line);
[[nodiscard]] std::expected<void, std::string> ReadCccs(DeckContext &context, const DeckLine &line);

// Behavioral sources
[[nodiscard]] std::vector<std::string> ListBehavioralNodes(const DeckLine &line);
[[nodiscard]] std::expected<void, std::string> ReadBehavioral(DeckContext &context, const DeckLine &line);

} // namespace Nodalys

#endif // NODALYS_PARSER_NETLIST_ELEMENT_SOURCES_H
