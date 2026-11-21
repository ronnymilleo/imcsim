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
    /// The type in upper case, such as "D" or "NPN".
    std::string TypeName;
    /// What follows the type, without the parentheses around the list, one token each, expected to be NAME=VALUE.
    std::vector<std::string> Assignments;
};

// Cards
std::optional<ModelCardWords> SplitModelCard(const std::vector<std::string> &tokens);
std::optional<ParsedModel::ModelKind> FindModelKind(std::string_view type);

// Device parameters; a parameter the device reads is added to read_keys when it is given
DiodeParameters BuildDiodeParameters(const ParsedModel &model, std::set<std::string> *read_keys = nullptr);
BjtParameters BuildBjtParameters(const ParsedModel &model, std::set<std::string> *read_keys = nullptr);
MosfetParameters BuildMosfetParameters(const ParsedModel &model, std::set<std::string> *read_keys = nullptr);
SwitchParameters BuildSwitchParameters(const ParsedModel &model, std::set<std::string> *read_keys = nullptr);

} // namespace Nodalys

#endif // NODALYS_PARSER_MODEL_CARDS_H
