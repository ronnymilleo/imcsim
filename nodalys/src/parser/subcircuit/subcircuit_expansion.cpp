/**
 * @file    subcircuit_expansion.cpp
 * @brief   Expands the lines of a subcircuit kept as SPICE text in a scope: its definitions, its elements and the
 *          instances of the subcircuits it defines, each in a scope of its own.
 */

#include "parser/subcircuit/subcircuit_expansion.h"

#include "parser/subcircuit/subcircuit_elements.h"
#include "parser/subcircuit/subcircuit_lines.h"
#include "parser/subcircuit/subcircuit_models.h"
#include "text/text_case.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <optional>
#include <utility>

namespace Nodalys {

namespace {

/// How many subcircuits may sit inside one another, which stops a block that places itself.
constexpr int MAX_NESTING_DEPTH = 16;

using Result = std::expected<void, std::string>;

/**
 * @brief   The message of a line the engine cannot simulate, with the subcircuit it is in.
 */
std::string Refuse(const ExpansionContext &context, const std::string_view line, const std::string_view reason) {
    return std::format("Nodalys cannot simulate the subcircuit {}: the line '{}' {}",
                       context.SubcircuitName,
                       line,
                       reason);
}

Result ExpandScope(TextScope &scope, const std::vector<std::string> &lines, int depth);

/**
 * @brief   Makes the subcircuits written in a text known to its scope, which the X lines of the scope and of the
 *          subcircuits defined in it look them up from.
 */
void RegisterSubcircuits(TextScope &scope, std::vector<TextSubcircuit> subcircuits) {
    for (TextSubcircuit &subcircuit : subcircuits) {
        subcircuit.DefinedIn = &scope;
        const std::string key = ToUpper(subcircuit.Name);
        scope.AddSubcircuit(key, std::move(subcircuit));
    }
}

/**
 * @brief   Makes the voltage sources of a text known to its scope before any element is added, so a controlled source
 *          or an expression can read the current of one written after it.
 */
void RegisterVoltageSources(TextScope &scope, const std::vector<std::string> &names) {
    for (const std::string &name : names) {
        scope.AddSourceName(ToUpper(name), name);
    }
}

/**
 * @brief   Evaluates the .param lines in order. A value the instance passed is kept, since it overrides the default
 *          the text gives.
 */
Result DefineParameters(TextScope &scope, const std::vector<std::vector<std::string>> &parameter_lines) {
    const ExpansionContext &context = scope.GetContext();
    for (const std::vector<std::string> &tokens : parameter_lines) {
        for (const auto &[name, text] : SplitParameterTokens(tokens, 1)) {
            if (text.empty()) {
                return std::unexpected(Refuse(context, tokens[0], "has a parameter without a value"));
            }
            if (scope.HasOwnParameter(name)) {
                continue;
            }
            const auto value = scope.Evaluate(text);
            if (!value) {
                return std::unexpected(Refuse(context, std::format(".param {}={}", name, text), value.error()));
            }
            scope.SetParameter(name, *value);
        }
    }
    return {};
}

Result DefineModels(TextScope &scope, const std::vector<std::vector<std::string>> &model_lines) {
    const ExpansionContext &context = scope.GetContext();
    for (const std::vector<std::string> &tokens : model_lines) {
        auto model = ParseTextModel(scope, tokens);
        if (!model) {
            return std::unexpected(Refuse(context,
                                          std::format(".model {}", tokens.size() > 1 ? tokens[1] : ""),
                                          "is not understood: " + model.error()));
        }
        scope.AddModel(ToUpper(tokens[1]), std::move(*model));
    }
    return {};
}

/**
 * @brief   Gives each parameter of a called subcircuit its value: what the X line passes, or else its default.
 */
Result SetInstanceParameters(const TextScope &caller,
                             TextScope &instance,
                             const TextSubcircuit &subcircuit,
                             const std::string_view line,
                             const std::vector<std::pair<std::string, std::string>> &arguments) {
    const ExpansionContext &context = caller.GetContext();
    for (const auto &[name, text] : arguments) {
        const bool is_known =
            std::ranges::any_of(subcircuit.Parameters, [&name](const auto &formal) { return formal.first == name; });
        if (!is_known) {
            return std::unexpected(Refuse(context,
                                          line,
                                          std::format("passes the parameter {}, which {} does not "
                                                      "have",
                                                      name,
                                                      subcircuit.Name)));
        }
    }
    for (const auto &[name, default_text] : subcircuit.Parameters) {
        const auto passed = std::ranges::find(arguments, name, &std::pair<std::string, std::string>::first);
        const auto value =
            passed != arguments.end() ? caller.Evaluate(passed->second) : instance.Evaluate(default_text);
        if (!value) {
            return std::unexpected(
                Refuse(context, line, std::format("gives the parameter {} a value that {}", name, value.error())));
        }
        instance.SetParameter(name, *value);
    }
    return {};
}

/**
 * @brief   Expands an X line: a nested subcircuit of the text, with its own nodes, parameters and models.
 */
Result ExpandInstance(TextScope &scope, const TextElement &element, const int depth) {
    const ExpansionContext &context = scope.GetContext();
    const std::string &line = element.Line;
    const std::optional<InstanceLine> call = SplitInstanceLine(element.Tokens);
    if (!call) {
        return std::unexpected(Refuse(context, line, "is not written X<name> <nodes> <subcircuit>"));
    }
    const std::string &subcircuit_name = call->SubcircuitName;
    const TextSubcircuit *subcircuit = scope.FindSubcircuit(ToUpper(subcircuit_name));
    if (subcircuit == nullptr) {
        return std::unexpected(
            Refuse(context,
                   line,
                   std::format("uses the subcircuit {}, which the text does not define", subcircuit_name)));
    }
    const std::size_t node_count = call->NodeNames.size();
    if (node_count != subcircuit->Ports.size()) {
        return std::unexpected(Refuse(context,
                                      line,
                                      std::format("gives {} nodes to {}, which has {} ports",
                                                  node_count,
                                                  subcircuit->Name,
                                                  subcircuit->Ports.size())));
    }
    if (depth >= MAX_NESTING_DEPTH) {
        return std::unexpected(Refuse(context, line, "nests subcircuits too deeply; one of them places itself"));
    }
    TextScope instance(scope.GetContext(), subcircuit->DefinedIn, scope.MakeName(element.Tokens[0]));
    if (const Result set =
            SetInstanceParameters(scope, instance, *subcircuit, line, SplitParameterTokens(call->ParameterTokens, 0));
        !set) {
        return set;
    }
    for (std::size_t port = 0; port < node_count; ++port) {
        instance.MapNode(ToUpper(subcircuit->Ports[port]), scope.ResolveNode(call->NodeNames[port]));
    }
    return ExpandScope(instance, subcircuit->Lines, depth + 1);
}

/**
 * @brief   Adds everything a text holds to the circuit, as one instance in a scope.
 */
Result ExpandScope(TextScope &scope, const std::vector<std::string> &lines, const int depth) {
    const ExpansionContext &context = scope.GetContext();
    auto classified = ClassifyLines(lines);
    if (!classified) {
        return std::unexpected(Refuse(context, classified.error().Line, classified.error().Reason));
    }
    RegisterSubcircuits(scope, std::move(classified->Subcircuits));
    RegisterVoltageSources(scope, classified->VoltageSources);
    if (const Result parameters = DefineParameters(scope, classified->Parameters); !parameters) {
        return parameters;
    }
    if (const Result models = DefineModels(scope, classified->Models); !models) {
        return models;
    }
    for (const TextElement &element : classified->Elements) {
        if (std::toupper(static_cast<unsigned char>(element.Tokens[0].front())) == 'X') {
            if (const Result expanded = ExpandInstance(scope, element, depth); !expanded) {
                return expanded;
            }
        } else if (const Result added = AddTextElement(scope, element.Line, element.Tokens); !added) {
            return std::unexpected(Refuse(context, element.Line, "cannot be simulated: " + added.error()));
        }
    }
    return {};
}

} // namespace

/**
 * @brief   Adds everything a text holds to the circuit, as the instance a scope stands for.
 * @param[in,out] scope  Scope of the instance, with its ports mapped and the parameters it was given.
 * @param[in]     lines  The logical lines of the text.
 * @return  Nothing, or why the text cannot be simulated, with the subcircuit and the line.
 */
std::expected<void, std::string> ExpandText(TextScope &scope, const std::vector<std::string> &lines) {
    return ExpandScope(scope, lines, 0);
}

/**
 * @brief   Expands one X line in a scope, over definitions written around it, as a netlist places a subcircuit.
 * @details The definitions are expanded in the scope first, so the instance sees their subcircuits, models and
 *          parameters as a block sees those around its own definition; they hold no element of their own.
 * @param[in,out] scope        Scope the X line is in, with the nodes of its line mapped.
 * @param[in]     definitions  The logical lines of the .subckt blocks, .model cards and .param lines.
 * @param[in]     call         The X line.
 * @return  Nothing, or why the line cannot be placed, with the subcircuit and the line.
 */
std::expected<void, std::string>
ExpandTextInstance(TextScope &scope, const std::vector<std::string> &definitions, const TextElement &call) {
    if (const Result defined = ExpandScope(scope, definitions, 0); !defined) {
        return defined;
    }
    return ExpandInstance(scope, call, 0);
}

} // namespace Nodalys
