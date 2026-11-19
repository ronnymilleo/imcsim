/**
 * @file    block_library.cpp
 * @brief   The block definitions and library models a schematic keeps, and the blocks as the simulator takes them.
 */

#include "schematic/block_library.h"

#include "circuit/components/subcircuit.h"
#include "schematic/block_definitions.h"
#include "schematic/block_nesting.h"
#include "schematic/connectivity.h"
#include "schematic/subcircuit_pins.h"
#include <algorithm>
#include <format>
#include <utility>

namespace GUI {

namespace {

/**
 * @brief   Builds a block into the definition the simulator takes, with each pin on the node of its point.
 */
std::shared_ptr<Core::SubcircuitDefinition> BuildDefinition(const SavedSubcircuit &saved) {
    const Connectivity connectivity(saved.Elements, saved.Wires);
    auto definition = std::make_shared<Core::SubcircuitDefinition>();
    definition->Name = saved.Name;
    definition->Parameters = saved.Parameters;
    definition->Spice = saved.Spice;
    // A pin on no node would be refused by the checks, which read node 0 as ground
    for (const SavedSubcircuitPin &pin : saved.Pins) {
        definition->Pins.push_back({pin.Name, connectivity.GetNode(pin.Point).value_or(0)});
    }
    definition->Body = BuildCircuit(saved.Elements, connectivity);
    return definition;
}

/**
 * @brief   The first name such as "1N4148_2" that no model has yet, for a model that differs from one of its name.
 */
std::string FindFreeModelName(const std::string &name, const std::vector<Core::LibraryModel> &models) {
    for (int suffix = 2;; ++suffix) {
        std::string candidate = std::format("{}_{}", name, suffix);
        if (!std::ranges::contains(models, candidate, &Core::LibraryModel::Name)) {
            return candidate;
        }
    }
}

void RenameModelUsers(std::vector<std::unique_ptr<UIElement>> &users,
                      const std::string &old_name,
                      const std::string &new_name) {
    for (auto &user : users) {
        Core::Component &component = user->GetComponent();
        if (component.GetLibraryModel() == old_name) {
            component.SetLibraryModel(new_name);
        }
    }
}

} // namespace

/**
 * @brief   Makes a library of the blocks and models of a schematic, as read from a file or a snapshot.
 * @param[in] subcircuits  Block definitions.
 * @param[in] models       Library models.
 */
BlockLibrary::BlockLibrary(std::vector<SavedSubcircuit> subcircuits, std::vector<Core::LibraryModel> models)
    : m_Subcircuits(std::move(subcircuits)), m_LibraryModels(std::move(models)) {
}

/**
 * @brief   Returns the block definitions.
 * @return  Every definition, in the order they were made, whether an instance uses it or not.
 */
const std::vector<SavedSubcircuit> &BlockLibrary::GetSubcircuits() const {
    return m_Subcircuits;
}

/**
 * @brief   Returns the device models imported from libraries.
 * @return  Every model, used by a part or not, in the order they were imported.
 */
const std::vector<Core::LibraryModel> &BlockLibrary::GetLibraryModels() const {
    return m_LibraryModels;
}

/**
 * @brief   Checks that a new block can take a name.
 * @param[in] name  The name asked for.
 * @return  Nothing when it is an identifier no block has; the reason otherwise.
 */
std::expected<void, std::string> BlockLibrary::CheckNewName(const std::string &name) const {
    if (!Core::SubcircuitInstance::IsValidIdentifier(name)) {
        return std::unexpected("The subcircuit name must be an identifier");
    }
    if (std::ranges::contains(m_Subcircuits, name, &SavedSubcircuit::Name)) {
        return std::unexpected("A subcircuit with this name already exists");
    }
    return {};
}

/**
 * @brief   Adds a new block definition.
 * @param[in] definition  The definition, with a name CheckNewName() allows.
 */
void BlockLibrary::AddSubcircuit(SavedSubcircuit definition) {
    m_Subcircuits.push_back(std::move(definition));
    m_BuiltSubcircuits.reset();
}

/**
 * @brief   Copies a drawn block, and the other blocks its parts may place, into copies that share nothing with these.
 * @param[in] name  Name of the definition.
 * @return  The copies; the reason otherwise, such as an unknown name or a block kept as SPICE text.
 */
std::expected<BlockCopy, std::string> BlockLibrary::CopyForEditing(const std::string &name) const {
    const auto found = std::ranges::find(m_Subcircuits, name, &SavedSubcircuit::Name);
    if (found == m_Subcircuits.end()) {
        return std::unexpected("Unknown subcircuit " + name);
    }
    if (found->Spice) {
        return std::unexpected(name + " is SPICE text, which has no drawing to open");
    }
    std::vector<SavedSubcircuit> definitions = CopyDefinitions(m_Subcircuits);
    const auto copied = std::ranges::find(definitions, name, &SavedSubcircuit::Name);
    if (copied == definitions.end()) {
        return std::unexpected("The subcircuit definition could not be opened");
    }
    BlockCopy copy = {.Definition = std::move(*copied), .OtherDefinitions = {}};
    definitions.erase(copied);
    copy.OtherDefinitions = std::move(definitions);
    return copy;
}

/**
 * @brief   Puts edited contents back into a drawn block, and brings its instances inside the other blocks up to date.
 * @param[in] name        Name of the definition.
 * @param[in] elements    Parts of the edited contents.
 * @param[in] wires       Wires of the edited contents.
 * @param[in] pins        Pins of the block, each at the point of its marker in the contents, in terminal order.
 * @param[in] parameters  Parameters of the block, with their defaults.
 * @return  The change, for the instances outside the library; the reason otherwise, with the library unchanged.
 * @note    A block that would contain itself, directly or through another block, is refused. The contents are copied,
 *          so they share nothing with the parts given.
 */
std::expected<DefinitionChange, std::string>
BlockLibrary::UpdateDrawn(const std::string &name,
                          const std::vector<std::unique_ptr<UIElement>> &elements,
                          const std::vector<UIWire> &wires,
                          const std::vector<SavedSubcircuitPin> &pins,
                          const std::vector<Core::SubcircuitParameter> &parameters) {
    const auto found = std::ranges::find(m_Subcircuits, name, &SavedSubcircuit::Name);
    if (found == m_Subcircuits.end()) {
        return std::unexpected("Unknown subcircuit " + name);
    }
    if (UsesBlock(elements, m_Subcircuits, name)) {
        return std::unexpected(std::format("{} cannot contain itself, directly or through another block", name));
    }
    if (auto valid = ValidateMetadata(pins, parameters); !valid) {
        return std::unexpected(valid.error());
    }
    if (const std::optional<std::string> problem = CheckPinNodes(Connectivity(elements, wires), pins)) {
        return std::unexpected(*problem);
    }
    if (auto valid = ValidateExpressions(elements, parameters); !valid) {
        return std::unexpected(valid.error());
    }
    // The text of a file copies the parts deeply; the blocks they place go along, so their instances read back
    auto copied = LoadSchematic(SaveSchematic(elements, wires, {}, {}, m_Subcircuits));
    if (!copied) {
        return std::unexpected(copied.error());
    }

    SavedSubcircuit &definition = *found;
    std::vector<std::optional<std::size_t>> matches = MatchPins(definition.Pins, pins);
    definition.Elements = std::move(copied->Elements);
    definition.Wires = std::move(copied->Wires);
    return ApplyPinChange(definition, pins, parameters, std::move(matches));
}

/**
 * @brief   Changes the pins and parameters of a block kept as SPICE text, and brings its instances inside the other
 *          blocks up to date.
 * @param[in] name        Name of the definition.
 * @param[in] pins        Its pins, one per port of its .subckt line and in that order, with their names and edges.
 * @param[in] parameters  Its parameters, with their defaults.
 * @return  The change, for the instances outside the library; the reason otherwise, with the library unchanged.
 * @note    The text stays as it came. Each pin keeps the wires of its instances wherever its edge moves it.
 */
std::expected<DefinitionChange, std::string>
BlockLibrary::UpdateSpice(const std::string &name,
                          const std::vector<SavedSubcircuitPin> &pins,
                          const std::vector<Core::SubcircuitParameter> &parameters) {
    const auto found = std::ranges::find(m_Subcircuits, name, &SavedSubcircuit::Name);
    if (found == m_Subcircuits.end()) {
        return std::unexpected("Unknown SPICE block " + name);
    }
    const std::optional<Core::SubcircuitText> &spice = found->Spice;
    if (!spice) {
        return std::unexpected(name + " is a drawn block, not SPICE text");
    }
    if (pins.size() != spice->Ports.size()) {
        return std::unexpected("Give every port of the SPICE text a pin");
    }
    if (auto valid = ValidateMetadata(pins, parameters); !valid) {
        return std::unexpected(valid.error());
    }
    SavedSubcircuit &definition = *found;
    std::vector<std::optional<std::size_t>> matches;
    for (std::size_t index = 0; index < definition.Pins.size(); ++index) {
        // A port is the same pin whatever it is called now
        matches.emplace_back(index);
    }
    return ApplyPinChange(definition, pins, parameters, std::move(matches));
}

/**
 * @brief   Adds the definitions a pasted group brought, so its instances have them.
 * @param[in]     definitions  Definitions the group's instances use, with the blocks those place.
 * @param[in,out] instances    Elements of the group; the instances of a renamed definition are pointed at its new name.
 * @return  The name each definition has here, and whether any was added.
 * @note    A definition equal to one of the same name already here is dropped, since the instances can use that one.
 *          One that differs is added under a free name, such as "block_2", so neither replaces the other; the blocks
 *          that place it are pointed at that name too. Each block is merged after the blocks it places, so it is
 *          compared with their names as they end up here.
 */
MergeResult BlockLibrary::MergeSubcircuits(std::vector<SavedSubcircuit> definitions,
                                           std::vector<std::unique_ptr<UIElement>> &instances) {
    MergeResult result = {.Names = std::vector<std::string>(definitions.size()), .IsAnyAdded = false};
    for (const std::size_t index : OrderByUse(definitions)) {
        SavedSubcircuit &definition = definitions[index];
        const auto existing = std::ranges::find(m_Subcircuits, definition.Name, &SavedSubcircuit::Name);
        if (existing != m_Subcircuits.end() && DescribeDefinition(*existing) == DescribeDefinition(definition)) {
            result.Names[index] = definition.Name;
            continue;
        }
        if (existing != m_Subcircuits.end()) {
            const std::string old_name = definition.Name;
            definition.Name = FindFreeName(old_name, m_Subcircuits);
            RenameBlockInstances(instances, old_name, definition.Name);
            for (SavedSubcircuit &other : definitions) {
                RenameBlockInstances(other.Elements, old_name, definition.Name);
            }
        }
        result.Names[index] = definition.Name;
        AddSubcircuit(std::move(definition));
        result.IsAnyAdded = true;
    }
    return result;
}

/**
 * @brief   Adds device models from a library or a paste, so the parts that name them have them.
 * @param[in]     models       Models to add.
 * @param[in,out] elements     Parts that came with them, pointed at the new name of a renamed model.
 * @param[in,out] definitions  Blocks that came with them, whose parts are pointed at it too.
 * @return  The name each model has here, and whether any was added.
 * @note    A model equal to one of the same name already here is dropped, since the parts can use that one; one that
 *          differs is added under a free name, such as "1N4148_2", so neither replaces the other.
 */
MergeResult BlockLibrary::MergeLibraryModels(std::vector<Core::LibraryModel> models,
                                             std::vector<std::unique_ptr<UIElement>> &elements,
                                             std::vector<SavedSubcircuit> &definitions) {
    MergeResult result;
    for (Core::LibraryModel &model : models) {
        const auto existing = std::ranges::find(m_LibraryModels, model.Name, &Core::LibraryModel::Name);
        if (existing != m_LibraryModels.end() && existing->Text == model.Text) {
            result.Names.push_back(model.Name);
            continue;
        }
        if (existing != m_LibraryModels.end()) {
            const std::string free_name = FindFreeModelName(model.Name, m_LibraryModels);
            RenameModelUsers(elements, model.Name, free_name);
            for (SavedSubcircuit &definition : definitions) {
                RenameModelUsers(definition.Elements, model.Name, free_name);
            }
            model.Text = Core::RenameLibraryModel(model, free_name);
            model.Name = free_name;
        }
        result.Names.push_back(model.Name);
        m_LibraryModels.push_back(std::move(model));
        result.IsAnyAdded = true;
    }
    return result;
}

/**
 * @brief   Takes a copy of the blocks and library models of another library, as the contents of an open block need
 *          those of the document after it gained some.
 * @param[in] library   The library whose blocks and models are copied.
 * @param[in] left_out  A block to leave out, such as the one these are the contents of, or empty.
 */
void BlockLibrary::CopyFrom(const BlockLibrary &library, const std::string_view left_out) {
    m_Subcircuits = CopyDefinitions(library.m_Subcircuits);
    std::erase_if(m_Subcircuits, [&](const SavedSubcircuit &definition) { return definition.Name == left_out; });
    m_LibraryModels = library.m_LibraryModels;
    m_BuiltSubcircuits.reset();
}

/**
 * @brief   Returns the blocks as the simulator takes them.
 * @return  One definition per block, in the order of GetSubcircuits(); built on first use after a change and kept
 *          until the next one.
 */
const std::vector<std::shared_ptr<Core::SubcircuitDefinition>> &BlockLibrary::GetBuiltDefinitions() {
    if (!m_BuiltSubcircuits) {
        m_BuiltSubcircuits.emplace();
        for (const SavedSubcircuit &saved : m_Subcircuits) {
            m_BuiltSubcircuits->push_back(BuildDefinition(saved));
        }
    }
    return *m_BuiltSubcircuits;
}

/**
 * @brief   Gives a definition its new pins and parameters, and brings its instances inside the other blocks up to date.
 * @details The tail both kinds of update share, once they checked the change and took the old pins' matches.
 */
DefinitionChange BlockLibrary::ApplyPinChange(SavedSubcircuit &definition,
                                              const std::vector<SavedSubcircuitPin> &pins,
                                              const std::vector<Core::SubcircuitParameter> &parameters,
                                              std::vector<std::optional<std::size_t>> matches) {
    DefinitionChange change = {.Definition = &definition, .OldNames = {}, .Matches = std::move(matches)};
    for (const SavedSubcircuitPin &pin : definition.Pins) {
        change.OldNames.push_back(pin.Name);
    }
    definition.Pins = pins;
    definition.Parameters = parameters;
    m_BuiltSubcircuits.reset();
    for (SavedSubcircuit &other : m_Subcircuits) {
        if (&other != &definition) {
            RefreshInstancesIn(other.Elements, other.Wires, definition, change.OldNames, change.Matches);
        }
    }
    return change;
}

} // namespace GUI
