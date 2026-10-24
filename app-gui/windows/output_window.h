/**
 * @file    output_window.h
 * @brief   Window that plots the transient, AC sweep and DC sweep results of the schematic.
 */

#ifndef IMCSIM_OUTPUT_WINDOW_H
#define IMCSIM_OUTPUT_WINDOW_H

#include "analysis_controls.h"
#include "implot.h"
#include "logic_trace.h"
#include "math_channels.h"
#include "playback.h"
#include "plot_export.h"
#include "plot_exporter.h"
#include "plot_types.h"
#include "point_window.h"
#include "schematic.h"
#include "simulator.h"
#include "trace_fit_state.h"
#include "windows/app_window.h"
#include <cstddef>
#include <optional>
#include <set>
#include <string>
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
    TRUTH_TABLE
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
 */
class OutputWindow : public AppWindow {
public:
    OutputWindow(Schematic &schematic, AnalysisControls &controls, TransientPlayback &playback);
    ~OutputWindow() override = default;

    void ShowTab(PlotTab tab, bool may_come_to_front = true);
    void DrawViewMenuItems();
    void RegisterSettingsHandler();

private:
    /**
     * @struct  FrameResults
     * @brief   What the tabs read in one frame: the math channels evaluated once over each result, their errors,
     *          and whether any trace is measured.
     */
    struct FrameResults {
        // The points of the transient in the window, and the steps of the same analysis cut to the same stretch of
        // time: the plot and its statistics only ever see these
        std::optional<Core::Transient> TransientWindow;
        std::optional<StepFamily<Core::Transient>> StepWindows;
        std::vector<MathTrace> TransientMath;
        std::vector<std::string> TransientErrors;
        // One list of channels per curve of the DC sweep
        std::vector<std::vector<MathTrace>> DCSweepMath;
        std::vector<std::string> DCSweepErrors;
        bool AnythingMeasured = false;
    };

    /**
     * @struct  FullMath
     * @brief   The math channels worked out over the whole run, and what they were worked out for.
     */
    struct FullMath {
        std::size_t DataVersion = 0;
        std::string Signature;
        std::vector<MathTrace> Traces;
        std::vector<std::string> Errors;
    };

    Schematic &m_Schematic;
    AnalysisControls &m_Controls;
    TransientPlayback &m_Playback;
    // What each plot showed last frame, so its axes fit for a new result and when its traces change
    TraceFitState m_TransientFit;
    TraceFitState m_ACSweepFit;
    TraceFitState m_DCSweepFit;
    // Cursors and visible X range of the transient, the Bode plots and the DC sweep; both Bode plots share theirs
    PlotCursors m_TransientCursors;
    PlotCursors m_ACSweepCursors;
    PlotCursors m_DCSweepCursors;
    PlotSpan m_TransientView;
    // The data version of the transient drawn last frame, to fit the axes as a run that is still going on grows
    std::size_t m_DrawnTransientDataVersion = 0;
    // The window the plot drew last frame, so a window that moved or changed size fits the axes
    PointWindow m_DrawnWindow;
    // Over the whole run, or none while it grows; the plot of a run in progress evaluates the window instead
    std::optional<FullMath> m_FullMath;
    // Whether the signals of logic parts get a lane each, in a plot of their own under the analog one
    bool m_ShowLogicLanes = true;
    // Whether each steady stretch of a lane is labeled 0, 1 or X
    bool m_ShowBitValues = false;
    PlotSpan m_ACSweepView{.From = 1.0, .To = 1e6};
    // Spectrum of the transient: its window and frequency scale, its visible range and the last spectra computed
    SpectrumWindow m_SpectrumWindow = SpectrumWindow::HANN;
    bool m_SpectrumLogarithmic = false;
    PlotSpan m_SpectrumView;
    std::optional<TransientSpectra> m_Spectra;
    bool m_SpectraChanged = false;
    MathChannels m_MathChannels;
    // The tab shown this frame, which Export saves
    PlotTab m_ShownTab = PlotTab::TRANSIENT;
    // Tab to bring to the front on the next frame, after a new result
    std::optional<PlotTab> m_TabToShow;
    // View preference, kept in imgui.ini: whether a new result brings the window to the front
    bool m_FrontOnNewResult = true;
    PlotSpan m_DCSweepView;
    // Noise: the result its axes were fitted to, its visible range and the parts whose share it plots
    TraceFitState m_NoiseFit;
    PlotSpan m_NoiseView;
    std::set<std::string> m_ShownNoiseContributions;
    std::vector<std::string> m_PlottedNoiseContributions;
    PlotExporter m_Exporter;

