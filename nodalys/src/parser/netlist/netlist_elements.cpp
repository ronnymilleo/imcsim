/**
 * @file    netlist_elements.cpp
 * @brief   Dispatches element lines of a SPICE netlist to passive, source, and device readers.
 */

#include "parser/netlist/netlist_elements.h"

#include "parser/netlist/netlist_element_devices.h"
#include "parser/netlist/netlist_element_sources.h"
#include "parser/subcircuit/subcircuit_lines.h"
#include "parser/syntax/element_arguments.h"
#include "parser/syntax/spice_text.h"
#include "text/spice_number.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <string_view>

namespace Nodalys {

namespace {

/// Where the nodes of an element start, after its name.
constexpr std::size_t FIRST_NODE = 1;

using ElementResult = std::expected<void, std::string>;

/**
 * @struct  ElementReader
 * @brief   What the engine knows of the elements of one letter: how to list the nodes of a line before they are
 *          numbered, and how to read it.
 */
struct ElementReader {
    char Letter;
    std::vector<std::string> (*ListNodes)(const DeckLine &);
    ElementResult (*Read)(DeckContext &, const DeckLine &);
};

/**
 * @struct  TwoTerminalValue
 * @brief   The nodes and the value of a resistor, capacitor or inductor line, "<name> <n1> <n2> <value> ...".
 */
struct TwoTerminalValue {
    NodeId First;
    NodeId Second;
    double Value;
};

// Passive parts

std::expected<TwoTerminalValue, std::string>
ReadTwoTerminalValue(const DeckContext &context, const DeckLine &line, const std::string_view kind) {
    constexpr std::size_t VALUE_INDEX = 3;
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() <= VALUE_INDEX) {
        return std::unexpected(std::format("Malformed {}: '{}'", kind, line.Text));
    }
    const std::optional<double> value = ParseSpiceNumber(tokens[VALUE_INDEX]);
    if (!value) {
        return std::unexpected(std::format("Invalid {} value '{}'", kind, tokens[VALUE_INDEX]));
    }
    return TwoTerminalValue{.First = context.Nodes.Resolve(tokens[1]),
                            .Second = context.Nodes.Resolve(tokens[2]),
                            .Value = *value};
}

ElementResult ReadResistor(DeckContext &context, const DeckLine &line) {
    const auto part = ReadTwoTerminalValue(context, line, "resistor");
    if (!part) {
        return std::unexpected(part.error());
    }
    context.Builder.AddResistor(line.Tokens.front(), part->First, part->Second, part->Value);
    return {};
}

/**
 * @brief   C<name> <n1> <n2> <value> [IC=<volts>].
 */
ElementResult ReadCapacitor(DeckContext &context, const DeckLine &line) {
    const auto part = ReadTwoTerminalValue(context, line, "capacitor");
    if (!part) {
        return std::unexpected(part.error());
    }
    context.Builder.AddCapacitor(line.Tokens.front(),
                                 part->First,
                                 part->Second,
                                 part->Value,
                                 FindElementOptionValue(line.Tokens, 4, "IC").value_or(0.0));
    return {};
}

/**
 * @brief   L<name> <n1> <n2> <value> [IC=<amps>].
 */
ElementResult ReadInductor(DeckContext &context, const DeckLine &line) {
    const auto part = ReadTwoTerminalValue(context, line, "inductor");
    if (!part) {
        return std::unexpected(part.error());
    }
    context.Builder.AddInductor(line.Tokens.front(),
                                part->First,
                                part->Second,
                                part->Value,
                                FindElementOptionValue(line.Tokens, 4, "IC").value_or(0.0));
    return {};
}

/**
 * @brief   T<name> <p1+> <p1-> <p2+> <p2-> Z0=<ohms> (TD=<sec> | F=<hz> [NL=<wavelengths>])
 *          [RDC=<ohms>] [RSKIN=<ohms>] [GDC=<siemens>] [TAND=<val>] [FREF=<hz>].
 */
ElementResult ReadTransmissionLine(DeckContext &context, const DeckLine &line) {
    constexpr std::size_t OPTION_START = 5;
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < OPTION_START) {
        return std::unexpected(std::format("Malformed transmission line: '{}'", line.Text));
    }
    const Arguments args = SplitArguments(tokens, OPTION_START);
    if (!args.Positional.empty()) {
        return std::unexpected(
            std::format("Unexpected argument '{}' on transmission line '{}'", args.Positional.front(), tokens.front()));
    }
    const auto params = ParseTransmissionLineOptions(
        args,
        [&](const std::string_view key, const std::string_view text) -> std::expected<double, std::string> {
            const std::optional<double> val = ParseSpiceNumber(text);
            if (!val) {
                return std::unexpected(std::format("invalid {} value '{}'", key, text));
            }
            return *val;
        });
    if (!params) {
        return std::unexpected(std::format("{}: {}", tokens.front(), params.error()));
    }
    context.Builder.AddTransmissionLine(tokens.front(),
                                        context.Nodes.Resolve(tokens[1]),
                                        context.Nodes.Resolve(tokens[2]),
                                        context.Nodes.Resolve(tokens[3]),
                                        context.Nodes.Resolve(tokens[4]),
                                        *params);
    return {};
}

