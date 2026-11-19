/**
 * @file    file_part_state_fields.h
 * @brief   Writes and reads the state that only some kinds of part carry in the schematic file.
 * @details The current a controlled source follows, the expression of a behavioral source, a net name, the port of a
 *          pin marker, a switch position, a wiper and the state of a logic input. Only the file modules include this
 *          header, since nlohmann/json is private to the library.
 */

#ifndef IMCSIM_FILE_PART_STATE_FIELDS_H
#define IMCSIM_FILE_PART_STATE_FIELDS_H

#include "circuit/components/component.h"
#include "nlohmann/json_fwd.hpp"
#include <expected>
#include <string>

namespace GUI {

// State
void WriteStateFields(const Core::Component &component, nlohmann::json &object);
[[nodiscard]] std::expected<void, std::string> ReadStateFields(const nlohmann::json &object,
                                                               Core::Component &component);

} // namespace GUI

#endif // IMCSIM_FILE_PART_STATE_FIELDS_H
