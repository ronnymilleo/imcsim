/**
 * @file    output_window.h
 * @brief   Window that plots the transient, AC sweep and DC sweep results of the schematic.
 */

#ifndef IMCSIM_OUTPUT_WINDOW_H
#define IMCSIM_OUTPUT_WINDOW_H

#include "file_dialog.h"
#include "implot.h"
#include "plot_export.h"
#include "schematic.h"
#include "simulator.h"
#include "trace_math.h"
#include "windows/app_window.h"
#include <cstddef>
#include <expected>
#include <optional>
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
 * @enum    PlotTab
 * @brief   The tabs of the Output window, one per analysis.
 */
enum class PlotTab {
    Transient,
    ACSweep,
    DCSweep
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
 * @details The results come from the Schematic, which the Simulation window fills, so the plots disappear as
 *          soon as the circuit changes. Plots show only the measured traces, picked with the Probe tool of the
 *          editor or in the list beside the plots; voltages take the colors the editor gives the nodes, and
 *          currents are dashed on the secondary Y axis on the right. Hovering a plot reads every shown trace at
 *          the nearest sample. The axes of each analysis fit its new results, and keep zoom and pan until the
 *          next one; the voltage and current axes also fit whenever their measured traces change. Like an
 *          oscilloscope, the transient and the Bode plots have two cursors and a panel on their right that
 *          measures every shown trace: the transient between the cursors or over what is visible, the Bode plot
 *          over the whole sweep. Math channels add traces computed from the others, such as V(1)-V(2) or
 *          V(1)*I(R1), to the transient and DC sweep plots: voltages and currents keep their axes, and any other
 *          unit, or none, goes on a third axis. Export saves the plot of the current tab as an SVG image, redrawn
 *          from its data at a chosen size and theme over the visible X range, or as a CSV table of every sample.
 *          The window floats instead of being docked, so the plots can be as large as needed.
 */
class OutputWindow : public AppWindow {
public:
    explicit OutputWindow(Schematic &schematic);
    ~OutputWindow() override = default;

private:
    Schematic &m_Schematic;
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
    PlotSpan m_ACSweepView{.From = 1.0, .To = 1e6};
    // Math channels, and the labels each analysis plotted last frame, so the axes fit when they change
    std::vector<MathChannel> m_MathChannels;
    int m_NextMathNumber = 1;
    std::vector<std::string> m_TransientShownMath;
    std::vector<std::string> m_DCSweepShownMath;
    // Export: the tab shown this frame, the image settings, and the figure waiting for the save dialog
    PlotTab m_ShownTab = PlotTab::Transient;
    PlotSpan m_DCSweepView;
    ExportStyle m_ExportStyle;
    FileDialog m_ExportDialog;
    std::optional<ExportFigure> m_PendingFigure;
    ExportFormat m_PendingFormat = ExportFormat::SVG;
    std::string m_ExportError;

    void Draw() override;
    void DrawTraceList(std::size_t node_count, const std::vector<Core::ComponentTrace> &currents);
    void DrawTransient(const Core::Transient &transient, const std::vector<MathTrace> &math);
    void DrawACSweep(const Core::ACSweep &sweep);
    void DrawDCSweep(const Core::DCSweep &sweep, const std::vector<std::vector<MathTrace>> &math);

    // Statistics
    void DrawTransientStatistics(const Core::Transient &transient, const std::vector<MathTrace> &math);
    void DrawACSweepStatistics(const Core::ACSweep &sweep);

    // Math channels
    void DrawMathChannels(const std::vector<std::string> &operands);
    bool DrawMathEditor(MathChannel &channel, const std::vector<std::string> &operands);
    std::vector<MathTrace> EvaluateMath(std::span<const double> xs, Dimension x_unit,
                                        const std::vector<std::vector<double>> &node_voltages,
                                        const std::vector<Core::ComponentTrace> &currents,
                                        std::vector<std::string> &errors) const;

    // Export
    std::optional<ExportFormat> DrawExportPopup(bool can_export);
    ExportFigure BuildTransientFigure(const Core::Transient &transient, const std::vector<MathTrace> &math) const;
    ExportFigure BuildACSweepFigure(const Core::ACSweep &sweep) const;
    ExportFigure BuildDCSweepFigure(const Core::DCSweep &sweep, const std::vector<std::vector<MathTrace>> &math) const;
    void StartExport(ExportFigure figure, ExportFormat format);
    void ProcessExportDialog();
    void DrawExportError();
};

} // namespace GUI

#endif // IMCSIM_OUTPUT_WINDOW_H
