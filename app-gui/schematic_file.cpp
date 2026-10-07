/**
 * @file    schematic_file.cpp
 * @brief   Saves schematics to JSON and loads them back, reporting the parts that could not be read.
 */

#include "schematic_file.h"

#include "components/voltage_source.h"
#include "element_factory.h"
#include "nlohmann/json.hpp"
#include <array>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace GUI {

namespace {

constexpr std::string_view FormatName = "imcsim-schematic";
constexpr int FormatVersion = 1;

/**
 * @struct  PulseKey
 * @brief   JSON key of one pulse parameter and the member it is read into.
 */
struct PulseKey {
    const char *Name;
    double Core::PulseParameters::*Value;
};

constexpr auto PulseKeys = std::to_array<PulseKey>({
    {"low", &Core::PulseParameters::Low},
    {"high", &Core::PulseParameters::High},
    {"delay", &Core::PulseParameters::Delay},
    {"rise", &Core::PulseParameters::RiseTime},
    {"fall", &Core::PulseParameters::FallTime},
    {"width", &Core::PulseParameters::Width},
    {"period", &Core::PulseParameters::Period},
});

// Readers return no value when the key is missing or holds another type, so a damaged file never throws
std::optional<int> ReadInt(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end() || !entry->is_number_integer()) {
        return std::nullopt;
    }
    return entry->get<int>();
}

std::optional<double> ReadNumber(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end() || !entry->is_number()) {
        return std::nullopt;
    }
    return entry->get<double>();
}

std::optional<std::string> ReadString(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end() || !entry->is_string()) {
        return std::nullopt;
    }
    return entry->get<std::string>();
}

std::optional<GridPoint> ReadPoint(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end() || !entry->is_array() || entry->size() != 2 || !(*entry)[0].is_number_integer() ||
        !(*entry)[1].is_number_integer()) {
        return std::nullopt;
    }
    return GridPoint{(*entry)[0].get<int>(), (*entry)[1].get<int>()};
}

nlohmann::json WriteElement(const UIElement &element) {
    const Core::Component &component = element.GetComponent();
    nlohmann::json object = {
        {"type", component.GetTypeName()},
        {"x", element.GetPosition().X},
        {"y", element.GetPosition().Y},
        {"rotation", ToDegrees(element.GetRotation())},
    };
    if (!component.GetName().empty()) {
        object["name"] = component.GetName();
    }
    if (component.HasValue()) {
        object["value"] = component.GetValue();
    }
    // Every waveform is saved, so switching type after loading keeps the parameters of the others
    if (component.GetType() == Core::ComponentType::VoltageSource) {
        const auto &source = static_cast<const Core::VoltageSource &>(component);
        object["source"] = Core::GetSourceTypeName(source.GetSourceType());
        object["amplitude"] = source.GetAmplitude();
        object["frequency"] = source.GetFrequency();
        object["offset"] = source.GetOffset();
        nlohmann::json pulse = nlohmann::json::object();
        for (const PulseKey &key : PulseKeys) {
            pulse[key.Name] = source.GetPulse().*key.Value;
        }
        object["pulse"] = pulse;
    }
    return object;
}

