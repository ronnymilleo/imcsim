/**
 * @file    netlist_element_sources.cpp
 * @brief   Reads independent, controlled, behavioral, and port element lines of a SPICE netlist.
 */

#include "parser/netlist/netlist_element_sources.h"

#include "nodalys/expression.h"
#include "parser/sources/source_line.h"
#include "parser/sources/source_waveforms.h"
#include "parser/syntax/expression_references.h"
#include "parser/syntax/spice_text.h"
#include "text/spice_number.h"
#include "text/text_case.h"

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <optional>
#include <string_view>

namespace Nodalys {

namespace {

/// The reference impedance of a port that does not give one.
constexpr double DEFAULT_PORT_OHMS = 50.0;
/// Where the nodes of an element start, after its name.
constexpr std::size_t FIRST_NODE = 1;

using CurrentControlledAdder =
    CircuitBuilder &(CircuitBuilder::*)(std::string, NodeId, NodeId, const std::string &, double);
using ElementResult = std::expected<void, std::string>;
using VoltageControlledAdder = CircuitBuilder &(CircuitBuilder::*)(std::string, NodeId, NodeId, NodeId, NodeId, double);

/**
 * @struct  BehavioralLine
 * @brief   The kind and the text of the expression of a B element line, "B1 n+ n- V=<expression>".
 * @details The text is taken from the line itself, since the tokens break an expression at its spaces and parentheses.
 */
struct BehavioralLine {
    bool IsVoltage = true;
    std::string Expression;
};

/**
 * @struct  PortWords
 * @brief   The port options of a V, I or P line: PORTNUM and Z0, written NAME=VALUE or NAME VALUE.
 */
struct PortWords {
    std::optional<int> Number;
    std::optional<double> Ohms;
};

/**
 * @struct  SourceWords
 * @brief   What a V or I line gives after its nodes.
 */
struct SourceWords {
    SourceValues Values;
    PortWords Port;
    bool IsPort = false; ///< Whether a port option made the line an S-parameter port.
};

/**
 * @brief   The number in the name of a port, such as 2 for "V_PORT2", or the fallback when it has none.
 */
int GetPortNumberFromName(const std::string &name, const int fallback) {
    const std::size_t digits = name.find_first_of("0123456789");
    return digits == std::string::npos ? fallback : std::atoi(name.c_str() + digits);
}

std::optional<BehavioralLine> SplitBehavioralLine(const std::string_view line) {
    constexpr int WORDS_BEFORE_EXPRESSION = 3;
    std::size_t position = 0;
    for (int word = 0; word < WORDS_BEFORE_EXPRESSION; ++word) {
        while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position]))) {
            ++position;
        }
        while (position < line.size() && !std::isspace(static_cast<unsigned char>(line[position]))) {
            ++position;
        }
    }
    std::string_view rest = TrimSpaces(line.substr(position));
    const char kind = rest.empty() ? '\0' : static_cast<char>(std::toupper(static_cast<unsigned char>(rest.front())));
    if (rest.size() < 2 || (kind != 'V' && kind != 'I')) {
        return std::nullopt;
    }
    rest = TrimSpaces(rest.substr(1));
    if (rest.empty() || rest.front() != '=') {
        return std::nullopt;
    }
    rest = TrimSpaces(rest.substr(1));
    if (rest.empty()) {
        return std::nullopt;
    }
    return BehavioralLine{.IsVoltage = kind == 'V', .Expression = std::string(rest)};
}

/**
 * @brief   Reads a port option at a token, which a V or I line also takes to become an S-parameter port.
 * @return  How many tokens the option takes, 1 or 2, or 0 when the token is no port option.
 */
std::size_t ReadPortWord(const std::vector<std::string> &tokens, const std::size_t index, PortWords &port) {
    const std::string upper = ToUpper(tokens[index]);
    const bool has_next = index + 1 < tokens.size();
    for (const std::string_view key : {std::string_view("PORTNUM"), std::string_view("Z0")}) {
        std::optional<double> value;
        std::size_t used = 0;
        if (upper.size() > key.size() && upper.starts_with(key) && upper[key.size()] == '=') {
            value = ParseSpiceNumber(std::string_view(upper).substr(key.size() + 1));
            used = 1;
        } else if (upper == key && has_next) {
            value = ParseSpiceNumber(tokens[index + 1]);
            used = 2;
        }
        if (used == 0) {
            continue;
        }
        if (key == "Z0") {
            port.Ohms = value.value_or(DEFAULT_PORT_OHMS);
        } else {
            port.Number = static_cast<int>(value.value_or(1.0));
        }
        return used;
    }
    return 0;
}

/**
 * @brief   Reads a number of a source line of a netlist, a literal with its scale suffix.
 */
std::expected<double, std::string> ReadLiteral(const std::string_view text) {
    if (const std::optional<double> value = ParseSpiceNumber(text)) {
        return *value;
    }
    return std::unexpected(std::format("'{}' is not a number", text));
}

