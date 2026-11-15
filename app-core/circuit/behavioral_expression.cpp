/**
 * @file    behavioral_expression.cpp
 * @brief   Reads the expression of a behavioral source and writes it for ngspice, with its nets and currents resolved.
 */

#include "circuit/behavioral_expression.h"

#include "circuit/components/net_label.h"
#include <algorithm>
#include <cctype>
#include <format>
#include <utility>

namespace Core {

namespace {

using Kind = BehavioralReference::Kind;

/// What an expression may hold besides letters, digits and spaces: the operators, comparisons and the ternary of
/// ngspice, parentheses, commas and the dot of a number or a current name.
constexpr std::string_view OPERATOR_CHARACTERS = "+-*/^(),.<>=!&|?:%_";

bool IsSpace(const char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
}

bool IsWordCharacter(const char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && IsSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && IsSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

/**
 * @brief   Checks every character and that each parenthesis closes, so a typo is told in the editor rather than by
 *          ngspice.
 */
std::optional<std::string> FindSyntaxProblem(const std::string_view text) {
    int depth = 0;
    for (const char character : text) {
        if (!IsWordCharacter(character) && !IsSpace(character) && !OPERATOR_CHARACTERS.contains(character)) {
            return std::format("\"{}\" cannot be part of an expression", character);
        }
        if (character == '(') {
            ++depth;
        } else if (character == ')' && --depth < 0) {
            return "A \")\" closes no parenthesis";
        }
    }
    if (depth > 0) {
        return "A parenthesis is not closed";
    }
    return std::nullopt;
}

/**
 * @brief   Reads the names between the parentheses of V(...) or I(...), and checks how many there are.
 */
std::expected<std::vector<std::string>, std::string> ReadNames(const std::string_view arguments, const Kind type) {
    std::vector<std::string> names;
    std::size_t start = 0;
    while (start <= arguments.size()) {
        const std::size_t comma = std::min(arguments.find(',', start), arguments.size());
        names.emplace_back(Trim(arguments.substr(start, comma - start)));
        start = comma + 1;
    }
    if (type == Kind::CURRENT) {
        if (names.size() != 1 || names.front().empty() ||
            std::ranges::any_of(names.front(), [](const char character) { return IsSpace(character); })) {
            return std::unexpected("I() takes the name of a current, such as I(R1) or I(Q1.C)");
        }
        return names;
    }
    if (names.size() > 2 || std::ranges::any_of(names, [](const std::string &name) { return name.empty(); })) {
        return std::unexpected("V() takes one or two net names, such as V(OUT) or V(IN, OUT)");
    }
    for (const std::string &name : names) {
        if (!NetLabel::IsValidNet(name)) {
            return std::unexpected(std::format("\"{}\" is not a net name", name));
        }
    }
    return names;
}

} // namespace

/**
 * @brief   Finds the voltages and currents an expression reads, and checks what imcsim can check before ngspice.
 * @param[in] text  Expression such as "V(IN)*V(LO)", "1m*tanh(V(IN, REF)/26m)" or "I(R1)*1k"; V and I ignore case.
 * @return  Every V() and I() in the order written, or a message saying what is wrong: an empty expression, a character
 *          no expression holds, a parenthesis that does not close, or a V() or I() without the names it needs.
 * @note    Functions, numbers and operators are left to ngspice, which reports its own errors when it runs.
 */
std::expected<std::vector<BehavioralReference>, std::string> ParseBehavioralExpression(const std::string_view text) {
    if (Trim(text).empty()) {
        return std::unexpected("Write an expression, such as V(IN)*2");
    }
    if (const std::optional<std::string> problem = FindSyntaxProblem(text)) {
        return std::unexpected(*problem);
    }
    std::vector<BehavioralReference> references;
    std::size_t position = 0;
    while (position < text.size()) {
        if (!IsWordCharacter(text[position])) {
            ++position;
            continue;
        }
        const std::size_t start = position;
        while (position < text.size() && IsWordCharacter(text[position])) {
            ++position;
        }
        const std::string_view word = text.substr(start, position - start);
        const bool is_voltage = word == "v" || word == "V";
        if (!is_voltage && word != "i" && word != "I") {
            continue;
        }
        std::size_t open = position;
        while (open < text.size() && IsSpace(text[open])) {
            ++open;
        }
        // A lone v or i, not followed by a parenthesis, is left to ngspice
        if (open >= text.size() || text[open] != '(') {
            continue;
        }
        const std::size_t close = text.find(')', open);
        const std::string_view arguments = text.substr(open + 1, close - open - 1);
        if (arguments.contains('(')) {
            return std::unexpected(std::format("{}() takes names, not an expression", word));
        }
        const Kind type = is_voltage ? Kind::VOLTAGE : Kind::CURRENT;
        auto names = ReadNames(arguments, type);
        if (!names) {
            return std::unexpected(names.error());
        }
        references.push_back({.Type = type, .Names = std::move(*names), .Start = start, .Length = close + 1 - start});
        position = close + 1;
    }
    return references;
}

/**
 * @brief   Lists the currents an expression reads, so the parts that carry them get a probe in the netlist.
 * @param[in] text  Expression of a behavioral source.
 * @return  The names in I(), in the order written; none when the expression does not parse.
 */
std::vector<std::string> ListBehavioralCurrents(const std::string_view text) {
    std::vector<std::string> currents;
    if (const auto references = ParseBehavioralExpression(text)) {
        for (const BehavioralReference &reference : *references) {
            if (reference.Type == Kind::CURRENT) {
                currents.push_back(reference.Names.front());
            }
        }
    }
    return currents;
}

/**
 * @brief   Writes an expression as ngspice reads it: every net becomes its node, v(3) or v(3,5), and every current the
 *          voltage source ngspice follows it through, i(vprobe-r1).
 * @param[in] text         Expression of a behavioral source.
 * @param[in] find_node    Finds the node of a net; ground is 0 or GND.
 * @param[in] find_source  Finds the source a current is read through.
 * @return  The expression for the netlist, or why it cannot be written: a syntax problem, a net no label names or a
 *          current the circuit does not have.
 */
std::expected<std::string, std::string> TranslateBehavioralExpression(const std::string_view text,
                                                                      const NetNodeFinder &find_node,
                                                                      const CurrentSourceFinder &find_source) {
    const auto references = ParseBehavioralExpression(text);
    if (!references) {
        return std::unexpected(references.error());
    }
    std::string written;
    std::size_t copied = 0;
    for (const BehavioralReference &reference : *references) {
        written += text.substr(copied, reference.Start - copied);
        copied = reference.Start + reference.Length;
        if (reference.Type == Kind::CURRENT) {
            const std::optional<std::string> source = find_source(reference.Names.front());
            if (!source) {
                return std::unexpected(std::format("The circuit has no current I({})", reference.Names.front()));
            }
            written += std::format("i({})", *source);
            continue;
        }
        std::string nodes;
        for (const std::string &name : reference.Names) {
            const std::optional<int> node = find_node(name);
            if (!node) {
                return std::unexpected(std::format("The circuit has no net named {}", name));
            }
            nodes += std::format("{}{}", nodes.empty() ? "" : ",", *node);
        }
        written += std::format("v({})", nodes);
    }
    written += text.substr(copied);
    return written;
}

} // namespace Core
