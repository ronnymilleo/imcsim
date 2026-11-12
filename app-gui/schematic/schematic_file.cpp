/**
 * @file    schematic_file.cpp
 * @brief   Saves schematics to JSON and loads them back, reporting the parts that could not be read.
 */

#include "schematic/schematic_file.h"

#include "nlohmann/json.hpp"
#include "schematic/file_elements.h"
#include "schematic/file_json.h"
#include "schematic/file_library_models.h"
#include "schematic/file_measurements.h"
#include "schematic/file_settings.h"
#include "schematic/file_subcircuits.h"
#include "schematic/file_wires.h"
#include <format>
#include <fstream>
#include <sstream>

namespace GUI {

namespace {

constexpr std::string_view FORMAT_NAME = "imcsim-schematic";
constexpr int FORMAT_VERSION = 1;

std::string WriteDocument(const std::span<const UIElement *const> elements, const std::vector<UIWire> &wires,
                          const SimulationSettings &settings, const SavedMeasurements &measurements,
                          const std::span<const SavedSubcircuit> subcircuits,
                          const std::span<const Core::LibraryModel> models) {
    nlohmann::json document = {
        {"format", FORMAT_NAME},
        {"version", FORMAT_VERSION},
        {"elements", WriteElements(elements)},
        {"wires", WriteWires(wires)},
        {"simulation", WriteSettings(settings)},
    };
    if (!subcircuits.empty()) {
        document["subcircuits"] = WriteSubcircuits(subcircuits);
    }
    if (!models.empty()) {
        document["models"] = WriteLibraryModels(models);
    }
    if (!measurements.Voltages.empty() || !measurements.Currents.empty() || !measurements.MathChannels.empty() ||
        !measurements.Buses.empty()) {
        document["measurements"] = WriteMeasurements(measurements);
    }
    return document.dump(2) + "\n";
}

} // namespace

/**
 * @brief   Writes a schematic as JSON.
 * @param[in] elements      Components placed on the grid.
 * @param[in] wires         Wire segments.
 * @param[in] settings      Settings of the analyses.
 * @param[in] measurements  Traces the plots show; the key is left out when there are none.
 * @param[in] subcircuits   Project-local definitions, including their internal elements and wires.
 * @param[in] models        Device models imported from libraries, as their text.
 * @return  The file contents, indented for reading and diffing. Values are plain numbers, never suffixed text, except
 *          for subcircuit parameter references saved as strings such as "{RESISTANCE}".
 */
std::string SaveSchematic(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires,
                          const SimulationSettings &settings, const SavedMeasurements &measurements,
                          const std::span<const SavedSubcircuit> subcircuits,
                          const std::span<const Core::LibraryModel> models) {
    std::vector<const UIElement *> pointers;
    for (const auto &element : elements) {
        pointers.push_back(element.get());
    }
    return WriteDocument(pointers, wires, settings, measurements, subcircuits, models);
}

/**
 * @brief   Writes some parts and wires as a schematic of their own, for the clipboard.
 * @param[in] elements  Parts to write, which stay where they are.
 * @param[in] wires        Wire segments to write.
 * @param[in] subcircuits  Definitions referenced by the copied instances.
 * @param[in] models       Library models the copied parts use.
 * @return  The same JSON a file holds, with the default settings and no measurements, so it loads back with
 *          LoadSchematic().
 */
std::string SaveParts(const std::span<const UIElement *const> elements, const std::vector<UIWire> &wires,
                      const std::span<const SavedSubcircuit> subcircuits,
                      const std::span<const Core::LibraryModel> models) {
    return WriteDocument(elements, wires, {}, {}, subcircuits, models);
}

/**
 * @brief   Reads a schematic from JSON.
 * @param[in] text  File contents.
 * @return  The schematic, or an error when the text is not a schematic this version can read. Elements and
 *          wires that cannot be read, such as diagonal wires, are skipped with a warning instead of failing
 *          the whole file, and so are repeated or missing names, which are replaced. Invalid analysis settings
 *          fall back to their defaults, also with a warning.
 */
std::expected<LoadedSchematic, std::string> LoadSchematic(const std::string_view text) {
    const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return std::unexpected("The file is not valid JSON");
    }
    if (ReadString(document, "format") != FORMAT_NAME) {
        return std::unexpected("The file is not an imcsim schematic");
    }
    const std::optional<int> version = ReadInt(document, "version");
    if (!version || *version < 1) {
        return std::unexpected("The file has no valid format version");
    }
    if (*version > FORMAT_VERSION) {
        return std::unexpected(std::format("The file uses format version {}, but this build only reads up to {}",
                                           *version, FORMAT_VERSION));
    }

    LoadedSchematic schematic;
    schematic.Models = ReadLibraryModels(document, schematic.Warnings);
    schematic.Subcircuits = ReadSubcircuits(document, schematic.Warnings);
    ReadElements(document, schematic.Subcircuits, schematic.Elements, schematic.Warnings);
    ReadWires(document, WireWording::SPECIFIC, schematic.Wires, schematic.Warnings);
    schematic.Settings = ReadSettings(document, schematic.Warnings);
    schematic.Measurements = ReadMeasurements(document, schematic.Warnings);
    return schematic;
}

/**
 * @brief   Reads a whole text file.
 * @param[in] path  File to read.
 * @return  The contents, or an error message naming the file.
 */
std::expected<std::string, std::string> ReadTextFile(const std::filesystem::path &path) {
    const std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(std::format("Could not open {}", path.string()));
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad()) {
        return std::unexpected(std::format("Could not read {}", path.string()));
    }
    return contents.str();
}

/**
 * @brief   Writes a whole text file, replacing it if it exists.
 * @param[in] path  File to write.
 * @param[in] text  Contents.
 * @return  Nothing on success, or an error message naming the file.
 */
std::expected<void, std::string> WriteTextFile(const std::filesystem::path &path, const std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return std::unexpected(std::format("Could not create {}", path.string()));
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
        return std::unexpected(std::format("Could not write {}", path.string()));
    }
    return {};
}

} // namespace GUI
