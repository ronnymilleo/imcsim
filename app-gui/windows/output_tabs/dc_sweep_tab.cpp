/**
 * @file    dc_sweep_tab.cpp
 * @brief   The DC sweep tab of the Output window: the measured traces against the swept source, and their statistics.
 */

#include "windows/output_tabs/dc_sweep_tab.h"

#include "analysis/parameter_step.h"
#include "implot.h"
#include "plots/plot_figures.h"
#include "plots/plot_helpers.h"
#include "plots/trace_statistics.h"
#include "spice/spice_value.h"
#include "windows/output_tabs/plot_widgets.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace GUI {

namespace {

/**
 * @brief   Labels every curve of each shown trace with the value of the stepped source, so the curves of a family tell
 *          apart.
 * @details The labels of a trace go where its curves are furthest apart, which is the end for most traces but the peak
 *          for a derivative, whose curves all end near zero. Each label sits on the axis of its curve.
 */
void LabelCurves(const Core::DCSweep &sweep, Schematic &schematic, const bool show_currents,
                 const std::vector<std::vector<MathTrace>> &math) {
    const ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
    for (const CurveFamily &family : ListCurveFamilies(sweep, schematic, show_currents, math)) {
        const std::size_t sample = FindWidestSpread(family.Curves);
        if (sample >= sweep.SweptValues.size() || !CurvesDiffer(family.Curves, sample)) {
            continue;
        }
        ImPlot::SetAxes(ImAxis_X1, family.Axis);
        // Labels closer than a line of text to one already drawn are skipped, so none is drawn over another
        std::vector<float> drawn;
        for (std::size_t curve = 0; curve < family.Curves.size(); ++curve) {
            const double y = (*family.Curves[curve])[sample];
            if (!std::isfinite(y)) {
                continue;
            }
            const float pixel_y = ImPlot::PlotToPixels(sweep.SweptValues[sample], y).y;
            if (std::ranges::any_of(
                    drawn, [pixel_y](const float other) { return std::abs(other - pixel_y) < ImGui::GetFontSize(); })) {
                continue;
            }
            drawn.push_back(pixel_y);
            ImPlot::Annotation(sweep.SweptValues[sample], y, color, ImVec2(4.0f, 0.0f), true, "%s",
                               FormatStepLabel(sweep, sweep.Curves[curve]).c_str());
        }
    }
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
}

} // namespace

/**
 * @brief   Creates the tab.
 * @param[in] schematic  Schematic whose DC sweep is plotted; it must outlive the tab.
 */
DCSweepTab::DCSweepTab(Schematic &schematic) : m_Schematic(schematic) {
}

/**
 * @brief   Draws the tab: the curves of the sweep and their statistics.
 * @param[in] frame  The math channels of each curve, and whether any trace is measured.
 */
void DCSweepTab::Draw(const FrameResults &frame) {
    const auto &dc_sweep = m_Schematic.GetDCSweep();
    if (!dc_sweep) {
        ImGui::TextDisabled("No DC sweep for the current circuit; pick it beside Run in the editor");
        return;
    }
    DrawWithStatistics(
        [&] {
            if (!frame.AnythingMeasured && (frame.DCSweepMath.empty() || frame.DCSweepMath.front().empty())) {
                ImGui::TextDisabled("%s", NOTHING_MEASURED);
            }
            DrawMathErrors(frame.DCSweepErrors);
            DrawPlot(*dc_sweep, frame.DCSweepMath);
        },
        [&] { DrawStatistics(*dc_sweep, frame.DCSweepMath); });
}

/**
 * @brief   Tells whether the tab has something to export.
 * @param[in] frame  The math channels of each curve, and whether any trace is measured.
 * @return  True with a DC sweep and a measured trace or a math channel.
 */
bool DCSweepTab::CanExport(const FrameResults &frame) const {
    return m_Schematic.GetDCSweep() &&
           (frame.AnythingMeasured || (!frame.DCSweepMath.empty() && !frame.DCSweepMath.front().empty()));
}

