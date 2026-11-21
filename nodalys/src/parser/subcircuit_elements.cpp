/**
 * @file    subcircuit_elements.cpp
 * @brief   Adds one element line of a subcircuit kept as SPICE text to the circuit being built.
 */

#include "parser/subcircuit_elements.h"

#include "nodalys/expression.h"
#include "parser/expression_references.h"
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
#include <set>
#include <string_view>

namespace Nodalys {

namespace {

using Result = std::expected<void, std::string>;

/// The index of the first node of an element line, after its name.
constexpr std::size_t FIRST_NODE_INDEX = 1;
constexpr std::size_t TWO_TERMINAL_VALUE_INDEX = 3;

/**
 * @struct  ElementLine
 * @brief   An element line as the handlers read it.
 */
struct ElementLine {
    /// The scope the line is in, which evaluates its values, names its nodes and leads to the expansion.
    TextScope &Scope;
    std::string_view Text;
    const std::vector<std::string> &Tokens;
    /// The element letter in upper case, such as 'V', which tells a handler that reads two letters which one it has.
    char Letter;
    /// The name the element has in the circuit, with the instance it sits in.
    std::string Name;

    NodeId ResolveNode(std::size_t position) const;
    CircuitBuilder &GetBuilder() const;
};

/**
 * @struct  Arguments
 * @brief   The tokens of an element after its nodes: those written as a bare value or word, and the NAME=VALUE ones.
 */
struct Arguments {
    std::vector<std::string> Positional;
    std::map<std::string, std::string> Options;
};

using Handler = Result (*)(const ElementLine &element);

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
 */
Result CheckOptions(const Arguments &arguments, const std::initializer_list<std::string_view> allowed) {
    for (const auto &[key, value] : arguments.Options) {
        if (std::ranges::find(allowed, key) == allowed.end()) {
            return std::unexpected(std::format("the option {}= is not simulated", key));
        }
    }
    return {};
}

Result CheckPositionalCount(const Arguments &arguments, const std::size_t most) {
    if (arguments.Positional.size() > most) {
        return std::unexpected(std::format("'{}' is not understood here", arguments.Positional[most]));
    }
    return {};
}

Result CheckNodeCount(const ElementLine &element, const std::size_t count) {
    if (element.Tokens.size() < FIRST_NODE_INDEX + count) {
        return std::unexpected(std::format("it needs {} nodes", count));
    }
    return {};
}

/**
 * @brief   The node at a position of the line, counted from its first node, which the scope makes when it is new.
 */
NodeId ElementLine::ResolveNode(const std::size_t position) const {
    return Scope.ResolveNode(Tokens[FIRST_NODE_INDEX + position]);
}

CircuitBuilder &ElementLine::GetBuilder() const {
    return Scope.GetContext().Builder;
}

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
 * @struct  SourceValues
 * @brief   What an independent source line gives: its DC value, its small-signal magnitude and phase, and its
 *          waveform with the numbers in the parentheses.
 */
struct SourceValues {
    SourceLevels Levels;
    SourceWaveform Waveform = SourceWaveform::NONE;
    std::vector<double> WaveformValues;
};

Result ReadInto(const TextScope &scope, const std::string &text, double &target) {
    const auto value = scope.Evaluate(text);
    if (!value) {
        return std::unexpected(value.error());
    }
    target = *value;
    return {};
}

/**
 * @brief   Reads the magnitude after the word AC, at index, and the phase when a value follows it.
 */
Result
ReadAcValues(const TextScope &scope, const std::vector<std::string> &tokens, std::size_t &index, SourceValues &values) {
    Result done = ReadInto(scope, tokens[index + 1], values.Levels.AcMagnitude);
    index += 2;
    if (done && index < tokens.size() && scope.Evaluate(tokens[index])) {
        done = ReadInto(scope, tokens[index], values.Levels.AcPhaseDegrees);
        ++index;
    }
    return done;
}

/**
 * @brief   Reads the numbers inside the parentheses of a waveform, each one a literal or an expression of the
 *          parameters the scope sees.
 */
Result ReadWaveformValues(const TextScope &scope, const std::string &token, std::vector<double> &values) {
    const std::size_t open = token.find('(');
    const std::size_t close = token.rfind(')');
    if (close == std::string::npos || close < open) {
        return std::unexpected(std::format("the waveform '{}' has no closing parenthesis", token));
    }
    for (const std::string &text : SplitTokens(std::string_view(token).substr(open + 1, close - open - 1))) {
        const auto value = scope.Evaluate(text);
        if (!value) {
            return std::unexpected(value.error());
        }
        values.push_back(*value);
    }
    return {};
}

/**
 * @brief   Reads the word of a source line at index, with the value that belongs to it, and moves index past them.
 * @return  Nothing, or why the word cannot be simulated: anything but a DC value, an AC value or a waveform.
 */
Result ReadSourceWord(const TextScope &scope,
                      const std::vector<std::string> &tokens,
                      std::size_t &index,
                      SourceValues &values) {
    const std::string upper = ToUpper(tokens[index]);
    const bool has_next = index + 1 < tokens.size();
    if (upper == "DC" && has_next) {
        index += 2;
        return ReadInto(scope, tokens[index - 1], values.Levels.Dc);
    }
    if (upper.starts_with("DC=")) {
        ++index;
        return ReadInto(scope, tokens[index - 1].substr(3), values.Levels.Dc);
    }
    if (upper == "AC" && has_next) {
        return ReadAcValues(scope, tokens, index, values);
    }
    if (const SourceWaveform waveform = FindSourceWaveform(tokens[index]); waveform != SourceWaveform::NONE) {
        values.Waveform = waveform;
        values.WaveformValues.clear();
        ++index;
        return ReadWaveformValues(scope, tokens[index - 1], values.WaveformValues);
    }
    if (index == TWO_TERMINAL_VALUE_INDEX && tokens[index].find('(') == std::string::npos) {
        ++index;
        return ReadInto(scope, tokens[index - 1], values.Levels.Dc);
    }
    const std::string word = tokens[index].substr(0, tokens[index].find('('));
    return std::unexpected(std::format("the source word or option '{}' is not simulated inside a "
                                       "subcircuit kept as text; only DC and AC values and the waveforms are",
                                       word));
}

/**
 * @brief   Reads "[DC] value [AC magnitude [phase]]" and a waveform such as PULSE or SIN of a source line.
 */
std::expected<SourceValues, std::string> ReadSource(const ElementLine &element) {
    if (const Result nodes = CheckNodeCount(element, 2); !nodes) {
        return std::unexpected(nodes.error());
    }
    SourceValues values;
    std::size_t index = TWO_TERMINAL_VALUE_INDEX;
    while (index < element.Tokens.size()) {
        if (const Result done = ReadSourceWord(element.Scope, element.Tokens, index, values); !done) {
            return std::unexpected(done.error());
        }
    }
    return values;
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
    if (const ExpansionContext &context = element.Scope.GetContext(); context.OnSource) {
        context.OnSource(element.Name, positive, negative);
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
    std::set<std::string> read_keys;
    const DiodeParameters parameters = BuildDiodeParameters((*model)->Card, &read_keys);
    ReportIgnoredParameters(element.Scope.GetContext(), **model, read_keys);
    element.GetBuilder().AddDiode(element.Name, element.ResolveNode(0), element.ResolveNode(1), parameters, *area);
    return {};
}

Result AddBjtLine(const ElementLine &element) {
    constexpr std::size_t NODES = 3;
    if (const Result nodes = CheckNodeCount(element, NODES + 1); !nodes) {
        return nodes;
    }
    // The fourth word is the model, unless the model is the fifth and the fourth is the substrate node
    const std::size_t model_index =
        element.Scope.FindModel(ToUpper(element.Tokens[FIRST_NODE_INDEX + NODES])) == nullptr
                && element.Tokens.size() > FIRST_NODE_INDEX + NODES + 1
            ? FIRST_NODE_INDEX + NODES + 1
            : FIRST_NODE_INDEX + NODES;
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
    std::set<std::string> read_keys;
    const BjtParameters parameters = BuildBjtParameters((*model)->Card, &read_keys);
    ReportIgnoredParameters(element.Scope.GetContext(), **model, read_keys);
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
    std::set<std::string> read_keys;
    MosfetParameters parameters = BuildMosfetParameters((*model)->Card, &read_keys);
    for (const auto &[key, text] : arguments.Options) {
        const auto value = element.Scope.Evaluate(text);
        if (!value) {
            return std::unexpected(value.error());
        }
        (key == "L" ? parameters.Length : parameters.Width) = *value;
    }
    ReportIgnoredParameters(element.Scope.GetContext(), **model, read_keys);
    element.GetBuilder().AddMosfet(element.Name,
                                   element.ResolveNode(0),
                                   element.ResolveNode(1),
                                   element.ResolveNode(2),
                                   element.ResolveNode(3),
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
    std::set<std::string> read_keys;
    const SwitchParameters parameters = BuildSwitchParameters((*model)->Card, &read_keys);
    ReportIgnoredParameters(element.Scope.GetContext(), **model, read_keys);
    element.GetBuilder().AddVoltageControlledSwitch(element.Name,
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
 * @brief   Remembers the name of a voltage source line, so that a controlled source or an expression of the same
 *          scope can read its current, even when it is written before the source.
 * @param[in,out] scope   Scope the line is in.
 * @param[in]     tokens  Tokens of the line.
 */
void NoteVoltageSource(TextScope &scope, const std::vector<std::string> &tokens) {
    if (!tokens.empty() && std::toupper(static_cast<unsigned char>(tokens[0].front())) == 'V') {
        scope.AddSourceName(ToUpper(tokens[0]), tokens[0]);
    }
}

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
