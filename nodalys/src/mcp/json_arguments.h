/**
 * @file    json_arguments.h
 * @brief   Reads typed arguments of a tool call, remembering the first one of the wrong type.
 */

#ifndef NODALYS_JSON_ARGUMENTS_H
#define NODALYS_JSON_ARGUMENTS_H

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

/**
 * @class   ArgumentReader
 * @brief   Reads the arguments object of a tool call without throwing on a value of the wrong type.
 * @details An absent argument, or a null one, reads as none. One of the wrong type reads as none too and is kept as an
 *          error, which the tool reports to the AI so it can correct the call.
 */
class ArgumentReader {
public:
    explicit ArgumentReader(const nlohmann::json &arguments);

    // Reading
    std::optional<std::string> GetString(std::string_view key);
    std::optional<double> GetNumber(std::string_view key);
    std::optional<int> GetInteger(std::string_view key);
    std::optional<bool> GetBoolean(std::string_view key);
    std::optional<std::vector<std::string>> GetStringList(std::string_view key);

    // Errors
    bool HasError() const;
    const std::string &GetError() const;

private:
    // Reading
    const nlohmann::json *Find(std::string_view key) const;
    void Fail(std::string_view key, std::string_view expected);

    // Arguments
    const nlohmann::json &m_Arguments;
    std::string m_Error;
};

} // namespace Nodalys

#endif // NODALYS_JSON_ARGUMENTS_H
