/**
 * @file    json_arguments.cpp
 * @brief   Reads typed arguments of a tool call, remembering the first one of the wrong type.
 */

#include "mcp/json_arguments.h"

#include "nodalys/netlist_parser.h"

#include <cmath>
#include <format>
#include <limits>

namespace Nodalys {

/**
 * @param[in] arguments  Arguments object of the call; it must outlive the reader.
 */
ArgumentReader::ArgumentReader(const nlohmann::json &arguments) : m_Arguments(arguments) {
}

/**
 * @brief   Reads a text argument.
 * @param[in] key  Name of the argument.
 * @return  The text, or none when the argument is absent or not a string.
 */
std::optional<std::string> ArgumentReader::GetString(const std::string_view key) {
    const nlohmann::json *value = Find(key);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (!value->is_string()) {
        Fail(key, "a string");
        return std::nullopt;
    }
    return value->get<std::string>();
}

/**
 * @brief   Reads a number, written as a JSON number or as a string with a SPICE scale such as "4.7k" or "100n".
 * @param[in] key  Name of the argument.
 * @return  The number, or none when the argument is absent or neither of those.
 */
std::optional<double> ArgumentReader::GetNumber(const std::string_view key) {
    const nlohmann::json *value = Find(key);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (value->is_number()) {
        return value->get<double>();
    }
    if (value->is_string()) {
        if (const std::optional<double> parsed = ParseSpiceNumber(value->get<std::string>())) {
            return parsed;
        }
    }
    Fail(key, "a number or a SPICE number such as \"1k\"");
    return std::nullopt;
}

/**
 * @brief   Reads a whole number.
 * @param[in] key  Name of the argument.
 * @return  The integer, or none when the argument is absent or not a whole number that fits an int.
 */
std::optional<int> ArgumentReader::GetInteger(const std::string_view key) {
    const std::optional<double> number = GetNumber(key);
    if (!number) {
        return std::nullopt;
    }
    const bool is_whole = std::floor(*number) == *number;
    const bool is_in_range = std::abs(*number) <= static_cast<double>(std::numeric_limits<int>::max());
    if (!is_whole || !is_in_range) {
        Fail(key, "a whole number");
        return std::nullopt;
    }
    return static_cast<int>(*number);
}

/**
 * @brief   Reads a true or false argument.
 * @param[in] key  Name of the argument.
 * @return  The value, or none when the argument is absent or not a boolean.
 */
std::optional<bool> ArgumentReader::GetBoolean(const std::string_view key) {
    const nlohmann::json *value = Find(key);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (!value->is_boolean()) {
        Fail(key, "true or false");
        return std::nullopt;
    }
    return value->get<bool>();
}

/**
 * @brief   Reads an array of texts.
 * @param[in] key  Name of the argument.
 * @return  The texts, or none when the argument is absent or not an array of strings.
 */
std::optional<std::vector<std::string>> ArgumentReader::GetStringList(const std::string_view key) {
    const nlohmann::json *value = Find(key);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (!value->is_array()) {
        Fail(key, "an array of strings");
        return std::nullopt;
    }
    std::vector<std::string> texts;
    for (const nlohmann::json &item : *value) {
        if (!item.is_string()) {
            Fail(key, "an array of strings");
            return std::nullopt;
        }
        texts.push_back(item.get<std::string>());
    }
    return texts;
}

/**
 * @return  Whether an argument of the wrong type was read.
 */
bool ArgumentReader::HasError() const {
    return !m_Error.empty();
}

/**
 * @return  The message about the first argument of the wrong type, empty when there was none.
 */
const std::string &ArgumentReader::GetError() const {
    return m_Error;
}

const nlohmann::json *ArgumentReader::Find(const std::string_view key) const {
    if (!m_Arguments.is_object()) {
        return nullptr;
    }
    const auto found = m_Arguments.find(key);
    if (found == m_Arguments.end() || found->is_null()) {
        return nullptr;
    }
    return &*found;
}

void ArgumentReader::Fail(const std::string_view key, const std::string_view expected) {
    if (m_Error.empty()) {
        m_Error = std::format("Argument '{}' must be {}", key, expected);
    }
}

} // namespace Nodalys
