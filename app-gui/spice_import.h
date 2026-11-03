/**
 * @file    spice_import.h
 * @brief   Turns the subcircuits of a SPICE library into blocks of the schematic, with pins the user can name and
 * place.
 */

#ifndef IMCSIM_SPICE_IMPORT_H
#define IMCSIM_SPICE_IMPORT_H

#include "schematic_file.h"
#include "spice_library.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

// Names
std::string MakeIdentifier(std::string_view text, std::string_view fallback);

// Pins
std::vector<SavedSubcircuitPin> SuggestSpicePins(const Core::LibrarySubcircuit &subcircuit);
std::optional<std::string> CheckSpicePins(const std::vector<SavedSubcircuitPin> &pins);

// Blocks
SavedSubcircuit MakeSpiceBlock(const Core::LibrarySubcircuit &subcircuit, std::vector<SavedSubcircuitPin> pins);

} // namespace GUI

#endif // IMCSIM_SPICE_IMPORT_H
