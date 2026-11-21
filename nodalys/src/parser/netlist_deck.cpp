/**
 * @file    netlist_deck.cpp
 * @brief   Sorts the lines of a netlist before they are read: the title, the circuit lines, the definitions the
 *          subcircuits are placed over, and the .model cards.
 */

#include "parser/netlist_deck.h"

#include "nodalys/netlist_parser.h"
#include "parser/spice_text.h"
#include "text/text_case.h"

#include <cctype>
#include <format>
#include <optional>
#include <utility>

namespace Nodalys {

namespace {

/**
 * @brief   Cuts the title off a netlist: its first line that is not blank, whatever it says, as in SPICE.
 * @return  The title and the text after it.
 */
std::pair<std::string_view, std::string_view> SplitTitle(std::string_view text) {
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = TrimSpaces(text.substr(0, end));
        const std::string_view rest = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (!line.empty()) {
            return {line, rest};
        }
        text = rest;
    }
    return {};
}

/**
 * @brief   Reads a .model card into the kind and the values of its parameters.
 * @details A parameter whose value is not a number is left out, and a type the engine has no device for keeps the
 *          diode kind with a diagnostic, so a card nobody uses does not refuse the netlist.
 */
std::expected<void, std::string> ReadModelLine(const DeckLine &line, Deck &deck) {
    const std::optional<ModelCardWords> card = SplitModelCard(line.Tokens);
    if (!card) {
        return std::unexpected(std::format("Malformed .model directive: '{}'", line.Text));
    }
    const std::string name = ToUpper(card->Name);
    ParsedModel model;
    if (const std::optional<ParsedModel::ModelKind> kind = FindModelKind(card->TypeName)) {
        model.Kind = *kind;
    } else {
        deck.Diagnostics.push_back({std::format("Unsupported .model type '{}' in model '{}'", card->TypeName, name)});
    }
    for (const std::string &assignment : card->Assignments) {
        const std::size_t equals = assignment.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        if (const std::optional<double> value = ParseSpiceNumber(std::string_view(assignment).substr(equals + 1))) {
            model.Params[ToUpper(TrimSpaces(std::string_view(assignment).substr(0, equals)))] = *value;
        }
    }
    deck.Models[name] = std::move(model);
    return {};
}

/**
 * @struct  OpenBlocks
 * @brief   The .subckt blocks open at a line of the netlist, whose lines are definitions until their .ends.
 */
struct OpenBlocks {
    int Depth = 0;
    /// The name of the outermost open block, for the message when the netlist never closes it.
    std::string OutermostName;
};

/**
 * @brief   Puts one line where it belongs: among the definitions, the models or the circuit lines.
 * @return  Whether reading goes on, false at .end; the reason a .subckt or .model line cannot be read otherwise.
 */
std::expected<bool, std::string> SortLine(DeckLine line, OpenBlocks &blocks, Deck &deck) {
    const std::string word = ToUpper(line.Tokens.front());
    if (word == ".SUBCKT") {
        if (line.Tokens.size() < 2) {
            return std::unexpected(std::format("Malformed .subckt directive: '{}'", line.Text));
        }
        if (blocks.Depth++ == 0) {
            blocks.OutermostName = line.Tokens[1];
        }
    } else if (word == ".ENDS") {
        if (blocks.Depth == 0) {
            return true;
        }
        --blocks.Depth;
    } else if (word == ".MODEL" && blocks.Depth == 0) {
        if (auto read = ReadModelLine(line, deck); !read) {
            return std::unexpected(read.error());
        }
    } else if (word == ".END" && blocks.Depth == 0) {
        return false;
    } else if (blocks.Depth == 0 && word != ".PARAM") {
        deck.CircuitLines.push_back(std::move(line));
        return true;
    }
    deck.DefinitionLines.push_back(std::move(line.Text));
    return true;
}

} // namespace

/**
 * @brief   Splits a netlist into its title and its lines, and sorts the lines by what reads them.
 * @details The lines go through the same splitting as a subcircuit kept as text: continuation lines are joined,
 *          comments and inline comments dropped. The lines of a .subckt block, a .model card or a .param line are
 *          the definitions; a .model outside the blocks is also read for the elements of the netlist. Reading stops
 *          at .end.
 * @param[in] text  The whole netlist.
 * @return  The sorted deck, or why it cannot be read: empty, a malformed .subckt or .model, or a block left open.
 */
std::expected<Deck, std::string> ReadDeck(const std::string_view text) {
    const auto [title, body] = SplitTitle(text);
    if (title.empty()) {
        return std::unexpected("The netlist is empty");
    }
    Deck deck;
    deck.Title = title;
    OpenBlocks blocks;
    for (std::string &text_line : SplitLogicalLines(body)) {
        DeckLine line{.Text = std::move(text_line), .Tokens = {}};
        line.Tokens = SplitTokens(line.Text);
        if (line.Tokens.empty()) {
            continue;
        }
        const std::expected<bool, std::string> is_read_on = SortLine(std::move(line), blocks, deck);
        if (!is_read_on) {
            return std::unexpected(is_read_on.error());
        }
        if (!*is_read_on) {
            break;
        }
    }
    if (blocks.Depth > 0) {
        return std::unexpected(std::format("The subcircuit '{}' has no .ends", blocks.OutermostName));
    }
    return deck;
}

/**
 * @brief   The letter that tells the kind of an element, which follows the last dot of a name such as "X1.R2".
 * @param[in] name  Element name as written.
 * @return  The letter in upper case, or '\0' for a name with nothing after its last dot.
 */
char GetElementLetter(const std::string_view name) {
    const std::size_t dot = name.rfind('.');
    const std::string_view base = dot == std::string_view::npos ? name : name.substr(dot + 1);
    return base.empty() ? '\0' : static_cast<char>(std::toupper(static_cast<unsigned char>(base.front())));
}

} // namespace Nodalys
