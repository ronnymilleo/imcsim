/**
 * @file    netlist_elements.h
 * @brief   Reads the element lines of a netlist, such as R1 or Q2, into the circuit being built.
 */

#ifndef NODALYS_PARSER_NETLIST_ELEMENTS_H
#define NODALYS_PARSER_NETLIST_ELEMENTS_H

#include "parser/netlist/netlist_context.h"
#include "parser/netlist/netlist_deck.h"

#include <expected>
#include <string>
#include <vector>

namespace Nodalys {

// Nodes
std::vector<std::string> ListElementNodes(const DeckLine &line);

// Elements
std::expected<void, std::string> ReadElement(DeckContext &context, const DeckLine &line);

} // namespace Nodalys

#endif // NODALYS_PARSER_NETLIST_ELEMENTS_H
