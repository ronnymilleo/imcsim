/**
 * @file    file_elements.cpp
 * @brief   Writes the parts of a schematic to JSON and reads them back, repairing their names.
 */

#include "schematic/file_elements.h"

#include "circuit/components/subcircuit.h"
#include "nlohmann/json.hpp"
#include "schematic/element_factory.h"
#include "schematic/file_figure_fields.h"
#include "schematic/file_json.h"
#include "schematic/file_model_fields.h"
#include "schematic/file_part_state_fields.h"
#include "schematic/file_source_fields.h"
#include "schematic/subcircuit_pins.h"
#include <algorithm>
#include <array>
#include <format>
#include <optional>

namespace GUI {

namespace {

// Placement

/**
 * @brief   Writes where a part sits and what kind it is.
 */
void WritePlacement(const UIElement &element, nlohmann::json &object) {
    object["type"] = element.GetComponent().GetTypeName();
    object["x"] = element.GetPosition().X;
    object["y"] = element.GetPosition().Y;
    object["rotation"] = ToDegrees(element.GetRotation());
}

/**
 * @brief   Creates the part a file entry describes, at its place.
 */
std::expected<std::unique_ptr<UIElement>, std::string> ReadPlacement(const nlohmann::json &object) {
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
    return CreateElement(*type, {*x, *y}, *rotation);
}

// Mirroring

void WriteMirrored(const UIElement &element, nlohmann::json &object) {
    // Only mirrored elements carry the key, so files of unmirrored schematics stay as they were
    if (element.IsMirrored()) {
        object["mirrored"] = true;
    }
}

std::expected<void, std::string> ReadMirrored(const nlohmann::json &object, UIElement &element) {
    bool is_mirrored = false;
    const std::expected<bool, std::string> has_flag =
        ReadOptionalBool(object, "mirrored", is_mirrored, "has a mirrored flag that is not true or false");
    if (!has_flag) {
        return std::unexpected(has_flag.error());
    }
    if (*has_flag) {
        element.SetMirrored(is_mirrored);
    }
    return {};
}

// Block instances

/**
 * @brief   An instance of a block names its definition and keeps the parameters it overrides.
 */
void WriteBlockInstance(const Core::Component &component, nlohmann::json &object) {
    if (component.GetType() != Core::ComponentType::SUBCIRCUIT) {
        return;
    }
    const auto &instance = static_cast<const Core::SubcircuitInstance &>(component);
    object["definition"] = instance.GetDefinitionName();
    if (!instance.GetParameters().empty()) {
        object["parameters"] = instance.GetParameters();
    }
}

std::expected<void, std::string> ReadBlockInstance(const nlohmann::json &object,
                                                   Core::Component &component,
                                                   const std::span<const SavedSubcircuit> definitions) {
    if (component.GetType() != Core::ComponentType::SUBCIRCUIT) {
        return {};
    }
    auto &instance = static_cast<Core::SubcircuitInstance &>(component);
    const std::optional<std::string> name = ReadString(object, "definition");
    if (!name || !Core::SubcircuitInstance::IsValidIdentifier(*name)) {
        return std::unexpected("has no valid subcircuit definition");
    }
    const auto definition = std::ranges::find(definitions, *name, &SavedSubcircuit::Name);
    if (definition == definitions.end()) {
        return std::unexpected(std::format("refers to unknown subcircuit \"{}\"", *name));
    }
    ApplyDefinitionPins(instance, *definition);
    if (const auto parameters = object.find("parameters"); parameters != object.end()) {
        if (!parameters->is_object()) {
            return std::unexpected("has subcircuit parameters that are not an object");
        }
        for (const auto &[parameter, value] : parameters->items()) {
            if (!value.is_number()
                || !std::ranges::contains(definition->Parameters, parameter, &Core::SubcircuitParameter::Name)) {
                return std::unexpected(std::format("has no valid value for subcircuit parameter {}", parameter));
            }
            instance.SetParameter(parameter, value.get<double>());
        }
    }
    return {};
}

// Value

/**
 * @brief   A value is a number, or the name of a parameter of the block that holds the part, such as "{R}".
 */
void WriteValue(const Core::Component &component, nlohmann::json &object) {
    if (!component.HasValue()) {
        return;
    }
    if (const auto &expression = component.GetValueExpression()) {
        object["value"] = *expression;
    } else {
        object["value"] = component.GetValue();
    }
}

std::expected<void, std::string> ReadValue(const nlohmann::json &object, Core::Component &component) {
    if (!component.HasValue()) {
        return {};
    }
    if (const std::optional<double> value = ReadNumber(object, "value")) {
        if (!component.IsValidValue(*value)) {
            return std::unexpected("has no valid value");
        }
        component.SetValue(*value);
    } else if (const std::optional<std::string> expression = ReadString(object, "value")) {
        if (!Core::SubcircuitInstance::IsValidValueExpression(*expression)) {
            return std::unexpected("has no valid parameter expression");
        }
        if (!component.CanFollowParameter()) {
            return std::unexpected("follows a parameter, which only resistors, capacitors and inductors can");
        }
        component.SetValueExpression(*expression);
    } else {
        return std::unexpected("has no valid value");
    }
    return {};
}

// Initial condition

/**
 * @brief   Only a capacitor or an inductor carries one, and only when it is set; a file saved before initial
 *          conditions had no key, so the part has none.
 */
void WriteInitialCondition(const Core::Component &component, nlohmann::json &object) {
    if (const std::optional<double> initial = component.GetInitialCondition()) {
        object["ic"] = *initial;
    }
}

std::expected<void, std::string> ReadInitialCondition(const nlohmann::json &object, Core::Component &component) {
    if (!component.SupportsInitialCondition()) {
        return {};
    }
    double initial = 0.0;
    const std::expected<bool, std::string> has_initial =
        ReadOptionalNumber(object, "ic", initial, "has an initial condition that is not a number");
    if (!has_initial) {
        return std::unexpected(has_initial.error());
    }
    if (*has_initial) {
        component.SetInitialCondition(initial);
    }
    return {};
}

// Tolerance

bool IsValidFileTolerance(const double tolerance) {
    return tolerance > 0.0 && tolerance <= Core::MAX_TOLERANCE_PERCENT;
}

/**
 * @brief   A file saved before tolerances had no key, so the part has none.
 */
void WriteTolerance(const Core::Component &component, nlohmann::json &object) {
    if (const std::optional<double> tolerance = component.GetTolerance()) {
        object["tolerance"] = *tolerance;
    }
}

std::expected<void, std::string> ReadTolerance(const nlohmann::json &object, Core::Component &component) {
    if (!component.SupportsTolerance()) {
        return {};
    }
    double tolerance = 0.0;
    const std::expected<bool, std::string> has_tolerance = ReadOptionalNumber(
        object,
        "tolerance",
        tolerance,
        std::format("has a tolerance that is not a percent above 0 and up to {}", Core::MAX_TOLERANCE_PERCENT),
        &IsValidFileTolerance);
    if (!has_tolerance) {
        return std::unexpected(has_tolerance.error());
    }
    if (*has_tolerance) {
        component.SetTolerance(tolerance);
    }
    return {};
}

// Name

void WriteName(const Core::Component &component, nlohmann::json &object) {
    if (!component.GetName().empty()) {
        object["name"] = component.GetName();
    }
}

/**
 * @brief   A name that is missing or not text is left for FixNames() to replace.
 */
std::expected<void, std::string> ReadName(const nlohmann::json &object, Core::Component &component) {
    if (std::optional<std::string> name = ReadString(object, "name")) {
        component.SetName(std::move(*name));
    }
    return {};
}

// Table of the fields of a component

/**
 * @struct  ComponentFieldCodec
 * @brief   The functions that write and read one field of a part, side by side so a key is never added to one only.
 */
struct ComponentFieldCodec {
    void (*Write)(const Core::Component &, nlohmann::json &);
    std::expected<void, std::string> (*Read)(const nlohmann::json &, Core::Component &);
};

/// In the order the fields are read, which decides which problem a damaged file reports first.
constexpr auto COMPONENT_FIELDS = std::to_array<ComponentFieldCodec>({
    {&WriteValue, &ReadValue},
    {&WriteInitialCondition, &ReadInitialCondition},
    {&WriteTolerance, &ReadTolerance},
    {&WriteSourceFields, &ReadSourceFields},
    {&WriteModelFields, &ReadModelFields},
    {&WriteStateFields, &ReadStateFields},
    {&WriteFigureFields, &ReadFigureFields},
    {&WriteName, &ReadName},
});

/**
 * @brief   Writes one part: its place, then its fields.
 */
nlohmann::json WriteElement(const UIElement &element) {
    nlohmann::json object = nlohmann::json::object();
    WritePlacement(element, object);
    WriteMirrored(element, object);
    WriteBlockInstance(element.GetComponent(), object);
    for (const ComponentFieldCodec &field : COMPONENT_FIELDS) {
        field.Write(element.GetComponent(), object);
    }
    return object;
}

/**
 * @brief   Reads one part.
 * @details Names are checked separately, once every element is read, so duplicates can be found.
 */
std::expected<std::unique_ptr<UIElement>, std::string> ReadElement(const nlohmann::json &object,
                                                                   const std::span<const SavedSubcircuit> definitions) {
    if (!object.is_object()) {
        return std::unexpected("is not an object");
    }
    std::expected<std::unique_ptr<UIElement>, std::string> element = ReadPlacement(object);
    if (!element) {
        return element;
    }
    if (const auto read = ReadMirrored(object, **element); !read) {
        return std::unexpected(read.error());
    }
    Core::Component &component = (*element)->GetComponent();
    if (const auto read = ReadBlockInstance(object, component, definitions); !read) {
        return std::unexpected(read.error());
    }
    for (const ComponentFieldCodec &field : COMPONENT_FIELDS) {
        if (const auto read = field.Read(object, component); !read) {
            return std::unexpected(read.error());
        }
    }
    return element;
}

/**
 * @brief   Keeps valid, unique names and renames the rest, so the netlist never gets two components with one name.
 */
void FixNames(const std::vector<std::unique_ptr<UIElement>> &elements, std::vector<std::string> &warnings) {
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
                                           component->GetTypeName(),
                                           component->GetName(),
                                           new_name));
        }
        used_names.push_back(new_name);
        component->SetName(std::move(new_name));
    }
}

} // namespace

