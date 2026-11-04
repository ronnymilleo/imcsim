/**
 * @file    ac_sweep_tab.cpp
 * @brief   The AC sweep tab of the Output window: the Bode plots of the measured traces and their response.
 */

#include "windows/output_tabs/ac_sweep_tab.h"

#include "implot.h"
#include "plots/plot_figures.h"
#include "plots/trace_statistics.h"
#include "windows/output_tabs/plot_widgets.h"
#include <cmath>
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
 * @param[in] frame  Whether any trace is measured, and whether there are math channels, which an AC sweep leaves out.
 */
void ACSweepTab::Draw(const FrameResults &frame) {
    const auto &sweep = m_Schematic.GetACSweep();
    if (!sweep) {
        ImGui::TextDisabled("No AC sweep for the current circuit; pick it beside Run in the editor");
        return;
    }
    DrawWithStatistics(
        [&] {
            ImGui::TextDisabled("%s",
                                frame.AnythingMeasured
                                    ? "Relative to the AC magnitude of the sources: a magnitude of 1 reads as gain"
                                    : NOTHING_MEASURED);
            if (frame.HasMathChannels) {
                ImGui::TextDisabled("Math channels are not calculated for AC sweeps yet");
            }
            if (const auto &steps = m_Schematic.GetACSweepSteps()) {
                if (const std::optional<std::size_t> picked =
                        DrawStepSelector(steps->Parameter, steps->Unit, steps->Values, steps->Selected)) {
                    m_Schematic.SelectACSweepStep(*picked);
                }
            }
            DrawPlots(*sweep);
        },
        [&] { DrawStatistics(*sweep); });
}

/**
 * @brief   Tells whether the tab has something to export.
 * @param[in] frame  Whether any trace is measured.
 * @return  True with an AC sweep and a measured trace.
 */
bool ACSweepTab::CanExport(const FrameResults &frame) const {
    return m_Schematic.GetACSweep() && frame.AnythingMeasured;
}

/**
 * @brief   The figure of both Bode plots for the export, over the visible frequency range.
 * @return  The figure, or an empty one without an AC sweep.
 */
ExportFigure ACSweepTab::BuildFigure() const {
    if (const std::optional<Core::ACSweep> &sweep = m_Schematic.GetACSweep()) {
        return BuildACSweepFigure(*sweep, m_Schematic, m_View);
    }
    return {};
}

/**
 * @brief   Currents go on the secondary axis of both plots: their magnitudes are relative to 1 A instead of 1 V, and
 *          their phases follow so each current stays on the same side in both plots.
 * @details Both plots share their frequency range and cursors, so zooming one zooms the other.
 */
void ACSweepTab::DrawPlots(const Core::ACSweep &sweep) {
    const bool fit = m_Fit.TakeNewResult(m_Schematic.GetACSweepVersion());
    const auto &steps = m_Schematic.GetACSweepSteps();
    std::vector<std::string> measured_currents = ListMeasuredCurrents(sweep.CurrentMagnitudesDecibels, m_Schematic);
    const bool show_currents = !measured_currents.empty();
    const bool currents_changed = m_Fit.TakeCurrentsChanged(std::move(measured_currents));
    const bool voltages_changed =
        m_Fit.TakeNodesChanged(ListMeasuredNodes(sweep.NodeMagnitudesDecibels.size(), m_Schematic));
    const ImPlotAxisFlags voltage_axis_flags =
        VoltageAxisFlags(HasMeasuredVoltage(sweep.NodeMagnitudesDecibels.size(), m_Schematic), show_currents);
    const float height = PlotHeight(2);
    SetNextFits(fit, voltages_changed, currents_changed);
    if (BeginTracePlot("Magnitude##ac", ImVec2(-1.0f, height))) {
        ImPlot::SetupAxes("Frequency", "Voltage (dB)", ImPlotAxisFlags_None, voltage_axis_flags);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisLinks(ImAxis_X1, &m_View.From, &m_View.To);
        ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
        if (show_currents) {
            ImPlot::SetupAxis(ImAxis_Y2, "Current (dB)", ImPlotAxisFlags_AuxDefault);
        }
        std::vector<StepTraces> step_traces;
        if (steps) {
            for (const Core::ACSweep &step : steps->Results) {
                step_traces.push_back(
                    {&step.Frequencies, &step.NodeMagnitudesDecibels, &step.CurrentMagnitudesDecibels});
            }
            PlotOtherSteps(step_traces, steps->Selected, m_Schematic, show_currents, {});
        }
        std::vector<PlottedTrace> plotted;
        PlotNodes(sweep.Frequencies, sweep.NodeMagnitudesDecibels, m_Schematic, " dB", "", plotted);
        if (show_currents) {
            PlotCurrents(sweep.Frequencies, sweep.CurrentMagnitudesDecibels, m_Schematic, " dB", "", plotted);
        }
        if (steps) {
            LabelSteps(step_traces, steps->Selected, ListStepLabels(steps->Parameter, steps->Unit, steps->Values),
                       m_Schematic, show_currents, {});
        }
        DrawCursors(m_Cursors, true);
        DrawCursorReadout(sweep.Frequencies, "Hz", true, plotted);
        ImPlot::EndPlot();
    }
    SetNextFits(fit, voltages_changed, currents_changed);
    if (BeginTracePlot("Phase##ac", ImVec2(-1.0f, height))) {
        ImPlot::SetupAxes("Frequency", "Phase (deg)", ImPlotAxisFlags_None, voltage_axis_flags);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisLinks(ImAxis_X1, &m_View.From, &m_View.To);
        ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
        if (show_currents) {
            ImPlot::SetupAxis(ImAxis_Y2, "Current phase (deg)", ImPlotAxisFlags_AuxDefault);
        }
        if (steps) {
            std::vector<StepTraces> step_traces;
            for (const Core::ACSweep &step : steps->Results) {
                step_traces.push_back({&step.Frequencies, &step.NodePhasesDegrees, &step.CurrentPhasesDegrees});
            }
            PlotOtherSteps(step_traces, steps->Selected, m_Schematic, show_currents, {});
        }
        std::vector<PlottedTrace> plotted;
        PlotNodes(sweep.Frequencies, sweep.NodePhasesDegrees, m_Schematic, " deg", "", plotted);
        if (show_currents) {
            PlotCurrents(sweep.Frequencies, sweep.CurrentPhasesDegrees, m_Schematic, " deg", "", plotted);
        }
        DrawCursors(m_Cursors, true);
        DrawCursorReadout(sweep.Frequencies, "Hz", true, plotted);
        ImPlot::EndPlot();
    }
}

