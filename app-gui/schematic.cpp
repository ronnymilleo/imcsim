/**
 * @file    schematic.cpp
 * @brief   The schematic document shared by every window: elements, wires, selection and file state.
 */

#include "schematic.h"

#include "components/component.h"
#include "schematic_file.h"
#include "wire_editing.h"
#include <algorithm>
#include <iterator>
#include <string_view>
#include <utility>

namespace GUI {

namespace {

// Older steps are dropped beyond this, so a long session does not keep growing
constexpr std::size_t MaxUndoSteps = 100;

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
 * @brief   Checks whether a grid point is the terminal of some element.
 * @param[in] point  Grid point to test.
 * @return  True when an element has a terminal there.
 */
bool Schematic::IsTerminal(const GridPoint point) const {
    return std::ranges::any_of(
        m_Elements, [point](const auto &element) { return std::ranges::contains(element->GetTerminals(), point); });
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
    m_ChangedSinceCommit = true;
    InvalidateDerivedData();
}

/**
 * @brief   Joins and deduplicates wire segments without changing the circuit.
 * @note    When something changes, wires are merged and removed, so a selected wire is deselected.
 */
void Schematic::SimplifyAllWires() {
    std::vector<UIWire> simplified = SimplifyWires(m_Wires, CollectTerminals());
    if (simplified == m_Wires) {
        return;
    }
    m_Wires = std::move(simplified);
    if (m_SelectedWire) {
        ClearSelection();
    }
    m_ChangedSinceCommit = true;
    InvalidateDerivedData();
}

/**
 * @brief   Deletes the selected element or wire, if any.
 * @note    Wires attached to a deleted element stay in place, like in LTspice.
 */
void Schematic::DeleteSelection() {
    if (m_SelectedElement) {
        m_Elements.erase(m_Elements.begin() + static_cast<std::ptrdiff_t>(*m_SelectedElement));
    } else if (m_SelectedWire) {
        m_Wires.erase(m_Wires.begin() + static_cast<std::ptrdiff_t>(*m_SelectedWire));
    } else {
        return;
    }
    ClearSelection();
    MarkModified();
}

/**
 * @brief   Empties the schematic, leaving it untitled and unmodified.
 */
void Schematic::Clear() {
    Replace({}, {});
    m_FilePath.reset();
}

/**
 * @brief   Replaces the whole content, for example with a schematic read from a file.
 * @param[in] elements  New elements, with names already set.
 * @param[in] wires     New wires.
 * @note    The schematic becomes unmodified and the undo history starts over; the file path is kept.
 */
void Schematic::Replace(std::vector<std::unique_ptr<UIElement>> elements, std::vector<UIWire> wires) {
    ClearSelection();
    m_Elements = std::move(elements);
    m_Wires = std::move(wires);
    InvalidateDerivedData();
    m_Modified = false;
    ResetHistory();
}

/**
 * @brief   Selects an element, replacing the previous selection.
 * @param[in] index  Index into GetElements().
 */
void Schematic::SelectElement(const std::size_t index) {
    ClearSelection();
    m_SelectedElement = index;
}

/**
 * @brief   Selects a wire, replacing the previous selection.
 * @param[in] index  Index into GetWires().
 */
void Schematic::SelectWire(const std::size_t index) {
    ClearSelection();
    m_SelectedWire = index;
}

/**
 * @brief   Deselects everything.
 */
void Schematic::ClearSelection() {
    m_SelectedElement.reset();
    m_SelectedWire.reset();
    ++m_SelectionVersion;
}

/**
 * @brief   Returns the index of the selected element.
 * @return  The index into GetElements(), or no value when no element is selected.
 */
std::optional<std::size_t> Schematic::GetSelectedElementIndex() const {
    return m_SelectedElement;
}

/**
 * @brief   Returns the index of the selected wire.
 * @return  The index into GetWires(), or no value when no wire is selected.
 */
std::optional<std::size_t> Schematic::GetSelectedWireIndex() const {
    return m_SelectedWire;
}

/**
 * @brief   Returns the selected element.
 * @return  The element, or nullptr when no element is selected.
 */
UIElement *Schematic::GetSelectedElement() {
    return m_SelectedElement ? m_Elements[*m_SelectedElement].get() : nullptr;
}

/**
 * @brief   Returns a number that changes every time the selection changes.
 * @return  The selection version; compare it with a stored one to know whether to reload selection data.
 */
std::size_t Schematic::GetSelectionVersion() const {
    return m_SelectionVersion;
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
    m_ChangedSinceCommit = true;
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
    m_SavedSnapshot = m_CurrentSnapshot;
}

/**
 * @brief   Turns the changes made since the last call into one undo step.
 * @note    Call it once the user finishes an interaction, such as at the end of a frame with no item active, so
 *          a whole drag counts as one step. Changes that leave the schematic as it was add no step. A new step
 *          clears what could be redone.
 */
void Schematic::CommitUndoStep() {
    if (!std::exchange(m_ChangedSinceCommit, false)) {
        return;
    }
    std::string snapshot = TakeSnapshot();
    if (snapshot == m_CurrentSnapshot) {
        return;
    }
    m_UndoSnapshots.push_back(std::move(m_CurrentSnapshot));
    if (m_UndoSnapshots.size() > MaxUndoSteps) {
        m_UndoSnapshots.erase(m_UndoSnapshots.begin());
    }
    m_CurrentSnapshot = std::move(snapshot);
    m_RedoSnapshots.clear();
}

/**
 * @brief   Tells whether there is a step to undo.
 * @return  True when Undo() would change something.
 */
bool Schematic::CanUndo() const {
    return !m_UndoSnapshots.empty() || m_ChangedSinceCommit;
}

/**
 * @brief   Tells whether there is an undone step to redo.
 * @return  True when Redo() would change something.
 */
bool Schematic::CanRedo() const {
    return !m_RedoSnapshots.empty();
}

/**
 * @brief   Goes back to the schematic as it was before the last step.
 * @note    Pending changes are committed first, so they are what gets undone. The selection is cleared.
 */
void Schematic::Undo() {
    CommitUndoStep();
    if (m_UndoSnapshots.empty()) {
        return;
    }
    m_RedoSnapshots.push_back(std::move(m_CurrentSnapshot));
    m_CurrentSnapshot = std::move(m_UndoSnapshots.back());
    m_UndoSnapshots.pop_back();
    RestoreSnapshot(m_CurrentSnapshot);
}

/**
 * @brief   Applies again the last step that was undone.
 * @note    The selection is cleared.
 */
void Schematic::Redo() {
    if (m_RedoSnapshots.empty()) {
        return;
    }
    m_UndoSnapshots.push_back(std::move(m_CurrentSnapshot));
    m_CurrentSnapshot = std::move(m_RedoSnapshots.back());
    m_RedoSnapshots.pop_back();
    RestoreSnapshot(m_CurrentSnapshot);
}

/**
 * @brief   Returns the nodes and junctions of the schematic.
 * @return  The connectivity, computed on first use after a change and cached until the next one.
 */
const Connectivity &Schematic::GetConnectivity() {
    if (!m_Connectivity) {
        m_Connectivity.emplace(m_Elements, m_Wires);
    }
    return *m_Connectivity;
}

/**
 * @brief   Builds the circuit topology of the schematic, for the simulator.
 * @return  The circuit; it points at the components of this schematic, so use it before the next change.
 */
Core::Circuit Schematic::BuildCircuit() {
    return GUI::BuildCircuit(m_Elements, GetConnectivity());
}

/**
 * @brief   Builds the SPICE netlist of the schematic.
 * @return  The netlist text, without analysis commands.
 */
std::string Schematic::BuildSpiceNetlist() {
    return BuildCircuit().ToSpiceNetlist();
}

/**
 * @brief   Stores the result of an operating point simulation of the current schematic.
 * @param[in] operating_point  Node voltages, numbered like GetConnectivity(), or no value to clear them.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void Schematic::SetOperatingPoint(std::optional<Core::OperatingPoint> operating_point) {
    m_OperatingPoint = std::move(operating_point);
}

/**
 * @brief   Returns the last operating point simulated for the current schematic.
 * @return  The node voltages, or no value when there was no simulation since the last change.
 */
const std::optional<Core::OperatingPoint> &Schematic::GetOperatingPoint() const {
    return m_OperatingPoint;
}

/**
 * @brief   Stores the result of a transient simulation of the current schematic.
 * @param[in] transient  Node voltages over time, numbered like GetConnectivity(), or no value to clear them.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void Schematic::SetTransient(std::optional<Core::Transient> transient) {
    m_Transient = std::move(transient);
    ++m_TransientVersion;
}

/**
 * @brief   Returns the last transient simulated for the current schematic.
 * @return  The node voltages over time, or no value when there was no simulation since the last change.
 */
const std::optional<Core::Transient> &Schematic::GetTransient() const {
    return m_Transient;
}

/**
 * @brief   Stores the result of an AC sweep of the current schematic.
 * @param[in] sweep  Node responses across frequency, numbered like GetConnectivity(), or no value to clear them.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void Schematic::SetACSweep(std::optional<Core::ACSweep> sweep) {
    m_ACSweep = std::move(sweep);
    ++m_ACSweepVersion;
}

/**
 * @brief   Returns the last AC sweep simulated for the current schematic.
 * @return  The node responses, or no value when there was no simulation since the last change.
 */
const std::optional<Core::ACSweep> &Schematic::GetACSweep() const {
    return m_ACSweep;
}

/**
 * @brief   Stores the result of a DC sweep of the current schematic.
 * @param[in] sweep  Node voltages and currents along the sweep, numbered like GetConnectivity(), or no value to
 *                   clear them.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void Schematic::SetDCSweep(std::optional<Core::DCSweep> sweep) {
    m_DCSweep = std::move(sweep);
    ++m_DCSweepVersion;
}

/**
 * @brief   Returns the last DC sweep simulated for the current schematic.
 * @return  The curves of the sweep, or no value when there was no simulation since the last change.
 */
const std::optional<Core::DCSweep> &Schematic::GetDCSweep() const {
    return m_DCSweep;
}

/**
 * @brief   Returns a number that changes whenever a transient result is stored.
 * @return  The transient version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t Schematic::GetTransientVersion() const {
    return m_TransientVersion;
}

/**
 * @brief   Returns a number that changes whenever an AC sweep result is stored.
 * @return  The AC sweep version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t Schematic::GetACSweepVersion() const {
    return m_ACSweepVersion;
}

/**
 * @brief   Returns a number that changes whenever a DC sweep result is stored.
 * @return  The DC sweep version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t Schematic::GetDCSweepVersion() const {
    return m_DCSweepVersion;
}

void Schematic::InvalidateDerivedData() {
    m_Connectivity.reset();
    m_OperatingPoint.reset();
    m_Transient.reset();
    m_ACSweep.reset();
    m_DCSweep.reset();
}

std::string Schematic::TakeSnapshot() const {
    return SaveSchematic(m_Elements, m_Wires);
}

// Snapshots come from TakeSnapshot(), so they always load back without warnings
void Schematic::RestoreSnapshot(const std::string &snapshot) {
    auto loaded = LoadSchematic(snapshot);
    if (!loaded) {
        return;
    }
    ClearSelection();
    m_Elements = std::move(loaded->Elements);
    m_Wires = std::move(loaded->Wires);
    m_ChangedSinceCommit = false;
    m_Modified = m_CurrentSnapshot != m_SavedSnapshot;
    InvalidateDerivedData();
}

// The current content becomes the only state, and also the saved one
void Schematic::ResetHistory() {
    m_CurrentSnapshot = TakeSnapshot();
    m_SavedSnapshot = m_CurrentSnapshot;
    m_UndoSnapshots.clear();
    m_RedoSnapshots.clear();
    m_ChangedSinceCommit = false;
}

} // namespace GUI
