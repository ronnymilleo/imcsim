/**
 * @file    file_wires.h
 * @brief   Writes the wires of a schematic to JSON and reads them back, skipping those that cannot be drawn.
 * @details Only the file modules include this header, since nlohmann/json is private to the library.
 */

#ifndef IMCSIM_FILE_WIRES_H
#define IMCSIM_FILE_WIRES_H

#include "nlohmann/json_fwd.hpp"
#include "ui_elements/ui_wire.h"
#include <string>
#include <vector>

namespace GUI {

/**
 * @enum    WireWording
 * @brief   How much a warning about a skipped wire says.
 */
enum class WireWording {
    /// Names the problem: no integer ends, zero length or diagonal.
    SPECIFIC,
    /// Only says the wire is invalid.
    GENERIC,
};

// Writing
nlohmann::json WriteWires(const std::vector<UIWire> &wires);

// Reading
void ReadWires(const nlohmann::json &object, WireWording wording, std::vector<UIWire> &wires,
               std::vector<std::string> &warnings);

} // namespace GUI

#endif // IMCSIM_FILE_WIRES_H
