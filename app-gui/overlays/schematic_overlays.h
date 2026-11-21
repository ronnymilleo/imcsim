/**
 * @file    schematic_overlays.h
 * @brief   What the editor draws over the schematic from a result: heat colors, glows, node voltages and the legend.
 */

#ifndef IMCSIM_SCHEMATIC_OVERLAYS_H
#define IMCSIM_SCHEMATIC_OVERLAYS_H

#include "analysis/analysis_controls.h"
#include "canvas/schematic_canvas.h"
#include "imgui.h"
#include "overlays/color_legend.h"
#include "overlays/displayed_state.h"
#include "overlays/glow_renderer.h"
#include "results/playback.h"
#include "results/result_store.h"
#include "schematic/preferences.h"
#include "schematic/probing.h"
#include "schematic/schematic.h"
#include <cstddef>
#include <optional>

namespace GUI {

/**
 * @class   SchematicOverlays
 * @brief   Draws the schematic in the colors of its last result, as the preferences ask: wires by node, voltage or
 *          current, parts by current or power, glows of lit LEDs and hot resistors, the voltage of every node, the
 *          value under the cursor and the legend of the heat scale.
 * @details Update() resolves once per frame what the schematic shows, through a DisplayedStateResolver, and tells the
 *          fuses what they show; the drawing methods then read it.
 */
class SchematicOverlays {
public:
    SchematicOverlays(GlowRenderer &glow_renderer, const TransientPlayback &playback, const Preferences &preferences);

    // What the schematic shows
    void Update(Schematic &schematic, const ResultStore &results, const AnalysisControls &controls);
    void Reset();
    const std::optional<DisplayedState> &GetDisplayed() const;

    // Drawing, in the order the editor draws them
    void DrawWires(SchematicCanvas &canvas, const ViewTransform &view, Schematic &schematic, float zoom) const;
    void DrawElements(SchematicCanvas &canvas,
                      const ViewTransform &view,
                      const Schematic &schematic,
                      bool marks_live_parts,
                      std::optional<std::size_t> lit_live_part) const;
    void DrawGlows(ImDrawList *draw_list, const ViewTransform &view, const Schematic &schematic, float zoom) const;
    void DrawNodeVoltages(ImDrawList *draw_list, const ViewTransform &view, Schematic &schematic, float zoom) const;
    void DrawHoveredValue(const MeasurementTarget &target, Schematic &schematic) const;
    void DrawColorLegend(ImDrawList *draw_list, ImVec2 origin, ImVec2 size, Schematic &schematic) const;

private:
    GlowRenderer &m_GlowRenderer;
    /// How the schematic is colored, and which live effects are on.
    const Preferences &m_Preferences;
    DisplayedStateResolver m_Resolver;
    /// The results of the frame, from Update(), which the sensitivity coloring reads while drawing; null before the
    /// first Update().
    const ResultStore *m_Results = nullptr;

    // Wires
    void DrawWireSelectionHalos(SchematicCanvas &canvas, const ViewTransform &view, const Schematic &schematic) const;
    void DrawWireCurrents(SchematicCanvas &canvas, const ViewTransform &view, const Schematic &schematic) const;
    void DrawNodeNumbers(SchematicCanvas &canvas, const ViewTransform &view, Schematic &schematic) const;
    ImU32 GetWireColor(int node) const;

    // Parts
    ImU32 GetElementColor(const Schematic &schematic, std::size_t index) const;
    ImU32 GetSensitivityColor(const Schematic &schematic, std::size_t index) const;
    void DrawLogicState(SchematicCanvas &canvas,
                        const ViewTransform &view,
                        const UIElement &element,
                        std::size_t index) const;

    // Heat colors
    std::optional<LegendLabels> GetLegendLabels(Schematic &schematic) const;
    bool AreWiresHeatColored() const;
    bool ArePartsHeatColored() const;
};

} // namespace GUI

#endif // IMCSIM_SCHEMATIC_OVERLAYS_H
