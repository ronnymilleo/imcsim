/**
 * @file    schematic_file.cpp
 * @brief   Saves schematics to JSON and loads them back, reporting the parts that could not be read.
 */

#include "schematic_file.h"

#include "components/bjt.h"
#include "components/diode.h"
#include "components/mosfet.h"
#include "components/source.h"
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
 * @struct  ParameterKey
 * @brief   JSON key of one member of a parameter set, such as Core::PulseParameters, and the member itself.
 */
template <typename Parameters> struct ParameterKey {
    const char *Name;
    double Parameters::*Value;
};

// The sine keys sit in the element object itself, where the first files with AC sources put them
constexpr auto ACKeys = std::to_array<ParameterKey<Core::ACParameters>>({
    {"amplitude", &Core::ACParameters::Amplitude},
    {"frequency", &Core::ACParameters::Frequency},
    {"offset", &Core::ACParameters::Offset},
});

constexpr auto PulseKeys = std::to_array<ParameterKey<Core::PulseParameters>>({
    {"low", &Core::PulseParameters::Low},
    {"high", &Core::PulseParameters::High},
    {"delay", &Core::PulseParameters::Delay},
    {"rise", &Core::PulseParameters::RiseTime},
    {"fall", &Core::PulseParameters::FallTime},
    {"width", &Core::PulseParameters::Width},
    {"period", &Core::PulseParameters::Period},
});

// Named after the SPICE diode parameters
constexpr auto DiodeKeys = std::to_array<ParameterKey<Core::DiodeParameters>>({
    {"is", &Core::DiodeParameters::SaturationCurrent},
    {"n", &Core::DiodeParameters::EmissionCoefficient},
    {"rs", &Core::DiodeParameters::SeriesResistance},
    {"bv", &Core::DiodeParameters::BreakdownVoltage},
    {"ibv", &Core::DiodeParameters::BreakdownCurrent},
    {"cjo", &Core::DiodeParameters::JunctionCapacitance},
    {"vj", &Core::DiodeParameters::JunctionPotential},
    {"m", &Core::DiodeParameters::GradingCoefficient},
    {"tt", &Core::DiodeParameters::TransitTime},
});

constexpr auto BJTKeys = std::to_array<ParameterKey<Core::BJTParameters>>({
    {"is", &Core::BJTParameters::SaturationCurrent},
    {"bf", &Core::BJTParameters::ForwardBeta},
    {"br", &Core::BJTParameters::ReverseBeta},
    {"vaf", &Core::BJTParameters::EarlyVoltage},
    {"ikf", &Core::BJTParameters::ForwardKneeCurrent},
    {"ise", &Core::BJTParameters::LeakageSaturationCurrent},
    {"ne", &Core::BJTParameters::LeakageEmissionCoefficient},
    {"rb", &Core::BJTParameters::BaseResistance},
    {"rc", &Core::BJTParameters::CollectorResistance},
    {"re", &Core::BJTParameters::EmitterResistance},
    {"cje", &Core::BJTParameters::EmitterCapacitance},
    {"cjc", &Core::BJTParameters::CollectorCapacitance},
    {"tf", &Core::BJTParameters::TransitTime},
    {"vceo", &Core::BJTParameters::MaxCollectorEmitterVoltage},
    {"icmax", &Core::BJTParameters::MaxCollectorCurrent},
    {"pmax", &Core::BJTParameters::MaxPower},
});

