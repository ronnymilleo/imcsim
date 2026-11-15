/**
 * @file    file_json.cpp
 * @brief   JSON helpers shared by the modules that read and write the schematic file.
 */

#include "schematic/file_json.h"

namespace GUI {

/**
 * @brief   Reads an integer member.
 * @param[in] object  JSON object to look in.
 * @param[in] key     Name of the member.
 * @return  The value, or nothing when the key is missing or holds another type, so a damaged file never throws.
 */
std::optional<int> ReadInt(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end() || !entry->is_number_integer()) {
        return std::nullopt;
    }
    return entry->get<int>();
}

/**
 * @brief   Reads a numeric member.
 * @param[in] object  JSON object to look in.
 * @param[in] key     Name of the member.
 * @return  The value, or nothing when the key is missing or is not a number.
 */
std::optional<double> ReadNumber(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end() || !entry->is_number()) {
        return std::nullopt;
    }
    return entry->get<double>();
}

/**
 * @brief   Reads a text member.
 * @param[in] object  JSON object to look in.
 * @param[in] key     Name of the member.
 * @return  The value, or nothing when the key is missing or is not text.
 */
std::optional<std::string> ReadString(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end() || !entry->is_string()) {
        return std::nullopt;
    }
    return entry->get<std::string>();
}

/**
 * @brief   Reads a true or false member.
 * @param[in] object  JSON object to look in.
 * @param[in] key     Name of the member.
 * @return  The value, or nothing when the key is missing or is not true or false.
 */
std::optional<bool> ReadBool(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end() || !entry->is_boolean()) {
        return std::nullopt;
    }
    return entry->get<bool>();
}

/**
 * @brief   Reads a point written as a list of two integers.
 * @param[in] value  JSON value to read.
 * @return  The point, or nothing when the value is not two integers.
 */
std::optional<GridPoint> ReadPointValue(const nlohmann::json &value) {
    if (!value.is_array() || value.size() != 2 || !value[0].is_number_integer() || !value[1].is_number_integer()) {
        return std::nullopt;
    }
    return GridPoint{value[0].get<int>(), value[1].get<int>()};
}

/**
 * @brief   Reads a member holding a point.
 * @param[in] object  JSON object to look in.
 * @param[in] key     Name of the member.
 * @return  The point, or nothing when the key is missing or does not hold two integers.
 */
std::optional<GridPoint> ReadPoint(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end()) {
        return std::nullopt;
    }
    return ReadPointValue(*entry);
}

} // namespace GUI
