/**
 * @file    file_subcircuits.h
 * @brief   Writes the block definitions of a schematic to JSON and reads them back.
 * @details Only the file modules include this header, since nlohmann/json is private to the library.
 */

#ifndef IMCSIM_FILE_SUBCIRCUITS_H
#define IMCSIM_FILE_SUBCIRCUITS_H

#include "nlohmann/json_fwd.hpp"
#include "schematic/schematic_file.h"
#include <span>
#include <string>
#include <vector>

namespace GUI {

// Writing
nlohmann::json WriteSubcircuits(std::span<const SavedSubcircuit> definitions);

// Reading
std::vector<SavedSubcircuit> ReadSubcircuits(const nlohmann::json &document, std::vector<std::string> &warnings);

} // namespace GUI

#endif // IMCSIM_FILE_SUBCIRCUITS_H
