/**
 * @file    output_window.h
 * @brief   Window that plots the transient, AC sweep and DC sweep results of the schematic.
 */

#ifndef IMCSIM_OUTPUT_WINDOW_H
#define IMCSIM_OUTPUT_WINDOW_H

#include "analysis/analysis_controls.h"
#include "analysis/playback.h"
#include "document/schematic.h"
#include "imgui.h"
#include "plots/logic_buses.h"
#include "plots/math_channels.h"
#include "plots/plot_export.h"
#include "plots/plot_exporter.h"
#include "preferences.h"
#include "simulation/simulation_results.h"
#include "windows/app_window.h"
#include "windows/output_tabs/ac_sweep_tab.h"
#include "windows/output_tabs/dc_sweep_tab.h"
#include "windows/output_tabs/frame_results.h"
#include "windows/output_tabs/monte_carlo_tab.h"
#include "windows/output_tabs/noise_tab.h"
#include "windows/output_tabs/pole_zero_tab.h"
#include "windows/output_tabs/s_parameter_tab.h"
#include "windows/output_tabs/spectrum_tab.h"
#include "windows/output_tabs/transient_tab.h"
#include <cstddef>
#include <optional>
#include <vector>

namespace GUI {

/**
 * @enum    PlotTab
 * @brief   The tabs of the Output window, one per analysis, and the spectrum of the transient.
 */
enum class PlotTab {
    OPERATING_POINT,
    TRANSIENT,
    SPECTRUM,
    AC_SWEEP,
    DC_SWEEP,
    NOISE,
    TRANSFER_FUNCTION,
    SENSITIVITY,
    POLE_ZERO,
    S_PARAMETERS,
    TRUTH_TABLE,
    MONTE_CARLO
};

/**
 * @class   OutputWindow
 * @brief   Plots the node voltages and component currents of the last transient over time, the last AC sweep as
 *          a Bode plot, and the last DC sweep against its swept source.
 * @details The results come from the Schematic, which the analysis controls fill, so the plots disappear as
 *          soon as the circuit changes. Plots show only the measured traces, picked with the Probe tool of the
 *          editor or in the list beside the plots; voltages take the colors the editor gives the nodes, and
 *          currents are dashed on the secondary Y axis on the right. Hovering a plot reads every shown trace at
 *          the nearest sample. The axes of each analysis fit its new results, and keep zoom and pan until the
 *          next one; the voltage and current axes also fit whenever their measured traces change. Like an
 *          oscilloscope, the transient, the Bode plots and the DC sweep have two cursors and a panel on their right
 *          that measures every shown trace: the transient between the cursors or over what is visible, the DC sweep
 *          the same way with the value, change and slope between its cursors, the Bode plot over the whole sweep.
 *          The Spectrum tab transforms the measured traces of the transient over the span its
 *          statistics measure, and reads the fundamental, the harmonics and the THD of each, as a spectrum
 *          analyzer would. The Noise tab plots the noise density at the output and referred to the input on
 *          log-log axes, with the share of each part, and the Transfer function tab lists its DC gain and its
 *          input and output impedances, and the Truth table tab lists the outputs of every combination of some logic
 * inputs. Math channels add traces computed from the others, such as V(1)-V(2) or V(1)*I(R1), to the transient and DC
 * sweep plots: voltages and currents keep their axes, and any other unit, or none, goes on a third axis. Export saves
 * the plot of the current tab as an SVG image, redrawn from its data at a chosen size and theme over the visible X
 * range, or as a CSV table of every sample. It starts closed and opens on the tab of each new result. It also comes to
 * the front then, unless the user turned that off in the View menu: a dot on its title or tab then tells a new result
 * is waiting.
 * The window keeps the list of traces, the math channels and the tab bar; each tab that plots is a class of its own,
 * which keeps its cursors, its view and the fit of its axes.
 */
class OutputWindow : public AppWindow {
public:
    OutputWindow(Schematic &schematic, AnalysisControls &controls, TransientPlayback &playback,
                 Preferences &preferences);
    ~OutputWindow() override = default;

    void ShowTab(PlotTab tab, bool may_come_to_front = true);

private:
    Schematic &m_Schematic;
    AnalysisControls &m_Controls;
    TransientPlayback &m_Playback;
    /// Whether a new result brings the window to the front, and the coloring the Sensitivity tab switches on.
    Preferences &m_Preferences;

    // Math channels
    MathChannels m_MathChannels;
    /// The panel of the logic buses, which the transient plot draws as lanes of numbers.
    LogicBuses m_Buses;
    /// Over the whole run, or none while it grows; the plot of a run in progress evaluates the window instead.
    std::optional<FullMath> m_FullMath;

    // Tabs
    TransientTab m_TransientTab;
    SpectrumTab m_SpectrumTab;
    ACSweepTab m_ACSweepTab;
    DCSweepTab m_DCSweepTab;
    NoiseTab m_NoiseTab;
    PoleZeroTab m_PoleZeroTab;
    SParameterTab m_SParameterTab;
    MonteCarloTab m_MonteCarloTab;
    /// The tab shown this frame, which Export saves.
    PlotTab m_ShownTab = PlotTab::TRANSIENT;
    /// Tab to bring to the front on the next frame, after a new result.
    std::optional<PlotTab> m_TabToShow;

    PlotExporter m_Exporter;

    // Frame
    void Draw() override;
    const std::vector<Core::ComponentTrace> &GetListedCurrents() const;
    std::size_t GetListedNodeCount() const;
    void UpdateFullMath();
    FrameResults EvaluateFrame(std::size_t node_count, const std::vector<Core::ComponentTrace> &currents) const;
    void DrawTraceList(std::size_t node_count, const std::vector<Core::ComponentTrace> &currents);
    void DrawMathPanel(std::size_t node_count, const std::vector<Core::ComponentTrace> &currents);
    void DrawBusPanel(std::size_t node_count);

    // Tabs
    void DrawTabs(const FrameResults &frame);

    // Export
    void DrawExportButton(ImVec2 tab_row, float tab_row_width, float export_width, const FrameResults &frame);
    bool CanExport(const FrameResults &frame) const;
    bool HasLongerRun() const;
    ExportFigure BuildShownFigure(const FrameResults &frame, bool is_whole_run, bool is_table) const;
};

} // namespace GUI

#endif // IMCSIM_OUTPUT_WINDOW_H
