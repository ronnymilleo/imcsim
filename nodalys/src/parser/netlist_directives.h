/**
 * @file    netlist_directives.h
 * @brief   Reads the directives of a netlist, such as .tran and .options, into the analyses and settings it asks for.
 */

#ifndef NODALYS_PARSER_NETLIST_DIRECTIVES_H
#define NODALYS_PARSER_NETLIST_DIRECTIVES_H

#include "nodalys/circuit_builder.h"
#include "nodalys/netlist_parser.h"
#include "parser/netlist_context.h"
#include "parser/netlist_deck.h"

#include <expected>
#include <string>
#include <vector>

namespace Nodalys {

// Nodes
std::vector<std::string> ListDirectiveNodes(const DeckLine &line);

// Directives
std::expected<void, std::string> ReadDirective(DeckContext &context, const DeckLine &line);
void ApplyDeckSettings(const DeckSettings &settings, CircuitBuilder &builder, std::vector<ParsedAnalysis> &analyses);

} // namespace Nodalys

#endif // NODALYS_PARSER_NETLIST_DIRECTIVES_H
