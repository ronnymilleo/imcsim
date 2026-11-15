/**
 * @file    file_library_models.h
 * @brief   Writes the device models imported from libraries to JSON and reads them back.
 * @details Only the file modules include this header, since nlohmann/json is private to the library.
 */

#ifndef IMCSIM_FILE_LIBRARY_MODELS_H
#define IMCSIM_FILE_LIBRARY_MODELS_H

#include "circuit/library_model.h"
#include "nlohmann/json_fwd.hpp"
#include <span>
#include <string>
#include <vector>

namespace GUI {

// Writing
nlohmann::json WriteLibraryModels(std::span<const Core::LibraryModel> models);

// Reading
std::vector<Core::LibraryModel> ReadLibraryModels(const nlohmann::json &document, std::vector<std::string> &warnings);

} // namespace GUI

#endif // IMCSIM_FILE_LIBRARY_MODELS_H
