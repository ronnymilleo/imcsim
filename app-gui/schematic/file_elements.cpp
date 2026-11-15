/**
 * @file    file_elements.cpp
 * @brief   Writes the parts of a schematic to JSON and reads them back, repairing their names.
 */

#include "schematic/file_elements.h"

#include "circuit/components/source.h"
#include "circuit/components/subcircuit.h"
#include "nlohmann/json.hpp"
#include "schematic/element_factory.h"
#include "schematic/file_json.h"
#include "schematic/file_part_fields.h"
#include "schematic/subcircuit_pins.h"
#include <algorithm>
#include <format>

namespace GUI {

namespace {

/**
 * @brief   Writes one part: its place, name, value and the fields of its kind.
 */
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
        if (const auto &expression = component.GetValueExpression()) {
            object["value"] = *expression;
        } else {
            object["value"] = component.GetValue();
        }
    }
    // Only a capacitor or an inductor carries one, and only when it is set
    if (const std::optional<double> initial = component.GetInitialCondition()) {
        object["ic"] = *initial;
    }
    if (const std::optional<double> tolerance = component.GetTolerance()) {
        object["tolerance"] = *tolerance;
    }
    if (Core::IsSource(component.GetType())) {
        WriteSourceFields(static_cast<const Core::Source &>(component), object);
    }
    WriteModelFields(component, object);
    WriteKindFields(component, object);
    WriteFigureFields(component, object);
    return object;
}

std::expected<void, std::string> ReadSubcircuitInstance(const nlohmann::json &object,
                                                        Core::SubcircuitInstance &instance,
                                                        const std::span<const SavedSubcircuit> definitions) {
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
            if (!value.is_number() ||
                !std::ranges::contains(definition->Parameters, parameter, &Core::SubcircuitParameter::Name)) {
                return std::unexpected(std::format("has no valid value for subcircuit parameter {}", parameter));
            }
            instance.SetParameter(parameter, value.get<double>());
        }
    }
    return {};
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
    if (component.GetType() == Core::ComponentType::SUBCIRCUIT) {
        if (const auto read =
                ReadSubcircuitInstance(object, static_cast<Core::SubcircuitInstance &>(component), definitions);
            !read) {
            return std::unexpected(read.error());
        }
    }
    if (component.HasValue()) {
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
    }
    // A file saved before initial conditions had no key, so the part has none
    if (component.SupportsInitialCondition() && object.contains("ic")) {
        const std::optional<double> initial = ReadNumber(object, "ic");
        if (!initial) {
            return std::unexpected("has an initial condition that is not a number");
        }
        component.SetInitialCondition(*initial);
    }
    // A file saved before tolerances had no key, so the part has none
    if (component.SupportsTolerance() && object.contains("tolerance")) {
        const std::optional<double> tolerance = ReadNumber(object, "tolerance");
        if (!tolerance || !(*tolerance > 0.0 && *tolerance <= Core::MAX_TOLERANCE_PERCENT)) {
            return std::unexpected(
                std::format("has a tolerance that is not a percent above 0 and up to {}", Core::MAX_TOLERANCE_PERCENT));
        }
        component.SetTolerance(*tolerance);
    }
    if (Core::IsSource(component.GetType())) {
        const std::expected<void, std::string> source =
            ReadSourceFields(object, static_cast<Core::Source &>(component));
        if (!source) {
            return std::unexpected(source.error());
        }
    }
    if (const std::expected<void, std::string> model = ReadModelFields(object, component); !model) {
        return std::unexpected(model.error());
    }
    if (const std::expected<void, std::string> state = ReadStateFields(object, component); !state) {
        return std::unexpected(state.error());
    }
    if (const std::expected<void, std::string> figures = ReadFigureFields(object, component); !figures) {
        return std::unexpected(figures.error());
    }
    if (const std::optional<std::string> name = ReadString(object, "name")) {
        component.SetName(*name);
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
                                           component->GetTypeName(), component->GetName(), new_name));
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
nlohmann::json WriteElements(const std::span<const UIElement *const> elements) {
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
void ReadElements(const nlohmann::json &object, const std::span<const SavedSubcircuit> definitions,
                  std::vector<std::unique_ptr<UIElement>> &elements, std::vector<std::string> &warnings) {
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
