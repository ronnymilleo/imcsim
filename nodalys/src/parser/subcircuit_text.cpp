/**
 * @file    subcircuit_text.cpp
 * @brief   Adds a subcircuit written as SPICE text, such as a vendor's op-amp model, to a circuit being built.
 */

#include "nodalys/subcircuit_text.h"

#include "parser/spice_text.h"
#include "parser/subcircuit_elements.h"
#include "parser/subcircuit_models.h"
#include "parser/subcircuit_scope.h"

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
 * @struct  ClassifiedLines
 * @brief   The lines of a text sorted by what they do, since the parameters and models come before the elements that
 *          use them whatever the order they are written in.
 */
struct ClassifiedLines {
    std::vector<std::vector<std::string>> Parameters;
    std::vector<std::vector<std::string>> Models;
    std::vector<std::string> Elements;
};

/**
 * @brief   The message of a line the engine cannot simulate, with the subcircuit it is in.
 */
std::string Refuse(const ExpansionContext &context, const std::string_view line, const std::string_view reason) {
    return std::format("Nodalys cannot simulate the subcircuit {}: the line '{}' {}", context.SubcircuitName, line,
                       reason);
}

/**
 * @brief   Splits the NAME=VALUE tokens that follow the nodes of a line, skipping the PARAMS: word.
 */
std::vector<std::pair<std::string, std::string>> SplitParameterTokens(const std::vector<std::string> &tokens,
                                                                      const std::size_t first) {
    std::vector<std::pair<std::string, std::string>> parameters;
    for (std::size_t index = first; index < tokens.size(); ++index) {
        std::string token = tokens[index];
        if (ToUpperCase(token).starts_with(PARAMETER_LIST_MARK)) {
            token.erase(0, PARAMETER_LIST_MARK.size());
        }
        if (token.empty()) {
            continue;
        }
        const std::size_t equals = token.find('=');
        parameters.emplace_back(ToUpperCase(token.substr(0, equals)),
                                equals == std::string::npos ? std::string() : token.substr(equals + 1));
    }
    return parameters;
}

/**
 * @brief   Reads the .subckt line of a nested definition: its name, its ports and its parameters with their defaults.
 */
std::expected<TextSubcircuit, std::string> ReadSubcircuitHeader(const std::vector<std::string> &tokens) {
    if (tokens.size() < 2) {
        return std::unexpected("a .subckt line needs a name");
    }
    TextSubcircuit subcircuit;
    subcircuit.Name = tokens[1];
    std::size_t index = 2;
    while (index < tokens.size() && !IsParameterStart(tokens[index])) {
        subcircuit.Ports.push_back(tokens[index]);
        ++index;
    }
    for (auto &[name, text] : SplitParameterTokens(tokens, index)) {
        if (text.empty()) {
            return std::unexpected(std::format("the parameter {} has no default value", name));
        }
        subcircuit.Parameters.emplace_back(std::move(name), std::move(text));
    }
    return subcircuit;
}

/**
 * @brief   Tells how many lines a nested .subckt takes, from the line after its header to its .ends.
 * @return  The number of lines of its body, or none when it never closes.
 */
std::optional<std::size_t> FindBodyLength(const std::vector<std::string> &lines, const std::size_t first) {
    int depth = 1;
    for (std::size_t index = first; index < lines.size(); ++index) {
        const std::vector<std::string> tokens = SplitTokens(lines[index]);
        const std::string directive = tokens.empty() ? std::string() : ToUpperCase(tokens[0]);
        if (directive == ".SUBCKT") {
            ++depth;
        } else if (directive == ".ENDS" && --depth == 0) {
            return index - first;
        }
    }
    return std::nullopt;
}

Result ExpandScope(ExpansionContext &context, TextScope &scope, const std::vector<std::string> &lines, int depth);

/**
 * @brief   Sorts the lines of a text and registers its nested subcircuits.
 */
std::expected<ClassifiedLines, std::string> ClassifyLines(ExpansionContext &context, TextScope &scope,
                                                          const std::vector<std::string> &lines) {
    ClassifiedLines classified;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::vector<std::string> tokens = SplitTokens(lines[index]);
        const std::string directive = ToUpperCase(tokens[0]);
        if (directive == ".SUBCKT") {
            auto subcircuit = ReadSubcircuitHeader(tokens);
            const std::optional<std::size_t> length = FindBodyLength(lines, index + 1);
            if (!subcircuit) {
                return std::unexpected(Refuse(context, lines[index], "is not understood: " + subcircuit.error()));
            }
            if (!length) {
                return std::unexpected(Refuse(context, lines[index], "starts a subcircuit that has no .ends"));
            }
            subcircuit->Lines.assign(lines.begin() + static_cast<std::ptrdiff_t>(index + 1),
                                     lines.begin() + static_cast<std::ptrdiff_t>(index + 1 + *length));
            subcircuit->DefinedIn = &scope;
            const std::string key = ToUpperCase(subcircuit->Name);
            scope.AddSubcircuit(key, std::move(*subcircuit));
            index += *length + 1;
        } else if (directive == ".PARAM") {
            classified.Parameters.push_back(tokens);
        } else if (directive == ".MODEL") {
            classified.Models.push_back(tokens);
        } else if (directive == ".END") {
            continue;
        } else if (directive.starts_with('.')) {
            return std::unexpected(Refuse(context, lines[index], "uses a directive that Nodalys does not read"));
        } else {
            classified.Elements.push_back(lines[index]);
        }
    }
    return classified;
}

/**
 * @brief   Evaluates the .param lines in order. A value the instance passed is kept, since it overrides the default
 *          the text gives.
 */