/**
 * @brief   Reads "[DC <value>] [AC <magnitude> [<phase>]] [<waveform>(...)] [<value>]" and the port options.
 * @details What the line does not know is skipped, as the netlist has always read it; the subcircuit text refuses it.
 */
SourceWords ReadSourceWords(const std::vector<std::string> &tokens) {
    constexpr std::size_t FIRST_AFTER_NODES = 3;
    SourceWords words;
    SourceLineReader reader(tokens, FIRST_AFTER_NODES, ReadLiteral, SourceWordPolicy::SKIP_UNKNOWN);
    while (!reader.IsAtEnd()) {
        if (const std::size_t used = ReadPortWord(tokens, reader.GetIndex(), words.Port); used > 0) {
            words.IsPort = true;
            reader.Skip(used);
            continue;
        }
        // A line that skips what it does not know never fails
        static_cast<void>(reader.ReadWord(words.Values));
    }
    return words;
}

/**
 * @brief   E or G lines: <name> <pos> <neg> <ctrl_pos> <ctrl_neg> <value>.
 */
ElementResult ReadVoltageControlled(DeckContext &context,
                                    const DeckLine &line,
                                    const std::string_view kind,
                                    const std::string_view quantity,
                                    const VoltageControlledAdder add) {
    constexpr std::size_t VALUE_INDEX = 5;
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() <= VALUE_INDEX) {
        return std::unexpected(std::format("Malformed {}: '{}'", kind, line.Text));
    }
    const std::optional<double> value = ParseSpiceNumber(tokens[VALUE_INDEX]);
    if (!value) {
        return std::unexpected(std::format("Invalid {} {} '{}'", kind, quantity, tokens[VALUE_INDEX]));
    }
    const NetlistNodes &nodes = context.Nodes;
    (context.Builder.*add)(tokens.front(),
                           nodes.Resolve(tokens[1]),
                           nodes.Resolve(tokens[2]),
                           nodes.Resolve(tokens[3]),
                           nodes.Resolve(tokens[4]),
                           *value);
    return {};
}

/**
 * @brief   H or F lines: <name> <pos> <neg> <control source> <value>.
 */
ElementResult ReadCurrentControlled(DeckContext &context,
                                    const DeckLine &line,
                                    const std::string_view kind,
                                    const std::string_view quantity,
                                    const CurrentControlledAdder add) {
    constexpr std::size_t VALUE_INDEX = 4;
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() <= VALUE_INDEX) {
        return std::unexpected(std::format("Malformed {}: '{}'", kind, line.Text));
    }
    const std::optional<double> value = ParseSpiceNumber(tokens[VALUE_INDEX]);
    if (!value) {
        return std::unexpected(std::format("Invalid {} {} '{}'", kind, quantity, tokens[VALUE_INDEX]));
    }
    (context.Builder
     .*add)(tokens.front(), context.Nodes.Resolve(tokens[1]), context.Nodes.Resolve(tokens[2]), tokens[3], *value);
    return {};
}

} // namespace

/**
 * @brief   Parses `V` or `I` lines: a source with its DC value, AC magnitude/phase, and waveform, or an S-parameter
 *          port when it specifies `PORTNUM` or `Z0`.
 * @param[in,out] context  Parser deck context receiving the source or port.
 * @param[in]     line     Tokenized source statement.
 * @return  Empty expected on success, or a parse error message.
 */
std::expected<void, std::string> ReadSource(DeckContext &context, const DeckLine &line) {
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 3) {
        return std::unexpected(std::format("Malformed source: '{}'", line.Text));
    }
    const std::string &name = tokens.front();
    const NodeId positive = context.Nodes.Resolve(tokens[1]);
    const SourceWords words = ReadSourceWords(tokens);
    if (words.IsPort) {
        const int number = words.Port.Number.value_or(0);
        context.Builder.AddPort(number > 0 ? number : GetPortNumberFromName(name, 1),
                                positive,
                                words.Port.Ohms.value_or(DEFAULT_PORT_OHMS));
        return {};
    }
    AddWaveformSource(context.Builder,
                      name,
                      GetElementLetter(name) == 'V',
                      positive,
                      context.Nodes.Resolve(tokens[2]),
                      words.Values.Waveform,
                      words.Values.WaveformValues,
                      words.Values.Levels);
    return {};
}

/**
 * @brief   Parses `P<name> <pos> <neg> [PORTNUM=<n>] [Z0=<ohms>]` (or with impedance alone); the number defaults to
 *          the one in the name.
 * @param[in,out] context  Parser deck context receiving the port.
 * @param[in]     line     Tokenized port statement.
 * @return  Empty expected on success, or a parse error message.
 */
std::expected<void, std::string> ReadPort(DeckContext &context, const DeckLine &line) {
    const std::vector<std::string> &tokens = line.Tokens;
    if (tokens.size() < 3) {
        return std::unexpected(std::format("Malformed port: '{}'", line.Text));
    }
    PortWords port;
    for (std::size_t index = 3; index < tokens.size();) {
        if (const std::size_t used = ReadPortWord(tokens, index, port); used > 0) {
            index += used;
            continue;
        }
        if (const std::optional<double> ohms = ParseSpiceNumber(tokens[index])) {
            port.Ohms = ohms;
        }
        ++index;
    }
    context.Builder.AddPort(port.Number.value_or(GetPortNumberFromName(tokens.front(), 1)),
                            context.Nodes.Resolve(tokens[1]),
                            port.Ohms.value_or(DEFAULT_PORT_OHMS));
    return {};
}

