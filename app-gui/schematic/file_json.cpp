/**
 * @file    file_json.cpp
 * @brief   JSON helpers shared by the modules that read and write the schematic file.
 */

#include "schematic/file_json.h"

#include <utility>

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

/**
 * @brief   Reads an integer member that may be left out.
 * @param[in]  object      JSON object to look in.
 * @param[in]  key         Name of the member.
 * @param[out] target      Receives the value when the key is there; left alone otherwise.
 * @param[in]  error_text  Message for a key that is not an integer inside the limits.
 * @param[in]  lowest      Smallest value accepted.
 * @param[in]  highest     Largest value accepted.
 * @return  True when the key was there and the target was set, false when the key is missing, or the error_text.
 */
std::expected<bool, std::string> ReadOptionalInt(const nlohmann::json &object,
                                                 const char *key,
                                                 int &target,
                                                 const std::string_view error_text,
                                                 const int lowest,
                                                 const int highest) {
    if (!object.contains(key)) {
        return false;
    }
    const std::optional<int> value = ReadInt(object, key);
    if (!value || *value < lowest || *value > highest) {
        return std::unexpected(std::string(error_text));
    }
    target = *value;
    return true;
}

/**
 * @brief   Reads a numeric member that may be left out.
 * @param[in]  object      JSON object to look in.
 * @param[in]  key         Name of the member.
 * @param[out] target      Receives the value when the key is there; left alone otherwise.
 * @param[in]  error_text  Message for a key that is not a number the check accepts.
 * @param[in]  is_valid    Check of the number, or null to accept any.
 * @return  True when the key was there and the target was set, false when the key is missing, or the error_text.
 */
std::expected<bool, std::string> ReadOptionalNumber(const nlohmann::json &object,
                                                    const char *key,
                                                    double &target,
                                                    const std::string_view error_text,
                                                    bool (*const is_valid)(double)) {
    if (!object.contains(key)) {
        return false;
    }
    const std::optional<double> value = ReadNumber(object, key);
    if (!value || (is_valid != nullptr && !is_valid(*value))) {
        return std::unexpected(std::string(error_text));
    }
    target = *value;
    return true;
}

/**
 * @brief   Reads a true or false member that may be left out.
 * @param[in]  object      JSON object to look in.
 * @param[in]  key         Name of the member.
 * @param[out] target      Receives the value when the key is there; left alone otherwise.
 * @param[in]  error_text  Message for a key that is not true or false.
 * @return  True when the key was there and the target was set, false when the key is missing, or the error_text.
 */
std::expected<bool, std::string>
ReadOptionalBool(const nlohmann::json &object, const char *key, bool &target, const std::string_view error_text) {
    if (!object.contains(key)) {
        return false;
    }
    const std::optional<bool> value = ReadBool(object, key);
    if (!value) {
        return std::unexpected(std::string(error_text));
    }
    target = *value;
    return true;
}

/**
 * @brief   Reads a text member that may be left out.
 * @param[in]  object      JSON object to look in.
 * @param[in]  key         Name of the member.
 * @param[out] target      Receives the value when the key is there; left alone otherwise.
 * @param[in]  error_text  Message for a key that is not text.
 * @return  True when the key was there and the target was set, false when the key is missing, or the error_text.
 */
std::expected<bool, std::string> ReadOptionalString(const nlohmann::json &object,
                                                    const char *key,
                                                    std::string &target,
                                                    const std::string_view error_text) {
    if (!object.contains(key)) {
        return false;
    }
    std::optional<std::string> value = ReadString(object, key);
    if (!value) {
        return std::unexpected(std::string(error_text));
    }
    target = std::move(*value);
    return true;
}

/**
 * @brief   Reads a point member that may be left out.
 * @param[in]  object      JSON object to look in.
 * @param[in]  key         Name of the member.
 * @param[out] target      Receives the point when the key is there; left alone otherwise.
 * @param[in]  error_text  Message for a key that does not hold two integers.
 * @return  True when the key was there and the target was set, false when the key is missing, or the error_text.
 */
std::expected<bool, std::string> ReadOptionalPoint(const nlohmann::json &object,
                                                   const char *key,
                                                   std::optional<GridPoint> &target,
                                                   const std::string_view error_text) {
    if (!object.contains(key)) {
        return false;
    }
    const std::optional<GridPoint> point = ReadPoint(object, key);
    if (!point) {
        return std::unexpected(std::string(error_text));
    }
    target = point;
    return true;
}

/**
 * @brief   Tells how a run of optional-member reads ended.
 * @details All reads have run by the time this is called, which is harmless because they only fill their targets and
 *          the caller drops what it filled when this fails.
 * @param[in] reads  Results of the reads, in the order of the file format.
 * @return  The first error among them, or nothing when every read went through.
 */
std::expected<void, std::string> FirstError(const std::initializer_list<std::expected<bool, std::string>> reads) {
    for (const std::expected<bool, std::string> &read : reads) {
        if (!read) {
            return std::unexpected(read.error());
        }
    }
    return {};
}

} // namespace GUI
