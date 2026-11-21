/**
 * @file    file_source_fields.h
 * @brief   Writes and reads the waveforms of a source in the schematic file.
 * @details Every waveform of a source is saved, whichever is selected, so switching the type after loading keeps the
 *          parameters of the others. Only the file modules include this header, since nlohmann/json is private to
 *          the library.
 */

#ifndef IMCSIM_FILE_SOURCE_FIELDS_H
#define IMCSIM_FILE_SOURCE_FIELDS_H

#include "circuit/components/component.h"
#include "nlohmann/json_fwd.hpp"
#include <expected>
#include <string>

namespace GUI {

// Source waveforms
void WriteSourceFields(const Core::Component &component, nlohmann::json &object);
[[nodiscard]] std::expected<void, std::string> ReadSourceFields(const nlohmann::json &object,
                                                                Core::Component &component);

} // namespace GUI

#endif // IMCSIM_FILE_SOURCE_FIELDS_H
