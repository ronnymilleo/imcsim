/**
 * @file    file_settings.h
 * @brief   Writes the analysis settings of a schematic to JSON and reads them back.
 * @details Only the file modules include this header, since nlohmann/json is private to the library.
 */

#ifndef IMCSIM_FILE_SETTINGS_H
#define IMCSIM_FILE_SETTINGS_H

#include "nlohmann/json_fwd.hpp"
#include "schematic/simulation_settings.h"
#include <string>
#include <vector>

namespace GUI {

// Writing
nlohmann::json WriteSettings(const SimulationSettings &settings);

// Reading
SimulationSettings ReadSettings(const nlohmann::json &document, std::vector<std::string> &warnings);

} // namespace GUI

#endif // IMCSIM_FILE_SETTINGS_H
