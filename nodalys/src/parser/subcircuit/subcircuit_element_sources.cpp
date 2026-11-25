/**
 * @file    subcircuit_element_sources.cpp
 * @brief   Independent, controlled, and behavioral source element handlers for subcircuits kept as SPICE text.
 */

#include "parser/subcircuit/subcircuit_element_sources.h"

#include "nodalys/expression.h"
#include "parser/sources/source_line.h"
#include "parser/sources/source_waveforms.h"
#include "parser/syntax/element_arguments.h"
#include "parser/syntax/expression_references.h"
#include "parser/syntax/spice_text.h"
#include "parser/syntax/spice_values.h"
#include "text/text_case.h"
#include <cctype>
#include <cstddef>
#include <format>
#include <optional>
#include <string_view>

namespace Nodalys {

namespace {

using Result = std::expected<void, std::string>;

/**
 * @brief   Reads "[DC] value [AC magnitude [phase]]" and a waveform such as PULSE or SIN of a source line, each number
 *          a literal or an expression of the parameters the scope sees, refusing any word it does not know.
 */
std::expected<SourceValues, std::string> ReadSource(const ElementLine &element) {
    if (const Result nodes = CheckNodeCount(element, 2); !nodes) {
        return std::unexpected(nodes.error());
    }
    const TextScope &scope = element.Scope;
    return ReadSourceLine(
        element.Tokens,
        TWO_TERMINAL_VALUE_INDEX,
        [&scope](const std::string_view text) { return scope.Evaluate(text); },
        SourceWordPolicy::REFUSE_UNKNOWN);
}

/**
 * @brief   Writes an expression of the text for the builder: parameters replaced by their values, nodes by their
 *          numbers and the sources whose current it reads by the names they have in the circuit.
 */
std::expected<std::string, std::string> ResolveExpression(TextScope &scope, const std::string_view text) {
    const std::string substituted = SubstituteParameters(StripValueBrackets(text), [&scope](const std::string &name) {
        return scope.FindParameter(name);
    });
    const auto expression = Expression::Parse(substituted);
    if (!expression) {
        return std::unexpected(std::format("the expression '{}' is not understood: {}", text, expression.error()));
    }
    return RewriteExpressionReferences(
        substituted,
        *expression,
        [&](const std::string_view node) { return scope.ResolveNode(node); },
        [&scope](const std::string_view source) -> std::expected<std::string, std::string> {
            if (const std::optional<std::string> name = scope.FindSourceName(ToUpper(source))) {
                return *name;
            }
            return std::unexpected(
                std::format("it reads the current of {}, which is not a voltage source of the subcircuit", source));
        });
}

/**
 * @brief   Adds a source whose output is an expression, from the text after the equals sign of its line.
 */
Result AddExpressionSource(const ElementLine &element, const bool is_voltage, const std::string_view expression_text) {
    const auto expression = ResolveExpression(element.Scope, expression_text);
    if (!expression) {
        return std::unexpected(expression.error());
    }
    const NodeId positive = element.ResolveNode(0);
    const NodeId negative = element.ResolveNode(1);
    if (is_voltage) {
        element.GetBuilder().AddBehavioralVoltage(element.Name, positive, negative, *expression);
    } else {
        element.GetBuilder().AddBehavioralCurrent(element.Name, positive, negative, *expression);
    }
    return {};
}

/**
 * @brief   The text after the first equals sign of a line, which a behavioral source writes its expression after.
 */
std::string_view FindExpressionText(const std::string_view line) {
    const std::size_t equals = line.find('=');
    return equals == std::string_view::npos ? std::string_view() : TrimSpaces(line.substr(equals + 1));
}

} // namespace

/**
 * @brief   Adds an independent voltage (`V`) or current (`I`) source line inside a subcircuit expansion.
 * @param[in] element  Parsed element line within its subcircuit scope.
 * @return  Nothing on success, or a diagnostic error string.
 */
Result AddSourceLine(const ElementLine &element) {
    const auto values = ReadSource(element);
    if (!values) {
        return std::unexpected(values.error());
    }
    const NodeId positive = element.ResolveNode(0);
    const NodeId negative = element.ResolveNode(1);
    AddWaveformSource(element.GetBuilder(),
                      element.Name,
                      element.Letter == 'V',
                      positive,
                      negative,
                      values->Waveform,
                      values->WaveformValues,
                      values->Levels);
    if (const InstanceObservers &observers = element.Scope.GetContext().Observers; observers.OnSource) {
        observers.OnSource(element.Name, positive, negative);
    }
    return {};
}

/**
 * @brief   Adds a behavioral source (`B`) line inside a subcircuit expansion.
 * @param[in] element  Parsed element line within its subcircuit scope.
 * @return  Nothing on success, or a diagnostic error string.
 */
Result AddBehavioralLine(const ElementLine &element) {
    if (const Result nodes = CheckNodeCount(element, 2); !nodes) {
        return nodes;
    }
    const std::size_t equals = element.Text.find('=');
    const std::string_view before = equals == std::string_view::npos ? element.Text : element.Text.substr(0, equals);
    const std::string_view kind = TrimSpaces(before);
    if (equals == std::string_view::npos || kind.empty()
        || (std::toupper(static_cast<unsigned char>(kind.back())) != 'V'
            && std::toupper(static_cast<unsigned char>(kind.back())) != 'I')) {
        return std::unexpected("a behavioral source is written B<name> <node> <node> V=<expression> or I=<expression>");
    }
    const bool is_voltage = std::toupper(static_cast<unsigned char>(kind.back())) == 'V';
    return AddExpressionSource(element, is_voltage, FindExpressionText(element.Text));
}

/**
 * @brief   Adds a voltage-controlled source (`E` or `G`) line inside a subcircuit expansion.
 * @param[in] element  Parsed element line within its subcircuit scope.
 * @return  Nothing on success, or a diagnostic error string.
 */
Result AddVoltageControlledLine(const ElementLine &element) {
    const bool is_voltage = element.Letter == 'E';
    if (const Result nodes = CheckNodeCount(element, 3); !nodes) {
        return nodes;
    }
    const std::string third = ToUpper(element.Tokens[FIRST_NODE_INDEX + 2]);
    if (third.starts_with("VALUE")) {
        return AddExpressionSource(element, is_voltage, FindExpressionText(element.Text));
    }
    if (third.starts_with("POLY") || third.starts_with("TABLE") || third.starts_with("LAPLACE")
        || third.starts_with("FREQ") || third.starts_with("CUR")) {
        return std::unexpected(std::format("the form '{}' of a controlled source is not simulated, only a gain and "
                                           "VALUE={{expression}}",
                                           element.Tokens[FIRST_NODE_INDEX + 2].substr(0, third.find('('))));
    }
    constexpr std::size_t NODES_AND_GAIN = 5;
    if (element.Tokens.size() != FIRST_NODE_INDEX + NODES_AND_GAIN) {
        return std::unexpected("it is written <node> <node> <control node> <control node> <gain>");
    }
    const auto gain = element.Scope.Evaluate(element.Tokens[FIRST_NODE_INDEX + NODES_AND_GAIN - 1]);
    if (!gain) {
        return std::unexpected(gain.error());
    }
    const NodeId positive = element.ResolveNode(0);
    const NodeId negative = element.ResolveNode(1);
    const NodeId control_positive = element.ResolveNode(2);
    const NodeId control_negative = element.ResolveNode(3);
    if (is_voltage) {
        element.GetBuilder().AddVcvs(element.Name, positive, negative, control_positive, control_negative, *gain);
    } else {
        element.GetBuilder().AddVccs(element.Name, positive, negative, control_positive, control_negative, *gain);
    }
    return {};
}

/**
 * @brief   Adds a current-controlled source (`F` or `H`) line inside a subcircuit expansion.
 * @param[in] element  Parsed element line within its subcircuit scope.
 * @return  Nothing on success, or a diagnostic error string.
 */
Result AddCurrentControlledLine(const ElementLine &element) {
    constexpr std::size_t NODES_SOURCE_AND_GAIN = 4;
    if (const Result nodes = CheckNodeCount(element, 3); !nodes) {
        return nodes;
    }
    if (ToUpper(element.Tokens[FIRST_NODE_INDEX + 2]).starts_with("POLY")) {
        return std::unexpected("the form POLY of a controlled source is not simulated, only a gain");
    }
    if (element.Tokens.size() != FIRST_NODE_INDEX + NODES_SOURCE_AND_GAIN) {
        return std::unexpected("it is written <node> <node> <voltage source> <gain>");
    }
    const std::string &controller = element.Tokens[FIRST_NODE_INDEX + 2];
    const std::optional<std::string> source = element.Scope.FindSourceName(ToUpper(controller));
    if (!source) {
        return std::unexpected(std::format("it reads the current of {}, which is not a voltage source of the "
                                           "subcircuit",
                                           controller));
    }
    const auto gain = element.Scope.Evaluate(element.Tokens[FIRST_NODE_INDEX + NODES_SOURCE_AND_GAIN - 1]);
    if (!gain) {
        return std::unexpected(gain.error());
    }
    const NodeId positive = element.ResolveNode(0);
    const NodeId negative = element.ResolveNode(1);
    if (element.Letter == 'H') {
        element.GetBuilder().AddCcvs(element.Name, positive, negative, *source, *gain);
    } else {
        element.GetBuilder().AddCccs(element.Name, positive, negative, *source, *gain);
    }
    return {};
}

} // namespace Nodalys
