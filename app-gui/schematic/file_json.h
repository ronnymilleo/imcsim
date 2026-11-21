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
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace GUI {

/**
 * @struct  ParameterKey
 * @brief   JSON key of one member of a parameter set, such as Core::PulseParameters, and the member itself.
 */
template <typename Parameters>
struct ParameterKey {
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

// Reading optional members
std::expected<bool, std::string> ReadOptionalInt(const nlohmann::json &object,
                                                 const char *key,
                                                 int &target,
                                                 std::string_view error_text,
                                                 int lowest = std::numeric_limits<int>::lowest(),
                                                 int highest = std::numeric_limits<int>::max());
std::expected<bool, std::string> ReadOptionalNumber(const nlohmann::json &object,
                                                    const char *key,
                                                    double &target,
                                                    std::string_view error_text,
                                                    bool (*is_valid)(double) = nullptr);
std::expected<bool, std::string>
ReadOptionalBool(const nlohmann::json &object, const char *key, bool &target, std::string_view error_text);
std::expected<bool, std::string>
ReadOptionalString(const nlohmann::json &object, const char *key, std::string &target, std::string_view error_text);
std::expected<bool, std::string> ReadOptionalPoint(const nlohmann::json &object,
                                                   const char *key,
                                                   std::optional<GridPoint> &target,
                                                   std::string_view error_text);
std::expected<void, std::string> FirstError(std::initializer_list<std::expected<bool, std::string>> reads);

// Parameter sets
/**
 * @brief   Writes every member of a parameter set under its key.
 * @param[in]     keys        Key and member of each number to write.
 * @param[in]     parameters  Parameter set to write.
 * @param[in,out] object      JSON object that receives one entry for each key.
 */
template <typename Parameters, std::size_t Count>
void WriteParameters(const std::array<ParameterKey<Parameters>, Count> &keys,
                     const Parameters &parameters,
                     nlohmann::json &object) {
    for (const ParameterKey<Parameters> &key : keys) {
        object[key.Name] = parameters.*key.Value;
    }
}

/**
 * @brief   Reads every member of a parameter set from its key.
 * @details Missing keys keep the value the parameters already have; a key with something other than a number is an
 *          error.
 * @param[in]     keys        Key and member of each number to read.
 * @param[in]     object      JSON object to look in.
 * @param[in,out] parameters  Parameter set that receives the numbers found.
 * @return  Nothing on success, or "has no valid" followed by the key that is wrong.
 */
template <typename Parameters, std::size_t Count>
std::expected<void, std::string> ReadParameters(const std::array<ParameterKey<Parameters>, Count> &keys,
                                                const nlohmann::json &object,
                                                Parameters &parameters) {
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

/**
 * @brief   Writes a parameter set as an object of its own, under a key of the parent object.
 * @param[in]     keys        Key and member of each number to write.
 * @param[in]     parameters  Parameter set to write.
 * @param[in]     name        Key of the parent object that receives the object.
 * @param[in,out] object      Parent JSON object.
 */
template <typename Parameters, std::size_t Count>
void WriteParametersObject(const std::array<ParameterKey<Parameters>, Count> &keys,
                           const Parameters &parameters,
                           const char *name,
                           nlohmann::json &object) {
    nlohmann::json values = nlohmann::json::object();
    WriteParameters(keys, parameters, values);
    object[name] = values;
}

/**
 * @brief   Reads a parameter set kept as an object under a key; nothing to read when the parent has no such key.
 * @param[in]     keys        Key and member of each number to read.
 * @param[in]     object      Parent JSON object to look in.
 * @param[in]     name        Key of the parent object that holds the parameter object.
 * @param[in]     error_text  Message for a key that holds something other than an object.
 * @param[in,out] parameters  Parameter set that receives the numbers found.
 * @return  Nothing on success, the error_text, or "has no valid" followed by the key of a number that is wrong.
 */
template <typename Parameters, std::size_t Count>
std::expected<void, std::string> ReadParametersObject(const std::array<ParameterKey<Parameters>, Count> &keys,
                                                      const nlohmann::json &object,
                                                      const char *name,
                                                      std::string_view error_text,
                                                      Parameters &parameters) {
    const auto entry = object.find(name);
    if (entry == object.end()) {
        return {};
    }
    if (!entry->is_object()) {
        return std::unexpected(std::string(error_text));
    }
    return ReadParameters(keys, *entry, parameters);
}

} // namespace GUI

#endif // IMCSIM_FILE_JSON_H
