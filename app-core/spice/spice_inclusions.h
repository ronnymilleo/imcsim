/**
 * @file    spice_inclusions.h
 * @brief   Puts the files a SPICE library includes in place of its .include and .lib lines, as a simulator reads them.
 */

#ifndef IMCSIM_SPICE_INCLUSIONS_H
#define IMCSIM_SPICE_INCLUSIONS_H

#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

/// Reads the text of a file a library names, or tells why it cannot.
using LibraryFileReader = std::function<std::expected<std::string, std::string>(const std::filesystem::path &)>;

/**
 * @struct  LibraryLines
 * @brief   The lines of a library, with what it includes in place, and where each one came from.
 */
struct LibraryLines {
    std::vector<std::string> Lines;
    /// Where each line is, such as "Line 12" or "Line 3 of models.lib", for the warnings.
    std::vector<std::string> Origins;
    /// What could not be included, and why.
    std::vector<std::string> Warnings;
};

// Reading
LibraryLines ListLibraryLines(std::string_view text);
LibraryLines ExpandInclusions(const std::filesystem::path &path, const LibraryFileReader &read_file);

} // namespace Core

#endif // IMCSIM_SPICE_INCLUSIONS_H
