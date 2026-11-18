/**
 * @file    subcircuit_models.cpp
 * @brief   Reads the .model cards of a subcircuit kept as SPICE text and hands them to the elements that use them.
 */

#include "parser/subcircuit_models.h"

#include "parser/spice_text.h"
#include "text/text_case.h"

#include <algorithm>
#include <cstddef>
#include <format>

namespace Nodalys {

namespace {

/// The one level of the diode, bipolar and MOSFET models that the engine has.
constexpr double SUPPORTED_LEVEL = 1.0;

/**
 * @brief   The kind of device a .model type stands for, or none for a type the engine has no device for.
 */
std::optional<ParsedModel::ModelKind> FindKind(const std::string &type) {
    if (type == "D") {
        return ParsedModel::ModelKind::DIODE;
    }
    if (type == "NPN") {
        return ParsedModel::ModelKind::BJT_NPN;
    }
    if (type == "PNP") {
        return ParsedModel::ModelKind::BJT_PNP;
    }
    if (type == "NMOS") {
        return ParsedModel::ModelKind::MOSFET_NMOS;
    }
    if (type == "PMOS") {
        return ParsedModel::ModelKind::MOSFET_PMOS;
    }
    if (type == "SW" || type == "VSWITCH") {
        return ParsedModel::ModelKind::SWITCH;
    }
    return std::nullopt;
}

/**
 * @brief   The text of the parameters of a card: what follows the type, without the parentheses around the list.
 */
std::string_view FindParameterText(const std::string_view after_name, std::string &type) {
    const std::string_view text = TrimSpaces(after_name);
    const std::size_t type_end = text.find_first_of(" \t(");
    type = ToUpper(text.substr(0, type_end));
    std::string_view parameters = type_end == std::string_view::npos ? std::string_view() : text.substr(type_end);
    parameters = TrimSpaces(parameters);
    if (parameters.size() >= 2 && parameters.front() == '(' && parameters.back() == ')') {
        parameters = parameters.substr(1, parameters.size() - 2);
    }
    return parameters;
}

} // namespace

/**
 * @brief   Reads one .model card, evaluating its parameters in the scope it is written in.
 * @details A type the engine has no device for is kept, marked unsupported, so it only refuses the element that uses
 *          it, as ngspice reads a card nobody uses without complaint.
 * @param[in] scope   Scope the card is written in, which the expressions of its parameters are evaluated in.
 * @param[in] tokens  Tokens of the card line, from the .model word on.
 * @return  The model, or what was not understood.
 */
std::expected<TextModel, std::string> ParseTextModel(const TextScope &scope, const std::vector<std::string> &tokens) {
    if (tokens.size() < 3) {
        return std::unexpected("a .model card needs a name and a type");
    }
    TextModel model;
    model.Name = tokens[1];
    std::string rest;
    for (std::size_t index = 2; index < tokens.size(); ++index) {
        rest += index == 2 ? "" : " ";
        rest += tokens[index];
    }
    const std::string_view parameter_text = FindParameterText(rest, model.TypeName);
    if (const std::optional<ParsedModel::ModelKind> kind = FindKind(model.TypeName)) {
        model.IsSupported = true;
        model.Card.Kind = *kind;
    }
    for (const std::string &token : SplitTokens(parameter_text)) {
        const std::size_t equals = token.find('=');
        if (equals == std::string::npos || equals == 0) {
            return std::unexpected(std::format("the model parameter '{}' is not written as NAME=VALUE", token));
        }
        const auto value = scope.Evaluate(std::string_view(token).substr(equals + 1));
        if (!value) {
            return std::unexpected(std::format("the parameter {} of the model {}: {}",
                                               ToUpper(token.substr(0, equals)),
                                               model.Name,
                                               value.error()));
        }
        model.Card.Params[ToUpper(token.substr(0, equals))] = *value;
    }
    return model;
}

/**
 * @brief   Finds the model an element names and checks that the engine can simulate it as that element.
 * @param[in] scope  Scope of the element.
 * @param[in] name   Model name as the element writes it.
 * @param[in] kinds  Kinds of device the element accepts, such as NPN and PNP for a bipolar transistor.
 * @return  The model, or why the element cannot use it: not defined, of another kind, of a type the engine has no
 *          device for, or of a level other than 1.
 */
std::expected<const TextModel *, std::string> UseTextModel(const TextScope &scope,
                                                           const std::string_view name,
                                                           const std::initializer_list<ParsedModel::ModelKind> kinds) {
    const TextModel *model = scope.FindModel(ToUpper(name));
    if (model == nullptr) {
        return std::unexpected(std::format("it uses the model {}, which the text does not define", name));
    }
    if (!model->IsSupported) {
        return std::unexpected(std::format("the model {} is of the type {}, which Nodalys has no device for",
                                           model->Name,
                                           model->TypeName));
    }
    if (std::ranges::find(kinds, model->Card.Kind) == kinds.end()) {
        return std::unexpected(
            std::format("the model {} is of the type {}, which this element cannot use", model->Name, model->TypeName));
    }
    if (const auto level = model->Card.Params.find("LEVEL");
        level != model->Card.Params.end() && level->second != SUPPORTED_LEVEL) {
        return std::unexpected(std::format("Nodalys simulates the level 1 model only, and the model {} is level {}",
                                           model->Name,
                                           level->second));
    }
    return model;
}

/**
 * @brief   Reports the parameters of a model that no device of the engine reads, once for each model.
 * @param[in,out] context    Expansion that collects the warnings.
 * @param[in]     model      Model an element used.
 * @param[in]     read_keys  Parameters the device took from it.
 */
void ReportIgnoredParameters(ExpansionContext &context,
                             const TextModel &model,
                             const std::set<std::string> &read_keys) {
    if (!context.ReportedModels.insert(ToUpper(model.Name)).second) {
        return;
    }
    std::string ignored;
    for (const auto &[key, value] : model.Card.Params) {
        if (key != "LEVEL" && !read_keys.contains(key)) {
            ignored += ignored.empty() ? key : ", " + key;
        }
    }
    if (!ignored.empty()) {
        context.Warnings.push_back(std::format("Nodalys does not read the parameters {} of the model {} in the "
                                               "subcircuit {}",
                                               ignored,
                                               model.Name,
                                               context.SubcircuitName));
    }
}

} // namespace Nodalys
