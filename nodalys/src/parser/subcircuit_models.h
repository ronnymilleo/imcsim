/**
 * @file    subcircuit_models.h
 * @brief   Reads the .model cards of a subcircuit kept as SPICE text and hands them to the elements that use them.
 */

#ifndef NODALYS_PARSER_SUBCIRCUIT_MODELS_H
#define NODALYS_PARSER_SUBCIRCUIT_MODELS_H

#include "parser/subcircuit_scope.h"

#include <expected>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

// Cards
std::expected<TextModel, std::string> ParseTextModel(const TextScope &scope, const std::vector<std::string> &tokens);

// Use
std::expected<const TextModel *, std::string> UseTextModel(const TextScope &scope, std::string_view name,
                                                           std::initializer_list<ParsedModel::ModelKind> kinds);
void ReportIgnoredParameters(ExpansionContext &context, const TextModel &model, const std::set<std::string> &read_keys);

} // namespace Nodalys

#endif // NODALYS_PARSER_SUBCIRCUIT_MODELS_H