/**
 * @brief   Like the measurements of a network analyzer: peak, -3 dB band, unity gain and phase margin for every shown
 *          trace, always over the whole sweep, since they describe the response and a narrower span would move the peak
 *          that the -3 dB points refer to; the cursors only read the gain and phase at two frequencies.
 */
void ACSweepTab::DrawStatistics(const Core::ACSweep &sweep) {
    DrawCursorToggle(m_Cursors);
    const PlotCursors &cursors = m_Cursors;
    if (cursors.Shown && BeginStatisticsTable("cursors")) {
        DrawStatistic("A", FormatQuantity(cursors.A, "Hz"));
        DrawStatistic("B", FormatQuantity(cursors.B, "Hz"));
        DrawStatistic("B/A", cursors.A > 0.0 ? FormatFixed(std::log10(cursors.B / cursors.A), " dec") : "-");
        ImGui::EndTable();
    }
    const std::vector<PlottedTrace> magnitudes =
        ListTraces(sweep.NodeMagnitudesDecibels, sweep.CurrentMagnitudesDecibels, m_Schematic, " dB", " dB");
    const std::vector<PlottedTrace> phases =
        ListTraces(sweep.NodePhasesDegrees, sweep.CurrentPhasesDegrees, m_Schematic, " deg", " deg");
    if (magnitudes.empty()) {
        ImGui::TextDisabled("Measure a trace to see its statistics");
        return;
    }
    ImGui::TextDisabled("Response over the whole sweep");
    for (std::size_t index = 0; index < magnitudes.size(); ++index) {
        const PlottedTrace &magnitude = magnitudes[index];
        const std::vector<double> &phase = *phases[index].Values;
        ImGui::Separator();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(magnitude.Color), "%s", magnitude.Label.c_str());
        if (!BeginStatisticsTable(magnitude.Label)) {
            continue;
        }
        if (const auto statistics = ComputeBodeStatistics(sweep.Frequencies, *magnitude.Values, phase,
                                                          sweep.Frequencies.front(), sweep.Frequencies.back())) {
            DrawStatistic("Peak", FormatFixed(statistics->PeakGain, " dB"));
            DrawStatistic("at", FormatQuantity(statistics->PeakFrequency, "Hz"));
            DrawStatistic("-3 dB low", FormatOptionalQuantity(statistics->LowerCutoff, "Hz"));
            DrawStatistic("-3 dB high", FormatOptionalQuantity(statistics->UpperCutoff, "Hz"));
            DrawStatistic("0 dB at", FormatOptionalQuantity(statistics->UnityGainFrequency, "Hz"));
            DrawStatistic("Phase margin",
                          statistics->PhaseMargin ? FormatFixed(*statistics->PhaseMargin, " deg") : "-");
        }
        if (cursors.Shown) {
            const double gain_a = InterpolateAt(sweep.Frequencies, *magnitude.Values, cursors.A, true);
            const double gain_b = InterpolateAt(sweep.Frequencies, *magnitude.Values, cursors.B, true);
            const double phase_a = InterpolateAt(sweep.Frequencies, phase, cursors.A, true);
            const double phase_b = InterpolateAt(sweep.Frequencies, phase, cursors.B, true);
            DrawStatistic("Gain at A", FormatFixed(gain_a, " dB"));
            DrawStatistic("Gain at B", FormatFixed(gain_b, " dB"));
            DrawStatistic("Gain B-A", FormatFixed(gain_b - gain_a, " dB"));
            DrawStatistic("Phase at A", FormatFixed(phase_a, " deg"));
            DrawStatistic("Phase at B", FormatFixed(phase_b, " deg"));
            DrawStatistic("Phase B-A", FormatFixed(phase_b - phase_a, " deg"));
        }
        ImGui::EndTable();
    }
}

} // namespace GUI
