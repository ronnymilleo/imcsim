/**
 * @file    ac_sweep_tab.cpp
 * @brief   The AC sweep tab of the Output window: the Bode plots of the measured traces and their response.
 */

#include "windows/output_tabs/ac_sweep_tab.h"

#include "implot.h"
#include "plots/plot_figures.h"
#include "plots/statistics_table.h"
#include "plots/trace_statistics.h"
#include "windows/output_tabs/plot_widgets.h"
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

/**
 * @brief   Creates the tab.
 * @param[in] schematic  Schematic whose AC sweep is plotted; it must outlive the tab.
 */
ACSweepTab::ACSweepTab(Schematic &schematic) : m_Schematic(schematic) {
}

/**
 * @brief   Draws the tab: the step picker of a stepped run, both Bode plots and the response of every shown trace.
 * @param[in] frame  Whether any trace is measured, and the math channels over the sweep with their errors.
 */
void ACSweepTab::Draw(const FrameResults &frame) {
    const auto &sweep = m_Schematic.GetACSweep();
    if (!sweep) {
        ImGui::TextDisabled("No AC sweep for the current circuit; pick it beside Run in the editor");
        return;
    }
    DrawWithStatistics(
        [&] {
            const float right_edge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s",
                                frame.AnythingMeasured || !frame.ACSweepMath.Magnitudes.empty()
                                    ? "Relative to the AC magnitude of the sources: a magnitude of 1 reads as gain"
                                    : NOTHING_MEASURED);
            DrawCursorToggle(m_Cursors, right_edge);
            DrawMathErrors(frame.ACSweepErrors);
            if (const auto &pole_zero = m_Schematic.GetPoleZero()) {
                ImGui::Checkbox("Show poles and zeros", &m_ShowPoleZero);
                ImGui::SetItemTooltip(
                    "Mark the natural frequency of each pole (p) and zero (z) of the last Poles and zeros "
                    "analysis with a dashed line; the colors are those of its s-plane");
            }
            if (const auto &steps = m_Schematic.GetACSweepSteps()) {
                if (const std::optional<std::size_t> picked =
                        DrawStepSelector(steps->Parameter, steps->Unit, steps->Values, steps->Selected)) {
                    m_Schematic.SelectACSweepStep(*picked);
                }
            }
            DrawPlots(*sweep, frame.ACSweepMath);
        },
        [&] { DrawStatistics(*sweep, frame.ACSweepMath); });
}

/**
 * @brief   Tells whether the tab has something to export.
 * @param[in] frame  Whether any trace is measured, and the math channels over the sweep.
 * @return  True with an AC sweep and a measured trace or a math channel.
 */
bool ACSweepTab::CanExport(const FrameResults &frame) const {
    return m_Schematic.GetACSweep() && (frame.AnythingMeasured || !frame.ACSweepMath.Magnitudes.empty());
}

/**
 * @brief   The figure of both Bode plots for the export, over the visible frequency range.
 * @param[in] frame  The math channels over the sweep.
 * @return  The figure, or an empty one without an AC sweep.
 */
ExportFigure ACSweepTab::BuildFigure(const FrameResults &frame) const {
    if (const std::optional<Core::ACSweep> &sweep = m_Schematic.GetACSweep()) {
        return BuildACSweepFigure(*sweep, frame.ACSweepMath, m_Schematic, m_View);
    }
    return {};
}

/**
 * @brief   Currents go on the secondary axis of both plots: their magnitudes are relative to 1 A instead of 1 V, and
 *          their phases follow so each current stays on the same side in both plots. Math channels in volts or amperes
 *          share those axes; any other unit, such as the ohms of an impedance, goes on a third.
 * @details Both plots share their frequency range and cursors, so zooming one zooms the other.
 */
