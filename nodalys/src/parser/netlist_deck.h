/**
 * @file    netlist_deck.h
 * @brief   Sorts the lines of a netlist before they are read: the title, the circuit lines, the definitions the
 *          subcircuits are placed over, and the .model cards.
 */

#ifndef NODALYS_PARSER_NETLIST_DECK_H
#define NODALYS_PARSER_NETLIST_DECK_H

#include "nodalys/types.h"
#include "parser/model_cards.h"

#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

/**
 * @struct  DeckLine
 * @brief   A logical line of the netlist, with its tokens split once for every pass that reads it.
 */
struct DeckLine {
    std::string Text;
    std::vector<std::string> Tokens;
};

/**
 * @struct  Deck
 * @brief   The lines of a netlist sorted by what reads them.
 */
struct Deck {
    /// The first line, which SPICE takes as the title whatever it says.
    std::string Title;
    /// The elements and the directives outside the subcircuits, up to .end.
    std::vector<DeckLine> CircuitLines;
    /// The .subckt blocks, .model cards and .param lines, which every X line is placed over.
    std::vector<std::string> DefinitionLines;
    /// The .model cards outside the subcircuits, by upper-case name.
    std::map<std::string, ParsedModel> Models;
    /// What the reading noticed without refusing the netlist, such as a model type the engine has no device for.
    std::vector<DiagnosticMessage> Diagnostics;
};

// Reading
std::expected<Deck, std::string> ReadDeck(std::string_view text);

// Element names
char GetElementLetter(std::string_view name);

} // namespace Nodalys

#endif // NODALYS_PARSER_NETLIST_DECK_H