// Every key is optional, so files saved before sources had a type load as DC with the default sine and pulse
std::expected<void, std::string> ReadVoltageSource(const nlohmann::json &object, Core::VoltageSource &source) {
    if (object.contains("source")) {
        const std::optional<std::string> type_name = ReadString(object, "source");
        const std::optional<Core::VoltageSource::SourceType> type =
            type_name ? Core::ParseSourceType(*type_name) : std::nullopt;
        if (!type) {
            return std::unexpected("has a source type other than DC or AC");
        }
        source.SetSourceType(*type);
    }
    if (object.contains("amplitude")) {
        const std::optional<double> amplitude = ReadNumber(object, "amplitude");
        if (!amplitude) {
            return std::unexpected("has no valid amplitude");
        }
        source.SetAmplitude(*amplitude);
    }
    if (object.contains("frequency")) {
        const std::optional<double> frequency = ReadNumber(object, "frequency");
        if (!frequency || !source.IsValidFrequency(*frequency)) {
            return std::unexpected("has no valid frequency");
        }
        source.SetFrequency(*frequency);
    }
    if (object.contains("offset")) {
        const std::optional<double> offset = ReadNumber(object, "offset");
        if (!offset) {
            return std::unexpected("has no valid offset");
        }
        source.SetOffset(*offset);
    }
    if (const auto pulse_object = object.find("pulse"); pulse_object != object.end()) {
        if (!pulse_object->is_object()) {
            return std::unexpected("has a pulse that is not an object");
        }
        Core::PulseParameters pulse = source.GetPulse();
        for (const PulseKey &key : PulseKeys) {
            if (!pulse_object->contains(key.Name)) {
                continue;
            }
            const std::optional<double> value = ReadNumber(*pulse_object, key.Name);
            if (!value) {
                return std::unexpected(std::format("has no valid pulse {}", key.Name));
            }
            pulse.*key.Value = *value;
        }
        if (!source.IsValidPulse(pulse)) {
            return std::unexpected("has a pulse with a negative time or a period that is not positive");
        }
        source.SetPulse(pulse);
    }
    return {};
}

// Names are checked separately, once every element is read, so duplicates can be found
std::expected<std::unique_ptr<UIElement>, std::string> ReadElement(const nlohmann::json &object) {
    if (!object.is_object()) {
        return std::unexpected("is not an object");
    }
    const std::optional<std::string> type_name = ReadString(object, "type");
    if (!type_name) {
        return std::unexpected("has no type");
    }
    const std::optional<Core::ComponentType> type = Core::ParseComponentType(*type_name);
    if (!type) {
        return std::unexpected(std::format("has unknown type \"{}\"", *type_name));
    }
    const std::optional<int> x = ReadInt(object, "x");
    const std::optional<int> y = ReadInt(object, "y");
    if (!x || !y) {
        return std::unexpected("has no integer position");
    }
    const std::optional<int> degrees = ReadInt(object, "rotation");
    const std::optional<Rotation> rotation = degrees ? RotationFromDegrees(*degrees) : std::nullopt;
    if (!rotation) {
        return std::unexpected("has no rotation of 0, 90, 180 or 270");
    }

    std::unique_ptr<UIElement> element = CreateElement(*type, {*x, *y}, *rotation);
    Core::Component &component = element->GetComponent();
    if (component.HasValue()) {
        const std::optional<double> value = ReadNumber(object, "value");
        if (!value || !component.IsValidValue(*value)) {
            return std::unexpected("has no valid value");
        }
        component.SetValue(*value);
    }
    if (component.GetType() == Core::ComponentType::VoltageSource) {
        const std::expected<void, std::string> source =
            ReadVoltageSource(object, static_cast<Core::VoltageSource &>(component));
        if (!source) {
            return std::unexpected(source.error());
        }
    }
    if (const std::optional<std::string> name = ReadString(object, "name")) {
        component.SetName(*name);
    }
    return element;
}

// Keeps valid, unique names and renames the rest, so the netlist never gets two components with one name
void FixNames(std::vector<std::unique_ptr<UIElement>> &elements, std::vector<std::string> &warnings) {
    std::vector<std::string> used_names;
    std::vector<Core::Component *> to_rename;
    for (const auto &element : elements) {
        Core::Component &component = element->GetComponent();
        const std::string_view prefix = component.GetNamePrefix();
        if (prefix.empty()) {
            component.SetName("");
            continue;
        }
        const std::string &name = component.GetName();
        if (Core::IsValidName(name, prefix) && !std::ranges::contains(used_names, name)) {
            used_names.push_back(name);
        } else {
            to_rename.push_back(&component);
        }
    }
    for (Core::Component *component : to_rename) {
        std::string new_name = Core::NextComponentName(component->GetNamePrefix(), used_names);
        if (component->GetName().empty()) {
            warnings.push_back(std::format("A {} had no name and was named {}", component->GetTypeName(), new_name));
        } else {
            warnings.push_back(std::format("{} \"{}\" was renamed to {} because the name was invalid or repeated",
                                           component->GetTypeName(), component->GetName(), new_name));
        }
        used_names.push_back(new_name);
        component->SetName(std::move(new_name));
    }
}

} // namespace

