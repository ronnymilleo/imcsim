/**
 * @file    s_parameter_tab.h
 * @brief   The S-parameters tab of the Output window: the Smith chart, the magnitude and phase and the stability of the
 *          last S-parameter analysis, in S, Z or Y, with the Touchstone export.
 */

#ifndef IMCSIM_S_PARAMETER_TAB_H
#define IMCSIM_S_PARAMETER_TAB_H

#include "plots/plot_export.h"
#include "plots/plot_exporter.h"
#include "plots/plot_types.h"
#include "plots/port_matrix_view.h"
#include "plots/s_parameter_view.h"
#include "plots/stability_view.h"
#include "plots/touchstone.h"
#include "results/result_store.h"
#include "results/two_port_metrics.h"
#include "schematic/schematic.h"
#include "simulation/simulation_results.h"
#include <cstddef>
#include <string>
#include <vector>

namespace GUI {

/**
 * @enum    SParameterView
 * @brief   What the tab draws: the Smith chart, the magnitude and phase plots or the stability of a two-port.
 */
enum class SParameterView {
    SMITH,
    MAGNITUDE_PHASE,
    STABILITY
};

/**
 * @class   SParameterTab
 * @brief   Plots the entries of the S, Z or Y matrix that the user checks, either on a Smith chart or as magnitude and
 *          phase on a logarithmic frequency axis, or the stability of a two-port, with the readings of the point under
 *          the pointer.
 * @details The Smith chart draws each entry as a curve over the circles of constant resistance and the arcs of
 *          constant reactance, on equal axes, and can add the source and load stability circles at one frequency. The
 *          readings of a reflection add the impedance, the standing wave ratio and the return loss. Z and Y, which only
 *          some backends give, have the magnitude and phase plots alone, and the view the user had for S comes back
 *          with S. The Stability view is for a two-port: K and mu against frequency, and the gain bound, under a
 *          sentence that says where the two-port can oscillate. The Touchstone button saves the whole S matrix,
 *          whatever is checked.
 */
class SParameterTab {
public:
    SParameterTab(Schematic &schematic, const ResultStore &results);

    void Draw(PlotExporter &exporter);

    // Export
    bool CanExport() const;
    ExportFigure BuildFigure(bool is_table) const;

private:
    Schematic &m_Schematic;
    const ResultStore &m_Results;
    /// The entries of S the magnitude and phase plots show.
    SParameterSelection m_Selection;
    /// The entries the Smith chart shows, which starts from the reflections.
    SParameterSelection m_SmithSelection;
    /// The entries of Z and of Y that the magnitude and phase plots show, each kept apart from S.
    SParameterSelection m_ImpedanceSelection;
    SParameterSelection m_AdmittanceSelection;
    SParameterQuantity m_Quantity = SParameterQuantity::S;
    /// The view the user chose for S, which Z and Y leave alone and S gets back.
    SParameterView m_Display = SParameterView::SMITH;
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
    /// Whether the Smith chart shows the stability circles of a two-port.
    bool m_AreCirclesShown = false;
    /// Index of the frequency of the circles, which each new result sets where mu is lowest.
    std::size_t m_CircleFrequency = 0;
    /// The metrics of the result, with the curves and the sentence that go with them; empty unless it is a two-port.
    TwoPortSweep m_Sweep;
    StabilityCurves m_Curves;
    std::string m_StabilitySummary;

    // Result
    void StartResult(const Core::SParameters &result);
    SParameterView GetDisplayedView(const Core::SParameters &result) const;
    SParameterSelection &GetShownSelection(SParameterView view);
    const SParameterSelection &GetShownSelection(SParameterView view) const;

    // Controls
    void DrawControls(const Core::SParameters &result, PlotExporter &exporter, SParameterView view);
    void DrawHeader(const Core::SParameters &result);
    void DrawQuantityChoice(const Core::SParameters &result);
    void DrawViewChoice(const Core::SParameters &result, SParameterView view, PlotExporter &exporter);
    void DrawCircleControls(const Core::SParameters &result);
    void DrawSelection(const Core::SParameters &result, SParameterView view);
    void DrawTouchstoneExport(const Core::SParameters &result, PlotExporter &exporter);
    void SaveTouchstone(const Core::SParameters &result, PlotExporter &exporter);

    // Smith chart
    void DrawSmith(const Core::SParameters &result, SParameterView view);
    void PlotSmithCurves(const Core::SParameters &result, const std::vector<SParameterEntry> &entries);
    void PlotStabilityCircles(const Core::SParameters &result);
    void ShowSmithReading(const Core::SParameters &result, const std::vector<SParameterEntry> &entries);

    // Magnitude and phase
    void DrawMagnitudePhase(const Core::SParameters &result, SParameterView view);
    void DrawBodePlot(const Core::SParameters &result, const std::vector<SParameterEntry> &entries, bool is_phase,
                      float height);

    // Stability
    void DrawStability();
    void DrawFactorPlot(float height);
    void DrawGainPlot(float height);
};

} // namespace GUI

#endif // IMCSIM_S_PARAMETER_TAB_H
