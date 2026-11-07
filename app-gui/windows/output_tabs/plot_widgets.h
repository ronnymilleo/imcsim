/**
 * @file    plot_widgets.h
 * @brief   What the tabs of the Output window share to draw their plots and their statistics panels.
 */

#ifndef IMCSIM_PLOT_WIDGETS_H
#define IMCSIM_PLOT_WIDGETS_H

#include "analysis/playback.h"
#include "document/schematic.h"
#include "imgui.h"
#include "implot.h"
#include "plots/math_channels.h"
#include "plots/plot_types.h"
#include "plots/statistics_table.h"
#include "simulation/simulation_results.h"
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

/// In font sizes, so the layout follows the DPI scale.
inline constexpr float MIN_PLOT_HEIGHT = 12.0f;
/// Math channels, and the noise at the output, are drawn with a heavier line than the traces they come from.
inline constexpr float MATH_LINE_WEIGHT = 2.0f;
/// A new result starts with empty plots until the user picks what to measure.
inline constexpr const char *NOTHING_MEASURED =
    "Nothing is measured yet: pick nodes and parts with Probe in the editor, or check traces in the list";

/**
 * @struct  PlottedTrace
 * @brief   A trace drawn in a plot, kept so the cursor readout can list its value at any sample.
 */
struct PlottedTrace {
    std::string Label;
    const std::vector<double> *Values;
    const char *Unit;
    ImU32 Color;
    /// Set for the lane of a logic signal, whose readout gives its logic state too.
    std::optional<double> LogicLevel = std::nullopt;
};

/**
 * @struct  StepTraces
 * @brief   The X values, node values and currents of one step of a stepped transient or AC sweep, as plotted.
 */
struct StepTraces {
    const std::vector<double> *Xs;
    const std::vector<std::vector<double>> *Nodes;
    const std::vector<Core::ComponentTrace> *Currents;
};

// Plots
int FormatAxisValue(double value, char *buffer, int size, void *unit);
bool BeginTracePlot(const char *id, ImVec2 size, bool has_legend = false);
float PlotHeight(int plot_count);
ImPlotAxisFlags VoltageAxisFlags(bool has_voltages, bool has_other_traces);
void SetNextFits(bool new_result, bool voltages_changed, bool currents_changed);

// Traces
void PlotNodes(const std::vector<double> &xs, const std::vector<std::vector<double>> &node_values, Schematic &schematic,
               const char *unit, const std::string &suffix, std::vector<PlottedTrace> &plotted,
               const std::vector<int> &excluded = {});
void PlotDashedLine(const std::string &label, const std::vector<double> &xs, const std::vector<double> &ys,
                    ImU32 color);
void PlotCurrents(const std::vector<double> &xs, const std::vector<Core::ComponentTrace> &currents,
                  const Schematic &schematic, const char *unit, const std::string &suffix,
                  std::vector<PlottedTrace> &plotted);
bool HasMeasuredVoltage(std::size_t node_count, const Schematic &schematic);
std::vector<int> ListMeasuredNodes(std::size_t node_count, const Schematic &schematic,
                                   const std::vector<int> &excluded = {});
std::vector<std::string> ListMeasuredCurrents(const std::vector<Core::ComponentTrace> &currents,
                                              const Schematic &schematic);
std::vector<PlottedTrace> ListTraces(const std::vector<std::vector<double>> &node_values,
                                     const std::vector<Core::ComponentTrace> &currents, Schematic &schematic,
                                     const char *voltage_unit, const char *current_unit);

// Steps of a stepped run
void PlotOtherSteps(const std::vector<StepTraces> &steps, std::size_t selected, const Schematic &schematic,
                    bool show_currents, const std::vector<int> &lane_nodes);
void LabelSteps(const std::vector<StepTraces> &steps, std::size_t selected, const std::vector<std::string> &labels,
                const Schematic &schematic, bool show_currents, const std::vector<int> &lane_nodes);
std::optional<std::size_t> DrawStepSelector(const std::string &parameter, const std::string &unit,
                                            std::span<const double> values, std::size_t selected);
std::vector<std::string> ListStepLabels(const std::string &parameter, const std::string &unit,
                                        std::span<const double> values);

// Math channels
void SetupMathAxes(const std::vector<MathTrace> &math, bool current_axis_ready, const std::string &third_unit);
void FitMathAxes(const std::vector<MathTrace> &math);
void PlotMath(const std::vector<double> &xs, const std::vector<MathTrace> &math, const std::string &suffix,
              std::vector<PlottedTrace> &plotted, const char *readout_unit = nullptr);
void DrawMathErrors(const std::vector<std::string> &errors);

// Cursors
void DrawCursorReadout(const std::vector<double> &xs, const char *x_unit, bool logarithmic,
                       const std::vector<PlottedTrace> &plotted);
void DrawCursors(PlotCursors &cursors, bool logarithmic);
PlotSpan MeasuredSpan(const PlotCursors &cursors, const PlotSpan &view);
float GetCursorToggleWidth();
void DrawCursorToggle(PlotCursors &cursors, float right_edge);
void DrawCursorCheckbox(PlotCursors &cursors);

// Playback
void DrawPlaybackBar(TransientPlayback &playback, double from, double to);

// Statistics
std::string FormatQuantity(double value, const char *unit);
std::string FormatFixed(double value, const char *unit);
std::string FormatOptionalQuantity(std::optional<double> value, const char *unit);
void DrawStatistic(const char *name, const std::string &value);
void DrawMeasuredValue(const char *name, double value, double scale, const char *unit);
bool BeginStatisticsTable(const std::string &id);
void DrawStatisticsHeader(const PlotCursors &cursors, const char *span_text,
                          const std::vector<std::pair<std::string, std::string>> &cursor_readings);
void DrawTraceTable(const char *id, const std::vector<PlottedTrace> &traces, const std::vector<StatisticsRow> &rows);
void DrawWithStatistics(const std::function<void()> &draw_plots, const std::function<void()> &draw_statistics);

// Poles and zeros
ImVec4 GetPoleZeroColor(bool is_pole, bool is_right_half_plane);
void DrawRootMarkers(const Core::PoleZero &result, bool has_labels);

// Lists
std::optional<bool> DrawAllNoneButtons(const char *id);

} // namespace GUI

#endif // IMCSIM_PLOT_WIDGETS_H
