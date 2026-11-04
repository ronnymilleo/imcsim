/**
 * @file    part_names.h
 * @brief   Renames a part: whether a new name is allowed, and the names of currents that follow it.
 */

#ifndef IMCSIM_PART_NAMES_H
#define IMCSIM_PART_NAMES_H

#include "ui_elements/ui_element.h"
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

// Checking
std::optional<std::string> CheckPartName(const std::vector<std::unique_ptr<UIElement>> &elements, std::size_t index,
                                         std::string_view name);

// Following a rename
std::string RenameCurrent(std::string_view current, std::string_view old_name, std::string_view new_name);
std::string RenameCurrentsIn(std::string_view expression, std::string_view old_name, std::string_view new_name);

} // namespace GUI

#endif // IMCSIM_PART_NAMES_H
