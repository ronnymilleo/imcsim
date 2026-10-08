/**
 * @file    spice_value.h
 * @brief   Reads and writes component values with scale suffixes, such as "4.7k" or "100n".
 */

#ifndef IMCSIM_SPICE_VALUE_H
#define IMCSIM_SPICE_VALUE_H

#include <optional>
#include <string>
#include <string_view>

namespace Core {

std::optional<double> ParseValue(std::string_view text, std::string_view unit);
std::string FormatValue(double value);
std::string FormatSpiceValue(double value);
std::string FormatFixed(double value, int decimals);

} // namespace Core

#endif // IMCSIM_SPICE_VALUE_H
