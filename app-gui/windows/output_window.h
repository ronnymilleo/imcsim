/**
 * @file    output_window.h
 * @brief   Window that plots the transient, AC sweep and DC sweep results of the schematic.
 */

#ifndef IMCSIM_OUTPUT_WINDOW_H
#define IMCSIM_OUTPUT_WINDOW_H

#include "file_dialog.h"
#include "implot.h"
#include "logic_trace.h"
#include "playback.h"
#include "plot_export.h"
#include "schematic.h"
#include "simulator.h"
#include "spectrum.h"
#include "trace_math.h"
#include "windows/app_window.h"
#include <cstddef>
#include <expected>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  PlotCursors
 * @brief   The two vertical cursors of a plot, A and B, as on an oscilloscope.
 * @details They are placed in the visible range when turned on, and the user drags them from there.
 */
struct PlotCursors {
    bool Shown = false;
    bool Placed = false;
    double A = 0.0;
    double B = 0.0;
};

/**
 * @struct  PlotSpan
 * @brief   A range of the X axis of a plot, such as what it shows.
 */
struct PlotSpan {
    double From = 0.0;
    double To = 0.0;
};

/**
 * @struct  MathChannel
 * @brief   A trace computed from others, like the math channel of an oscilloscope.
 * @details Either an operation between two traces, picked with buttons, or a typed expression; both compile to
 *          the same program. Channels belong to the window for now and are not saved with the schematic.
 */
struct MathChannel {
    std::string Name;
    bool Shown = true;
    bool UsesExpression = false;
    std::string First;
    MathOperator Operator = MathOperator::Subtract;
    std::string Second;
    std::string Expression;
    // Compiled from the operation or the expression, whichever is in use
    std::expected<MathProgram, std::string> Program = std::unexpected("Not compiled yet");
};

/**
 * @struct  MathTrace
 * @brief   A math channel evaluated over one result, ready to plot and measure.
 */
struct MathTrace {
    std::string Label;
    std::vector<double> Values;
    std::string Unit;
    ImAxis Axis = ImAxis_Y1;
    ImU32 Color = 0;
};

/**
 * @struct  SpectrumTrace
 * @brief   The spectrum of one measured trace of the transient, in decibels, and its harmonics.
 */
struct SpectrumTrace {
    std::string Label;
    // Unit of the trace, such as "V"; the spectrum reads in decibels relative to 1 of it
    std::string Unit;
    ImAxis Axis = ImAxis_Y1;
    ImU32 Color = 0;
    bool Dashed = false;
    // No value when the trace is undefined somewhere in the span, as a math channel dividing by zero
    std::optional<std::vector<double>> Decibels;
    // Of the mean and every frequency, so a negligible mean reads as about zero
    double LargestAmplitude = 0.0;
    HarmonicAnalysis Harmonics;
};

/**
 * @struct  TransientSpectra
 * @brief   The spectra of the measured traces of a transient over one span, and what they were computed from.
 * @details Transforms take long enough to keep for as long as their inputs stay the same.
 */
struct TransientSpectra {
    std::size_t TransientVersion = 0;
    PlotSpan Span;
    SpectrumWindow Window = SpectrumWindow::Hann;
    std::vector<std::string> Labels;
    // From the first frequency above 0 Hz to half the sampling rate, shared by every trace
    std::vector<double> Frequencies;
    std::vector<SpectrumTrace> Traces;
};

/**
 * @enum    PlotTab
 * @brief   The tabs of the Output window, one per analysis, and the spectrum of the transient.
 */
enum class PlotTab {
    OperatingPoint,
    Transient,
    Spectrum,
    ACSweep,
    DCSweep,
    Noise
};

/**
 * @enum    ExportFormat
 * @brief   File types a plot is exported to: an SVG image of the plot, or a CSV table of its samples.
 */
enum class ExportFormat {
    SVG,
    CSV
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
 *          oscilloscope, the transient and the Bode plots have two cursors and a panel on their right that
 *          measures every shown trace: the transient between the cursors or over what is visible, the Bode plot
 *          over the whole sweep. The Spectrum tab transforms the measured traces of the transient over the span its
 *          statistics measure, and reads the fundamental, the harmonics and the THD of each, as a spectrum
 *          analyzer would. The Noise tab plots the noise density at the output and referred to the input on
 *          log-log axes, with the share of each part. Math channels add traces computed from the others, such as
 * V(1)-V(2) or V(1)*I(R1), to the transient and DC sweep plots: voltages and currents keep their axes, and any other
 *          unit, or none, goes on a third axis. Export saves the plot of the current tab as an SVG image, redrawn
 *          from its data at a chosen size and theme over the visible X range, or as a CSV table of every sample.
 *          It starts closed and opens on the tab of each new result. It also comes to the front then, unless the
 *          user turned that off in the View menu: a dot on its title or tab then tells a new result is waiting.
 */
class OutputWindow : public AppWindow {
public:
    OutputWindow(Schematic &schematic, TransientPlayback &playback);
    ~OutputWindow() override = default;

    void ShowTab(PlotTab tab);
    void DrawViewMenuItems();
    void RegisterSettingsHandler();

private:
    /**
     * @struct  FrameResults
     * @brief   What the tabs read in one frame: the math channels evaluated once over each result, their errors,
     *          and whether any trace is measured.
     */
    struct FrameResults {
        std::vector<MathTrace> TransientMath;
        std::vector<std::string> TransientErrors;
        // One list of channels per curve of the DC sweep
        std::vector<std::vector<MathTrace>> DCSweepMath;
        std::vector<std::string> DCSweepErrors;
        bool AnythingMeasured = false;
    };

