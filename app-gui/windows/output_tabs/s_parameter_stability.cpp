/**
 * @file    s_parameter_stability.cpp
 * @brief   The stability view of the S-parameters tab: K and mu, the gain bound, and the frequency of the stability
 *          circles of a two-port.
 */

#include "windows/output_tabs/s_parameter_stability.h"

#include "imgui.h"
#include "implot.h"
#include "plots/s_parameter_view.h"
#include "results/stability_circles.h"
#include "results/stability_summary.h"
#include "spice/spice_value.h"
#include "windows/output_tabs/plot_widgets.h"
#include "windows/output_tabs/s_parameter_widgets.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace GUI {

namespace {

/// Width of the slider of the frequency of the stability circles, in font sizes.
constexpr float CIRCLE_SLIDER_WIDTH = 14.0f;
/// The line at 1 and the line at the frequency of the circles are lighter than the curves.
constexpr float MARKER_LINE_WEIGHT = 1.5f;
/// Distance, in pixels, of the label of the circle frequency from the corner of the Smith chart.
constexpr float LABEL_OFFSET = 10.0f;

} // namespace

/**
 * @brief   Works out the metrics of a result that has just arrived, and puts the circles where mu is lowest, the worst
 *          case.
 * @param[in] result  The new result.
 */
void SParameterStability::StartResult(const Core::SParameters &result) {
    m_Sweep = ComputeTwoPortSweep(result);
    m_Curves = BuildStabilityCurves(m_Sweep);
    m_Summary = DescribeStability(m_Sweep);
    m_CircleFrequency = FindLowestMu(m_Sweep).value_or(0);
}

/**
 * @brief   The factors K and mu over the gain bound, sharing the frequency axis.
 * @param[in,out] view        The frequency range the plots of the tab share.
 * @param[in]     is_fitting  Whether the axes fit to the curves this frame.
 */
void SParameterStability::Draw(PlotSpan &view, const bool is_fitting) {
    const float height = PlotHeight(2);
    DrawFactorPlot(view, is_fitting, height);
    DrawGainPlot(view, is_fitting, height);
}

/**
 * @brief   The sentence that says where the two-port can oscillate.
 * @return  The sentence; empty unless the result is a two-port.
 */
const std::string &SParameterStability::GetSummary() const {
    return m_Summary;
}

/**
 * @brief   The frequency the Smith chart draws the stability circles at.
 * @return  Its index in the sweep.
 */
std::size_t SParameterStability::GetCircleFrequency() const {
    return m_CircleFrequency;
}

/**
 * @brief   The frequency the circles are drawn at, the button that takes them to the worst case, and the note that says
 *          when a circle leaves the whole chart stable or unstable.
 * @param[in] result  The result shown, a two-port.
 */
void SParameterStability::DrawCirclePicker(const Core::SParameters &result) {
    m_CircleFrequency = std::min(m_CircleFrequency, result.Frequencies.size() - 1);
    ImGui::SameLine();
    ImGui::TextUnformatted("Circles at");
    ImGui::SameLine();
    ImGui::BeginDisabled(result.Frequencies.size() < 2);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * CIRCLE_SLIDER_WIDTH);
    int index = static_cast<int>(m_CircleFrequency);
    const std::string frequency = Core::FormatValue(result.Frequencies[m_CircleFrequency]) + "Hz";
    if (ImGui::SliderInt("##circles_at",
                         &index,
                         0,
                         static_cast<int>(result.Frequencies.size()) - 1,
                         frequency.c_str(),
                         ImGuiSliderFlags_AlwaysClamp)) {
        m_CircleFrequency = static_cast<std::size_t>(index);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("The frequency of the circles, one point of the sweep at a time");
    ImGui::SameLine();
    if (ImGui::SmallButton("Worst case")) {
        m_CircleFrequency = FindLowestMu(m_Sweep).value_or(0);
    }
    ImGui::SetItemTooltip("Goes to the frequency where mu is lowest, which is where the two-port is the least stable");
    const std::string note = DescribeCircles(ComputeStabilityCircles(ReadTwoPortMatrix(result, m_CircleFrequency)),
                                             result.Frequencies[m_CircleFrequency]);
    if (!note.empty()) {
        ShowWrapped(note);
    }
}