/**
 * @brief   X lines are placed before the other lines are read, since the directives may name their nodes.
 */
ElementResult SkipInstance(DeckContext & /*context*/, const DeckLine & /*line*/) {
    return {};
}

// Nodes of each kind of element

/**
 * @brief   The first count node tokens of a line, or none when the line is too short to have them.
 */
std::vector<std::string> ListFirstNodes(const std::vector<std::string> &tokens, const std::size_t count) {
    if (tokens.size() <= count) {
        return {};
    }
    return {tokens.begin() + FIRST_NODE, tokens.begin() + static_cast<std::ptrdiff_t>(FIRST_NODE + count)};
}

std::vector<std::string> ListTwoNodes(const DeckLine &line) {
    return ListFirstNodes(line.Tokens, 2);
}

std::vector<std::string> ListThreeNodes(const DeckLine &line) {
    return ListFirstNodes(line.Tokens, 3);
}

std::vector<std::string> ListFourNodes(const DeckLine &line) {
    return ListFirstNodes(line.Tokens, 4);
}

std::vector<std::string> ListInstanceNodes(const DeckLine &line) {
    const std::optional<InstanceLine> instance = SplitInstanceLine(line.Tokens);
    return instance ? instance->NodeNames : std::vector<std::string>{};
}

/**
 * @brief   The reader of the elements of a line, by the letter of its name.
 * @return  The reader, or null for a letter the engine has no element for.
 */
const ElementReader *FindElementReader(const DeckLine &line) {
    static constexpr auto ELEMENT_READERS = std::to_array<ElementReader>({
        {'R', ListTwoNodes, ReadResistor},
        {'C', ListTwoNodes, ReadCapacitor},
        {'L', ListTwoNodes, ReadInductor},
        {'V', ListTwoNodes, ReadSource},
        {'I', ListTwoNodes, ReadSource},
        {'P', ListTwoNodes, ReadPort},
        {'E', ListFourNodes, ReadVcvs},
        {'G', ListFourNodes, ReadVccs},
        {'H', ListTwoNodes, ReadCcvs},
        {'F', ListTwoNodes, ReadCccs},
        {'B', ListBehavioralNodes, ReadBehavioral},
        {'D', ListTwoNodes, ReadDiode},
        {'Q', ListThreeNodes, ReadBjt},
        {'M', ListFourNodes, ReadMosfet},
        {'S', ListFourNodes, ReadSwitch},
        {'T', ListFourNodes, ReadTransmissionLine},
        {'U', ListGateNodes, ReadLogicGate},
        {'X', ListInstanceNodes, SkipInstance},
    });
    const auto reader =
        std::ranges::find(ELEMENT_READERS, GetElementLetter(line.Tokens.front()), &ElementReader::Letter);
    return reader == ELEMENT_READERS.end() ? nullptr : &*reader;
}

} // namespace

/**
 * @brief   The nodes an element line names, before the nodes are numbered.
 * @param[in] line  An element line.
 * @return  Its nodes as written; none for a kind of element the engine does not read.
 */
std::vector<std::string> ListElementNodes(const DeckLine &line) {
    const ElementReader *reader = FindElementReader(line);
    return reader == nullptr ? std::vector<std::string>{} : reader->ListNodes(line);
}

/**
 * @brief   Reads one element line into the circuit being built.
 * @param[in,out] context  The netlist being parsed.
 * @param[in]     line     A line that is not a directive.
 * @return  Nothing once read, also for a letter the engine has no element for, which is noted in the diagnostics;
 *          the reason the line cannot be read otherwise.
 */
std::expected<void, std::string> ReadElement(DeckContext &context, const DeckLine &line) {
    if (GetElementLetter(line.Tokens.front()) == '\0') {
        return {};
    }
    const ElementReader *reader = FindElementReader(line);
    if (reader == nullptr) {
        context.Result.Diagnostics.push_back({std::format("Ignored unrecognized device line: '{}'", line.Text)});
        return {};
    }
    return reader->Read(context, line);
}

} // namespace Nodalys
