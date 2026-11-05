/**
 * @file    schematic.h
 * @brief   The schematic document shared by every window: elements, wires, selection and file state.
 */

#ifndef IMCSIM_SCHEMATIC_H
#define IMCSIM_SCHEMATIC_H

#include "analysis/monte_carlo_results.h"
#include "analysis/parameter_step.h"
#include "document/schematic_file.h"
#include "document/simulation_settings.h"
#include "editing/connectivity.h"
#include "helpers.h"
#include "plots/logic_bus.h"
#include "plots/math_channel.h"
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
 *          cached until the next change, and so is the last result of each analysis, which no longer matches
 *          the circuit once it changes; a stepped transient or AC sweep keeps every step, and hands its selected step
 *          to the views that read one result. The selection is a set of elements and a set of wires, by index; the
 *          indices stay valid until an element or wire is added, deleted or the wires are replaced. Undo works on whole
 * snapshots: changes pile up until CommitUndoStep(), which the application calls once the user finishes an interaction,
 * so a whole drag or a typed value is undone in one step. Measurements, the traces the plots show, are view state
 *          shared by the editor and the plots: they are kept by key (node number or current name) across edits,
 *          since small edits rarely renumber nodes, and are saved with the file so it opens ready to plot, but
 *          they stay out of the history and changing them does not count as an unsaved change. The simulation
 *          settings belong to the document: they are saved, undone and count as changes, but changing them keeps
 *          the results, which still match the circuit. While a live transient runs, flipping a switch, pressing a
 *          button, setting a logic input or turning a potentiometer (MarkLiveChanged()) is a change that keeps the
 *          transient and the circuit version: the part changes in the run instead of stopping it.
 */
class Schematic {
public:
    /**
     * @struct  SubcircuitPinCandidate
     * @brief   A pin marker that can become a pin of a block: where it sits and the name it suggests.
     * @details The name is the marker's own, or P1, P2... for an unnamed marker.
     */
    struct SubcircuitPinCandidate {
        GridPoint Point;
        std::string SuggestedName;
    };

    Schematic();

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
    void SetOperatingPoint(std::optional<Core::OperatingPoint> operating_point);
    const std::optional<Core::OperatingPoint> &GetOperatingPoint() const;
    void SetTransient(std::optional<Core::Transient> transient);
    void AppendTransient(Core::Transient chunk);
    const std::optional<Core::Transient> &GetTransient() const;
    void SetTransientSteps(StepFamily<Core::Transient> steps);
    const std::optional<StepFamily<Core::Transient>> &GetTransientSteps() const;
    void SelectTransientStep(std::size_t step);
    void SetACSweep(std::optional<Core::ACSweep> sweep);
    const std::optional<Core::ACSweep> &GetACSweep() const;
    void SetACSweepSteps(StepFamily<Core::ACSweep> steps);
    const std::optional<StepFamily<Core::ACSweep>> &GetACSweepSteps() const;
    void SelectACSweepStep(std::size_t step);
    void SetDCSweep(std::optional<Core::DCSweep> sweep);
    const std::optional<Core::DCSweep> &GetDCSweep() const;
    void SetMonteCarlo(MonteCarloTable table);
    const std::optional<MonteCarloTable> &GetMonteCarlo() const;
    void SetNoise(std::optional<Core::Noise> noise);
    const std::optional<Core::Noise> &GetNoise() const;
    void SetTransferFunction(std::optional<Core::TransferFunction> transfer_function);
    const std::optional<Core::TransferFunction> &GetTransferFunction() const;
    void SetSensitivity(std::optional<Core::Sensitivity> sensitivity);
    const std::optional<Core::Sensitivity> &GetSensitivity() const;
    void SetPoleZero(std::optional<Core::PoleZero> pole_zero);
    const std::optional<Core::PoleZero> &GetPoleZero() const;
    void SetTruthTable(std::optional<Core::TruthTable> truth_table);
    const std::optional<Core::TruthTable> &GetTruthTable() const;
    std::size_t GetOperatingPointVersion() const;
    std::size_t GetTransientVersion() const;
    std::size_t GetTransientDataVersion() const;
    std::size_t GetACSweepVersion() const;
    std::size_t GetDCSweepVersion() const;
    std::size_t GetMonteCarloVersion() const;
    std::size_t GetNoiseVersion() const;
    std::size_t GetTransferFunctionVersion() const;
    std::size_t GetSensitivityVersion() const;
    std::size_t GetPoleZeroVersion() const;
    std::size_t GetTruthTableVersion() const;

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

