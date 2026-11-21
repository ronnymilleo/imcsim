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
    const std::optional<ModelCardWords> card = SplitModelCard(tokens);
    if (!card) {
        return std::unexpected("a .model card needs a name and a type");
    }
    TextModel model;
    model.Name = card->Name;
    model.TypeName = card->TypeName;
    if (const std::optional<ParsedModel::ModelKind> kind = FindModelKind(model.TypeName)) {
        model.IsSupported = true;
        model.Card.Kind = *kind;
    }
    for (const std::string &token : card->Assignments) {
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
