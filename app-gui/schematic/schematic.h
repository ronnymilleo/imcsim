/**
 * @file    schematic.h
 * @brief   The schematic document shared by every window: elements, wires, selection and file state.
 */

#ifndef IMCSIM_SCHEMATIC_H
#define IMCSIM_SCHEMATIC_H

#include "base/helpers.h"
#include "schematic/block_definitions.h"
#include "schematic/circuit_listener.h"
#include "schematic/connectivity.h"
#include "schematic/logic_bus_definition.h"
#include "schematic/math_channel_definition.h"
#include "schematic/schematic_file.h"
#include "schematic/simulation_settings.h"
#include "schematic/undo_history.h"
#include "schematic/voltage_probes.h"
#include "simulation/live_change.h"
#include "simulation/simulator.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/**
 * @class   Schematic
 * @brief   The circuit being edited, owned by the application and shared by the windows that show or edit it.
 * @details Adding and deleting mark the schematic as modified. Geometry updates that may be undone within the
 *          same gesture, such as wires following a dragged element, only invalidate the nodes; the caller
 *          decides when the gesture counts as a change and calls MarkModified(). Nodes are computed lazily and
 *          cached until the next change. The results of the simulations are not kept here: whoever wants to hear
 *          when a change makes them stale registers a CircuitListener (SetListener()), which the schematic tells at
 *          each such change. The selection is a set of elements and a set of wires, by index; the
 *          indices stay valid until an element or wire is added, deleted or the wires are replaced. Undo works on whole
 *          snapshots: changes pile up until CommitUndoStep(), which the application calls once the user finishes an
 *          interaction, so a whole drag or a typed value is undone in one step. Measurements, the traces the plots
 *          show, are view state shared by the editor and the plots: they are kept by key (node number or current name)
 *          across edits, since small edits rarely renumber nodes, and are saved with the file so it opens ready to
 *          plot, but they stay out of the history and changing them does not count as an unsaved change. The
 *          simulation settings belong to the document: they are saved, undone and count as changes, but changing them
 *          tells nobody, since the results still match the circuit. While a live transient runs, flipping a switch,
 *          pressing a button, setting a logic input or turning a potentiometer (MarkLiveChanged()) is a change that
 *          keeps the transient and the circuit version: the part changes in the run instead of stopping it. A
 *          schematic is never copied and can only be moved into a new one, which starts without a listener, so no other
 *          schematic ever tells the store of the document.
 */
class Schematic {
public:
    /// A pin marker that can become a pin of a block, as BlockDefinitions finds it.
    using SubcircuitPinCandidate = GUI::SubcircuitPinCandidate;

    Schematic();
    Schematic(const Schematic &) = delete;
    Schematic(Schematic &&) = default;
    Schematic &operator=(const Schematic &) = delete;
    Schematic &operator=(Schematic &&) = delete;

    // Content
    const std::vector<std::unique_ptr<UIElement>> &GetElements() const;
    UIElement &GetElement(std::size_t index);
    const std::vector<UIWire> &GetWires() const;
    const std::vector<SavedSubcircuit> &GetSubcircuits() const;
    const std::vector<Core::LibraryModel> &GetLibraryModels() const;
    bool IsTerminal(GridPoint point) const;
    std::vector<GridPoint> CollectTerminals() const;

    // Editing
    void AddElement(std::unique_ptr<UIElement> element);
    void AddWire(GridPoint start, GridPoint end);
    void SetWires(std::vector<UIWire> wires);
    void SimplifyAllWires();
    void DeleteSelection();
    std::expected<void, std::string> RenameElement(std::size_t index, const std::string &name);
    void Clear();
    void Replace(std::vector<std::unique_ptr<UIElement>> elements, std::vector<UIWire> wires,
                 SimulationSettings settings, std::vector<SavedSubcircuit> subcircuits = {},
                 std::vector<Core::LibraryModel> models = {});

    // Blocks
    std::expected<std::vector<SubcircuitPinCandidate>, std::string> GetSubcircuitPinCandidates() const;
    std::expected<std::vector<SubcircuitPinCandidate>, std::string> GetSubcircuitPinMarkers() const;
    bool SetPinMarkerSide(GridPoint point, Core::PinSide side);
    std::expected<void, std::string>
    CreateSubcircuitFromSelection(std::string name, std::vector<SavedSubcircuitPin> pins,
                                  std::vector<Core::SubcircuitParameter> parameters = {});
    std::expected<std::unique_ptr<Schematic>, std::string> OpenSubcircuit(const std::string &name) const;
    std::expected<void, std::string> UpdateSubcircuit(const std::string &name, const Schematic &content,
                                                      const std::vector<SavedSubcircuitPin> &pins,
                                                      const std::vector<Core::SubcircuitParameter> &parameters);
    std::expected<void, std::string> UpdateSpiceSubcircuit(const std::string &name,
                                                           const std::vector<SavedSubcircuitPin> &pins,
                                                           const std::vector<Core::SubcircuitParameter> &parameters);
    std::vector<std::string> MergeSubcircuits(std::vector<SavedSubcircuit> definitions,
                                              std::vector<std::unique_ptr<UIElement>> &instances);

    // Library models
    std::vector<std::string> MergeLibraryModels(std::vector<Core::LibraryModel> models,
                                                std::vector<std::unique_ptr<UIElement>> &elements,
                                                std::vector<SavedSubcircuit> &definitions);
    void CopyDefinitionsFrom(const Schematic &library, std::string_view left_out = {});

