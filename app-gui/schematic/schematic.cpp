/**
 * @file    schematic.cpp
 * @brief   The schematic document shared by every window: elements, wires, selection and file state.
 */

#include "schematic/schematic.h"

#include "circuit/components/behavioral_source.h"
#include "circuit/components/component.h"
#include "circuit/components/controlled_source.h"
#include "circuit/components/net_label.h"
#include "circuit/components/subcircuit.h"
#include "schematic/block_definitions.h"
#include "schematic/block_wiring.h"
#include "schematic/element_factory.h"
#include "schematic/group_editing.h"
#include "schematic/part_names.h"
#include "schematic/schematic_file.h"
#include "schematic/subcircuit_pins.h"
#include "schematic/wire_editing.h"
#include "ui_elements/ui_subcircuit.h"
#include <algorithm>
#include <iterator>
#include <string_view>
#include <utility>

namespace GUI {

namespace {

/**
 * @brief   Keeps the controlled sources that follow the current of a renamed part, and the behavioral sources whose
 *          expressions read it, on that part.
 */
void RenameCurrentReferences(std::vector<std::unique_ptr<UIElement>> &elements,
                             const std::string &old_name,
                             const std::string &new_name) {
    for (const auto &element : elements) {
        if (Core::IsControlledSource(element->GetComponent().GetType())) {
            auto &source = static_cast<Core::ControlledSource &>(element->GetComponent());
            source.SetControllingCurrent(RenameCurrent(source.GetControllingCurrent(), old_name, new_name));
        } else if (Core::IsBehavioralSource(element->GetComponent().GetType())) {
            auto &source = static_cast<Core::BehavioralSource &>(element->GetComponent());
            source.SetExpression(RenameCurrentsIn(source.GetExpression(), old_name, new_name));
        }
    }
}

/**
 * @brief   Adds a PIN marker at each pin of a block whose marker is gone from its contents, so every pin can be edited.
 */
void AddMissingPinMarkers(Schematic &content, const std::vector<SavedSubcircuitPin> &pins) {
    for (const SavedSubcircuitPin &pin : pins) {
        if (FindPinMarker(content.GetElements(), pin.Point) == nullptr) {
            auto marker = CreateElement(Core::ComponentType::PIN, pin.Point, Rotation::R0);
            static_cast<Core::NetLabel &>(marker->GetComponent()).SetNet(pin.Name);
            content.AddElement(std::move(marker));
        }
    }
}

} // namespace

/**
 * @brief   Creates an empty, untitled and unmodified schematic.
 */
Schematic::Schematic() {
    ResetHistory();
}

/**
 * @brief   Returns every element, in the order they were added.
 * @return  The elements; later ones are drawn on top.
 */
const std::vector<std::unique_ptr<UIElement>> &Schematic::GetElements() const {
    return m_Elements;
}

/**
 * @brief   Returns an element for editing its position, rotation or component.
 * @param[in] index  Index into GetElements().
 * @return  The element. Call MarkModified() or SetWires() after changing it, as appropriate.
 */
UIElement &Schematic::GetElement(const std::size_t index) {
    return *m_Elements[index];
}

/**
 * @brief   Returns every wire segment.
 * @return  The wires, all horizontal or vertical.
 */
const std::vector<UIWire> &Schematic::GetWires() const {
    return m_Wires;
}

/**
 * @brief   Returns the block definitions of the schematic.
 * @return  Every definition, in the order they were made, whether an instance uses it or not.
 */
const std::vector<SavedSubcircuit> &Schematic::GetSubcircuits() const {
    return m_Blocks.GetSubcircuits();
}

/**
 * @brief   Returns the device models imported from libraries.
 * @return  Every model, used by a part or not, in the order they were imported.
 */
const std::vector<Core::LibraryModel> &Schematic::GetLibraryModels() const {
    return m_Blocks.GetLibraryModels();
}

/**
 * @brief   Checks whether a grid point is the terminal of some element.
 * @param[in] point  Grid point to test.
 * @return  True when an element has a terminal there.
 */
bool Schematic::IsTerminal(const GridPoint point) const {
    return std::ranges::any_of(m_Elements, [point](const auto &element) {
        return std::ranges::contains(element->GetTerminals(), point);
    });
}

/**
 * @brief   Returns the terminals of every element.
 * @return  Terminal positions in world units.
 */
std::vector<GridPoint> Schematic::CollectTerminals() const {
    std::vector<GridPoint> terminals;
    for (const auto &element : m_Elements) {
        std::ranges::copy(element->GetTerminals(), std::back_inserter(terminals));
    }
    return terminals;
}

/**
 * @brief   Adds an element and gives its component the next free SPICE name.
 * @param[in] element  Element to add; its component is renamed even if it already had a name.
 */
void Schematic::AddElement(std::unique_ptr<UIElement> element) {
    Core::Component &component = element->GetComponent();
    const std::string_view prefix = component.GetNamePrefix();
    if (!prefix.empty()) {
        std::vector<std::string> names;
        for (const auto &existing : m_Elements) {
            names.push_back(existing->GetComponent().GetName());
        }
        component.SetName(Core::NextComponentName(prefix, names));
    }
    m_Elements.push_back(std::move(element));
    MarkModified();
}

/**
 * @brief   Adds a wire segment.
 * @param[in] start  First end.
 * @param[in] end    Second end; when it equals start, nothing is added.
 * @note    Zero-length segments appear when an L-shaped bend lands on one of its ends.
 */
void Schematic::AddWire(const GridPoint start, const GridPoint end) {
    if (start == end) {
        return;
    }
    m_Wires.emplace_back(start, end);
    MarkModified();
}

/**
 * @brief   Replaces every wire, for example while wires follow a dragged element.
 * @param[in] wires  New wires.
 * @note    Only the derived data is invalidated, and only when the wires differ: call MarkModified() once the
 *          gesture counts as a change.
 */
void Schematic::SetWires(std::vector<UIWire> wires) {
    // Drags call this every frame, so unchanged wires must not throw away the nodes and the results
    if (wires == m_Wires) {
        return;
    }
    m_Wires = std::move(wires);
    m_History.NoteChange();
    InvalidateDerivedData();
}

/**
 * @brief   Joins and deduplicates wire segments without changing the circuit.
 * @note    When something changes, wires are merged and removed, so the selected wires are deselected; the selected
 *          elements stay selected.
 */
void Schematic::SimplifyAllWires() {
    std::vector<UIWire> simplified = SimplifyWires(m_Wires, CollectTerminals());
    if (simplified == m_Wires) {
        return;
    }
    m_Wires = std::move(simplified);
    if (!m_Selection.Wires.empty()) {
        SetSelection({.Elements = m_Selection.Elements, .Wires = {}});
    }
    m_History.NoteChange();
    InvalidateDerivedData();
}

/**
 * @brief   Deletes every selected element and wire, if any.
 * @note    Wires attached to a deleted element stay in place, like in LTspice.
 */
void Schematic::DeleteSelection() {
    if (!HasSelection()) {
        return;
    }
    // Highest index first, so the indices still to erase stay valid
    for (auto index = m_Selection.Elements.rbegin(); index != m_Selection.Elements.rend(); ++index) {
        m_Elements.erase(m_Elements.begin() + static_cast<std::ptrdiff_t>(*index));
    }
    for (auto index = m_Selection.Wires.rbegin(); index != m_Selection.Wires.rend(); ++index) {
        m_Wires.erase(m_Wires.begin() + static_cast<std::ptrdiff_t>(*index));
    }
    ClearSelection();
    MarkModified();
}

/**
 * @brief   Gives a part another name, and keeps what refers to it by name on it.
 * @param[in] index  Index into GetElements() of a part that has a name.
 * @param[in] name   The new name, which CheckPartName() must allow.
 * @return  Nothing on success, the name unchanged too; the reason otherwise, with the schematic unchanged.
 * @note    The controlled sources that follow the part's current, the measured currents of the part and the math
 *          channels that read them follow the new name. The results go, since they name the part as it was.
 */
std::expected<void, std::string> Schematic::RenameElement(const std::size_t index, const std::string &name) {
    Core::Component &component = m_Elements[index]->GetComponent();
    const std::string old_name = component.GetName();
    if (name == old_name) {
        return {};
    }
    if (const std::optional<std::string> problem = CheckPartName(m_Elements, index, name)) {
        return std::unexpected(*problem);
    }
    component.SetName(name);
    RenameCurrentReferences(m_Elements, old_name, name);
    m_Measurements.RenamePart(old_name, name);
    MarkModified();
    return {};
}

/**
 * @brief   Empties the schematic, leaving it untitled and unmodified.
 */
void Schematic::Clear() {
    Replace({}, {}, {});
    m_FilePath.reset();
}

/**
 * @brief   Replaces the whole content, for example with a schematic read from a file.
 * @param[in] elements  New elements, with names already set.
 * @param[in] wires     New wires.
 * @param[in] settings     New simulation settings.
 * @param[in] subcircuits  New block definitions.
 * @param[in] models       New library models.
 * @note    The schematic becomes unmodified and the undo history starts over; the file path is kept.
 */
void Schematic::Replace(std::vector<std::unique_ptr<UIElement>> elements,
                        std::vector<UIWire> wires,
                        SimulationSettings settings,
                        std::vector<SavedSubcircuit> subcircuits,
                        std::vector<Core::LibraryModel> models) {
    ClearSelection();
    m_Elements = std::move(elements);
    m_Wires = std::move(wires);
    m_Blocks = BlockLibrary(std::move(subcircuits), std::move(models));
    m_SimulationSettings = std::move(settings);
    ++m_SimulationSettingsVersion;
    ++m_DocumentVersion;
    InvalidateDerivedData();
    // The probes belong to the old content; whoever replaces it sets them again
    m_Measurements.ClearVoltageProbes();
    m_Modified = false;
    ResetHistory();
}

/**
 * @brief   Lists the markers of the selection that would become the pins of a block made from it.
 * @return  The selected pin markers, in the order of the elements; the reason a block cannot be made
 *          otherwise: nothing selected, no marker, or markers that repeat a name or share a node. Blocks in the
 *          selection go into the new block.
 */
std::expected<std::vector<Schematic::SubcircuitPinCandidate>, std::string>
Schematic::GetSubcircuitPinCandidates() const {
    if (m_Selection.Elements.empty()) {
        return std::unexpected("Select the circuit to turn into a subcircuit");
    }
    auto candidates = CollectSubcircuitPinMarkers(m_Elements, m_Wires, &m_Selection.Elements);
    if (candidates && candidates->empty()) {
        return std::unexpected("Select pin markers with the circuit to choose the block connections");
    }
    return candidates;
}

/**
 * @brief   Lists every pin marker, as the contents of a block being edited have them.
 * @return  The markers, in the order of the elements; the reason they cannot be pins when names repeat or markers share
 *          a node.
 */
std::expected<std::vector<Schematic::SubcircuitPinCandidate>, std::string> Schematic::GetSubcircuitPinMarkers() const {
    return CollectSubcircuitPinMarkers(m_Elements, m_Wires, nullptr);
}

/**
 * @brief   Turns the pin marker on a point so its pin goes on an edge of the block.
 * @param[in] point  Point of the marker's terminal.
 * @param[in] side   Edge of the block the pin should go on.
 * @return  True when a marker turned; false when no marker is on the point or it already gives that edge.
 * @note    The marker turns about its terminal, so the wires on it stay where they are; only its arrow and square face
 *          the other way. It counts as an edit, which CommitUndoStep() makes one undo step.
 */
bool Schematic::SetPinMarkerSide(const GridPoint point, const Core::PinSide side) {
    UIElement *marker = FindPinMarker(m_Elements, point);
    if (marker == nullptr || GetMarkerSide(*marker) == side) {
        return false;
    }
    marker->SetRotation(GetSideRotation(side));
    marker->SetMirrored(false);
    MarkModified();
    return true;
}

/**
 * @brief   Turns the selection into a block definition and puts one instance of it in its place.
 * @param[in] name        Name of the new definition, an identifier no other definition has.
 * @param[in] pins        One pin per selected marker, at the marker's point, in the order of the instance's terminals.
 * @param[in] parameters  Parameters of the definition, with their defaults.
 * @return  Nothing on success; the reason otherwise, with the schematic unchanged.
 * @note    The markers take the names of their pins. The instance sits at the middle of the selection, wired to the
 *          points where the markers were, and ends up selected. Call CommitUndoStep() after it, so it undoes in one
 * step.
 */
std::expected<void, std::string>
Schematic::CreateSubcircuitFromSelection(std::string name,
                                         std::vector<SavedSubcircuitPin> pins,
                                         std::vector<Core::SubcircuitParameter> parameters) {
    if (auto valid = CheckNewBlock(name, pins, parameters); !valid) {
        return valid;
    }
    SavedSubcircuit made;
    made.Name = std::move(name);
    made.Pins = std::move(pins);
    made.Parameters = std::move(parameters);
    auto drawn = DrawDefinition(m_Elements, m_Wires, m_Selection, m_Blocks.GetSubcircuits(), std::move(made));
    if (!drawn) {
        return std::unexpected(drawn.error());
    }
    SavedSubcircuit &definition = *drawn;

    const GridPoint pivot = FindPivot(m_Elements, m_Wires, m_Selection);
    auto instance_element = CreateElement(Core::ComponentType::SUBCIRCUIT, pivot, Rotation::R0);
    ApplyDefinitionPins(static_cast<Core::SubcircuitInstance &>(instance_element->GetComponent()), definition);
    std::vector<GridPoint> old_points;
    for (const SavedSubcircuitPin &pin : definition.Pins) {
        old_points.push_back(pin.Point);
    }

    DeleteSelection();
    m_Blocks.AddSubcircuit(std::move(definition));
    PlaceBlockInstance(std::move(instance_element), old_points);
    return {};
}

/**
 * @brief   Copies the contents of a block definition into a schematic of their own, to edit them.
 * @param[in] name  Name of the definition.
 * @return  The contents, with a PIN marker added at any pin that lost its marker, and a copy of the other blocks and
 * the library models, which its parts may use; the reason otherwise.
 * @note    The copy shares nothing with this schematic; UpdateSubcircuit() puts the edited contents back.
 */
std::expected<std::unique_ptr<Schematic>, std::string> Schematic::OpenSubcircuit(const std::string &name) const {
    auto copy = m_Blocks.CopyForEditing(name);
    if (!copy) {
        return std::unexpected(copy.error());
    }
    auto content = std::make_unique<Schematic>();
    // The parts inside can use the blocks and library models of the document, as its own parts can; the block itself
    // is left out, so it cannot be placed inside itself
    content->Replace(std::move(copy->Definition.Elements),
                     std::move(copy->Definition.Wires),
                     {},
                     std::move(copy->OtherDefinitions),
                     m_Blocks.GetLibraryModels());
    AddMissingPinMarkers(*content, copy->Definition.Pins);
    return content;
}

/**
 * @brief   Puts the edited contents of a block back into its definition and brings every instance up to date.
 * @param[in] name        Name of the definition.
 * @param[in] content     Edited contents, as OpenSubcircuit() handed them out.
 * @param[in] pins        Pins of the block, each at the point of its marker in the contents, in terminal order.
 * @param[in] parameters  Parameters of the block, with their defaults.
 * @return  Nothing on success; the reason otherwise, with the schematic unchanged.
 * @note    A pin keeps the wires of its instances when its marker stays where it was, even renamed, or keeps its name;
 *          the wires on the terminals of the other old pins are removed, and the overrides of removed parameters
 *          are dropped, in the schematic and inside the other blocks alike. A block that would contain itself,
 *          directly or through another block, is refused. Call CommitUndoStep() after it, so it undoes in one step.
 */
std::expected<void, std::string> Schematic::UpdateSubcircuit(const std::string &name,
                                                             const Schematic &content,
                                                             const std::vector<SavedSubcircuitPin> &pins,
                                                             const std::vector<Core::SubcircuitParameter> &parameters) {
    auto change = m_Blocks.UpdateDrawn(name, content.GetElements(), content.GetWires(), pins, parameters);
    if (!change) {
        return std::unexpected(change.error());
    }
    FollowDefinitionChange(*change);
    MarkModified();
    return {};
}

/**
 * @brief   Changes the pins and parameters of a block kept as SPICE text, and brings every instance up to date.
 * @param[in] name        Name of the definition.
 * @param[in] pins        Its pins, one per port of its .subckt line and in that order, with their names and edges.
 * @param[in] parameters  Its parameters, with their defaults.
 * @return  Nothing on success; the reason otherwise, with the schematic unchanged.
 * @note    The text stays as it came. Each pin keeps the wires of its instances wherever its edge moves it. Call
 *          CommitUndoStep() after it, so it undoes in one step.
 */
std::expected<void, std::string>
Schematic::UpdateSpiceSubcircuit(const std::string &name,
                                 const std::vector<SavedSubcircuitPin> &pins,
                                 const std::vector<Core::SubcircuitParameter> &parameters) {
    auto change = m_Blocks.UpdateSpice(name, pins, parameters);
    if (!change) {
        return std::unexpected(change.error());
    }
    FollowDefinitionChange(*change);
    MarkModified();
    return {};
}

/**
 * @brief   Adds the definitions a pasted group brought, so its instances have them.
 * @param[in]     definitions  Definitions the group's instances use, with the blocks those place.
 * @param[in,out] instances    Elements of the group; the instances of a renamed definition are pointed at its new name.
 * @return  The name each definition has here, in the order they came: its own, or the free name it was given.
 * @note    A definition equal to one of the same name already here is dropped, since the instances can use that one.
 *          One that differs is added under a free name, such as "block_2", so neither replaces the other; the blocks
 *          that place it are pointed at that name too. Each block is merged after the blocks it places, so it is
 *          compared with their names as they end up here.
 */
std::vector<std::string> Schematic::MergeSubcircuits(std::vector<SavedSubcircuit> definitions,
                                                     std::vector<std::unique_ptr<UIElement>> &instances) {
    MergeResult merged = m_Blocks.MergeSubcircuits(std::move(definitions), instances);
    if (merged.IsAnyAdded) {
        MarkModified();
    }
    return std::move(merged.Names);
}

/**
 * @brief   Adds definitions that come without parts of their own, such as the blocks of a SPICE library.
 * @param[in] definitions  Definitions to add, with the blocks those place.
 * @return  The name each definition has here, in the order they came, as MergeSubcircuits() with instances gives.
 */
std::vector<std::string> Schematic::MergeSubcircuits(std::vector<SavedSubcircuit> definitions) {
    std::vector<std::unique_ptr<UIElement>> no_instances;
    return MergeSubcircuits(std::move(definitions), no_instances);
}

/**
 * @brief   Adds device models from a library or a paste, so the parts that name them have them.
 * @param[in]     models       Models to add.
 * @param[in,out] elements     Parts that came with them, pointed at the new name of a renamed model.
 * @param[in,out] definitions  Blocks that came with them, whose parts are pointed at it too.
 * @return  The name each model has here, in order: its own, or the free name it was given.
 * @note    A model equal to one of the same name already here is dropped, since the parts can use that one; one that
 *          differs is added under a free name, such as "1N4148_2", so neither replaces the other.
 */
std::vector<std::string> Schematic::MergeLibraryModels(std::vector<Core::LibraryModel> models,
                                                       std::vector<std::unique_ptr<UIElement>> &elements,
                                                       std::vector<SavedSubcircuit> &definitions) {
    MergeResult merged = m_Blocks.MergeLibraryModels(std::move(models), elements, definitions);
    if (merged.IsAnyAdded) {
        MarkModified();
    }
    return std::move(merged.Names);
}

/**
 * @brief   Adds device models that come without parts or blocks of their own, such as those of a SPICE library.
 * @param[in] models  Models to add.
 * @return  The name each model has here, in order, as MergeLibraryModels() with parts gives.
 */
std::vector<std::string> Schematic::MergeLibraryModels(std::vector<Core::LibraryModel> models) {
    std::vector<std::unique_ptr<UIElement>> no_parts;
    std::vector<SavedSubcircuit> no_blocks;
    return MergeLibraryModels(std::move(models), no_parts, no_blocks);
}

/**
 * @brief   Takes a copy of the blocks and library models of another schematic, as the contents of an open block need
 *          those of the document after it gained some.
 * @param[in] library  The schematic whose blocks and models are copied.
 * @param[in] left_out  A block to leave out, such as the one these are the contents of, or empty.
 * @note    The parts here are left as they are, so call it only with a library that still has every block they place.
 */
void Schematic::CopyDefinitionsFrom(const Schematic &library, const std::string_view left_out) {
    m_Blocks.CopyFrom(library.m_Blocks, left_out);
}

/**
 * @brief   Selects an element, replacing the previous selection.
 * @param[in] index  Index into GetElements().
 */
void Schematic::SelectElement(const std::size_t index) {
    SetSelection({.Elements = {index}, .Wires = {}});
}

/**
 * @brief   Selects a wire, replacing the previous selection.
 * @param[in] index  Index into GetWires().
 */
void Schematic::SelectWire(const std::size_t index) {
    SetSelection({.Elements = {}, .Wires = {index}});
}

/**
 * @brief   Replaces the selection.
 * @param[in] selection  Indices into GetElements() and GetWires().
 */
void Schematic::SetSelection(Selection selection) {
    m_Selection = std::move(selection);
    ++m_SelectionVersion;
}

/**
 * @brief   Deselects everything.
 */
void Schematic::ClearSelection() {
    SetSelection({});
}

/**
 * @brief   Tells whether an element is selected.
 * @param[in] index  Index into GetElements().
 * @return  True when the element is in the selection.
 */
bool Schematic::IsElementSelected(const std::size_t index) const {
    return m_Selection.Elements.contains(index);
}

/**
 * @brief   Tells whether a wire is selected.
 * @param[in] index  Index into GetWires().
 * @return  True when the wire is in the selection.
 */
bool Schematic::IsWireSelected(const std::size_t index) const {
    return m_Selection.Wires.contains(index);
}

/**
 * @brief   Returns what is selected.
 * @return  Indices into GetElements() and GetWires(), in order.
 */
const Selection &Schematic::GetSelection() const {
    return m_Selection;
}

/**
 * @brief   Tells whether anything is selected.
 * @return  True when at least one element or wire is selected.
 */
bool Schematic::HasSelection() const {
    return !m_Selection.Empty();
}

/**
 * @brief   Returns the selected element when it is the only thing selected.
 * @return  The element, or nullptr when there is no selection, a wire is selected too, or there are several elements.
 * @note    Editing a part, such as its value, applies to one part at a time, so this is what those edits ask for.
 */
UIElement *Schematic::GetSelectedElement() {
    if (m_Selection.Elements.size() != 1 || !m_Selection.Wires.empty()) {
        return nullptr;
    }
    return m_Elements[*m_Selection.Elements.begin()].get();
}

/**
 * @brief   Returns a number that changes every time the selection changes.
 * @return  The selection version; compare it with a stored one to know whether to reload selection data.
 */
std::size_t Schematic::GetSelectionVersion() const {
    return m_SelectionVersion;
}

/**
 * @brief   Returns the traces the plots show.
 * @return  The measurements; set a voltage through SetVoltageMeasured(), which knows the nodes.
 */
Measurements &Schematic::GetMeasurements() {
    return m_Measurements;
}

/**
 * @brief   Returns the traces the plots show, to read them.
 * @return  The measurements.
 */
const Measurements &Schematic::GetMeasurements() const {
    return m_Measurements;
}

/**
 * @brief   Adds or removes the voltage of a node from the plots.
 * @param[in] node      Node number, as numbered by GetConnectivity().
 * @param[in] measured  Whether to show it.
 */
void Schematic::SetVoltageMeasured(const int node, const bool measured) {
    m_Measurements.SetVoltageMeasured(node, measured, GetConnectivity());
}

/**
 * @brief   Describes the measurements for a file, with each voltage as a point of its node.
 * @return  The measured voltages and currents, the math channels and the buses; nodes that no longer exist are left
 *          out.
 */
SavedMeasurements Schematic::SaveMeasurements() {
    return m_Measurements.Save(GetConnectivity());
}

/**
 * @brief   Replaces the measurements with those read from a file.
 * @param[in] measurements  Measured voltages and currents, math channels and buses; call it after the content they
 *                          refer to is in place.
 * @note    Points that connect to nothing, or to ground, are skipped. Currents of parts that do not exist are kept
 *          but plot nothing, like after deleting a measured part.
 */
void Schematic::RestoreMeasurements(const SavedMeasurements &measurements) {
    m_Measurements.Restore(measurements, GetConnectivity());
}

/**
 * @brief   Returns how each analysis runs.
 * @return  The settings, saved with the schematic.
 */
const SimulationSettings &Schematic::GetSimulationSettings() const {
    return m_SimulationSettings;
}

/**
 * @brief   Changes how the analyses run, as the user edits the settings.
 * @param[in] settings  New settings; when they equal the current ones, nothing changes.
 * @note    A change counts as unsaved and is undone like an edit, but keeps the results and the version.
 */
void Schematic::SetSimulationSettings(const SimulationSettings &settings) {
    if (settings == m_SimulationSettings) {
        return;
    }
    m_SimulationSettings = settings;
    if (m_Circuit) {
        m_Circuit->SetSolverOptions(m_SimulationSettings.Solver);
    }
    m_Modified = true;
    m_History.NoteChange();
}

/**
 * @brief   Returns a number that changes whenever the settings are replaced by something other than
 *          SetSimulationSettings(), such as opening a file, starting a new schematic, undoing or redoing.
 * @return  The settings version; compare it with a stored one to know whether to reload the settings.
 */
std::size_t Schematic::GetSimulationSettingsVersion() const {
    return m_SimulationSettingsVersion;
}

/**
 * @brief   Tells whether there are unsaved changes.
 * @return  True after any change since the last save, open or clear.
 */
bool Schematic::IsModified() const {
    return m_Modified;
}

/**
 * @brief   Records that the schematic changed, so it counts as unsaved and its nodes are recomputed.
 * @note    Call it after editing an element obtained from GetElement() or GetSelectedElement().
 */
void Schematic::MarkModified() {
    m_Modified = true;
    m_History.NoteChange();
    InvalidateDerivedData();
}

/**
 * @brief   Returns the file the schematic was last opened from or saved to.
 * @return  The path, or no value for an untitled schematic.
 */
const std::optional<std::filesystem::path> &Schematic::GetFilePath() const {
    return m_FilePath;
}

/**
 * @brief   Records that the schematic was written to a file, or opened from one.
 * @param[in] path  The file.
 * @note    The schematic becomes unmodified, and undoing or redoing back to this state keeps it unmodified.
 */
void Schematic::MarkSaved(std::filesystem::path path) {
    CommitUndoStep();
    m_FilePath = std::move(path);
    m_Modified = false;
    m_History.MarkSaved();
}

/**
 * @brief   Records whether a live transient runs, so the parts the user sets by hand change in it.
 * @param[in] is_live  True from the start of a live run to its end.
 * @note    The changes the run has not taken are dropped when it ends.
 */
void Schematic::SetLiveRun(const bool is_live) {
    m_IsLiveRun = is_live;
    if (!is_live) {
        m_LiveChanges.clear();
    }
}

/**
 * @brief   Tells whether a live transient runs.
 * @return  True between SetLiveRun(true) and SetLiveRun(false).
 */
bool Schematic::IsLiveRun() const {
    return m_IsLiveRun;
}

/**
 * @brief   Records that the user flipped a switch, pressed or released a button, set a logic input or turned a
 *          potentiometer.
 * @param[in] component  The part, already in its new state.
 * @note    It is a change like any other, saved and undone, but while a live run goes on it keeps the transient and
 *          the circuit version, and queues the change for the run (TakeLiveChanges()); the other results no longer
 *          match the circuit and are dropped. Without a live run, or for any other part, it is MarkModified().
 */
void Schematic::MarkLiveChanged(const Core::Component &component) {
    if (!m_IsLiveRun || !Core::IsLiveChangeable(component.GetType())) {
        MarkModified();
        return;
    }
    m_Modified = true;
    m_History.NoteChange();
    std::ranges::move(Core::DescribeLiveChange(component), std::back_inserter(m_LiveChanges));
    if (m_Listener.Target != nullptr) {
        m_Listener.Target->OnLiveChanged();
    }
}

/**
 * @brief   Takes the changes the live run should apply.
 * @return  The changes recorded by MarkLiveChanged() since the last call, in order.
 */
std::vector<Core::LiveChange> Schematic::TakeLiveChanges() {
    return std::exchange(m_LiveChanges, {});
}

/**
 * @brief   Chooses who hears of the changes that make the results of a simulation stale.
 * @param[in] listener  Told at each change, from then on; or null for nobody. Not owned, and it must outlive the
 *                      schematic or be removed first.
 * @note    Replacing the content, undoing and every other change leave the listener as it is.
 */
void Schematic::SetListener(CircuitListener *listener) {
    m_Listener.Target = listener;
}

/**
 * @brief   Turns the changes made since the last call into one undo step.
 * @note    Call it once the user finishes an interaction, such as at the end of a frame with no item active, so
 *          a whole drag counts as one step. Changes that leave the schematic as it was add no step. A new step
 *          clears what could be redone.
 */
void Schematic::CommitUndoStep() {
    if (m_History.HasPendingChange()) {
        m_History.Commit(TakeSnapshot());
    }
}

/**
 * @brief   Tells whether there is a step to undo.
 * @return  True when Undo() would change something.
 */
bool Schematic::CanUndo() const {
    return m_History.CanUndo();
}

/**
 * @brief   Tells whether there is an undone step to redo.
 * @return  True when Redo() would change something.
 */
bool Schematic::CanRedo() const {
    return m_History.CanRedo();
}

/**
 * @brief   Goes back to the schematic as it was before the last step.
 * @note    Pending changes are committed first, so they are what gets undone. The selection is cleared.
 */
void Schematic::Undo() {
    CommitUndoStep();
    if (const std::optional<std::string> snapshot = m_History.Undo()) {
        RestoreSnapshot(*snapshot);
    }
}

/**
 * @brief   Applies again the last step that was undone.
 * @note    The selection is cleared.
 */
void Schematic::Redo() {
    if (const std::optional<std::string> snapshot = m_History.Redo()) {
        RestoreSnapshot(*snapshot);
    }
}

/**
 * @brief   Returns the nodes and junctions of the schematic.
 * @return  The connectivity, computed on first use after a change and cached until the next one.
 */
const Connectivity &Schematic::GetConnectivity() {
    if (!m_Connectivity) {
        const Connectivity &connectivity = m_Connectivity.emplace(m_Elements, m_Wires);
        m_Measurements.FollowNodes(connectivity);
        return connectivity;
    }
    return *m_Connectivity;
}

/**
 * @brief   Returns the circuit topology of the schematic, with the solver options of its settings.
 * @return  The circuit, built on first use after a change and cached until the next one.
 */
const Core::Circuit &Schematic::GetCircuit() {
    if (!m_Circuit) {
        Core::Circuit circuit = GUI::BuildCircuit(m_Elements, GetConnectivity());
        for (const auto &definition : m_Blocks.GetBuiltDefinitions()) {
            circuit.AddSubcircuit(definition);
        }
        for (const Core::LibraryModel &model : m_Blocks.GetLibraryModels()) {
            circuit.AddLibraryModel(model);
        }
        circuit.SetSolverOptions(m_SimulationSettings.Solver);
        m_Circuit = std::move(circuit);
    }
    return *m_Circuit;
}

/**
 * @brief   Builds the circuit topology of the schematic, for the simulator, with the solver options of its settings.
 * @return  The circuit; it points at the components of this schematic, so use it before the next change.
 */
Core::Circuit Schematic::BuildCircuit() {
    return GetCircuit();
}

/**
 * @brief   Returns a number that changes whenever the circuit does.
 * @return  A count of the edits that drop the results; it differs from an earlier value once the circuit was edited.
 */
std::size_t Schematic::GetCircuitVersion() const {
    return m_CircuitVersion;
}

/**
 * @brief   Returns a number that changes whenever the whole content is replaced, as by opening a file, an example or a
 *          new schematic.
 * @return  A count of the replacements; edits, undo and redo leave it alone.
 */
std::size_t Schematic::GetDocumentVersion() const {
    return m_DocumentVersion;
}

/**
 * @brief   Builds the SPICE netlist of the schematic.
 * @return  The netlist text, without analysis commands.
 */
std::string Schematic::BuildSpiceNetlist() {
    return GetCircuit().ToSpiceNetlist();
}

/**
 * @brief   Moves a slot into a new one, which hears nobody: the listener stays with the schematic it was set on.
 */
Schematic::ListenerSlot::ListenerSlot(ListenerSlot &&) noexcept {
}

void Schematic::InvalidateDerivedData() {
    ++m_CircuitVersion;
    m_Connectivity.reset();
    m_Circuit.reset();
    if (m_Listener.Target != nullptr) {
        m_Listener.Target->OnCircuitChanged();
    }
}

/**
 * @brief   Brings the instances of a definition that changed up to date in the schematic, as the library did inside
 *          its blocks.
 */
void Schematic::FollowDefinitionChange(const DefinitionChange &change) {
    RefreshInstancesIn(m_Elements, m_Wires, *change.Definition, change.OldNames, change.Matches);
}

/**
 * @brief   Checks that a block can be made from the selection with these settings, before anything changes.
 * @return  Nothing when the name is free, every selected marker has one pin and the names are unique identifiers;
 *          the reason otherwise.
 */
std::expected<void, std::string>
Schematic::CheckNewBlock(const std::string &name,
                         const std::vector<SavedSubcircuitPin> &pins,
                         const std::vector<Core::SubcircuitParameter> &parameters) const {
    if (auto valid = m_Blocks.CheckNewName(name); !valid) {
        return valid;
    }
    const auto candidates = GetSubcircuitPinCandidates();
    if (!candidates) {
        return std::unexpected(candidates.error());
    }
    if (auto valid = CheckPinChoice(*candidates, pins); !valid) {
        return valid;
    }
    return ValidateMetadata(pins, parameters);
}

/**
 * @brief   Puts the instance of a block just made where it has room, wires it to the points where its pin markers
 *          were, and selects it.
 */
void Schematic::PlaceBlockInstance(std::unique_ptr<UIElement> instance, const std::vector<GridPoint> &old_points) {
    auto &block = static_cast<UISubcircuit &>(*instance);
    MoveToFreeSpot(block, old_points, m_Elements, m_Wires);
    AddElement(std::move(instance));
    WireNewBlock(block, old_points);
    SelectElement(m_Elements.size() - 1);
}

/**
 * @brief   Wires the terminals of a block just placed to the points where its pin markers were.
 * @details Each wire goes around the box from the edge its pin is on, clear of the other wires and terminals; one
 *          that finds no route bends once instead, as it can always do.
 */
void Schematic::WireNewBlock(const UISubcircuit &block, const std::vector<GridPoint> &old_points) {
    const std::vector<GridPoint> new_points = block.GetTerminals();
    std::vector<WireLink> links;
    for (std::size_t index = 0; index < new_points.size(); ++index) {
        links.push_back({.From = old_points[index], .To = new_points[index]});
    }
    RoutingObstacles obstacles;
    KeepOffBlock(obstacles, block);
    for (const UIWire &wire : RouteLinks(m_Elements, m_Wires, obstacles, links)) {
        AddWire(wire.GetStart(), wire.GetEnd());
    }
}

/**
 * @brief   Measurements stay out of snapshots, so probing adds no undo step and does not count as a change.
 */
std::string Schematic::TakeSnapshot() const {
    return SaveSchematic(m_Elements,
                         m_Wires,
                         m_SimulationSettings,
                         {},
                         m_Blocks.GetSubcircuits(),
                         m_Blocks.GetLibraryModels());
}

/**
 * @brief   Snapshots come from TakeSnapshot(), so they always load back without warnings.
 */
void Schematic::RestoreSnapshot(const std::string &snapshot) {
    auto loaded = LoadSchematic(snapshot);
    if (!loaded) {
        return;
    }
    ClearSelection();
    m_Elements = std::move(loaded->Elements);
    m_Wires = std::move(loaded->Wires);
    m_Blocks = BlockLibrary(std::move(loaded->Subcircuits), std::move(loaded->Models));
    if (loaded->Settings != m_SimulationSettings) {
        m_SimulationSettings = std::move(loaded->Settings);
        ++m_SimulationSettingsVersion;
    }
    m_Modified = !m_History.IsAtSavedState();
    InvalidateDerivedData();
}

/**
 * @brief   The current content becomes the only state, and also the saved one.
 */
void Schematic::ResetHistory() {
    m_History.Reset(TakeSnapshot());
}

} // namespace GUI
