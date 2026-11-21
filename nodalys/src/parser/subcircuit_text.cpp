/**
 * @file    subcircuit_text.cpp
 * @brief   Adds a subcircuit written as SPICE text, such as a vendor's op-amp model, to a circuit being built.
 */

#include "nodalys/subcircuit_text.h"

#include "parser/classified_lines.h"
#include "parser/spice_text.h"
#include "parser/subcircuit_elements.h"
#include "parser/subcircuit_lines.h"
#include "parser/subcircuit_models.h"
#include "parser/subcircuit_scope.h"
#include "text/text_case.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <utility>

namespace Nodalys {

namespace {

using Result = std::expected<void, std::string>;

/// How many subcircuits may sit inside one another, which stops a block that places itself.
constexpr int MAX_NESTING_DEPTH = 16;

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
 * @brief   Adds a subcircuit written as SPICE text to a circuit being built, as one instance.
 * @param[in,out] builder   Circuit to add the elements to.
 * @param[in]     text      The subcircuit.
 * @param[in]     instance  Where its ports connect, the name its elements start with and its parameter values.
 * @return  The warnings, such as the parameters of a model that the engine does not read, or the reason the text
 *          cannot be simulated. The builder may hold part of the elements after a failure and must be discarded.
 */
std::expected<std::vector<std::string>, std::string>
AddSubcircuitText(CircuitBuilder &builder, const SubcircuitText &text, const SubcircuitTextInstance &instance) {
    if (instance.PortNodes.size() != text.Ports.size()) {
        return std::unexpected(std::format("Nodalys cannot simulate the subcircuit {}: it has {} ports and the "
                                           "instance connects {}",
                                           text.Name,
                                           text.Ports.size(),
                                           instance.PortNodes.size()));
    }
    ExpansionContext context{.Builder = builder,
                             .SubcircuitName = text.Name,
                             .Warnings = {},
                             .ReportedModels = {},
                             .Observers = instance.Observers};
    TextScope scope(context, nullptr, instance.Prefix);
    for (const auto &[name, value] : instance.Parameters) {
        scope.SetParameter(ToUpper(name), value);
    }
    for (std::size_t port = 0; port < text.Ports.size(); ++port) {
        scope.MapNode(ToUpper(text.Ports[port]), instance.PortNodes[port]);
    }
    if (const Result expanded = ExpandScope(scope, SplitLogicalLines(text.Body), 0); !expanded) {
        return std::unexpected(expanded.error());
    }
    return std::move(context.Warnings);
}

/**
 * @brief   Tells whether the engine can simulate a subcircuit text, without a circuit to put it in.
 * @param[in] text        The subcircuit.
 * @param[in] parameters  The value of each parameter of its .subckt line.
 * @return  The reason it cannot, written for the user, or none when it can.
 */
std::optional<std::string> CheckSubcircuitText(const SubcircuitText &text,
                                               const std::map<std::string, double> &parameters) {
    CircuitBuilder scratch;
    SubcircuitTextInstance instance{.Prefix = "check", .PortNodes = {}, .Parameters = parameters, .Observers = {}};
    for (std::size_t port = 0; port < text.Ports.size(); ++port) {
        instance.PortNodes.push_back(scratch.AddNode());
    }
    const auto added = AddSubcircuitText(scratch, text, instance);
    return added ? std::nullopt : std::optional<std::string>(added.error());
}

} // namespace Nodalys
