/**
 * @file    element_arguments.cpp
 * @brief   Reads the generic parts of an element line of a subcircuit kept as SPICE text: its nodes, its bare and
 *          NAME=VALUE arguments, and the checks that refuse what the element has no use for.
 */

#include "parser/syntax/element_arguments.h"

#include "text/text_case.h"

#include <algorithm>
#include <format>
#include <optional>

namespace Nodalys {

namespace {

constexpr double DEFAULT_QUARTER_WAVE = 0.25;
constexpr double DEFAULT_REFERENCE_FREQUENCY_HZ = 1e9;

std::expected<std::optional<double>, std::string>
EvaluateOptional(const Arguments &arguments, const std::string_view key, const OptionEvaluator &evaluate) {
    const auto found = arguments.Options.find(std::string(key));
    if (found == arguments.Options.end()) {
        return std::nullopt;
    }
    const auto value = evaluate(key, found->second);
    if (!value) {
        return std::unexpected(value.error());
    }
    return *value;
}

std::expected<TransmissionLineParameters, std::string>
BuildTransmissionLineFromMap(const std::map<std::string_view, std::optional<double>> &values) {
    const std::optional<double> z0 = values.at("Z0") ? values.at("Z0") : values.at("ZO");
    if (!z0) {
        return std::unexpected("transmission line requires Z0");
    }
    std::optional<double> td = values.at("TD");
    const std::optional<double> freq = values.at("F");
    if (!td && freq && *freq > 0.0) {
        td = values.at("NL").value_or(DEFAULT_QUARTER_WAVE) / *freq;
    }
    if (!td) {
        return std::unexpected("transmission line requires TD or F");
    }
    return TransmissionLineParameters{
        .ImpedanceOhms = *z0,
        .DelaySeconds = *td,
        .DcResistanceOhms = values.at("RDC").value_or(0.0),
        .SkinResistanceOhms = values.at("RSKIN").value_or(0.0),
        .DcConductanceSiemens = values.at("GDC").value_or(0.0),
        .LossTangent = values.at("TAND").value_or(0.0),
        .ReferenceFrequencyHz = values.at("FREF").value_or(freq.value_or(DEFAULT_REFERENCE_FREQUENCY_HZ)),
    };
}

} // namespace

/**
 * @brief   The node at a position of the line, counted from its first node, which the scope makes when it is new.
 * @param[in] position  Position among the nodes, 0 for the first.
 * @return  The node.
 */
NodeId ElementLine::ResolveNode(const std::size_t position) const {
    return Scope.ResolveNode(Tokens[FIRST_NODE_INDEX + position]);
}

/**
 * @brief   Returns the builder of the expansion the line is added to.
 * @return  Mutable reference to the target CircuitBuilder.
 */
CircuitBuilder &ElementLine::GetBuilder() const {
    return Scope.GetContext().Builder;
}

/**
 * @brief   Parses and validates the `NAME=VALUE` options of a SPICE `T` transmission line element.
 * @param[in] arguments  Split positional and named arguments after the 4 port nodes.
 * @param[in] evaluate   Evaluates one `(key, text)` option value into a number.
 * @return  Populated `TransmissionLineParameters`, or an error description.
 */
std::expected<TransmissionLineParameters, std::string> ParseTransmissionLineOptions(const Arguments &arguments,
                                                                                    const OptionEvaluator &evaluate) {
    static constexpr std::initializer_list<std::string_view> KEYS =
        {"Z0", "ZO", "TD", "F", "NL", "RDC", "RSKIN", "GDC", "TAND", "FREF"};
    if (const auto checked = CheckOptions(arguments, KEYS); !checked) {
        return std::unexpected(checked.error());
    }
    std::map<std::string_view, std::optional<double>> values;
    for (const std::string_view key : KEYS) {
        const auto parsed = EvaluateOptional(arguments, key, evaluate);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        values.emplace(key, *parsed);
    }
    return BuildTransmissionLineFromMap(values);
}

/**
 * @brief   Sorts the tokens of an element from a position on into bare words and NAME=VALUE options.
 * @details A token is an option when its equals sign comes before any parenthesis and it is no braced or quoted
 *          expression, so "{A=B}" and "SIN(0 1 F=2)" stay bare.
 * @param[in] tokens  Tokens of the line.
 * @param[in] first   Index of the first token to sort.
 * @return  The bare words in order, and the options by upper-case name.
 */
Arguments SplitArguments(const std::vector<std::string> &tokens, const std::size_t first) {
    Arguments arguments;
    for (std::size_t index = first; index < tokens.size(); ++index) {
        const std::size_t equals = tokens[index].find('=');
        const bool is_option = equals != std::string::npos && equals > 0 && tokens[index].front() != '{'
                            && tokens[index].front() != '\'' && tokens[index].find('(') > equals;
        if (is_option) {
            arguments.Options[ToUpper(tokens[index].substr(0, equals))] = tokens[index].substr(equals + 1);
        } else {
            arguments.Positional.push_back(tokens[index]);
        }
    }
    return arguments;
}

/**
 * @brief   Refuses an option the element has no use for, so that none is dropped without a word.
 * @param[in] arguments  Arguments of the element.
 * @param[in] allowed    Upper-case names of the options the element reads.
 * @return  Nothing, or the first option it does not read.
 */
std::expected<void, std::string> CheckOptions(const Arguments &arguments,
                                              const std::initializer_list<std::string_view> allowed) {
    for (const auto &[key, value] : arguments.Options) {
        if (std::ranges::find(allowed, key) == allowed.end()) {
            return std::unexpected(std::format("the option {}= is not simulated", key));
        }
    }
    return {};
}

/**
 * @brief   Refuses a bare word past those the element reads.
 * @param[in] arguments  Arguments of the element.
 * @param[in] most       How many bare words the element reads.
 * @return  Nothing, or the first word too many.
 */
std::expected<void, std::string> CheckPositionalCount(const Arguments &arguments, const std::size_t most) {
    if (arguments.Positional.size() > most) {
        return std::unexpected(std::format("'{}' is not understood here", arguments.Positional[most]));
    }
    return {};
}

/**
 * @brief   Refuses a line with fewer nodes than its element needs.
 * @param[in] element  The element line.
 * @param[in] count    How many nodes the element needs.
 * @return  Nothing, or how many nodes it needs.
 */
std::expected<void, std::string> CheckNodeCount(const ElementLine &element, const std::size_t count) {
    if (element.Tokens.size() < FIRST_NODE_INDEX + count) {
        return std::unexpected(std::format("it needs {} nodes", count));
    }
    return {};
}

} // namespace Nodalys