void ACSweepTab::DrawPlots(const Core::ACSweep &sweep, const ACMathTraces &math) {
    std::vector<std::string> measured_currents = ListMeasuredCurrents(sweep.CurrentMagnitudesDecibels, m_Schematic);
    const bool shows_currents = !measured_currents.empty();
    const bool has_voltages =
        HasMeasuredVoltage(sweep.NodeMagnitudesDecibels.size(), m_Schematic) || UsesAxis(math.Magnitudes, ImAxis_Y1);
    const bool has_current_axis = shows_currents || UsesAxis(math.Magnitudes, ImAxis_Y2);
    const PlotFits fits{.IsNewResult = m_Fit.TakeNewResult(m_Schematic.GetACSweepVersion()),
                        .HaveVoltagesChanged =
                            m_Fit.TakeNodesChanged(ListMeasuredNodes(sweep.NodeMagnitudesDecibels.size(), m_Schematic)),
                        .HaveCurrentsChanged = m_Fit.TakeCurrentsChanged(std::move(measured_currents)),
                        .HasMathChanged = m_Fit.TakeMathChanged(ListMathLabels(math.Magnitudes)),
                        .ShowsCurrents = shows_currents,
                        .HasCurrentAxis = has_current_axis,
                        .VoltageAxisFlags =
                            VoltageAxisFlags(has_voltages, has_current_axis || UsesAxis(math.Magnitudes, ImAxis_Y3)),
                        .Height = PlotHeight(2)};
    const std::string third_unit = ThirdAxisUnit(math.Magnitudes);
    const std::string third_name = third_unit.empty() ? "Math" : third_unit;
    DrawPlot(sweep,
             {.Id = "Magnitude##ac",
              .VoltageAxis = "Voltage (dB)",
              .CurrentAxis = "Current (dB)",
              .ThirdAxis = std::format("{} (dB)", third_name),
              .Unit = " dB",
              .Nodes = &Core::ACSweep::NodeMagnitudesDecibels,
              .Currents = &Core::ACSweep::CurrentMagnitudesDecibels,
              .Math = &math.Magnitudes,
              .HasLabels = true},
             fits);
    DrawPlot(sweep,
             {.Id = "Phase##ac",
              .VoltageAxis = "Phase (deg)",
              .CurrentAxis = "Current phase (deg)",
              .ThirdAxis = std::format("{} phase (deg)", third_name),
              .Unit = " deg",
              .Nodes = &Core::ACSweep::NodePhasesDegrees,
              .Currents = &Core::ACSweep::CurrentPhasesDegrees,
              .Math = &math.Phases,
              .HasLabels = false},
             fits);
}

/**
 * @brief   One of the two plots: the other steps faint, the measured traces, the math channels, the poles and zeros
 *          and the cursors, on a logarithmic frequency axis.
 */
void ACSweepTab::DrawPlot(const Core::ACSweep &sweep, const BodePlot &plot, const PlotFits &fits) {
    SetNextFits(fits.IsNewResult, fits.HaveVoltagesChanged, fits.HaveCurrentsChanged);
    if (fits.HasMathChanged) {
        FitMathAxes(*plot.Math);
    }
    if (!BeginTracePlot(plot.Id, ImVec2(-1.0f, fits.Height))) {
        return;
    }
    ImPlot::SetupAxes("Frequency", plot.VoltageAxis, ImPlotAxisFlags_None, fits.VoltageAxisFlags);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
    ImPlot::SetupAxisLinks(ImAxis_X1, &m_View.From, &m_View.To);
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
    if (fits.HasCurrentAxis) {
        ImPlot::SetupAxis(ImAxis_Y2, plot.CurrentAxis, ImPlotAxisFlags_AuxDefault);
    }
    if (UsesAxis(*plot.Math, ImAxis_Y3)) {
        ImPlot::SetupAxis(ImAxis_Y3, plot.ThirdAxis.c_str(), ImPlotAxisFlags_AuxDefault);
    }
    const auto &steps = m_Schematic.GetACSweepSteps();
    std::vector<StepTraces> step_traces;
    if (steps) {
        for (const Core::ACSweep &step : steps->Results) {
            step_traces.push_back({&step.Frequencies, &(step.*plot.Nodes), &(step.*plot.Currents)});
        }
        PlotOtherSteps(step_traces, steps->Selected, m_Schematic, fits.ShowsCurrents, {});
    }
    std::vector<PlottedTrace> plotted;
    PlotNodes(sweep.Frequencies, sweep.*plot.Nodes, m_Schematic, plot.Unit, "", plotted);
    if (fits.ShowsCurrents) {
        PlotCurrents(sweep.Frequencies, sweep.*plot.Currents, m_Schematic, plot.Unit, "", plotted);
    }
    PlotMath(sweep.Frequencies, *plot.Math, "", plotted, plot.Unit);
    if (steps && plot.HasLabels) {
        LabelSteps(step_traces, steps->Selected, ListStepLabels(steps->Parameter, steps->Unit, steps->Values),
                   m_Schematic, fits.ShowsCurrents, {});
    }
    DrawRoots(plot.HasLabels);
    DrawCursors(m_Cursors, true);
    DrawCursorReadout(sweep.Frequencies, "Hz", true, plotted);
    ImPlot::EndPlot();
}

