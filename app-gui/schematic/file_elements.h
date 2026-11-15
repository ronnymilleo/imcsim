/**
 * @file    file_elements.h
 * @brief   Writes the parts of a schematic to JSON and reads them back, repairing their names.
 * @details Only the file modules include this header, since nlohmann/json is private to the library.
 */

#ifndef IMCSIM_FILE_ELEMENTS_H
#define IMCSIM_FILE_ELEMENTS_H

#include "nlohmann/json_fwd.hpp"
#include "schematic/schematic_file.h"
#include "ui_elements/ui_element.h"
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace GUI {

// Writing
nlohmann::json WriteElements(std::span<const UIElement *const> elements);

// Reading
void ReadElements(const nlohmann::json &object, std::span<const SavedSubcircuit> definitions,
                  std::vector<std::unique_ptr<UIElement>> &elements, std::vector<std::string> &warnings);

} // namespace GUI

#endif // IMCSIM_FILE_ELEMENTS_H
