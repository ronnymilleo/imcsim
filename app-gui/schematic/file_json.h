/**
 * @file    file_json.h
 * @brief   JSON helpers shared by the modules that read and write the schematic file.
 * @details Only the file modules include this header, since nlohmann/json is private to the library.
 */

#ifndef IMCSIM_FILE_JSON_H
#define IMCSIM_FILE_JSON_H

#include "base/helpers.h"
#include "nlohmann/json.hpp"
#include <array>
#include <expected>
#include <format>
#include <optional>
#include <string>

namespace GUI {

/**
 * @struct  ParameterKey
 * @brief   JSON key of one member of a parameter set, such as Core::PulseParameters, and the member itself.
 */
template <typename Parameters> struct ParameterKey {
    const char *Name;
    double Parameters::*Value;
};

// Reading single values
std::optional<int> ReadInt(const nlohmann::json &object, const char *key);
std::optional<double> ReadNumber(const nlohmann::json &object, const char *key);
std::optional<std::string> ReadString(const nlohmann::json &object, const char *key);
std::optional<bool> ReadBool(const nlohmann::json &object, const char *key);
std::optional<GridPoint> ReadPointValue(const nlohmann::json &value);
std::optional<GridPoint> ReadPoint(const nlohmann::json &object, const char *key);

// Parameter sets
/**
 * @brief   Writes every member of a parameter set under its key.
 */
template <typename Parameters, std::size_t Count>
void WriteParameters(const std::array<ParameterKey<Parameters>, Count> &keys, const Parameters &parameters,
                     nlohmann::json &object) {
    for (const ParameterKey<Parameters> &key : keys) {
        object[key.Name] = parameters.*key.Value;
    }
}

/**
 * @brief   Missing keys keep the value the parameters already have; a key with something other than a number is an
 *          error.
 */
template <typename Parameters, std::size_t Count>
std::expected<void, std::string> ReadParameters(const std::array<ParameterKey<Parameters>, Count> &keys,
                                                const nlohmann::json &object, Parameters &parameters) {
    for (const ParameterKey<Parameters> &key : keys) {
        if (!object.contains(key.Name)) {
            continue;
        }
        const std::optional<double> value = ReadNumber(object, key.Name);
        if (!value) {
            return std::unexpected(std::format("has no valid {}", key.Name));
        }
        parameters.*key.Value = *value;
    }
    return {};
}

} // namespace GUI

#endif // IMCSIM_FILE_JSON_H
