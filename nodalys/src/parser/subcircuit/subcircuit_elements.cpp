/**
 * @file    subcircuit_elements.cpp
 * @brief   Adds one element line of a subcircuit kept as SPICE text to the circuit being built.
 */

#include "parser/subcircuit/subcircuit_elements.h"

#include "parser/subcircuit/subcircuit_element_sources.h"
#include "parser/subcircuit/subcircuit_models.h"
#include "parser/syntax/element_arguments.h"
#include "text/text_case.h"

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