/**
 * @brief   Writes parts as a JSON list.
 * @param[in] elements  Parts to write, in the order they are listed.
 * @return  The list, with one object per part.
 */
nlohmann::json WriteElements(std::span<const UIElement *const> elements) {
    nlohmann::json list = nlohmann::json::array();
    for (const UIElement *element : elements) {
        list.push_back(WriteElement(*element));
    }
    return list;
}

/**
 * @brief   Reads the "elements" list of a document or of a block definition.
 * @details A part that cannot be read is skipped with a warning, and repeated or missing names are replaced, each with
 *          a warning.
 * @param[in]     object       Document or definition object that holds the list.
 * @param[in]     definitions  Block definitions the parts may place, which must be read already.
 * @param[out]    elements     Receives the parts that were read.
 * @param[in,out] warnings     Receives one message for each part skipped or renamed.
 */
void ReadElements(const nlohmann::json &object,
                  const std::span<const SavedSubcircuit> definitions,
                  std::vector<std::unique_ptr<UIElement>> &elements,
                  std::vector<std::string> &warnings) {
    const auto list = object.find("elements");
    if (list != object.end() && list->is_array()) {
        for (std::size_t index = 0; index < list->size(); ++index) {
            auto element = ReadElement((*list)[index], definitions);
            if (element) {
                elements.push_back(std::move(*element));
            } else {
                warnings.push_back(std::format("Element {} {} and was skipped", index + 1, element.error()));
            }
        }
    }
    FixNames(elements, warnings);
}

} // namespace GUI
