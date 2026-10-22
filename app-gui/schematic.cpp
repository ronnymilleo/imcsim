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
#include <map>
#include <set>
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
 * @note    When something changes, wires are merged and removed, so the selected wires are deselected; the selected
 *          elements stay selected.
 */
void Schematic::SimplifyAllWires() {
    std::vector<UIWire> simplified = SimplifyWires(m_Wires, CollectTerminals());
    if (simplified == m_Wires) {
        return;
    }
    m_Wires = std::move(simplified);
    if (!m_SelectedWires.empty()) {
        SetSelection(m_SelectedElements, {});
    }
    m_ChangedSinceCommit = true;
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
    for (auto index = m_SelectedElements.rbegin(); index != m_SelectedElements.rend(); ++index) {
        m_Elements.erase(m_Elements.begin() + static_cast<std::ptrdiff_t>(*index));
    }
    for (auto index = m_SelectedWires.rbegin(); index != m_SelectedWires.rend(); ++index) {
        m_Wires.erase(m_Wires.begin() + static_cast<std::ptrdiff_t>(*index));
    }
    ClearSelection();
    MarkModified();
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
 * @param[in] settings  New simulation settings.
 * @note    The schematic becomes unmodified and the undo history starts over; the file path is kept.
 */
void Schematic::Replace(std::vector<std::unique_ptr<UIElement>> elements, std::vector<UIWire> wires,
                        SimulationSettings settings) {
    ClearSelection();
    m_Elements = std::move(elements);
    m_Wires = std::move(wires);
    m_SimulationSettings = std::move(settings);
    ++m_SimulationSettingsVersion;
    InvalidateDerivedData();
    // The probes belong to the old content; whoever replaces it sets them again
    m_VoltageProbes.clear();
    m_Modified = false;
    ResetHistory();
}

/**
 * @brief   Selects an element, replacing the previous selection.
 * @param[in] index  Index into GetElements().
 */
void Schematic::SelectElement(const std::size_t index) {
    SetSelection({index}, {});
}

/**
 * @brief   Selects a wire, replacing the previous selection.
 * @param[in] index  Index into GetWires().
 */
void Schematic::SelectWire(const std::size_t index) {
    SetSelection({}, {index});
}

/**
 * @brief   Replaces the selection.
 * @param[in] elements  Indices into GetElements().
 * @param[in] wires     Indices into GetWires().
 */
void Schematic::SetSelection(std::set<std::size_t> elements, std::set<std::size_t> wires) {
    m_SelectedElements = std::move(elements);
    m_SelectedWires = std::move(wires);
    ++m_SelectionVersion;
}

/**
 * @brief   Deselects everything.
 */
void Schematic::ClearSelection() {
    SetSelection({}, {});
}

/**
 * @brief   Tells whether an element is selected.
 * @param[in] index  Index into GetElements().
 * @return  True when the element is in the selection.
 */
bool Schematic::IsElementSelected(const std::size_t index) const {
    return m_SelectedElements.contains(index);
}

/**
 * @brief   Tells whether a wire is selected.
 * @param[in] index  Index into GetWires().
 * @return  True when the wire is in the selection.
 */
bool Schematic::IsWireSelected(const std::size_t index) const {
    return m_SelectedWires.contains(index);
}

/**
 * @brief   Returns the indices of the selected elements.
 * @return  Indices into GetElements(), in order.
 */
const std::set<std::size_t> &Schematic::GetSelectedElements() const {
    return m_SelectedElements;
}

/**
 * @brief   Returns the indices of the selected wires.
 * @return  Indices into GetWires(), in order.
 */
const std::set<std::size_t> &Schematic::GetSelectedWires() const {
    return m_SelectedWires;
}

/**
 * @brief   Tells whether anything is selected.
 * @return  True when at least one element or wire is selected.
 */
bool Schematic::HasSelection() const {
    return !m_SelectedElements.empty() || !m_SelectedWires.empty();
}

/**
 * @brief   Returns the selected element when it is the only thing selected.
 * @return  The element, or nullptr when there is no selection, a wire is selected too, or there are several elements.
 * @note    Editing a part, such as its value, applies to one part at a time, so this is what those edits ask for.
 */
UIElement *Schematic::GetSelectedElement() {
    if (m_SelectedElements.size() != 1 || !m_SelectedWires.empty()) {
        return nullptr;
    }
    return m_Elements[*m_SelectedElements.begin()].get();
}

/**
 * @brief   Returns a number that changes every time the selection changes.
 * @return  The selection version; compare it with a stored one to know whether to reload selection data.
 */
std::size_t Schematic::GetSelectionVersion() const {
    return m_SelectionVersion;
}

/**
 * @brief   Tells whether the plots show the voltage of a node.
 * @param[in] node  Node number, as numbered by GetConnectivity().
 * @return  True when the node is measured.
 */
bool Schematic::IsVoltageMeasured(const int node) const {
    return std::ranges::any_of(m_VoltageProbes, [node](const VoltageProbe &probe) { return probe.Node == node; });
}

/**
 * @brief   Adds or removes the voltage of a node from the plots.
 * @param[in] node      Node number, as numbered by GetConnectivity().
 * @param[in] measured  Whether to show it.
 */
void Schematic::SetVoltageMeasured(const int node, const bool measured) {
    if (!measured) {
        std::erase_if(m_VoltageProbes, [node](const VoltageProbe &probe) { return probe.Node == node; });
    } else if (!IsVoltageMeasured(node)) {
        m_VoltageProbes.push_back({.Points = GetConnectivity().GetPointsOfNode(node), .Node = node});
    }
}

/**
 * @brief   Tells whether the plots show a current.
 * @param[in] name  Current name as in the results, such as "R1" or "Q1.C".
 * @return  True when the current is measured.
 */
bool Schematic::IsCurrentMeasured(const std::string &name) const {
    return m_MeasuredCurrents.contains(name);
}

/**
 * @brief   Adds or removes a current from the plots.
 * @param[in] name      Current name as in the results, such as "R1" or "Q1.C".
 * @param[in] measured  Whether to show it.
 */
void Schematic::SetCurrentMeasured(const std::string &name, const bool measured) {
    if (measured) {
        m_MeasuredCurrents.insert(name);
    } else {
        m_MeasuredCurrents.erase(name);
    }
}

/**
 * @brief   Removes every measurement, so the plots start empty; for a new or opened schematic.
 */
void Schematic::ClearMeasurements() {
    m_VoltageProbes.clear();
    m_MeasuredCurrents.clear();
}

/**
 * @brief   Describes the measurements for a file, with each voltage as a point of its node.
 * @return  The measured voltages and currents; nodes that no longer exist are left out.
 */
SavedMeasurements Schematic::SaveMeasurements() {
    SavedMeasurements measurements;
    GetConnectivity();
    for (const VoltageProbe &probe : m_VoltageProbes) {
        if (probe.Node) {
            if (const std::optional<GridPoint> point = m_Connectivity->FindPointOfNode(*probe.Node)) {
                measurements.Voltages.push_back(*point);
            }
        }
    }
    measurements.Currents.assign(m_MeasuredCurrents.begin(), m_MeasuredCurrents.end());
    return measurements;
}

/**
 * @brief   Replaces the measurements with those read from a file.
 * @param[in] measurements  Measured voltages and currents; call it after the content they refer to is in place.
 * @note    Points that connect to nothing, or to ground, are skipped. Currents of parts that do not exist are kept
 *          but plot nothing, like after deleting a measured part.
 */
void Schematic::RestoreMeasurements(const SavedMeasurements &measurements) {
    ClearMeasurements();
    for (const GridPoint point : measurements.Voltages) {
        if (const std::optional<int> node = GetConnectivity().GetNode(point); node && *node != 0) {
            SetVoltageMeasured(*node, true);
        }
    }
    m_MeasuredCurrents.insert(measurements.Currents.begin(), measurements.Currents.end());
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
    m_Modified = true;
    m_ChangedSinceCommit = true;
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
        FollowVoltageProbes();
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
    ++m_OperatingPointVersion;
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
    m_TransientSteps.reset();
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
    m_ACSweepSteps.reset();
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
 * @brief   Stores the transients of a stepped run, one per value of its parameter.
 * @param[in] steps  Every step, with at least one result, and the selected one, which GetTransient() returns.
 * @note    The results are dropped on the next change, like those of a single run.
 */
void Schematic::SetTransientSteps(StepFamily<Core::Transient> steps) {
    steps.Selected = std::min(steps.Selected, steps.Results.size() - 1);
    m_Transient = steps.Results[steps.Selected];
    m_TransientSteps = std::move(steps);
    ++m_TransientVersion;
}

/**
 * @brief   Returns every step of the last stepped transient.
 * @return  The steps, or no value when the last transient was a single run or there is none.
 */
const std::optional<StepFamily<Core::Transient>> &Schematic::GetTransientSteps() const {
    return m_TransientSteps;
}

/**
 * @brief   Shows another step of the stepped transient wherever one result is shown: the schematic, the statistics
 *          and the spectrum.
 * @param[in] step  Index of the step; ignored when out of range or without steps.
 */
void Schematic::SelectTransientStep(const std::size_t step) {
    if (!m_TransientSteps || step >= m_TransientSteps->Results.size() || step == m_TransientSteps->Selected) {
        return;
    }
    m_TransientSteps->Selected = step;
    m_Transient = m_TransientSteps->Results[step];
    ++m_TransientVersion;
}

/**
 * @brief   Stores the AC sweeps of a stepped run, one per value of its parameter.
 * @param[in] steps  Every step, with at least one result, and the selected one, which GetACSweep() returns.
 * @note    The results are dropped on the next change, like those of a single run.
 */
void Schematic::SetACSweepSteps(StepFamily<Core::ACSweep> steps) {
    steps.Selected = std::min(steps.Selected, steps.Results.size() - 1);
    m_ACSweep = steps.Results[steps.Selected];
    m_ACSweepSteps = std::move(steps);
    ++m_ACSweepVersion;
}

/**
 * @brief   Returns every step of the last stepped AC sweep.
 * @return  The steps, or no value when the last AC sweep was a single run or there is none.
 */
const std::optional<StepFamily<Core::ACSweep>> &Schematic::GetACSweepSteps() const {
    return m_ACSweepSteps;
}

/**
 * @brief   Measures another step of the stepped AC sweep in the statistics.
 * @param[in] step  Index of the step; ignored when out of range or without steps.
 */
void Schematic::SelectACSweepStep(const std::size_t step) {
    if (!m_ACSweepSteps || step >= m_ACSweepSteps->Results.size() || step == m_ACSweepSteps->Selected) {
        return;
    }
    m_ACSweepSteps->Selected = step;
    m_ACSweep = m_ACSweepSteps->Results[step];
    ++m_ACSweepVersion;
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
 * @brief   Stores the result of a noise analysis of the current schematic.
 * @param[in] noise  Noise densities, totals and contributions, or no value to clear them.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void Schematic::SetNoise(std::optional<Core::Noise> noise) {
    m_Noise = std::move(noise);
    ++m_NoiseVersion;
}

/**
 * @brief   Returns the last noise analysis simulated for the current schematic.
 * @return  The noise result, or no value when there was no simulation since the last change.
 */
const std::optional<Core::Noise> &Schematic::GetNoise() const {
    return m_Noise;
}

/**
 * @brief   Stores the result of a transfer function analysis of the current schematic.
 * @param[in] transfer_function  Gain, input impedance and output impedance, or no value to clear them.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void Schematic::SetTransferFunction(std::optional<Core::TransferFunction> transfer_function) {
    m_TransferFunction = std::move(transfer_function);
    ++m_TransferFunctionVersion;
}

/**
 * @brief   Returns the last transfer function simulated for the current schematic.
 * @return  The transfer function result, or no value when there was no simulation since the last change.
 */
const std::optional<Core::TransferFunction> &Schematic::GetTransferFunction() const {
    return m_TransferFunction;
}

/**
 * @brief   Returns a number that changes whenever an operating point result is stored.
 * @return  The operating point version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t Schematic::GetOperatingPointVersion() const {
    return m_OperatingPointVersion;
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

/**
 * @brief   Returns a number that changes whenever a noise result is stored.
 * @return  The noise version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t Schematic::GetNoiseVersion() const {
    return m_NoiseVersion;
}

/**
 * @brief   Returns a number that changes whenever a transfer function result is stored.
 * @return  The transfer function version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t Schematic::GetTransferFunctionVersion() const {
    return m_TransferFunctionVersion;
}

void Schematic::InvalidateDerivedData() {
    m_Connectivity.reset();
    m_OperatingPoint.reset();
    m_Transient.reset();
    m_TransientSteps.reset();
    m_ACSweep.reset();
    m_ACSweepSteps.reset();
    m_DCSweep.reset();
    m_Noise.reset();
    m_TransferFunction.reset();
}

// A probe is on the node most of its points belong to now, and takes the points of that node as its own. With no such
// node, because its net is ground or its wires are gone, it waits with the points it had and comes back when they
// belong to a node again. Two probes that end up on one node are one
void Schematic::FollowVoltageProbes() {
    std::set<int> taken;
    std::vector<VoltageProbe> followed;
    for (VoltageProbe &probe : m_VoltageProbes) {
        std::map<int, int> votes;
        for (const GridPoint point : probe.Points) {
            if (const std::optional<int> node = m_Connectivity->GetNode(point); node && *node != 0) {
                ++votes[*node];
            }
        }
        const auto best = std::ranges::max_element(votes, {}, &std::pair<const int, int>::second);
        if (best == votes.end()) {
            probe.Node.reset();
        } else if (taken.insert(best->first).second) {
            probe.Node = best->first;
            probe.Points = m_Connectivity->GetPointsOfNode(best->first);
        } else {
            continue;
        }
        followed.push_back(std::move(probe));
    }
    m_VoltageProbes = std::move(followed);
}

// Measurements stay out of snapshots, so probing adds no undo step and does not count as a change
std::string Schematic::TakeSnapshot() const {
    return SaveSchematic(m_Elements, m_Wires, m_SimulationSettings, {});
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
    if (loaded->Settings != m_SimulationSettings) {
        m_SimulationSettings = std::move(loaded->Settings);
        ++m_SimulationSettingsVersion;
    }
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
