/**
 * @file    file_measurements.h
 * @brief   Writes the measured traces of a schematic to JSON and reads them back.
 * @details Only the file modules include this header, since nlohmann/json is private to the library.
 */

#ifndef IMCSIM_FILE_MEASUREMENTS_H
#define IMCSIM_FILE_MEASUREMENTS_H

#include "nlohmann/json_fwd.hpp"
#include "schematic/schematic_file.h"
#include <string>
#include <vector>

namespace GUI {

// Writing
nlohmann::json WriteMeasurements(const SavedMeasurements &measurements);

// Reading
SavedMeasurements ReadMeasurements(const nlohmann::json &document, std::vector<std::string> &warnings);

} // namespace GUI

#endif // IMCSIM_FILE_MEASUREMENTS_H
