/**
 * @file    spice_pin_table.h
 * @brief   The table of the pins of a block kept as SPICE text, which the import and the settings dialogs both show.
 */

#ifndef IMCSIM_SPICE_PIN_TABLE_H
#define IMCSIM_SPICE_PIN_TABLE_H

#include "schematic/schematic_file.h"
#include <string>
#include <vector>

namespace GUI {

/// Width of a name field of the SPICE dialogs, in font sizes, so the dialogs follow the font.
constexpr float SPICE_NAME_FIELD_WIDTH = 8.0f;

// Drawing
void DrawSpicePinTable(std::vector<SavedSubcircuitPin> &pins, const std::vector<std::string> &ports);

} // namespace GUI

#endif // IMCSIM_SPICE_PIN_TABLE_H
