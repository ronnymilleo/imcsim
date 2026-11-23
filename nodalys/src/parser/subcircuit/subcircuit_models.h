/**
 * @file    subcircuit_models.h
 * @brief   Reads the .model cards of a subcircuit kept as SPICE text and hands them to the elements that use them.
 */

#ifndef NODALYS_PARSER_SUBCIRCUIT_MODELS_H
#define NODALYS_PARSER_SUBCIRCUIT_MODELS_H

#include "parser/subcircuit/subcircuit_scope.h"

#include <expected>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Nodalys {

// Cards
std::expected<TextModel, std::string> ParseTextModel(const TextScope &scope, const std::vector<std::string> &tokens);

// Use
std::expected<const TextModel *, std::string>
UseTextModel(const TextScope &scope, std::string_view name, std::initializer_list<ParsedModel::ModelKind> kinds);
void ReportIgnoredParameters(ExpansionContext &context, const TextModel &model, const std::set<std::string> &read_keys);

/**
 * @brief   Builds the device parameters an element takes from its model, and reports the parameters of the model that
 *          the device does not read.
 * @param[in,out] context  Expansion that collects the warnings.
 * @param[in]     model    Model the element uses.
 * @param[in]     build    Builds the parameters of the device from the card, such as BuildDiodeParameters.
 * @return  The device parameters.
 */
template <typename T>
T UseModelParameters(ExpansionContext &context,
                     const TextModel &model,
                     BuiltParameters<T> (*const build)(const ParsedModel &model)) {
    BuiltParameters<T> built = build(model.Card);
    ReportIgnoredParameters(context, model, built.ReadKeys);
    return std::move(built.Parameters);
}

} // namespace Nodalys

#endif // NODALYS_PARSER_SUBCIRCUIT_MODELS_H