constexpr auto MOSFETKeys = std::to_array<ParameterKey<Core::MOSFETParameters>>({
    {"vto", &Core::MOSFETParameters::ThresholdVoltage},
    {"kp", &Core::MOSFETParameters::Transconductance},
    {"lambda", &Core::MOSFETParameters::ChannelModulation},
    {"rd", &Core::MOSFETParameters::DrainResistance},
    {"rs", &Core::MOSFETParameters::SourceResistance},
    {"cgso", &Core::MOSFETParameters::GateSourceOverlap},
    {"cgdo", &Core::MOSFETParameters::GateDrainOverlap},
    {"w", &Core::MOSFETParameters::Width},
    {"l", &Core::MOSFETParameters::Length},
    {"vdsmax", &Core::MOSFETParameters::MaxDrainSourceVoltage},
    {"vgsmax", &Core::MOSFETParameters::MaxGateSourceVoltage},
    {"idmax", &Core::MOSFETParameters::MaxDrainCurrent},
    {"pmax", &Core::MOSFETParameters::MaxPower},
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

std::optional<bool> ReadBool(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end() || !entry->is_boolean()) {
        return std::nullopt;
    }
    return entry->get<bool>();
}

std::optional<GridPoint> ReadPoint(const nlohmann::json &object, const char *key) {
    const auto entry = object.find(key);
    if (entry == object.end() || !entry->is_array() || entry->size() != 2 || !(*entry)[0].is_number_integer() ||
        !(*entry)[1].is_number_integer()) {
        return std::nullopt;
    }
    return GridPoint{(*entry)[0].get<int>(), (*entry)[1].get<int>()};
}

template <typename Parameters, std::size_t Count>
void WriteParameters(const std::array<ParameterKey<Parameters>, Count> &keys, const Parameters &parameters,
                     nlohmann::json &object) {
    for (const ParameterKey<Parameters> &key : keys) {
        object[key.Name] = parameters.*key.Value;
    }
}

// Missing keys keep the value the parameters already have; a key with something other than a number is an error
template <typename Parameters, std::size_t Count>
std::expected<void, std::string> ReadParameters(const std::array<ParameterKey<Parameters>, Count> &keys,
                                                const nlohmann::json &object, Parameters &parameters) {
    for (const ParameterKey<Parameters> &key : keys) {
        if (!object.contains(key.Name)) {
            continue;
        }
        const std::optional<double> value = ReadNumber(object, key.Name);
        if (!value) {
            return std::unexpected(std::format("has no valid {}", key.Name));
        }
        parameters.*key.Value = *value;
    }
    return {};
}

// Parts with a model save its name, plus their parameters when custom
template <typename Part, typename Parameters, std::size_t Count>
void WriteModelChoice(const Part &part, const std::array<ParameterKey<Parameters>, Count> &keys,
                      nlohmann::json &object) {
    object["model"] = part.GetModelName();
    if (part.IsCustom()) {
        nlohmann::json parameters = nlohmann::json::object();
        WriteParameters(keys, part.GetParameters(), parameters);
        object["parameters"] = parameters;
    }
}

nlohmann::json WriteElement(const UIElement &element) {
    const Core::Component &component = element.GetComponent();
    nlohmann::json object = {
        {"type", component.GetTypeName()},
        {"x", element.GetPosition().X},
        {"y", element.GetPosition().Y},
        {"rotation", ToDegrees(element.GetRotation())},
    };
    // Only mirrored elements carry the key, so files of unmirrored schematics stay as they were
    if (element.IsMirrored()) {
        object["mirrored"] = true;
    }
    if (!component.GetName().empty()) {
        object["name"] = component.GetName();
    }
    if (component.HasValue()) {
        object["value"] = component.GetValue();
    }
    // Every waveform is saved, so switching type after loading keeps the parameters of the others
    if (Core::IsSource(component.GetType())) {
        const auto &source = static_cast<const Core::Source &>(component);
        object["source"] = Core::GetSourceTypeName(source.GetSourceType());
        WriteParameters(ACKeys, source.GetAC(), object);
        nlohmann::json pulse = nlohmann::json::object();
        WriteParameters(PulseKeys, source.GetPulse(), pulse);
        object["pulse"] = pulse;
    }
    const Core::ComponentType type = component.GetType();
    if (Core::IsDiode(type)) {
        WriteModelChoice(static_cast<const Core::Diode &>(component), DiodeKeys, object);
    } else if (Core::IsBJT(type)) {
        WriteModelChoice(static_cast<const Core::BJT &>(component), BJTKeys, object);
    } else if (Core::IsMOSFET(type)) {
        WriteModelChoice(static_cast<const Core::MOSFET &>(component), MOSFETKeys, object);
    }
    return object;
}

// Every key is optional, so files saved before sources had a type load as DC with the default sine and pulse
std::expected<void, std::string> ReadSource(const nlohmann::json &object, Core::Source &source) {
    if (object.contains("source")) {
        const std::optional<std::string> type_name = ReadString(object, "source");
        const std::optional<Core::Source::SourceType> type =
            type_name ? Core::ParseSourceType(*type_name) : std::nullopt;
        if (!type) {
            return std::unexpected("has a source type other than DC, AC or Pulse");
        }
        source.SetSourceType(*type);
    }
    Core::ACParameters ac = source.GetAC();
    if (const auto read = ReadParameters(ACKeys, object, ac); !read) {
        return read;
    }
    if (!source.IsValidAC(ac)) {
        return std::unexpected("has an AC frequency that is not positive");
    }
    source.SetAC(ac);

    if (const auto pulse_object = object.find("pulse"); pulse_object != object.end()) {
        if (!pulse_object->is_object()) {
            return std::unexpected("has a pulse that is not an object");
        }
        Core::PulseParameters pulse = source.GetPulse();
        if (const auto read = ReadParameters(PulseKeys, *pulse_object, pulse); !read) {
            return read;
        }
        if (!source.IsValidPulse(pulse)) {
            return std::unexpected("has a pulse with a negative time or a period that is not positive");
        }
        source.SetPulse(pulse);
    }
    return {};
}

// Missing custom parameters keep those of the default model of the part
template <typename Part, typename Model, typename Parameters, std::size_t Count>
std::expected<void, std::string> ReadModelChoice(const nlohmann::json &object, Part &part,
                                                 const Model *(*find_model)(Core::ComponentType, std::string_view),
                                                 const char *custom_name,
                                                 const std::array<ParameterKey<Parameters>, Count> &keys) {
    const std::optional<std::string> model_name = ReadString(object, "model");
    if (!model_name) {
        return std::unexpected("has no valid model");
    }
    if (*model_name != custom_name) {
        const Model *model = find_model(part.GetType(), *model_name);
        if (model == nullptr) {
            return std::unexpected("has no valid model");
        }
        part.SetModel(*model);
        return {};
    }

    Parameters parameters = part.GetParameters();
    if (const auto parameters_object = object.find("parameters"); parameters_object != object.end()) {
        if (!parameters_object->is_object()) {
            return std::unexpected("has model parameters that are not an object");
        }
        if (const auto read = ReadParameters(keys, *parameters_object, parameters); !read) {
            return read;
        }
    }
    if (!part.IsValidParameters(parameters)) {
        return std::unexpected("has model parameters out of range");
    }
    part.SetCustomParameters(parameters);
    return {};
}

std::expected<void, std::string> ReadModel(const nlohmann::json &object, Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if (Core::IsDiode(type)) {
        return ReadModelChoice(object, static_cast<Core::Diode &>(component), &Core::FindDiodeModel,
                               Core::CustomDiodeModelName, DiodeKeys);
    }
    if (Core::IsBJT(type)) {
        return ReadModelChoice(object, static_cast<Core::BJT &>(component), &Core::FindBJTModel,
                               Core::CustomBJTModelName, BJTKeys);
    }
    if (Core::IsMOSFET(type)) {
        return ReadModelChoice(object, static_cast<Core::MOSFET &>(component), &Core::FindMOSFETModel,
                               Core::CustomMOSFETModelName, MOSFETKeys);
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
    if (object.contains("mirrored")) {
        const std::optional<bool> mirrored = ReadBool(object, "mirrored");
        if (!mirrored) {
            return std::unexpected("has a mirrored flag that is not true or false");
        }
        element->SetMirrored(*mirrored);
    }
    Core::Component &component = element->GetComponent();
    if (component.HasValue()) {
        const std::optional<double> value = ReadNumber(object, "value");
        if (!value || !component.IsValidValue(*value)) {
            return std::unexpected("has no valid value");
        }
        component.SetValue(*value);
    }
    if (Core::IsSource(component.GetType())) {
        const std::expected<void, std::string> source = ReadSource(object, static_cast<Core::Source &>(component));
        if (!source) {
            return std::unexpected(source.error());
        }
    }
    if (const std::expected<void, std::string> model = ReadModel(object, component); !model) {
        return std::unexpected(model.error());
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