/**
 * @brief   Marks the poles and zeros with a dashed vertical line each, when the user asked and there is a result.
 * @param[in] has_labels  Whether the lines are labelled with their frequency, as on the magnitude plot.
 * @note    Call it inside a plot.
 */
void ACSweepTab::DrawRoots(const bool has_labels) {
    if (m_ShowPoleZero && m_Schematic.GetPoleZero()) {
        DrawRootMarkers(*m_Schematic.GetPoleZero(), has_labels);
    }
}

/**
 * @brief   Like the measurements of a network analyzer: peak, -3 dB band, unity gain and phase margin for every shown
 *          trace, always over the whole sweep, since they describe the response and a narrower span would move the peak
 *          that the -3 dB points refer to; the cursors only read the gain and phase at two frequencies.
 */
void ACSweepTab::DrawStatistics(const Core::ACSweep &sweep, const ACMathTraces &math) {
    const PlotCursors &cursors = m_Cursors;
    const CursorReadings cursor_readings = {
        {"A", FormatQuantity(cursors.A, "Hz")},
        {"B", FormatQuantity(cursors.B, "Hz")},
        {"B/A", cursors.A > 0.0 ? FormatFixed(std::log10(cursors.B / cursors.A), " dec") : "-"}};
    DrawStatisticsHeader("Response over the whole sweep");
    std::vector<PlottedTrace> magnitudes =
        ListTraces(sweep.NodeMagnitudesDecibels, sweep.CurrentMagnitudesDecibels, m_Schematic, " dB", " dB");
    std::vector<PlottedTrace> phases =
        ListTraces(sweep.NodePhasesDegrees, sweep.CurrentPhasesDegrees, m_Schematic, " deg", " deg");
    for (std::size_t index = 0; index < math.Magnitudes.size(); ++index) {
        const MathTrace &magnitude = math.Magnitudes[index];
        magnitudes.push_back({magnitude.Label, &magnitude.Values, " dB", magnitude.Color});
        phases.push_back({magnitude.Label, &math.Phases[index].Values, " deg", magnitude.Color});
    }
    if (magnitudes.empty()) {
        ImGui::TextDisabled("Measure a trace to see its statistics");
        return;
    }
    std::vector<BodeTrace> traces;
    for (std::size_t index = 0; index < magnitudes.size(); ++index) {
        traces.push_back({.MagnitudesDecibels = *magnitudes[index].Values, .PhasesDegrees = *phases[index].Values});
    }
    const std::vector<StatisticsRow> rows =
        BuildBodeStatistics(sweep.Frequencies, traces, cursors.Shown ? std::optional(cursors) : std::nullopt);
    DrawTraceTable("bode_statistics", magnitudes, rows, cursors.Shown ? cursor_readings : CursorReadings{});
}

} // namespace GUI