/**
 * @brief   Writes a schematic as JSON.
 * @param[in] elements  Components placed on the grid.
 * @param[in] wires     Wire segments.
 * @return  The file contents, indented for reading and diffing. Values are plain numbers, never suffixed text.
 */
std::string SaveSchematic(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires) {
    nlohmann::json element_list = nlohmann::json::array();
    for (const auto &element : elements) {
        element_list.push_back(WriteElement(*element));
    }
    nlohmann::json wire_list = nlohmann::json::array();
    for (const UIWire &wire : wires) {
        wire_list.push_back({
            {"start", {wire.GetStart().X, wire.GetStart().Y}},
            {"end", {wire.GetEnd().X, wire.GetEnd().Y}},
        });
    }
    const nlohmann::json document = {
        {"format", FormatName},
        {"version", FormatVersion},
        {"elements", element_list},
        {"wires", wire_list},
    };
    return document.dump(2) + "\n";
}

/**
 * @brief   Reads a schematic from JSON.
 * @param[in] text  File contents.
 * @return  The schematic, or an error when the text is not a schematic this version can read. Elements and
 *          wires that cannot be read, such as diagonal wires, are skipped with a warning instead of failing
 *          the whole file, and so are repeated or missing names, which are replaced.
 */
std::expected<LoadedSchematic, std::string> LoadSchematic(const std::string_view text) {
    const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return std::unexpected("The file is not valid JSON");
    }
    if (ReadString(document, "format") != FormatName) {
        return std::unexpected("The file is not an imcsim schematic");
    }
    const std::optional<int> version = ReadInt(document, "version");
    if (!version || *version < 1) {
        return std::unexpected("The file has no valid format version");
    }
    if (*version > FormatVersion) {
        return std::unexpected(std::format("The file uses format version {}, but this build only reads up to {}",
                                           *version, FormatVersion));
    }

    LoadedSchematic schematic;
    const auto elements = document.find("elements");
    if (elements != document.end() && elements->is_array()) {
        for (std::size_t index = 0; index < elements->size(); ++index) {
            auto element = ReadElement((*elements)[index]);
            if (element) {
                schematic.Elements.push_back(std::move(*element));
            } else {
                schematic.Warnings.push_back(std::format("Element {} {} and was skipped", index + 1, element.error()));
            }
        }
    }
    FixNames(schematic.Elements, schematic.Warnings);

    const auto wires = document.find("wires");
    if (wires != document.end() && wires->is_array()) {
        for (std::size_t index = 0; index < wires->size(); ++index) {
            const nlohmann::json &wire = (*wires)[index];
            const std::optional<GridPoint> start = wire.is_object() ? ReadPoint(wire, "start") : std::nullopt;
            const std::optional<GridPoint> end = wire.is_object() ? ReadPoint(wire, "end") : std::nullopt;
            if (!start || !end) {
                schematic.Warnings.push_back(
                    std::format("Wire {} has no integer start and end and was skipped", index + 1));
            } else if (*start == *end) {
                schematic.Warnings.push_back(std::format("Wire {} has zero length and was skipped", index + 1));
            } else if (start->X != end->X && start->Y != end->Y) {
                schematic.Warnings.push_back(
                    std::format("Wire {} from ({}, {}) to ({}, {}) is diagonal and was skipped; redraw it", index + 1,
                                start->X, start->Y, end->X, end->Y));
            } else {
                schematic.Wires.emplace_back(*start, *end);
            }
        }
    }
    return schematic;
}

/**
 * @brief   Reads a whole text file.
 * @param[in] path  File to read.
 * @return  The contents, or an error message naming the file.
 */
std::expected<std::string, std::string> ReadTextFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
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