/**
 * @brief   Writes in the corner of the Smith chart the frequency the stability circles are drawn at and mu there.
 * @note    Call it inside the plot of the Smith chart.
 */
void SParameterStability::DrawCircleFrequencyLabel() const {
    const std::size_t frequency = std::min(m_CircleFrequency, m_Sweep.Frequencies.size() - 1);
    const std::string text = DescribeCircleFrequency(m_Sweep.Metrics[frequency], m_Sweep.Frequencies[frequency]);
    const ImVec2 corner = ImPlot::GetPlotPos();
    ImPlot::GetPlotDrawList()->AddText(ImVec2(corner.x + LABEL_OFFSET, corner.y + LABEL_OFFSET),
                                       ImGui::GetColorU32(ImGuiCol_Text),
                                       text.c_str());
}

/**
 * @brief   K and mu against frequency, with the line at 1 above which the two-port is unconditionally stable.
 * @details Hovering reads the factors and the gain at the nearest frequency.
 */
void SParameterStability::DrawFactorPlot(PlotSpan &view, const bool is_fitting, const float height) {
    if (is_fitting) {
        ImPlot::SetNextAxisToFit(ImAxis_X1);
    }
    if (!BeginTracePlot("Stability##s", ImVec2(-1.0f, height), true)) {
        return;
    }
    ImPlot::SetupAxes("Frequency", "K and mu");
    SetupLogFrequencyAxis(&view);
    ImPlot::SetupAxisLimits(ImAxis_Y1,
                            m_Curves.FactorRange.From,
                            m_Curves.FactorRange.To,
                            is_fitting ? ImPlotCond_Always : ImPlotCond_Once);
    const std::size_t count = m_Curves.Frequencies.size();
    ImPlotSpec k_spec;
    k_spec.LineColor = ToImVec4(STABILITY_K_COLOR);
    k_spec.LineWeight = CURVE_WEIGHT;
    ImPlot::PlotLine("K", m_Curves.Frequencies.data(), m_Curves.K.data(), static_cast<int>(count), k_spec);
    ImPlotSpec mu_spec;
    mu_spec.LineColor = ToImVec4(STABILITY_MU_COLOR);
    mu_spec.LineWeight = CURVE_WEIGHT;
    ImPlot::PlotLine("mu", m_Curves.Frequencies.data(), m_Curves.Mu.data(), static_cast<int>(count), mu_spec);
    constexpr double STABILITY_LIMIT = 1.0;
    ImPlotSpec limit_spec;
    limit_spec.LineColor = ToImVec4(STABILITY_LINE_AT_ONE_COLOR);
    limit_spec.LineWeight = MARKER_LINE_WEIGHT;
    limit_spec.Flags = ImPlotInfLinesFlags_Horizontal;
    ImPlot::PlotInfLines("1, stable above", &STABILITY_LIMIT, 1, limit_spec);
    if (const std::optional<std::size_t> picked = PickCircleFrequencyByClick()) {
        m_CircleFrequency = *picked;
    }
    PlotCircleFrequencyLine();
    DrawMetricsTooltip();
    ImPlot::EndPlot();
}

/**
 * @brief   The gain bound in decibels, as one curve in two kinds: MAG where the two-port is unconditionally stable and
 *          MSG, dashed in the export and in another color here, where it is not.
 */