    /**
     * @struct  VoltageProbe
     * @brief   A measured node, kept by the points of the node because its number changes with any edit.
     * @details Node is the number the probe has in the nodes as last built, or none while the points belong to no
     *          node a plot can show, such as while the net is joined to ground; the points stay, so the probe comes
     *          back when they are a node again.
     */
    struct VoltageProbe {
        std::vector<GridPoint> Points;
        std::optional<int> Node;
    };

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

    // Live runs
    /// Whether a live run is going on.
    bool m_IsLiveRun = false;
    /// Changes of the parts of the live run that it has not taken yet.
    std::vector<Core::LiveChange> m_LiveChanges;

    // History, as whole schematics saved to text: simple and always consistent, at the cost of some memory
    std::string m_CurrentSnapshot;
    std::string m_SavedSnapshot;
    std::vector<std::string> m_UndoSnapshots;
    std::vector<std::string> m_RedoSnapshots;
    /// Set by every change and cleared by CommitUndoStep(), so unchanged frames skip taking a snapshot.
    bool m_ChangedSinceCommit = false;

    // Derived data, dropped on every change: nodes are rebuilt on first use, results by the next simulation
    std::optional<Connectivity> m_Connectivity;
    std::optional<Core::OperatingPoint> m_OperatingPoint;
    /// The transient the views read; for a stepped run, its selected step.
    std::optional<Core::Transient> m_Transient;
    /// Every step of a stepped transient, or none.
    std::optional<StepFamily<Core::Transient>> m_TransientSteps;
    /// The AC sweep the views read; for a stepped run, its selected step.
    std::optional<Core::ACSweep> m_ACSweep;
    /// Every step of a stepped AC sweep, or none.
    std::optional<StepFamily<Core::ACSweep>> m_ACSweepSteps;
    std::optional<Core::DCSweep> m_DCSweep;
    /// What every run of the last Monte Carlo analysis drew and measured, or none.
    std::optional<MonteCarloTable> m_MonteCarlo;
    std::optional<Core::Noise> m_Noise;
    std::optional<Core::TransferFunction> m_TransferFunction;
    std::optional<Core::Sensitivity> m_Sensitivity;
    std::optional<Core::PoleZero> m_PoleZero;
    std::optional<Core::TruthTable> m_TruthTable;
    // Result versions, which change with every new result, so windows can tell a rerun from one they showed
    std::size_t m_OperatingPointVersion = 0;
    std::size_t m_TransientVersion = 0;
    /// Also counts the chunks added to a transient that is still being made.
    std::size_t m_TransientDataVersion = 0;
    std::size_t m_ACSweepVersion = 0;
    std::size_t m_DCSweepVersion = 0;
    std::size_t m_MonteCarloVersion = 0;
    std::size_t m_NoiseVersion = 0;
    std::size_t m_TransferFunctionVersion = 0;
    std::size_t m_SensitivityVersion = 0;
    std::size_t m_PoleZeroVersion = 0;
    std::size_t m_TruthTableVersion = 0;

    void InvalidateDerivedData();
    void ClearMonteCarlo();
    void FollowVoltageProbes();
    void RefreshInstances(const SavedSubcircuit &definition, const std::vector<std::string> &old_names,
                          const std::vector<std::optional<std::size_t>> &matches);
    void WireNewBlock(const std::vector<GridPoint> &old_points);
    std::string TakeSnapshot() const;
    void RestoreSnapshot(const std::string &snapshot);
    void ResetHistory();
};

} // namespace GUI

#endif // IMCSIM_SCHEMATIC_H
