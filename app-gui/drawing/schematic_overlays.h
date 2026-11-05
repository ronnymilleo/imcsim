/**
 * @file    schematic_overlays.h
 * @brief   What the editor draws over the schematic from a result: heat colors, glows, node voltages and the legend.
 */

#ifndef IMCSIM_SCHEMATIC_OVERLAYS_H
#define IMCSIM_SCHEMATIC_OVERLAYS_H

#include "analysis/analysis_controls.h"
#include "analysis/circuit_snapshot.h"
#include "analysis/playback.h"
#include "analysis/point_window.h"
#include "document/schematic.h"
#include "drawing/glow_renderer.h"
#include "drawing/schematic_canvas.h"
#include "editing/probing.h"
#include "helpers.h"
#include "imgui.h"
#include "plots/logic_trace.h"
#include "preferences.h"
#include "simulation/simulation_results.h"
#include <cstddef>
#include <optional>
#include <vector>

namespace GUI {

/**
 * @struct  DisplayedState
 * @brief   The voltages, currents and powers the schematic shows this frame, and the scale their colors use.
 * @details It is the last operating point, or the last transient read at the instant of the playback; the drawing code
 *          does not tell them apart. Time is set for a transient only.
 */
struct DisplayedState {
    Core::OperatingPoint Point;
    HeatScale Scale;
    std::vector<ComponentPower> Powers;
    std::optional<double> Time;
    /// When each fuse blew in the transient on display, by the index of its element; none for a fuse still whole and
    /// for the other parts.
    std::vector<std::optional<double>> FuseBlowTimes;
    /// What the terminals of each logic display read, by the index of its element, in terminal order; empty for the
    /// other parts, and none for a terminal whose node the result has no voltage for.
    std::vector<std::vector<std::optional<LogicState>>> LogicReadings;
};

/**
 * @class   SchematicOverlays
 * @brief   Draws the schematic in the colors of its last result, as the preferences ask: wires by node, voltage or
 *          current, parts by current or power, glows of lit LEDs and hot resistors, the voltage of every node, the
 *          value under the cursor and the legend of the heat scale.
 * @details Update() resolves once per frame what the schematic shows; the drawing methods then read it. The heat scale
 *          of a transient goes over the window of points that plays, and is kept until the result or the window
 *          changes.
 */
class SchematicOverlays {
public:
    SchematicOverlays(GlowRenderer &glow_renderer, const TransientPlayback &playback, const Preferences &preferences);

    // What the schematic shows
    void Update(Schematic &schematic, const AnalysisControls &controls);
    void Reset();
    const std::optional<DisplayedState> &GetDisplayed() const;

    // Drawing, in the order the editor draws them
    void DrawWires(SchematicCanvas &canvas, const ViewTransform &view, Schematic &schematic, float zoom) const;
    void DrawElements(SchematicCanvas &canvas, const ViewTransform &view, const Schematic &schematic,
                      bool marks_live_parts, std::optional<std::size_t> lit_live_part) const;
    void DrawGlows(ImDrawList *draw_list, const ViewTransform &view, const Schematic &schematic, float zoom) const;
    void DrawNodeVoltages(ImDrawList *draw_list, const ViewTransform &view, Schematic &schematic) const;
    void DrawHoveredValue(const MeasurementTarget &target, Schematic &schematic) const;
    void DrawColorLegend(ImDrawList *draw_list, ImVec2 origin, ImVec2 size) const;

private:
    GlowRenderer &m_GlowRenderer;
    /// The instant of the transient shown, and the window of points the scale goes over.
    const TransientPlayback &m_Playback;
    /// How the schematic is colored, and which live effects are on.
    const Preferences &m_Preferences;

    /// What the schematic shows, resolved at the start of each frame.
    std::optional<DisplayedState> m_Displayed;
    // Heat scale of a transient
    /// Version of the transient the heat scale was worked out for, so it is worked out once per result and kept.
    std::optional<std::size_t> m_ScaledTransientVersion;
    /// The window of points the scale was worked out over.
    PointWindow m_ScaledWindow;
    /// When the heat scale of a transient that is still being made was last worked out.
    double m_LastScaleTime = 0.0;
    HeatScale m_TransientScale;

    // Wires
    void DrawWireSelectionHalos(SchematicCanvas &canvas, const ViewTransform &view, const Schematic &schematic) const;
    void DrawWireCurrents(SchematicCanvas &canvas, const ViewTransform &view, const Schematic &schematic) const;
    void DrawNodeNumbers(SchematicCanvas &canvas, const ViewTransform &view, Schematic &schematic) const;
    ImU32 GetWireColor(int node) const;

    // Parts
    ImU32 GetElementColor(const Schematic &schematic, std::size_t index) const;
    void DrawLogicReading(SchematicCanvas &canvas, const ViewTransform &view, const UIElement &element,
                          std::size_t index) const;

    // Fuses
    /// Blow times of the fuses of the transient shown, for the version of its data they were read from.
    std::vector<std::optional<double>> m_FuseBlowTimes;
    std::optional<std::size_t> m_FuseBlowTimesVersion;
    /// Whether each element was a fuse shown open in the last frame; empty when that frame showed no transient.
    std::vector<bool> m_WasFuseBlown;
    /// When the screen clock last saw each fuse blow, by element.
    std::vector<double> m_FuseBlownSince;

    void UpdateFuses(Schematic &schematic, const Core::Transient *shown, std::optional<double> instant);

    // Heat colors
    bool AreWiresHeatColored() const;
    bool ArePartsHeatColored() const;
};

} // namespace GUI

#endif // IMCSIM_SCHEMATIC_OVERLAYS_H