    Schematic &m_Schematic;
    TransientPlayback &m_Playback;
    // Result versions the axes were last fitted to
    std::optional<std::size_t> m_FittedTransient;
    std::optional<std::size_t> m_FittedACSweep;
    std::optional<std::size_t> m_FittedDCSweep;
    // Nodes and currents each analysis plotted last frame; the axis of each fits whenever they change
    std::vector<int> m_TransientShownNodes;
    std::vector<int> m_ACSweepShownNodes;
    std::vector<int> m_DCSweepShownNodes;
    std::vector<std::string> m_TransientShownCurrents;
    std::vector<std::string> m_ACSweepShownCurrents;
    std::vector<std::string> m_DCSweepShownCurrents;
    // Cursors and visible X range of the transient and the Bode plots; both Bode plots share theirs
    PlotCursors m_TransientCursors;
    PlotCursors m_ACSweepCursors;
    PlotSpan m_TransientView;
    // Whether the signals of logic parts get a lane each, in a plot of their own under the analog one
    bool m_ShowLogicLanes = true;
    // Whether each steady stretch of a lane is labeled 0, 1 or X
    bool m_ShowBitValues = false;
    PlotSpan m_ACSweepView{.From = 1.0, .To = 1e6};
    // Spectrum of the transient: its window and frequency scale, its visible range and the last spectra computed
    SpectrumWindow m_SpectrumWindow = SpectrumWindow::Hann;
    bool m_SpectrumLogarithmic = false;
    PlotSpan m_SpectrumView;
    std::optional<TransientSpectra> m_Spectra;
    bool m_SpectraChanged = false;
    // Math channels, and the labels each analysis plotted last frame, so the axes fit when they change
    std::vector<MathChannel> m_MathChannels;
    int m_NextMathNumber = 1;
    std::vector<std::string> m_TransientShownMath;
    std::vector<std::string> m_DCSweepShownMath;
    // Export: the tab shown this frame, the image settings, and the figure waiting for the save dialog
    PlotTab m_ShownTab = PlotTab::Transient;
    // Tab to bring to the front on the next frame, after a new result
    std::optional<PlotTab> m_TabToShow;
    // View preference, kept in imgui.ini: whether a new result brings the window to the front
    bool m_FrontOnNewResult = true;
    PlotSpan m_DCSweepView;
    // Noise: the result its axes were fitted to, its visible range and the parts whose share it plots
    std::optional<std::size_t> m_FittedNoise;
    PlotSpan m_NoiseView;
    std::set<std::string> m_ShownNoiseContributions;
    std::vector<std::string> m_PlottedNoiseContributions;
    ExportStyle m_ExportStyle;
    FileDialog m_ExportDialog;
    std::optional<ExportFigure> m_PendingFigure;
    ExportFormat m_PendingFormat = ExportFormat::SVG;
    std::string m_ExportError;

    void Draw() override;
    const std::vector<Core::ComponentTrace> &GetListedCurrents() const;
    std::size_t GetListedNodeCount() const;
    FrameResults EvaluateFrame(std::size_t node_count, const std::vector<Core::ComponentTrace> &currents) const;
    void DrawTraceList(std::size_t node_count, const std::vector<Core::ComponentTrace> &currents);

    // Tabs
    void DrawTabs(const FrameResults &frame);
    void DrawTransientTab(const FrameResults &frame);
    void DrawSpectrumTab(const FrameResults &frame);
    void DrawACSweepTab(const FrameResults &frame);
    void DrawDCSweepTab(const FrameResults &frame);
    void DrawNoiseTab();

    // Plots
    void DrawTransient(const Core::Transient &transient, const std::vector<MathTrace> &math);
    void DrawTransientAnalog(const Core::Transient &transient, const std::vector<MathTrace> &math,
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
    void DrawNoiseStatistics(const Core::Noise &noise);

    // Math channels
    void DrawMathChannels(const std::vector<std::string> &operands);
    bool DrawMathEditor(MathChannel &channel, const std::vector<std::string> &operands);
    std::vector<MathTrace> EvaluateMath(std::span<const double> xs, Dimension x_unit,
                                        const std::vector<std::vector<double>> &node_voltages,
                                        const std::vector<Core::ComponentTrace> &currents,
                                        std::vector<std::string> &errors) const;

    // Export
    void DrawExportButton(ImVec2 tab_row, float tab_row_width, float export_width, const FrameResults &frame);
    bool CanExport(const FrameResults &frame) const;
    ExportFigure BuildShownFigure(const FrameResults &frame) const;
    std::optional<ExportFormat> DrawExportPopup(bool can_export);
    ExportFigure BuildTransientFigure(const Core::Transient &transient, const std::vector<MathTrace> &math) const;
    ExportFigure BuildSpectrumFigure() const;
    ExportFigure BuildACSweepFigure(const Core::ACSweep &sweep) const;
    ExportFigure BuildDCSweepFigure(const Core::DCSweep &sweep, const std::vector<std::vector<MathTrace>> &math) const;
    ExportFigure BuildNoiseFigure(const Core::Noise &noise) const;
    void StartExport(ExportFigure figure, ExportFormat format);
    void ProcessExportDialog();
    void DrawExportError();
};

} // namespace GUI

#endif // IMCSIM_OUTPUT_WINDOW_H
