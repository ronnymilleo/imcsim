/**
 * @file    s_parameter_tab.h
 * @brief   The S-parameters tab of the Output window: the Smith chart and the magnitude and phase of the last
 *          S-parameter analysis, with the Touchstone export.
 */

#ifndef IMCSIM_S_PARAMETER_TAB_H
#define IMCSIM_S_PARAMETER_TAB_H

#include "plots/plot_export.h"
#include "plots/plot_exporter.h"
#include "plots/plot_types.h"
#include "plots/s_parameter_view.h"
#include "plots/touchstone.h"
#include "schematic/schematic.h"
#include "simulation/simulation_results.h"
#include <cstddef>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   SParameterTab
 * @brief   Plots the entries of the S matrix that the user checks, either on a Smith chart or as magnitude and phase on
 *          a logarithmic frequency axis, with the readings of the point under the pointer.
 * @details The Smith chart draws each entry as a curve over the circles of constant resistance and the arcs of
 *          constant reactance, on equal axes. The readings of a reflection add the impedance, the standing wave ratio
 *          and the return loss. The Touchstone button saves the whole matrix, whatever entries are checked.
 */
class SParameterTab {
public:
    explicit SParameterTab(Schematic &schematic);

    void Draw(PlotExporter &exporter);

    // Export
    bool CanExport() const;
    ExportFigure BuildFigure(bool is_table) const;

private:
    Schematic &m_Schematic;
    /// The entries of the matrix the magnitude and phase plots show.
    SParameterSelection m_Selection;
    /// The entries the Smith chart shows, which starts from the reflections.
    SParameterSelection m_SmithSelection;
    /// Whether the Smith chart is shown in place of the magnitude and phase plots.
    bool m_IsSmith = true;
    /// The visible frequency range of the magnitude and phase plots, shared by both.
    PlotSpan m_View{.From = 1.0, .To = 1e9};
    TouchstoneOptions m_Touchstone;
    /// The result the selection and the axes were set for.
    std::size_t m_FittedVersion = 0;
    /// Size of the room for the chart when its axes were last fitted, so a resized window fits them again.
    float m_FittedWidth = 0.0f;
    float m_FittedHeight = 0.0f;
    /// Whether the next frame fits the axes to the curves, after a new result or another choice of entries.
    bool m_IsFitting = true;
    /// Why the last Touchstone export could not be written, or empty.
    std::string m_ExportError;

    void DrawControls(const Core::SParameters &result, PlotExporter &exporter);
    void DrawSelection(const Core::SParameters &result);
    void DrawTouchstoneExport(const Core::SParameters &result, PlotExporter &exporter);
    void SaveTouchstone(const Core::SParameters &result, PlotExporter &exporter);
    void DrawSmith(const Core::SParameters &result);
    void DrawMagnitudePhase(const Core::SParameters &result);
    void DrawBodePlot(const Core::SParameters &result, const std::vector<SParameterEntry> &entries, bool is_phase,
                      float height);
    SParameterSelection &GetShownSelection();
    const SParameterSelection &GetShownSelection() const;
};

} // namespace GUI

#endif // IMCSIM_S_PARAMETER_TAB_H