/**
 * @brief   The figure of the sweep for the export, over the visible range of the swept source.
 * @param[in] frame  The math channels of each curve.
 * @return  The figure, or an empty one without a DC sweep.
 */
ExportFigure DCSweepTab::BuildFigure(const FrameResults &frame) const {
    if (const std::optional<Core::DCSweep> &sweep = m_Schematic.GetDCSweep()) {
        return BuildDCSweepFigure(*sweep, frame.DCSweepMath, m_Schematic, m_View);
    }
    return {};
}

/**
 * @brief   Every curve of a family plots under the same label, so a trace keeps one color and one legend entry.
 */
void DCSweepTab::DrawPlot(const Core::DCSweep &sweep, const std::vector<std::vector<MathTrace>> &math) {
    static const std::vector<MathTrace> no_math;
    const std::vector<MathTrace> &first_math = math.empty() ? no_math : math.front();
    const Core::DCSweepCurve &first_curve = sweep.Curves.front();
    std::vector<std::string> measured_currents = ListMeasuredCurrents(first_curve.Currents, m_Schematic);
    const bool show_currents = !measured_currents.empty();
    const bool currents_changed = m_Fit.TakeCurrentsChanged(std::move(measured_currents));
    const bool voltages_changed =
        m_Fit.TakeNodesChanged(ListMeasuredNodes(first_curve.NodeVoltages.size(), m_Schematic));
    SetNextFits(m_Fit.TakeNewResult(m_Schematic.GetDCSweepVersion()), voltages_changed, currents_changed);
    if (m_Fit.TakeMathChanged(ListMathLabels(first_math))) {
        FitMathAxes(first_math);
    }
    const bool stepped = !sweep.SteppedSource.empty();
    if (stepped) {
        const std::string note =
            sweep.SteppedSource == MONTE_CARLO_PARAMETER
                ? std::string("One curve per Monte Carlo run, the nominal one first, labeled where "
                              "the curves part most")
                : std::format("One curve per value of {}, labeled where the curves part most", sweep.SteppedSource);
        ImGui::TextDisabled("%s", note.c_str());
    }
    if (!BeginTracePlot("##dc", ImVec2(-1.0f, PlotHeight(1)))) {
        return;
    }
    const bool has_voltages =
        HasMeasuredVoltage(first_curve.NodeVoltages.size(), m_Schematic) || UsesAxis(first_math, ImAxis_Y1);
    ImPlot::SetupAxes(sweep.SweptSource.c_str(), "Voltage", ImPlotAxisFlags_None,
                      VoltageAxisFlags(has_voltages, show_currents || !first_math.empty()));
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>(sweep.SweptUnit.c_str()));
    ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>("V"));
    if (show_currents) {
        ImPlot::SetupAxis(ImAxis_Y2, "Current", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisFormat(ImAxis_Y2, FormatAxisValue, const_cast<char *>("A"));
    }
    const std::string third_unit = ThirdAxisUnit(first_math);
    SetupMathAxes(first_math, show_currents, third_unit);
    std::vector<PlottedTrace> plotted;
    for (std::size_t curve_index = 0; curve_index < sweep.Curves.size(); ++curve_index) {
        const Core::DCSweepCurve &curve = sweep.Curves[curve_index];
        const std::string suffix = stepped ? " at " + FormatStepLabel(sweep, curve) : "";
        PlotNodes(sweep.SweptValues, curve.NodeVoltages, m_Schematic, "V", suffix, plotted);
        if (show_currents) {
            PlotCurrents(sweep.SweptValues, curve.Currents, m_Schematic, "A", suffix, plotted);
        }
        if (curve_index < math.size()) {
            PlotMath(sweep.SweptValues, math[curve_index], suffix, plotted);
        }
    }
    if (stepped) {
        LabelCurves(sweep, m_Schematic, show_currents, math);
    }
    DrawCursors(m_Cursors, false);
    DrawCursorReadout(sweep.SweptValues, sweep.SweptUnit.c_str(), false, plotted);
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    m_View = {.From = view.Min, .To = view.Max};
    ImPlot::EndPlot();
}