void SParameterStability::DrawGainPlot(PlotSpan &view, const bool is_fitting, const float height) {
    if (is_fitting) {
        ImPlot::SetNextAxesToFit();
    }
    if (!BeginTracePlot("Gain##s", ImVec2(-1.0f, height), true)) {
        return;
    }
    ImPlot::SetupAxes("Frequency", "Gain (dB)");
    SetupLogFrequencyAxis(&view);
    const std::size_t count = m_Curves.Frequencies.size();
    const auto has_value = [](const std::vector<double> &values) {
        return std::ranges::any_of(values, [](const double value) { return std::isfinite(value); });
    };
    if (has_value(m_Curves.MaximumAvailableGainDecibels)) {
        ImPlotSpec spec;
        spec.LineColor = ToImVec4(STABILITY_MAG_COLOR);
        spec.LineWeight = CURVE_WEIGHT;
        ImPlot::PlotLine(m_Curves.MaximumAvailableGainLabel.c_str(),
                         m_Curves.Frequencies.data(),
                         m_Curves.MaximumAvailableGainDecibels.data(),
                         static_cast<int>(count),
                         spec);
    }
    if (has_value(m_Curves.MaximumStableGainDecibels)) {
        ImPlotSpec spec;
        spec.LineColor = ToImVec4(STABILITY_MSG_COLOR);
        spec.LineWeight = CURVE_WEIGHT;
        ImPlot::PlotLine(m_Curves.MaximumStableGainLabel.c_str(),
                         m_Curves.Frequencies.data(),
                         m_Curves.MaximumStableGainDecibels.data(),
                         static_cast<int>(count),
                         spec);
    }
    if (const std::optional<std::size_t> picked = PickCircleFrequencyByClick()) {
        m_CircleFrequency = *picked;
    }
    PlotCircleFrequencyLine();
    DrawMetricsTooltip();
    ImPlot::EndPlot();
}

/**
 * @brief   Reads a click on the plot being built, which moves the stability circles to the swept frequency nearest to
 *          the pointer.
 * @details The factor and gain plots share this, so the Smith chart circles follow whichever one is clicked; only the
 *          hovered plot reads the click. It moves the circles even while they are hidden, so they are already there
 *          when the checkbox is turned on.
 * @return  The index of the frequency clicked, or none without a click on this plot.
 * @note    Call it between BeginPlot and EndPlot.
 */
std::optional<std::size_t> SParameterStability::PickCircleFrequencyByClick() const {
    if (m_Curves.Frequencies.empty() || !ImPlot::IsPlotHovered()) {
        return std::nullopt;
    }
    // A press also starts a pan, so only a release that did not drag counts as a click
    const ImGuiIO &io = ImGui::GetIO();
    const bool is_click =
        ImGui::IsMouseReleased(ImGuiMouseButton_Left)
        && io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] <= io.MouseDragThreshold * io.MouseDragThreshold;
    if (!is_click) {
        return std::nullopt;
    }
    return FindNearestFrequency(m_Curves.Frequencies, ImPlot::GetPlotMousePos().x);
}

/**
 * @brief   Draws a vertical line at the frequency of the stability circles on the plot being built.
 * @note    Call it between BeginPlot and EndPlot.
 */
void SParameterStability::PlotCircleFrequencyLine() const {
    if (m_Curves.Frequencies.empty()) {
        return;
    }
    const double circle_frequency = m_Curves.Frequencies[std::min(m_CircleFrequency, m_Curves.Frequencies.size() - 1)];
    ImPlotSpec spec;
    spec.LineColor = ToImVec4(SOURCE_CIRCLE_COLOR);
    spec.LineWeight = MARKER_LINE_WEIGHT;
    ImPlot::PlotInfLines("Circles at", &circle_frequency, 1, spec);
}

/**
 * @brief   Reads the factors and the gain at the frequency nearest to the pointer, while it hovers the plot.
 * @note    Call it between BeginPlot and EndPlot.
 */
void SParameterStability::DrawMetricsTooltip() const {
    if (!ImPlot::IsPlotHovered()) {
        return;
    }
    const std::size_t frequency = FindNearestFrequency(m_Curves.Frequencies, ImPlot::GetPlotMousePos().x);
    std::string text;
    for (const std::string &line : DescribeMetrics(m_Sweep.Metrics[frequency], m_Sweep.Frequencies[frequency])) {
        text += (text.empty() ? "" : "\n") + line;
    }
    ImGui::SetTooltip("%s", text.c_str());
}

} // namespace GUI
