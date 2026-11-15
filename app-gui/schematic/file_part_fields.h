/**
 * @file    file_part_fields.h
 * @brief   Writes and reads what each kind of part carries in the schematic file, besides its position and name.
 * @details A source with its waveforms, a diode, transistor or op-amp with its model, the state of a switch or a net
 *          label, and the figures of a relay, a fuse, a clock or a logic part. Only the file modules include this
 *          header, since nlohmann/json is private to the library.
 */

#ifndef IMCSIM_FILE_PART_FIELDS_H
#define IMCSIM_FILE_PART_FIELDS_H

#include "circuit/components/component.h"
#include "circuit/components/source.h"
#include "nlohmann/json_fwd.hpp"
#include <expected>
#include <string>

namespace GUI {

// Writing
void WriteSourceFields(const Core::Source &source, nlohmann::json &object);
void WriteModelFields(const Core::Component &component, nlohmann::json &object);
void WriteKindFields(const Core::Component &component, nlohmann::json &object);
void WriteFigureFields(const Core::Component &component, nlohmann::json &object);

// Reading
std::expected<void, std::string> ReadSourceFields(const nlohmann::json &object, Core::Source &source);
std::expected<void, std::string> ReadModelFields(const nlohmann::json &object, Core::Component &component);
std::expected<void, std::string> ReadStateFields(const nlohmann::json &object, Core::Component &component);
std::expected<void, std::string> ReadFigureFields(const nlohmann::json &object, Core::Component &component);

} // namespace GUI

#endif // IMCSIM_FILE_PART_FIELDS_H
