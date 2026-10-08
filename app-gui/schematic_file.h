/**
 * @file    schematic_file.h
 * @brief   Saves schematics to JSON and loads them back, reporting the parts that could not be read.
 */

#ifndef IMCSIM_SCHEMATIC_FILE_H
#define IMCSIM_SCHEMATIC_FILE_H

#include "helpers.h"
#include "simulation_settings.h"
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
 * @struct  SavedMeasurements
 * @brief   The traces the plots show, as saved in a file.
 * @details Voltages are kept as a point of their node instead of its number, so they survive changes in how
 *          nodes are numbered; currents by name, such as "R1" or "Q1.C".
 */
struct SavedMeasurements {
    std::vector<GridPoint> Voltages;
    std::vector<std::string> Currents;

    bool operator==(const SavedMeasurements &) const = default;
};

/**
 * @struct  LoadedSchematic
 * @brief   Everything read from a schematic file, plus a message for each part that was skipped or changed.
 */
struct LoadedSchematic {
    std::vector<std::unique_ptr<UIElement>> Elements;
    std::vector<UIWire> Wires;
    SimulationSettings Settings;
    SavedMeasurements Measurements;
    std::vector<std::string> Warnings;
};

inline constexpr std::string_view SchematicExtension = ".imcsim";

std::string SaveSchematic(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires,
                          const SimulationSettings &settings, const SavedMeasurements &measurements);
std::expected<LoadedSchematic, std::string> LoadSchematic(std::string_view text);
std::expected<std::string, std::string> ReadTextFile(const std::filesystem::path &path);
std::expected<void, std::string> WriteTextFile(const std::filesystem::path &path, std::string_view text);

} // namespace GUI

#endif // IMCSIM_SCHEMATIC_FILE_H
