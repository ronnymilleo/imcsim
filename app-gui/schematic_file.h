/**
 * @file    schematic_file.h
 * @brief   Saves schematics to JSON and loads them back, reporting the parts that could not be read.
 */

#ifndef IMCSIM_SCHEMATIC_FILE_H
#define IMCSIM_SCHEMATIC_FILE_H

#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/**
 * @struct  LoadedSchematic
 * @brief   Everything read from a schematic file, plus a message for each part that was skipped or changed.
 */
struct LoadedSchematic {
    std::vector<std::unique_ptr<UIElement>> Elements;
    std::vector<UIWire> Wires;
    std::vector<std::string> Warnings;
};

inline constexpr std::string_view SchematicExtension = ".imcsim";

std::string SaveSchematic(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires);
std::expected<LoadedSchematic, std::string> LoadSchematic(std::string_view text);
std::expected<std::string, std::string> ReadTextFile(const std::filesystem::path &path);
std::expected<void, std::string> WriteTextFile(const std::filesystem::path &path, std::string_view text);

} // namespace GUI

#endif // IMCSIM_SCHEMATIC_FILE_H