/**
 * @brief   Parses an `E` voltage-controlled voltage source statement.
 * @param[in,out] context  Parser deck context receiving the VCVS.
 * @param[in]     line     Tokenized VCVS statement.
 * @return  Empty expected on success, or a parse error message.
 */
std::expected<void, std::string> ReadVcvs(DeckContext &context, const DeckLine &line) {
    return ReadVoltageControlled(context, line, "VCVS", "gain", &CircuitBuilder::AddVcvs);
}

/**
 * @brief   Parses a `G` voltage-controlled current source statement.
 * @param[in,out] context  Parser deck context receiving the VCCS.
 * @param[in]     line     Tokenized VCCS statement.
 * @return  Empty expected on success, or a parse error message.
 */
std::expected<void, std::string> ReadVccs(DeckContext &context, const DeckLine &line) {
    return ReadVoltageControlled(context, line, "VCCS", "transconductance", &CircuitBuilder::AddVccs);
}

/**
 * @brief   Parses an `H` current-controlled voltage source statement.
 * @param[in,out] context  Parser deck context receiving the CCVS.
 * @param[in]     line     Tokenized CCVS statement.
 * @return  Empty expected on success, or a parse error message.
 */
std::expected<void, std::string> ReadCcvs(DeckContext &context, const DeckLine &line) {
    return ReadCurrentControlled(context, line, "CCVS", "transresistance", &CircuitBuilder::AddCcvs);
}

/**
 * @brief   Parses an `F` current-controlled current source statement.
 * @param[in,out] context  Parser deck context receiving the CCCS.
 * @param[in]     line     Tokenized CCCS statement.
 * @return  Empty expected on success, or a parse error message.
 */
std::expected<void, std::string> ReadCccs(DeckContext &context, const DeckLine &line) {
    return ReadCurrentControlled(context, line, "CCCS", "gain", &CircuitBuilder::AddCccs);
}

/**
 * @brief   Lists the nodes of a `B` line, including those its expression reads, which are circuit nodes even when no
 *          other element touches them.
 * @param[in] line  Tokenized behavioral source statement.
 * @return  List of referenced node names.
 */
std::vector<std::string> ListBehavioralNodes(const DeckLine &line) {
    std::vector<std::string> nodes;
    if (line.Tokens.size() > 2) {
        nodes.assign(line.Tokens.begin() + FIRST_NODE, line.Tokens.begin() + FIRST_NODE + 2);
    }
    const std::optional<BehavioralLine> behavioral = SplitBehavioralLine(line.Text);
    if (!behavioral) {
        return nodes;
    }
    if (const auto expression = Expression::Parse(behavioral->Expression)) {
        for (const ExpressionInput &input : expression->GetInputs()) {
            if (input.Type == ExpressionInput::Kind::VOLTAGE) {
                nodes.push_back(input.First);
                nodes.push_back(input.Second);
            }
        }
    }
    return nodes;
}

/**
 * @brief   Parses `B<name> <pos> <neg> V=<expression>` or `I=<expression>`.
 * @details The builder reads nodes by number, so every `v(...)` is written with the numbers this netlist gave them;
 *          a current keeps the source it names.
 * @param[in,out] context  Parser deck context receiving the behavioral source.
 * @param[in]     line     Tokenized behavioral source statement.
 * @return  Empty expected on success, or a parse error message.
 */
std::expected<void, std::string> ReadBehavioral(DeckContext &context, const DeckLine &line) {
    const std::string &name = line.Tokens.front();
    const std::optional<BehavioralLine> behavioral = SplitBehavioralLine(line.Text);
    if (line.Tokens.size() < 4 || !behavioral) {
        return std::unexpected(std::format("Malformed behavioral source: '{}'", line.Text));
    }
    const auto expression = Expression::Parse(behavioral->Expression);
    if (!expression) {
        return std::unexpected(std::format("{}: {}", name, expression.error()));
    }
    const auto resolved = RewriteExpressionReferences(
        behavioral->Expression,
        *expression,
        [&context](const std::string_view node) { return context.Nodes.Resolve(node); },
        [](const std::string_view source) -> std::expected<std::string, std::string> { return std::string(source); });
    if (!resolved) {
        return std::unexpected(resolved.error());
    }
    const NodeId positive = context.Nodes.Resolve(line.Tokens[1]);
    const NodeId negative = context.Nodes.Resolve(line.Tokens[2]);
    if (behavioral->IsVoltage) {
        context.Builder.AddBehavioralVoltage(name, positive, negative, *resolved);
    } else {
        context.Builder.AddBehavioralCurrent(name, positive, negative, *resolved);
    }
    return {};
}

} // namespace Nodalys
