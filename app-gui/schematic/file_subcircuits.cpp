/**
 * @file    file_subcircuits.cpp
 * @brief   Writes the block definitions of a schematic to JSON and reads them back.
 */

#include "schematic/file_subcircuits.h"

#include "circuit/components/subcircuit.h"
#include "nlohmann/json.hpp"
#include "schematic/connectivity.h"
#include "schematic/file_elements.h"
#include "schematic/file_json.h"
#include "schematic/file_wires.h"
#include "schematic/subcircuit_pins.h"
#include <algorithm>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <set>

namespace GUI {

namespace {

/**
 * @brief   Writes one block definition: its pins, parameters and either its parts and wires or its SPICE text.
 */
nlohmann::json WriteSubcircuit(const SavedSubcircuit &definition) {
    nlohmann::json pins = nlohmann::json::array();
    for (const SavedSubcircuitPin &pin : definition.Pins) {
        if (definition.Spice) {
            pins.push_back({{"name", pin.Name}, {"side", GetPinSideName(pin.Side)}});
        } else {
            pins.push_back({{"name", pin.Name}, {"point", {pin.Point.X, pin.Point.Y}}});
        }
    }
    nlohmann::json parameters = nlohmann::json::array();
    for (const Core::SubcircuitParameter &parameter : definition.Parameters) {
        parameters.push_back({{"name", parameter.Name}, {"default", parameter.DefaultValue}});
    }
    // A block kept as SPICE text is its ports and its text, which the file keeps as they came
    if (definition.Spice) {
        return {{"name", definition.Name},
                {"pins", pins},
                {"parameters", parameters},
                {"spice", {{"ports", definition.Spice->Ports}, {"text", definition.Spice->Text}}}};
    }
    std::vector<const UIElement *> elements;
    for (const auto &element : definition.Elements) {
        elements.push_back(element.get());
    }
    return {{"name", definition.Name},
            {"pins", pins},
            {"parameters", parameters},
            {"elements", WriteElements(elements)},
            {"wires", WriteWires(definition.Wires)}};
}

/**
 * @brief   One pin of a block: a drawn one sits at the point of its marker, one of SPICE text keeps the edge it goes
 *          on.
 */
std::expected<SavedSubcircuitPin, std::string> ReadSubcircuitPin(const nlohmann::json &pin, const bool is_text) {
    const std::optional<std::string> name = pin.is_object() ? ReadString(pin, "name") : std::nullopt;
    if (!name || !Core::SubcircuitInstance::IsValidIdentifier(*name)) {
        return std::unexpected("has an invalid or repeated pin");
    }
    if (is_text) {
        const std::optional<std::string> side_name = ReadString(pin, "side");
        const std::optional<Core::PinSide> side = side_name ? ParsePinSide(*side_name) : std::nullopt;
        if (!side) {
            return std::unexpected(std::format("has no valid edge for pin {}", *name));
        }
        return SavedSubcircuitPin{.Name = *name, .Point = {}, .Side = *side};
    }
    const std::optional<GridPoint> point = ReadPoint(pin, "point");
    if (!point) {
        return std::unexpected("has an invalid or repeated pin");
    }
    return SavedSubcircuitPin{.Name = *name, .Point = *point};
}

/**
 * @brief   The pins of a block, in order, each with a name of its own.
 */
std::expected<void, std::string> ReadSubcircuitPins(const nlohmann::json &object, const bool is_text,
                                                    std::vector<SavedSubcircuitPin> &pins) {
    const auto list = object.find("pins");
    if (list == object.end()) {
        return {};
    }
    if (!list->is_array()) {
        return std::unexpected("has pins that are not a list");
    }
    for (const nlohmann::json &entry : *list) {
        std::expected<SavedSubcircuitPin, std::string> pin = ReadSubcircuitPin(entry, is_text);
        if (!pin) {
            return std::unexpected(pin.error());
        }
        if (std::ranges::contains(pins, pin->Name, &SavedSubcircuitPin::Name)) {
            return std::unexpected("has an invalid or repeated pin");
        }
        pins.push_back(std::move(*pin));
    }
    return {};
}

/**
 * @brief   The ports and text of a block kept as SPICE text; each pin is the port of the same place.
 */
std::expected<SavedSubcircuit, std::string> ReadSubcircuitText(const nlohmann::json &object,
                                                               SavedSubcircuit definition) {
    if (!object.is_object()) {
        return std::unexpected("has SPICE text that is not an object");
    }
    const auto ports = object.find("ports");
    const std::optional<std::string> text = ReadString(object, "text");
    if (ports == object.end() || !ports->is_array() || !text) {
        return std::unexpected("has no SPICE ports or text");
    }
    Core::SubcircuitText spice{.Ports = {}, .Text = *text};
    for (const nlohmann::json &port : *ports) {
        if (!port.is_string() || port.get<std::string>().empty() ||
            port.get<std::string>().find_first_of(" \t\n") != std::string::npos) {
            return std::unexpected("has a SPICE port that is not one word");
        }
        spice.Ports.push_back(port.get<std::string>());
    }
    if (spice.Ports.size() != definition.Pins.size()) {
        return std::unexpected("has a different number of pins and SPICE ports");
    }
    definition.Spice = std::move(spice);
    return definition;
}

/**
 * @brief   Reads one block definition; the blocks its parts place must be read already, since their instances take
 *          their pins from them.
 */
std::expected<SavedSubcircuit, std::string> ReadSubcircuit(const nlohmann::json &object,
                                                           std::vector<std::string> &warnings,
                                                           const std::span<const SavedSubcircuit> definitions) {
    if (!object.is_object()) {
        return std::unexpected("is not an object");
    }
    SavedSubcircuit definition;
    const std::optional<std::string> name = ReadString(object, "name");
    if (!name || !Core::SubcircuitInstance::IsValidIdentifier(*name)) {
        return std::unexpected("has no valid name");
    }
    definition.Name = *name;
    const bool is_text = object.contains("spice");
    if (auto read = ReadSubcircuitPins(object, is_text, definition.Pins); !read) {
        return std::unexpected(read.error());
    }
    const auto parameters = object.find("parameters");
    if (parameters != object.end() && !parameters->is_array()) {
        return std::unexpected("has parameters that are not a list");
    }
    if (parameters != object.end()) {
        for (const nlohmann::json &parameter : *parameters) {
            const std::optional<std::string> parameter_name =
                parameter.is_object() ? ReadString(parameter, "name") : std::nullopt;
            const std::optional<double> value = parameter.is_object() ? ReadNumber(parameter, "default") : std::nullopt;
            if (!parameter_name || !Core::SubcircuitInstance::IsValidIdentifier(*parameter_name) || !value ||
                std::ranges::contains(definition.Parameters, *parameter_name, &Core::SubcircuitParameter::Name)) {
                return std::unexpected("has an invalid or repeated parameter");
            }
            definition.Parameters.push_back({*parameter_name, *value});
        }
    }
    if (is_text) {
        return ReadSubcircuitText(object.at("spice"), std::move(definition));
    }

    std::vector<std::string> local_warnings;
    ReadElements(object, definitions, definition.Elements, local_warnings);
    ReadWires(object, WireWording::GENERIC, definition.Wires, local_warnings);
    if (const std::optional<std::string> problem =
            CheckPinNodes(Connectivity(definition.Elements, definition.Wires), definition.Pins)) {
        return std::unexpected(std::format("has pins it cannot connect: {}", *problem));
    }
    for (std::string &warning : local_warnings) {
        warnings.push_back(std::format("Subcircuit {}: {}", definition.Name, warning));
    }
    return definition;
}

/**
 * @brief   The names of the blocks the parts of a block definition place, as the file lists them.
 */
std::set<std::string> ListPlacedDefinitions(const nlohmann::json &object) {
    std::set<std::string> names;
    const auto elements = object.is_object() ? object.find("elements") : object.end();
    if (elements == object.end() || !elements->is_array()) {
        return names;
    }
    for (const nlohmann::json &element : *elements) {
        if (const std::optional<std::string> name =
                element.is_object() ? ReadString(element, "definition") : std::nullopt) {
            names.insert(*name);
        }
    }
    return names;
}

/**
 * @brief   The order to read the definitions in, so each block is read after the blocks it places, whatever order the
 *          file lists them in; a block in a cycle, which only a damaged file can hold, comes after the others it can.
 */
std::vector<std::size_t> OrderDefinitionsForReading(const nlohmann::json &list) {
    std::map<std::string, std::size_t, std::less<>> first_of_name;
    for (std::size_t index = 0; index < list.size(); ++index) {
        if (const std::optional<std::string> name =
                list[index].is_object() ? ReadString(list[index], "name") : std::nullopt) {
            first_of_name.try_emplace(*name, index);
        }
    }
    std::vector<std::size_t> order;
    std::vector<bool> is_placed(list.size(), false);
    std::vector<bool> is_open(list.size(), false);
    const std::function<void(std::size_t)> place = [&](const std::size_t index) {
        if (is_placed[index] || is_open[index]) {
            return;
        }
        is_open[index] = true;
        for (const std::string &used : ListPlacedDefinitions(list[index])) {
            if (const auto inner = first_of_name.find(used); inner != first_of_name.end()) {
                place(inner->second);
            }
        }
        is_open[index] = false;
        is_placed[index] = true;
        order.push_back(index);
    };
    for (std::size_t index = 0; index < list.size(); ++index) {
        place(index);
    }
    return order;
}

} // namespace

/**
 * @brief   Writes the block definitions as a JSON list.
 * @param[in] definitions  Definitions to write, in the order they are listed.
 * @return  The list, with one object for each definition.
 */
nlohmann::json WriteSubcircuits(const std::span<const SavedSubcircuit> definitions) {
    nlohmann::json list = nlohmann::json::array();
    for (const SavedSubcircuit &definition : definitions) {
        list.push_back(WriteSubcircuit(definition));
    }
    return list;
}

/**
 * @brief   Reads the "subcircuits" list of a document.
 * @details Each definition is read after the blocks it places, whatever order the file lists them in, and kept in the
 *          order of the file. A definition that cannot be read or repeats a name is skipped with a warning.
 * @param[in]     document  Document object that holds the list.
 * @param[in,out] warnings  Receives one message for each problem found.
 * @return  The definitions read.
 */
std::vector<SavedSubcircuit> ReadSubcircuits(const nlohmann::json &document, std::vector<std::string> &warnings) {
    std::vector<SavedSubcircuit> definitions;
    const auto list = document.find("subcircuits");
    if (list == document.end()) {
        return definitions;
    }
    if (!list->is_array()) {
        warnings.push_back("The subcircuit definitions are not a list and were skipped");
        return definitions;
    }
    std::vector<std::size_t> file_positions;
    for (const std::size_t index : OrderDefinitionsForReading(*list)) {
        auto definition = ReadSubcircuit((*list)[index], warnings, definitions);
        if (!definition) {
            warnings.push_back(std::format("Subcircuit {} {} and was skipped", index + 1, definition.error()));
        } else if (const auto existing = std::ranges::find(definitions, definition->Name, &SavedSubcircuit::Name);
                   existing != definitions.end()) {
            // The first in the file wins, whichever was read first
            warnings.push_back(std::format("Subcircuit {} is repeated and was skipped", definition->Name));
            const auto position = static_cast<std::size_t>(existing - definitions.begin());
            if (index < file_positions[position]) {
                *existing = std::move(*definition);
                file_positions[position] = index;
            }
        } else {
            definitions.push_back(std::move(*definition));
            file_positions.push_back(index);
        }
    }
    // Back to the order of the file, so saving writes them where they were
    std::vector<std::size_t> by_file(definitions.size());
    for (std::size_t position = 0; position < by_file.size(); ++position) {
        by_file[position] = position;
    }
    std::ranges::sort(by_file, {}, [&](const std::size_t position) { return file_positions[position]; });
    std::vector<SavedSubcircuit> ordered;
    for (const std::size_t position : by_file) {
        ordered.push_back(std::move(definitions[position]));
    }
    return ordered;
}

} // namespace GUI
