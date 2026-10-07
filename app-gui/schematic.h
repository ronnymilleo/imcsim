/**
 * @file    schematic.h
 * @brief   The schematic document shared by every window: elements, wires, selection and file state.
 */

#ifndef IMCSIM_SCHEMATIC_H
#define IMCSIM_SCHEMATIC_H

#include "connectivity.h"
#include "helpers.h"
#include "simulator.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   Schematic
 * @brief   The circuit being edited, owned by the application and shared by the windows that show or edit it.
 * @details Adding and deleting mark the schematic as modified. Geometry updates that may be undone within the
 *          same gesture, such as wires following a dragged element, only invalidate the nodes; the caller
 *          decides when the gesture counts as a change and calls MarkModified(). Nodes are computed lazily and
 *          cached until the next change, and so is the last result of each analysis, which no longer matches
 *          the circuit once it changes. Indices returned by the selection stay valid until an element or wire
 *          is added, deleted or the wires are replaced. Undo works on whole snapshots: changes pile up until
 *          CommitUndoStep(), which the application calls once the user finishes an interaction, so a whole drag
 *          or a typed value is undone in one step.
 */
class Schematic {
public:
    Schematic();

    // Content
    const std::vector<std::unique_ptr<UIElement>> &GetElements() const;
    UIElement &GetElement(std::size_t index);
    const std::vector<UIWire> &GetWires() const;
    bool IsTerminal(GridPoint point) const;
    std::vector<GridPoint> CollectTerminals() const;

    // Editing
    void AddElement(std::unique_ptr<UIElement> element);
    void AddWire(GridPoint start, GridPoint end);
    void SetWires(std::vector<UIWire> wires);
    void SimplifyAllWires();
    void DeleteSelection();
    void Clear();
    void Replace(std::vector<std::unique_ptr<UIElement>> elements, std::vector<UIWire> wires);

    // Selection
    void SelectElement(std::size_t index);
    void SelectWire(std::size_t index);
    void ClearSelection();
    std::optional<std::size_t> GetSelectedElementIndex() const;
    std::optional<std::size_t> GetSelectedWireIndex() const;
    UIElement *GetSelectedElement();
    std::size_t GetSelectionVersion() const;

    // Document state
    bool IsModified() const;
    void MarkModified();
    const std::optional<std::filesystem::path> &GetFilePath() const;
    void MarkSaved(std::filesystem::path path);

    // History
    void CommitUndoStep();
    bool CanUndo() const;
    bool CanRedo() const;
    void Undo();
    void Redo();

    // Derived data
    const Connectivity &GetConnectivity();
    Core::Circuit BuildCircuit();
    std::string BuildSpiceNetlist();
    void SetOperatingPoint(std::optional<Core::OperatingPoint> operating_point);
    const std::optional<Core::OperatingPoint> &GetOperatingPoint() const;
    void SetTransient(std::optional<Core::Transient> transient);
    const std::optional<Core::Transient> &GetTransient() const;
    void SetACSweep(std::optional<Core::ACSweep> sweep);
    const std::optional<Core::ACSweep> &GetACSweep() const;
    void SetDCSweep(std::optional<Core::DCSweep> sweep);
    const std::optional<Core::DCSweep> &GetDCSweep() const;
    std::size_t GetTransientVersion() const;
    std::size_t GetACSweepVersion() const;
    std::size_t GetDCSweepVersion() const;

private:
    // Content
    std::vector<std::unique_ptr<UIElement>> m_Elements;
    std::vector<UIWire> m_Wires;

    // Selection; the version changes with every change, so windows can tell even when an index is reused
    std::optional<std::size_t> m_SelectedElement;
    std::optional<std::size_t> m_SelectedWire;
    std::size_t m_SelectionVersion = 0;

    // Document state
    bool m_Modified = false;
    std::optional<std::filesystem::path> m_FilePath;

    // History, as whole schematics saved to text: simple and always consistent, at the cost of some memory
    std::string m_CurrentSnapshot;
    std::string m_SavedSnapshot;
    std::vector<std::string> m_UndoSnapshots;
    std::vector<std::string> m_RedoSnapshots;
    // Set by every change and cleared by CommitUndoStep(), so unchanged frames skip taking a snapshot
    bool m_ChangedSinceCommit = false;

    // Derived data, dropped on every change: nodes are rebuilt on first use, results by the next simulation
    std::optional<Connectivity> m_Connectivity;
    std::optional<Core::OperatingPoint> m_OperatingPoint;
    std::optional<Core::Transient> m_Transient;
    std::optional<Core::ACSweep> m_ACSweep;
    std::optional<Core::DCSweep> m_DCSweep;
    // Change with every new result, so windows can tell a rerun from the result they already showed
    std::size_t m_TransientVersion = 0;
    std::size_t m_ACSweepVersion = 0;
    std::size_t m_DCSweepVersion = 0;

    void InvalidateDerivedData();
    std::string TakeSnapshot() const;
    void RestoreSnapshot(const std::string &snapshot);
    void ResetHistory();
};

} // namespace GUI

#endif // IMCSIM_SCHEMATIC_H