    void Draw() override;
    const std::vector<Core::ComponentTrace> &GetListedCurrents() const;
    std::size_t GetListedNodeCount() const;
    void UpdateFullMath();
    FrameResults EvaluateFrame(std::size_t node_count, const std::vector<Core::ComponentTrace> &currents) const;
    void DrawTraceList(std::size_t node_count, const std::vector<Core::ComponentTrace> &currents);

    // Tabs
    void DrawTabs(const FrameResults &frame);
    void DrawTransientTab(const FrameResults &frame);
    void DrawSpectrumTab(const FrameResults &frame);
    void DrawACSweepTab(const FrameResults &frame);
    void DrawDCSweepTab(const FrameResults &frame);
    void DrawNoiseTab();
    void DrawTransferFunctionTab();
    void DrawTruthTableTab();

    // Plots
    void DrawIncompleteNote(const Core::Transient &transient);
    void DrawLiveNote(const Core::Transient &transient);
    void DrawWindowControls(const Core::Transient &shown);
    void DrawWindowSize(ImVec2 row_start, float width);
    void DrawTransient(const Core::Transient &transient, const std::vector<MathTrace> &math,
                       const std::optional<StepFamily<Core::Transient>> &steps);
    void DrawTransientAnalog(const Core::Transient &transient, const std::vector<MathTrace> &math,
                             const std::optional<StepFamily<Core::Transient>> &steps,
                             const std::vector<int> &lane_nodes, bool show_currents, bool has_voltages, float height);
    void DrawLogicLanes(const Core::Transient &transient, const std::vector<LogicNode> &lanes, float height);
    std::vector<LogicNode> FindMeasuredLanes(std::size_t node_count) const;
    void DrawACSweep(const Core::ACSweep &sweep);
    void DrawDCSweep(const Core::DCSweep &sweep, const std::vector<std::vector<MathTrace>> &math);
    void DrawNoise(const Core::Noise &noise);
    void SetupNoiseAxes(const Core::Noise &noise, bool input_shown, const std::string &output_unit,
                        const std::string &input_unit);

    // Spectrum
    PlotSpan GetSpectrumSpan(const Core::Transient &transient) const;
    void UpdateSpectra(const Core::Transient &transient, const std::vector<MathTrace> &math);
    void DrawSpectrum();

    // Statistics
    void DrawTransientStatistics(const Core::Transient &transient, const std::vector<MathTrace> &math);
    void DrawSpectrumStatistics();
    void DrawSpectrumControls();
    void DrawACSweepStatistics(const Core::ACSweep &sweep);
    void DrawDCSweepStatistics(const Core::DCSweep &sweep, const std::vector<std::vector<MathTrace>> &math);
    void DrawNoiseStatistics(const Core::Noise &noise);

    // Export
    void DrawExportButton(ImVec2 tab_row, float tab_row_width, float export_width, const FrameResults &frame);
    bool CanExport(const FrameResults &frame) const;
    bool HasLongerRun() const;
    ExportFigure BuildShownTransientFigure(const FrameResults &frame, bool is_whole_run) const;
    ExportFigure BuildShownFigure(const FrameResults &frame, bool is_whole_run) const;
};

} // namespace GUI

#endif // IMCSIM_OUTPUT_WINDOW_H
