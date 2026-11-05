/**
 * @file    schematic_file.h
 * @brief   Saves schematics to JSON and loads them back, reporting the parts that could not be read.
 */

#ifndef IMCSIM_SCHEMATIC_FILE_H
#define IMCSIM_SCHEMATIC_FILE_H

#include "components/subcircuit.h"
#include "document/simulation_settings.h"
#include "helpers.h"
#include "model/library_model.h"
#include "model/subcircuit.h"
#include "plots/logic_bus.h"
#include "plots/math_channel.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/**
 * @struct  SavedMeasurements
 * @brief   The traces the plots show, as saved in a file.
 * @details Voltages are kept as a point of their node instead of its number, so they survive changes in how
 *          nodes are numbered; currents by name, such as "R1" or "Q1.C"; math channels by what they compute; logic
 * buses by the voltages of their bits.
 */
struct SavedMeasurements {
    std::vector<GridPoint> Voltages;
    std::vector<std::string> Currents;
    std::vector<MathChannel> MathChannels;
    std::vector<LogicBus> Buses;

    bool operator==(const SavedMeasurements &) const = default;
};

/**
 * @struct  SavedSubcircuitPin
 * @brief   A connection of a block: its name and the point of its pin marker inside the block.
 * @details A block kept as SPICE text has no markers: its pins have no point, and the edge of the box each goes on is
 *          kept in Side instead of following a marker's arrow.
 */
struct SavedSubcircuitPin {
    std::string Name;
    GridPoint Point;
    Core::PinSide Side = Core::PinSide::LEFT;

    bool operator==(const SavedSubcircuitPin &) const = default;
};

/**
 * @struct  SavedSubcircuit
 * @brief   A block definition as the schematic keeps it: its pins in order, its parameters and its contents.
 * @details The contents are drawn parts and wires, like a schematic of their own; Schematic::BuildCircuit() turns them
 *          into a Core::SubcircuitDefinition for each run. A block imported from a SPICE library keeps its text in
 * Spice instead, and has no parts or wires.
 */
struct SavedSubcircuit {
    std::string Name;
    std::vector<SavedSubcircuitPin> Pins;
    std::vector<Core::SubcircuitParameter> Parameters;
    std::vector<std::unique_ptr<UIElement>> Elements;
    std::vector<UIWire> Wires;
    std::optional<Core::SubcircuitText> Spice;
};

/**
 * @struct  LoadedSchematic
 * @brief   Everything read from a schematic file, plus a message for each part that was skipped or changed.
 */
struct LoadedSchematic {
    std::vector<std::unique_ptr<UIElement>> Elements;
    std::vector<UIWire> Wires;
    std::vector<SavedSubcircuit> Subcircuits;
    std::vector<Core::LibraryModel> Models;
    SimulationSettings Settings;
    SavedMeasurements Measurements;
    std::vector<std::string> Warnings;
};

inline constexpr std::string_view SCHEMATIC_EXTENSION = ".imcsim";

std::string SaveSchematic(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires,
                          const SimulationSettings &settings, const SavedMeasurements &measurements,
                          std::span<const SavedSubcircuit> subcircuits = {},
                          std::span<const Core::LibraryModel> models = {});
std::string SaveParts(std::span<const UIElement *const> elements, const std::vector<UIWire> &wires,
                      std::span<const SavedSubcircuit> subcircuits = {},
                      std::span<const Core::LibraryModel> models = {});
std::expected<LoadedSchematic, std::string> LoadSchematic(std::string_view text);
std::expected<std::string, std::string> ReadTextFile(const std::filesystem::path &path);
std::expected<void, std::string> WriteTextFile(const std::filesystem::path &path, std::string_view text);

} // namespace GUI

#endif // IMCSIM_SCHEMATIC_FILE_H
