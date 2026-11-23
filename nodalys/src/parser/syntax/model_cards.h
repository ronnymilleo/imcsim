/**
 * @file    model_cards.h
 * @brief   The parameters of a SPICE .model card and how they set the device parameters of the engine.
 */

#ifndef NODALYS_PARSER_MODEL_CARDS_H
#define NODALYS_PARSER_MODEL_CARDS_H

#include "nodalys/types.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

/**
 * @struct  ParsedModel
 * @brief   One .model card: the kind of device and its parameters by upper-case name.
 */
struct ParsedModel {
    /// Semiconductor or switch device family recognized on a `.model` card.
    enum class ModelKind {
        DIODE,
        BJT_NPN,
        BJT_PNP,
        MOSFET_NMOS,
        MOSFET_PMOS,
        SWITCH
    };
    ModelKind Kind = ModelKind::DIODE;
    std::map<std::string, double> Params;
};

/**
 * @struct  ModelCardWords
 * @brief   The words of a .model card as written, before its values are read: ".model <name> <type>(<NAME=VALUE>...)".
 */
struct ModelCardWords {
    std::string Name;
    std::string TypeName; ///< The type in upper case, such as "D" or "NPN".
    /// What follows the type, without the parentheses around the list, one token each, expected to be NAME=VALUE.
    std::vector<std::string> Assignments;
};

/**
 * @struct  BuiltParameters
 * @brief   The parameters of a device built from a .model card, with the names of the card parameters it read.
 */
template <typename T>
struct BuiltParameters {
    T Parameters;
    /// Upper-case names of the parameters the card gives and the device took, so the others can be reported.
    std::set<std::string> ReadKeys;
};

// Cards
[[nodiscard]] std::optional<ModelCardWords> SplitModelCard(const std::vector<std::string> &tokens);
[[nodiscard]] std::optional<ParsedModel::ModelKind> FindModelKind(std::string_view type);

// Device parameters
BuiltParameters<DiodeParameters> BuildDiodeParameters(const ParsedModel &model);
BuiltParameters<BjtParameters> BuildBjtParameters(const ParsedModel &model);
BuiltParameters<MosfetParameters> BuildMosfetParameters(const ParsedModel &model);
BuiltParameters<SwitchParameters> BuildSwitchParameters(const ParsedModel &model);

// Ignored parameters
std::string ListIgnoredParameters(const ParsedModel &model, const std::set<std::string> &read_keys);

} // namespace Nodalys

#endif // NODALYS_PARSER_MODEL_CARDS_H