/**
 * @brief   Like the measurements of a curve tracer: for every shown trace, its value at each cursor with the change and
 *          slope between them, and its extremes and mean over the span.
 * @details A stepped sweep lists every curve, named by its step value.
 */
void DCSweepTab::DrawStatistics(const Core::DCSweep &sweep, const std::vector<std::vector<MathTrace>> &math) {
    DrawCursorToggle(m_Cursors);
    const PlotCursors &cursors = m_Cursors;
    if (cursors.Shown && BeginStatisticsTable("cursors")) {
        DrawStatistic("A", FormatQuantity(cursors.A, sweep.SweptUnit.c_str()));
        DrawStatistic("B", FormatQuantity(cursors.B, sweep.SweptUnit.c_str()));
        DrawStatistic("B-A", FormatQuantity(cursors.B - cursors.A, sweep.SweptUnit.c_str()));
        ImGui::EndTable();
    }
    const bool show_currents = HasMeasuredCurrent(sweep.Curves.front().Currents, m_Schematic);
    const std::vector<CurveFamily> families = ListCurveFamilies(sweep, m_Schematic, show_currents, math);
    if (families.empty()) {
        ImGui::TextDisabled("Measure a trace to see its statistics");
        return;
    }
    ImGui::TextDisabled("%s", cursors.Shown ? "Between the cursors" : "Over the visible sweep");
    const PlotSpan span = MeasuredSpan(cursors, m_View);
    const bool stepped = !sweep.SteppedSource.empty();
    for (const CurveFamily &family : families) {
        ImGui::Separator();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(family.Color), "%s", family.Label.c_str());
        for (std::size_t curve = 0; curve < family.Curves.size(); ++curve) {
            if (stepped) {
                ImGui::TextDisabled("%s", FormatStepLabel(sweep, sweep.Curves[curve]).c_str());
            }
            const std::vector<double> &values = *family.Curves[curve];
            if (!BeginStatisticsTable(std::format("{} {}", family.Label, curve))) {
                continue;
            }
            // The size of the trace in the span tells residue from real values in every readout
            const auto statistics = ComputeSweepStatistics(sweep.SweptValues, values, span.From, span.To);
            const double scale =
                statistics ? std::max(std::abs(statistics->Maximum), std::abs(statistics->Minimum)) : 0.0;
            if (cursors.Shown) {
                const double at_a = InterpolateAt(sweep.SweptValues, values, cursors.A, false);
                const double at_b = InterpolateAt(sweep.SweptValues, values, cursors.B, false);
                DrawMeasuredValue("At A", at_a, scale, family.Unit);
                DrawMeasuredValue("At B", at_b, scale, family.Unit);
                DrawMeasuredValue("B-A", at_b - at_a, scale, family.Unit);
                if (const std::optional<double> slope = ComputeSlope(sweep.SweptValues, values, cursors.A, cursors.B)) {
                    DrawStatistic("Slope",
                                  std::format("{}{}/{}", Core::FormatValue(*slope), family.Unit, sweep.SweptUnit));
                    // A conductance, as a current over a swept voltage, is also a resistance, the inverse; a zero
                    // slope is an open path, shown as such rather than as an infinite value
                    if (std::string_view(family.Unit) == "A" && sweep.SweptUnit == "V") {
                        const std::optional<double> resistance = ResistanceFromConductance(*slope);
                        const bool open = !resistance || std::abs(*resistance) >= OPEN_RESISTANCE;
                        DrawStatistic("Resistance", open ? "open" : FormatQuantity(*resistance, "Ohm"));
                    }
                }
            }
            if (statistics) {
                DrawMeasuredValue("Min", statistics->Minimum, scale, family.Unit);
                DrawMeasuredValue("Max", statistics->Maximum, scale, family.Unit);
                DrawMeasuredValue("Mean", statistics->Mean, scale, family.Unit);
            }
            ImGui::EndTable();
        }
    }
}

} // namespace GUI