Result DefineParameters(ExpansionContext &context, TextScope &scope,
                        const std::vector<std::vector<std::string>> &parameter_lines) {
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

Result DefineModels(ExpansionContext &context, TextScope &scope,
                    const std::vector<std::vector<std::string>> &model_lines) {
    for (const std::vector<std::string> &tokens : model_lines) {
        auto model = ParseTextModel(scope, tokens);
        if (!model) {
            return std::unexpected(Refuse(context, std::format(".model {}", tokens.size() > 1 ? tokens[1] : ""),
                                          "is not understood: " + model.error()));
        }
        scope.AddModel(ToUpperCase(tokens[1]), std::move(*model));
    }
    return {};
}

/**
 * @brief   Gives each parameter of a called subcircuit its value: what the X line passes, or else its default.
 */
Result SetInstanceParameters(ExpansionContext &context, const TextScope &caller, TextScope &instance,
                             const TextSubcircuit &subcircuit, const std::string_view line,
                             const std::vector<std::pair<std::string, std::string>> &arguments) {
    for (const auto &[name, text] : arguments) {
        const bool is_known =
            std::ranges::any_of(subcircuit.Parameters, [&name](const auto &formal) { return formal.first == name; });
        if (!is_known) {
            return std::unexpected(Refuse(context, line,
                                          std::format("passes the parameter {}, which {} does not "
                                                      "have",
                                                      name, subcircuit.Name)));
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
Result ExpandInstance(ExpansionContext &context, TextScope &scope, const std::string &line,
                      const std::vector<std::string> &tokens, const int depth) {
    constexpr std::size_t FIRST_AFTER_NAME = 2;
    std::size_t parameters_start = FIRST_AFTER_NAME;
    while (parameters_start < tokens.size() && !IsParameterStart(tokens[parameters_start])) {
        ++parameters_start;
    }
    if (parameters_start <= FIRST_AFTER_NAME) {
        return std::unexpected(Refuse(context, line, "is not written X<name> <nodes> <subcircuit>"));
    }
    const std::string &subcircuit_name = tokens[parameters_start - 1];
    const TextSubcircuit *subcircuit = scope.FindSubcircuit(ToUpperCase(subcircuit_name));
    if (subcircuit == nullptr) {
        return std::unexpected(Refuse(
            context, line, std::format("uses the subcircuit {}, which the text does not define", subcircuit_name)));
    }
    const std::size_t node_count = parameters_start - 1 - 1;
    if (node_count != subcircuit->Ports.size()) {
        return std::unexpected(Refuse(context, line,
                                      std::format("gives {} nodes to {}, which has {} ports", node_count,
                                                  subcircuit->Name, subcircuit->Ports.size())));
    }
    if (depth >= MAX_NESTING_DEPTH) {
        return std::unexpected(Refuse(context, line, "nests subcircuits too deeply; one of them places itself"));
    }
    TextScope instance(subcircuit->DefinedIn, scope.MakeName(tokens[0]));
    if (const Result set = SetInstanceParameters(context, scope, instance, *subcircuit, line,
                                                 SplitParameterTokens(tokens, parameters_start));
        !set) {
        return set;
    }
    for (std::size_t port = 0; port < node_count; ++port) {
        instance.MapNode(ToUpperCase(subcircuit->Ports[port]), scope.ResolveNode(context, tokens[1 + port]));
    }
    return ExpandScope(context, instance, subcircuit->Lines, depth + 1);
}

/**
 * @brief   Adds everything a text holds to the circuit, as one instance in a scope.
 */
Result ExpandScope(ExpansionContext &context, TextScope &scope, const std::vector<std::string> &lines,
                   const int depth) {
    const auto classified = ClassifyLines(context, scope, lines);
    if (!classified) {
        return std::unexpected(classified.error());
    }
    if (const Result parameters = DefineParameters(context, scope, classified->Parameters); !parameters) {
        return parameters;
    }
    if (const Result models = DefineModels(context, scope, classified->Models); !models) {
        return models;
    }
    for (const std::string &line : classified->Elements) {
        NoteVoltageSource(scope, SplitTokens(line));
    }
    for (const std::string &line : classified->Elements) {
        const std::vector<std::string> tokens = SplitTokens(line);
        if (std::toupper(static_cast<unsigned char>(tokens[0].front())) == 'X') {
            if (const Result expanded = ExpandInstance(context, scope, line, tokens, depth); !expanded) {
                return expanded;
            }
        } else if (const Result added = AddTextElement(context, scope, line, tokens); !added) {
            return std::unexpected(Refuse(context, line, "cannot be simulated: " + added.error()));
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
                                           text.Name, text.Ports.size(), instance.PortNodes.size()));
    }
    ExpansionContext context{.Builder = builder,
                             .SubcircuitName = text.Name,
                             .Warnings = {},
                             .ReportedModels = {},
                             .OnInternalNode = instance.OnInternalNode,
                             .OnSource = instance.OnSource};
    TextScope scope(nullptr, instance.Prefix);
    for (const auto &[name, value] : instance.Parameters) {
        scope.SetParameter(ToUpperCase(name), value);
    }
    for (std::size_t port = 0; port < text.Ports.size(); ++port) {
        scope.MapNode(ToUpperCase(text.Ports[port]), instance.PortNodes[port]);
    }
    if (const Result expanded = ExpandScope(context, scope, SplitLogicalLines(text.Body), 0); !expanded) {
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
    SubcircuitTextInstance instance{.Prefix = "check", .PortNodes = {}, .Parameters = parameters};
    for (std::size_t port = 0; port < text.Ports.size(); ++port) {
        instance.PortNodes.push_back(scratch.AddNode());
    }
    const auto added = AddSubcircuitText(scratch, text, instance);
    return added ? std::nullopt : std::optional<std::string>(added.error());
}

} // namespace Nodalys
