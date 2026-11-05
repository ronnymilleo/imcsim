/**
 * @file    spice_library.h
 * @brief   Reads the subcircuits of a SPICE library file, such as a vendor's op-amp models, as text to keep.
 */

#ifndef IMCSIM_SPICE_LIBRARY_H
#define IMCSIM_SPICE_LIBRARY_H

#include "model/library_model.h"
#include "model/subcircuit.h"
#include "spice/spice_inclusions.h"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

/**
 * @struct  LibrarySubcircuit
 * @brief   One .subckt of a library file, ready to be kept as a block of SPICE text.
 * @details Name and Ports are as the file writes them. Parameters are the numeric defaults of its params: list, which
 *          an instance can override; Text holds everything else the subcircuit needs: its own lines, any parameter
 *          whose default is an expression, and the .model, .subckt and .param definitions of the file that it uses,
 *          so it simulates without the file. Description is the comment right above its .subckt line, where vendors
 *          say what each numbered port is. Warnings tell what it uses that was not found or cannot be followed.
 */
struct LibrarySubcircuit {
    std::string Name;
    std::string Description;
    std::vector<std::string> Ports;
    std::vector<SubcircuitParameter> Parameters;
    std::string Text;
    std::vector<std::string> Warnings;
};

/**
 * @struct  SpiceLibrary
 * @brief   The subcircuits of a library file and the device models its diodes and transistors can use, in file order,
 *          and what could not be read at all.
 */
struct SpiceLibrary {
    std::vector<LibrarySubcircuit> Subcircuits;
    std::vector<LibraryModel> Models;
    std::vector<std::string> Warnings;
};

// Reading
SpiceLibrary ReadSpiceLibrary(std::string_view text);
SpiceLibrary ReadSpiceLibraryFile(const std::filesystem::path &path, const LibraryFileReader &read_file);
std::optional<double> ParseSpiceNumber(std::string_view text);

} // namespace Core

#endif // IMCSIM_SPICE_LIBRARY_H
