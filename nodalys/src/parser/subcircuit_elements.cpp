/**
 * @file    subcircuit_elements.cpp
 * @brief   Adds one element line of a subcircuit kept as SPICE text to the circuit being built.
 */

#include "parser/subcircuit_elements.h"

#include "nodalys/expression.h"
#include "parser/element_arguments.h"
#include "parser/expression_references.h"
#include "parser/source_line.h"
#include "parser/source_waveforms.h"
#include "parser/spice_text.h"
#include "parser/spice_values.h"
#include "parser/subcircuit_models.h"
#include "text/text_case.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <string_view>

namespace Nodalys {

namespace {

using Result = std::expected<void, std::string>;

using Handler = Result (*)(const ElementLine &element);

/**
 * @brief   The value of a two-terminal element and its initial condition.
 */
struct PassiveValues {
    double Value = 0.0;
    double InitialCondition = 0.0;
};

/**
 * @brief   The text of the value of a two-terminal element: its NAME=VALUE option, such as R=1k, or else the word after
 *          its nodes.
 */
std::optional<std::string> FindValueText(const Arguments &arguments, const std::string_view value_option) {
    if (const auto option = arguments.Options.find(std::string(value_option)); option != arguments.Options.end()) {
        return option->second;
    }
    if (!arguments.Positional.empty()) {
        return arguments.Positional.front();
    }
    return std::nullopt;
}

/**
 * @brief   Reads the value of a resistor, capacitor or inductor and, when the element has one, its initial condition.
 * @param[in] element  The element line.
 * @param[in] options  The options the element takes; the first is the one that may give its value, such as "R", and
 *                     an element that keeps an initial condition lists "IC" too.
 */
std::expected<PassiveValues, std::string> ReadPassive(const ElementLine &element,
                                                      const std::initializer_list<std::string_view> options) {
    if (const Result nodes = CheckNodeCount(element, 2); !nodes) {
        return std::unexpected(nodes.error());
    }
    const Arguments arguments = SplitArguments(element.Tokens, TWO_TERMINAL_VALUE_INDEX);
    if (const Result checked = CheckOptions(arguments, options); !checked) {
        return std::unexpected(checked.error());
    }
    if (const Result counted = CheckPositionalCount(arguments, 1); !counted) {
        return std::unexpected(counted.error());
    }
    const std::optional<std::string> text = FindValueText(arguments, *options.begin());
    if (!text) {
        return std::unexpected("it has no value");
    }
    PassiveValues values;
    const auto value = element.Scope.Evaluate(*text);
    if (!value) {
        return std::unexpected(value.error());
    }
    values.Value = *value;
    // CheckOptions has refused IC on an element that does not take it
    if (const auto initial = arguments.Options.find("IC"); initial != arguments.Options.end()) {
        const auto condition = element.Scope.Evaluate(initial->second);
        if (!condition) {
            return std::unexpected(condition.error());
        }
        values.InitialCondition = *condition;
    }
    return values;
}

Result AddResistorLine(const ElementLine &element) {
    const auto values = ReadPassive(element, {"R"});
    if (!values) {
        return std::unexpected(values.error());
    }
    element.GetBuilder().AddResistor(element.Name, element.ResolveNode(0), element.ResolveNode(1), values->Value);
    return {};
}

/**
 * @brief   Adds the capacitor C or the inductor L, which take their value under their own letter and keep an initial
 *          condition.
 */
Result AddStorageLine(const ElementLine &element) {
    const std::string_view value_option(&element.Letter, 1);
    const auto values = ReadPassive(element, {value_option, "IC"});
    if (!values) {
        return std::unexpected(values.error());
    }
    const auto add = element.Letter == 'C' ? &CircuitBuilder::AddCapacitor : &CircuitBuilder::AddInductor;
    std::invoke(add,
                element.GetBuilder(),
                element.Name,
                element.ResolveNode(0),
                element.ResolveNode(1),
                values->Value,
                values->InitialCondition);
    return {};
}

/**
 * @brief   Reads "[DC] value [AC magnitude [phase]]" and a waveform such as PULSE or SIN of a source line, each number
 * a literal or an expression of the parameters the scope sees, refusing any word it does not know.
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
 * @brief   Adds the independent source V or I, and tells the caller of the expansion about it.
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
 * @brief   Adds the voltage-controlled source E or G: a gain between two nodes, or "VALUE = expression".
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
 * @brief   Adds the current-controlled source F or H, which reads the current of a voltage source of the subcircuit.
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

/**
 * @brief   Reads the area of a diode or a bipolar transistor, given as the word after its model or as AREA=.
 */
std::expected<double, std::string>
ReadArea(const TextScope &scope, const Arguments &arguments, const std::size_t area_position) {
    if (const Result checked = CheckOptions(arguments, {"AREA"}); !checked) {
        return std::unexpected(checked.error());
    }
    if (const Result counted = CheckPositionalCount(arguments, area_position + 1); !counted) {
        return std::unexpected(counted.error());
    }
    if (const auto option = arguments.Options.find("AREA"); option != arguments.Options.end()) {
        return scope.Evaluate(option->second);
    }
    if (arguments.Positional.size() > area_position) {
        return scope.Evaluate(arguments.Positional[area_position]);
    }
    return 1.0;
}

Result AddDiodeLine(const ElementLine &element) {
    constexpr std::size_t NODES = 2;
    if (const Result nodes = CheckNodeCount(element, NODES + 1); !nodes) {
        return nodes;
    }
    const auto model =
        UseTextModel(element.Scope, element.Tokens[FIRST_NODE_INDEX + NODES], {ParsedModel::ModelKind::DIODE});
    if (!model) {
        return std::unexpected(model.error());
    }
    const auto area = ReadArea(element.Scope, SplitArguments(element.Tokens, FIRST_NODE_INDEX + NODES + 1), 0);
    if (!area) {
        return std::unexpected(area.error());
    }
    const DiodeParameters parameters = UseModelParameters(element.Scope.GetContext(), **model, BuildDiodeParameters);
    element.GetBuilder().AddDiode(element.Name, element.ResolveNode(0), element.ResolveNode(1), parameters, *area);
    return {};
}

/**
 * @brief   Tells where the model of a bipolar transistor line is: the word after its three nodes, unless no model has
 *          that name and a word follows it, which makes it the substrate node and the next word the model.
 * @note    The line must have its three nodes and one word after them.
 */
std::size_t FindBjtModelIndex(const ElementLine &element) {
    constexpr std::size_t AFTER_NODES = FIRST_NODE_INDEX + 3;
    const bool is_substrate = element.Scope.FindModel(ToUpper(element.Tokens[AFTER_NODES])) == nullptr
                           && element.Tokens.size() > AFTER_NODES + 1;
    return is_substrate ? AFTER_NODES + 1 : AFTER_NODES;
}

Result AddBjtLine(const ElementLine &element) {
    constexpr std::size_t NODES = 3;
    if (const Result nodes = CheckNodeCount(element, NODES + 1); !nodes) {
        return nodes;
    }
    const std::size_t model_index = FindBjtModelIndex(element);
    const auto model = UseTextModel(element.Scope,
                                    element.Tokens[model_index],
                                    {ParsedModel::ModelKind::BJT_NPN, ParsedModel::ModelKind::BJT_PNP});
    if (!model) {
        return std::unexpected(model.error());
    }
    const auto area = ReadArea(element.Scope, SplitArguments(element.Tokens, model_index + 1), 0);
    if (!area) {
        return std::unexpected(area.error());
    }
    const BjtParameters parameters = UseModelParameters(element.Scope.GetContext(), **model, BuildBjtParameters);
    element.GetBuilder().AddBjt(element.Name,
                                element.ResolveNode(0),
                                element.ResolveNode(1),
                                element.ResolveNode(2),
                                (*model)->Card.Kind != ParsedModel::ModelKind::BJT_PNP,
                                parameters,
                                *area);
    return {};
}

Result AddMosfetLine(const ElementLine &element) {
    constexpr std::size_t NODES = 4;
    if (const Result nodes = CheckNodeCount(element, NODES + 1); !nodes) {
        return nodes;
    }
    const auto model = UseTextModel(element.Scope,
                                    element.Tokens[FIRST_NODE_INDEX + NODES],
                                    {ParsedModel::ModelKind::MOSFET_NMOS, ParsedModel::ModelKind::MOSFET_PMOS});
    if (!model) {
        return std::unexpected(model.error());
    }
    const Arguments arguments = SplitArguments(element.Tokens, FIRST_NODE_INDEX + NODES + 1);
    if (const Result checked = CheckOptions(arguments, {"L", "W"}); !checked) {
        return checked;
    }
    if (const Result counted = CheckPositionalCount(arguments, 0); !counted) {
        return counted;
    }
    MosfetParameters parameters = UseModelParameters(element.Scope.GetContext(), **model, BuildMosfetParameters);
    for (const auto &[key, text] : arguments.Options) {
        const auto value = element.Scope.Evaluate(text);
        if (!value) {
            return std::unexpected(value.error());
        }
        (key == "L" ? parameters.Length : parameters.Width) = *value;
    }
    element.GetBuilder().AddMosfet(element.Name,
                                   element.ResolveNode(0),
                                   element.ResolveNode(1),
                                   element.ResolveNode(2),
                                   (*model)->Card.Kind != ParsedModel::ModelKind::MOSFET_PMOS,
                                   parameters);
    return {};
}

Result AddSwitchLine(const ElementLine &element) {
    constexpr std::size_t NODES = 4;
    if (const Result nodes = CheckNodeCount(element, NODES + 1); !nodes) {
        return nodes;
    }
    const auto model =
        UseTextModel(element.Scope, element.Tokens[FIRST_NODE_INDEX + NODES], {ParsedModel::ModelKind::SWITCH});
    if (!model) {
        return std::unexpected(model.error());
    }
    if (element.Tokens.size() > FIRST_NODE_INDEX + NODES + 1) {
        return std::unexpected(std::format("'{}' is not understood here; the initial state of a switch is not "
                                           "simulated",
                                           element.Tokens[FIRST_NODE_INDEX + NODES + 1]));
    }
    const SwitchParameters parameters = UseModelParameters(element.Scope.GetContext(), **model, BuildSwitchParameters);
    element.GetBuilder().AddSwitch(element.Name,
                                   element.ResolveNode(0),
                                   element.ResolveNode(1),
                                   element.ResolveNode(2),
                                   element.ResolveNode(3),
                                   parameters);
    return {};
}

/**
 * @brief   What an element letter that the engine has no device for stands for, to say it in the message.
 */
std::string_view DescribeUnsupportedLetter(const char letter) {
    switch (letter) {
    case 'J':
        return "a JFET";
    case 'Z':
        return "a MESFET";
    case 'K':
        return "coupled inductors";
    case 'T':
        return "a transmission line";
    case 'O':
        return "a lossy transmission line";
    case 'U':
        return "a distributed RC line or a digital gate";
    case 'W':
        return "a current-controlled switch";
    case 'A':
        return "a code model";
    case 'P':
        return "a port";
    default:
        return "an element";
    }
}

/**
 * @brief   The handler of each element letter the engine can simulate inside a block kept as text.
 */
const std::map<char, Handler> &GetHandlers() {
    static const std::map<char, Handler> handlers{
        {'R', AddResistorLine},
        {'C', AddStorageLine},
        {'L', AddStorageLine},
        {'V', AddSourceLine},
        {'I', AddSourceLine},
        {'E', AddVoltageControlledLine},
        {'G', AddVoltageControlledLine},
        {'H', AddCurrentControlledLine},
        {'F', AddCurrentControlledLine},
        {'B', AddBehavioralLine},
        {'D', AddDiodeLine},
        {'Q', AddBjtLine},
        {'M', AddMosfetLine},
        {'S', AddSwitchLine},
    };
    return handlers;
}

} // namespace

/**
 * @brief   Adds one element line to the circuit: R, C, L, V, I, E, G, H, F, B, D, Q, M or S.
 * @details Any other element letter, and any option the element has no use for, is refused with the reason, since a
 *          line dropped without a word would change the circuit without anyone knowing.
 * @param[in,out] scope   Scope the line is in, which leads to the builder of the expansion.
 * @param[in]     line    The logical line.
 * @param[in]     tokens  Tokens of the line, the first being the element name.
 * @return  Nothing when the element was added, or why the engine cannot simulate it.
 */
std::expected<void, std::string>
AddTextElement(TextScope &scope, const std::string_view line, const std::vector<std::string> &tokens) {
    const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(tokens[0].front())));
    const auto handler = GetHandlers().find(letter);
    if (handler == GetHandlers().end()) {
        return std::unexpected(std::format("elements that start with {} ({}) are not simulated by Nodalys",
                                           letter,
                                           DescribeUnsupportedLetter(letter)));
    }
    return handler->second(ElementLine{.Scope = scope,
                                       .Text = line,
                                       .Tokens = tokens,
                                       .Letter = letter,
                                       .Name = scope.MakeName(tokens[0])});
}

} // namespace Nodalys
