/**
 * @file    value_field.h
 * @brief   Text input for a value typed with SPICE suffixes, such as "4.7k" or "10m".
 */

#ifndef IMCSIM_VALUE_FIELD_H
#define IMCSIM_VALUE_FIELD_H

#include <array>
#include <functional>
#include <optional>
#include <string>

namespace GUI {

/**
 * @class   ValueField
 * @brief   Keeps the text being typed for one value and reports the value once the text is valid.
 * @details Values are applied as soon as they are valid instead of when the field loses focus, because a click
 *          elsewhere may change what the field edits before a deferred edit was applied.
 */
class ValueField {
public:
    void Load(double value);
    std::optional<double> Draw(const char *label, const std::string &unit, const std::function<bool(double)> &is_valid);

private:
    std::array<char, 32> m_Text{};
    bool m_Invalid = false;
};

} // namespace GUI

#endif // IMCSIM_VALUE_FIELD_H
