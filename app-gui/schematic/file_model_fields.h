/**
 * @file    file_model_fields.h
 * @brief   Writes and reads the model of a diode, transistor or op-amp in the schematic file.
 * @details Only the file modules include this header, since nlohmann/json is private to the library.
 */

#ifndef IMCSIM_FILE_MODEL_FIELDS_H
#define IMCSIM_FILE_MODEL_FIELDS_H

#include "circuit/components/component.h"
#include "nlohmann/json_fwd.hpp"
#include <expected>
#include <string>

namespace GUI {

// Models
void WriteModelFields(const Core::Component &component, nlohmann::json &object);
[[nodiscard]] std::expected<void, std::string> ReadModelFields(const nlohmann::json &object,
                                                               Core::Component &component);

} // namespace GUI

#endif // IMCSIM_FILE_MODEL_FIELDS_H