    // Selection
    void SelectElement(std::size_t index);
    void SelectWire(std::size_t index);
    void SetSelection(std::set<std::size_t> elements, std::set<std::size_t> wires);
    void ClearSelection();
    bool IsElementSelected(std::size_t index) const;
    bool IsWireSelected(std::size_t index) const;
    const std::set<std::size_t> &GetSelectedElements() const;
    const std::set<std::size_t> &GetSelectedWires() const;
    bool HasSelection() const;
    UIElement *GetSelectedElement();
    std::size_t GetSelectionVersion() const;

    // Measurements
    bool IsVoltageMeasured(int node) const;
    void SetVoltageMeasured(int node, bool measured);
    bool IsCurrentMeasured(const std::string &name) const;
    void SetCurrentMeasured(const std::string &name, bool measured);
    const std::vector<MathChannel> &GetMathChannels() const;
    void SetMathChannels(std::vector<MathChannel> channels);
    const std::vector<LogicBus> &GetBuses() const;
    void SetBuses(std::vector<LogicBus> buses);
    void ClearMeasurements();
    SavedMeasurements SaveMeasurements();
    void RestoreMeasurements(const SavedMeasurements &measurements);

    // Simulation settings
    const SimulationSettings &GetSimulationSettings() const;
    void SetSimulationSettings(const SimulationSettings &settings);
    std::size_t GetSimulationSettingsVersion() const;

    // Document state
    bool IsModified() const;
    void MarkModified();
    const std::optional<std::filesystem::path> &GetFilePath() const;
    void MarkSaved(std::filesystem::path path);

    // Live runs
    void SetLiveRun(bool is_live);
    bool IsLiveRun() const;
    void MarkLiveChanged(const Core::Component &component);
    std::vector<Core::LiveChange> TakeLiveChanges();

    // Listener
    void SetListener(CircuitListener *listener);

    // History
    void CommitUndoStep();
    bool CanUndo() const;
    bool CanRedo() const;
    void Undo();
    void Redo();

    // Derived data
    const Connectivity &GetConnectivity();
    Core::Circuit BuildCircuit();
    std::size_t GetCircuitVersion() const;
    std::size_t GetDocumentVersion() const;
    std::string BuildSpiceNetlist();

private:
    // Content
    std::vector<std::unique_ptr<UIElement>> m_Elements;
    std::vector<UIWire> m_Wires;
    std::vector<SavedSubcircuit> m_Subcircuits;
    /// Device models imported from libraries, which diodes and transistors anywhere in the document can use.
    std::vector<Core::LibraryModel> m_LibraryModels;
    /// The blocks as the simulator takes them, built once from m_Subcircuits and dropped when it changes.
    std::optional<std::vector<std::shared_ptr<Core::SubcircuitDefinition>>> m_BuiltSubcircuits;

    // Selection
    std::set<std::size_t> m_SelectedElements;
    std::set<std::size_t> m_SelectedWires;
    /// Changes with every change of the selection, so windows can tell even when an index is reused.
    std::size_t m_SelectionVersion = 0;

    // Measurements
    /// Probes on nodes.
    std::vector<VoltageProbe> m_VoltageProbes;
    /// Current names such as "R1" or "Q1.C".
    std::set<std::string> m_MeasuredCurrents;
    std::vector<MathChannel> m_MathChannels;
    /// Logic signals the transient plot reads together as numbers.
    std::vector<LogicBus> m_Buses;

    // Simulation settings
    SimulationSettings m_SimulationSettings;
    /// Changes only when the settings are replaced from outside the analysis settings, such as by opening a file or
    /// undoing, so the window knows to reload its fields.
    std::size_t m_SimulationSettingsVersion = 0;
    /// Counts the edits that drop the results, so a run that started before one knows its answer is stale.
    std::size_t m_CircuitVersion = 0;
    /// Counts the schematics opened or created in place of the content, which edits and undo do not change.
    std::size_t m_DocumentVersion = 0;

    // Document state
    bool m_Modified = false;
    std::optional<std::filesystem::path> m_FilePath;

    /**
     * @struct  ListenerSlot
     * @brief   Holds the listener of a schematic, and stays with that schematic when it is moved.
     * @details A schematic moved into another one leaves its listener behind: the listener hears of the schematic it
     *          was set on, and the new one is not that one.
     */
    struct ListenerSlot {
        /// Told when a change drops the results, or null; never owned.
        CircuitListener *Target = nullptr;

        ListenerSlot() = default;
        ListenerSlot(const ListenerSlot &) = delete;
        ListenerSlot(ListenerSlot &&other) noexcept;
        ListenerSlot &operator=(const ListenerSlot &) = delete;
        ListenerSlot &operator=(ListenerSlot &&) = delete;
        ~ListenerSlot() = default;
    };

    // Listener
    ListenerSlot m_Listener;

    // Live runs
    /// Whether a live run is going on.
    bool m_IsLiveRun = false;
    /// Changes of the parts of the live run that it has not taken yet.
    std::vector<Core::LiveChange> m_LiveChanges;

    // History
    UndoHistory m_History;

    // Derived data, dropped on every change: nodes are rebuilt on first use
    std::optional<Connectivity> m_Connectivity;

    void InvalidateDerivedData();
    void RefreshInstances(const SavedSubcircuit &definition, const std::vector<std::string> &old_names,
                          const std::vector<std::optional<std::size_t>> &matches);
    void WireNewBlock(const std::vector<GridPoint> &old_points);
    std::string TakeSnapshot() const;
    void RestoreSnapshot(const std::string &snapshot);
    void ResetHistory();
};

} // namespace GUI

#endif // IMCSIM_SCHEMATIC_H
