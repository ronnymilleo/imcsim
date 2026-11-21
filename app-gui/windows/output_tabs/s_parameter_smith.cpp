/**
 * @file    s_parameter_smith.cpp
 * @brief   The Smith chart of the S-parameters tab: the grid, a curve per checked entry, the stability circles of a
 *          two-port and the reading of the point under the pointer.
 */

#include "windows/output_tabs/s_parameter_smith.h"

#include "implot.h"
#include "plots/port_matrix_view.h"
#include "plots/smith_chart.h"
#include "plots/stability_view.h"
#include "results/stability_circles.h"
#include "results/two_port_metrics.h"
#include "windows/output_tabs/s_parameter_widgets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace GUI {

namespace {

/// How near the pointer must be to a point of a curve of the Smith chart, in pixels, to read it.
constexpr float HOVER_DISTANCE = 16.0f;
/// Radius, in pixels, of the ring around the point being read and of the dot at the first frequency.
constexpr float READING_RING_RADIUS = 7.0f;
constexpr float START_MARKER_SIZE = 5.0f;
constexpr float GRID_ALPHA = 0.35f;
constexpr float OUTER_CIRCLE_ALPHA = 0.7f;
/// Distance, in pixels, of a label of the grid from the point it names.
constexpr float LABEL_OFFSET = 10.0f;
/// Room around the unit circle at first, and when a curve leaves it.
constexpr double SMITH_MARGIN = 1.12;
/// Room taken by the axis labels beside and under the chart, in font sizes, which the fit leaves out.
constexpr float PLOT_SIDE_MARGIN = 3.0f;
constexpr float PLOT_BOTTOM_MARGIN = 5.0f;
/// Opacity of the hatching of the unstable side of a circle, light so the grid and the curves show through.
constexpr float HATCH_ALPHA = 0.35f;
constexpr float CIRCLE_WEIGHT = 2.0f;
constexpr float HATCH_WEIGHT = 1.0f;

/**
 * @struct  GridLine
 * @brief   A curve of the Smith grid as the arrays ImPlot reads.
 */
struct GridLine {
    std::vector<double> Xs;
    std::vector<double> Ys;
    SmithCurve Curve;
};

/**
 * @brief   The grid never changes, so its curves are made once.
 */
const std::vector<GridLine> &GetGridLines() {
    static const std::vector<GridLine> lines = [] {
        std::vector<GridLine> built;
        for (SmithCurve &curve : BuildSmithGrid(SMITH_CURVE_SEGMENTS)) {
            GridLine line;
            for (const SmithPoint &point : curve.Points) {
                line.Xs.push_back(point.X);
                line.Ys.push_back(point.Y);
            }
            line.Curve = std::move(curve);
            built.push_back(std::move(line));
        }
        return built;
    }();
    return lines;
}

void PlotGrid() {
    const std::vector<GridLine> &lines = GetGridLines();
    for (std::size_t index = 0; index < lines.size(); ++index) {
        ImPlotSpec spec;
        spec.LineColor = ImVec4(0.55f, 0.55f, 0.55f, index == 0 ? OUTER_CIRCLE_ALPHA : GRID_ALPHA);
        spec.LineWeight = 1.0f;
        ImPlot::PlotLine(std::format("##smith_grid_{}", index).c_str(),
                         lines[index].Xs.data(),
                         lines[index].Ys.data(),
                         static_cast<int>(lines[index].Xs.size()),
                         spec);
    }
    const std::array<double, 2> real_axis_xs = {-1.0, 1.0};
    const std::array<double, 2> real_axis_ys = {0.0, 0.0};
    ImPlotSpec axis_spec;
    axis_spec.LineColor = ImVec4(0.55f, 0.55f, 0.55f, GRID_ALPHA);
    ImPlot::PlotLine("##smith_real_axis", real_axis_xs.data(), real_axis_ys.data(), 2, axis_spec);
    for (const GridLine &line : lines) {
        if (line.Curve.Label.empty()) {
            continue;
        }
        // A resistance label sits just above the axis where its circle crosses it; a reactance label sits outside the
        // outer circle, along the radius
        ImVec2 offset(0.0f, -LABEL_OFFSET);
        if (!line.Curve.IsResistance) {
            const auto radius = static_cast<float>(std::hypot(line.Curve.LabelPosition.X, line.Curve.LabelPosition.Y));
            offset = ImVec2(static_cast<float>(line.Curve.LabelPosition.X) / radius * LABEL_OFFSET,
                            -static_cast<float>(line.Curve.LabelPosition.Y) / radius * LABEL_OFFSET);
        }
        ImPlot::PlotText(line.Curve.Label.c_str(), line.Curve.LabelPosition.X, line.Curve.LabelPosition.Y, offset);
    }
}

/**
 * @brief   The farthest a curve is from the origin, which sets how much of the plane the chart shows.
 */
double FindLargestMagnitude(const Core::SParameters &result, const std::vector<SParameterEntry> &entries) {
    double largest = 1.0;
    for (const SParameterEntry &entry : entries) {
        for (const std::complex<double> value : ListValues(result, entry)) {
            largest = std::max(largest, std::abs(value));
        }
    }
    return largest;
}

/**
 * @brief   The width of the room for the chart over its height, less the axis labels.
 */
double GetChartAspect(const ImVec2 room) {
    const float font_size = ImGui::GetFontSize();
    return std::max(room.x - font_size * PLOT_SIDE_MARGIN, 1.0f)
         / std::max(room.y - font_size * PLOT_BOTTOM_MARGIN, 1.0f);
}

/**
 * @brief   Equal axes that show the whole chart and every curve: the shorter side shows the extent and the longer one
 *          shows more.
 * @param[in] extent      Half the side of the square the chart and the curves fit in.
 * @param[in] aspect      Width of the room over its height.
 * @param[in] is_fitting  Whether the axes fit this frame; otherwise they keep the user's zoom.
 */
void SetupSmithAxes(const double extent, const double aspect, const bool is_fitting) {
    constexpr ImPlotAxisFlags AXIS_FLAGS = ImPlotAxisFlags_NoGridLines;
    ImPlot::SetupAxes("Real", "Imaginary", AXIS_FLAGS, AXIS_FLAGS);
    ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_NoButtons);
    const ImPlotCond condition = is_fitting ? ImPlotCond_Always : ImPlotCond_Once;
    const double half_width = extent * std::max(1.0, aspect);
    ImPlot::SetupAxisLimits(ImAxis_X1, -half_width, half_width, condition);
    ImPlot::SetupAxisLimits(ImAxis_Y1, -extent, extent, condition);
}

/**
 * @brief   A curve per checked entry of S, with a dot at its first frequency.
 */
void PlotSmithCurves(const Core::SParameters &result, const std::vector<SParameterEntry> &entries) {
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const std::vector<std::complex<double>> values = ListValues(result, entries[index]);
        std::vector<double> xs;
        std::vector<double> ys;
        for (const std::complex<double> value : values) {
            xs.push_back(value.real());
            ys.push_back(value.imag());
        }
        ImPlotSpec spec;
        spec.LineColor = ToImVec4(GetEntryColor(index));
        spec.LineWeight = CURVE_WEIGHT;
        ImPlot::PlotLine(GetEntryName(entries[index]).c_str(), xs.data(), ys.data(), static_cast<int>(xs.size()), spec);
        ImPlotSpec start_spec = spec;
        start_spec.Marker = ImPlotMarker_Circle;
        start_spec.MarkerSize = START_MARKER_SIZE;
        start_spec.MarkerLineColor = spec.LineColor;
        start_spec.MarkerFillColor = spec.LineColor;
        ImPlot::PlotScatter(std::format("##start_{}", index).c_str(), xs.data(), ys.data(), 1, start_spec);
    }
}

/**
 * @brief   The source and load stability circles at one frequency, each with the hatching of its unstable side.
 * @details A boundary that does not cross the chart draws nothing; the note under the controls says why.
 * @param[in] result     The result shown, a two-port.
 * @param[in] frequency  Index of the frequency of the circles.
 */
void PlotStabilityCircles(const Core::SParameters &result, const std::size_t frequency) {
    const StabilityCircles circles = ComputeStabilityCircles(ReadTwoPortMatrix(result, frequency));
    const std::array<std::pair<const char *, const StabilityBoundary *>, 2> boundaries = {
        {{"Source stability circle", &circles.Source}, {"Load stability circle", &circles.Load}}};
    for (std::size_t index = 0; index < boundaries.size(); ++index) {
        const auto &[name, boundary] = boundaries[index];
        if (boundary->Shape == BoundaryShape::NONE || boundary->Coverage != ChartCoverage::PARTLY) {
            continue;
        }
        const ExportColor color = index == 0 ? SOURCE_CIRCLE_COLOR : LOAD_CIRCLE_COLOR;
        std::vector<double> hatch_xs;
        std::vector<double> hatch_ys;
        for (const HatchSegment &stroke : BuildUnstableHatch(*boundary)) {
            hatch_xs.insert(hatch_xs.end(), {stroke.From.real(), stroke.To.real()});
            hatch_ys.insert(hatch_ys.end(), {stroke.From.imag(), stroke.To.imag()});
        }
        ImPlotSpec hatch_spec;
        hatch_spec.LineColor = ToImVec4(color, HATCH_ALPHA);
        hatch_spec.LineWeight = HATCH_WEIGHT;
        hatch_spec.Flags = static_cast<int>(ImPlotItemFlags_NoLegend) | static_cast<int>(ImPlotItemFlags_NoFit)
                         | static_cast<int>(ImPlotLineFlags_Segments);
        ImPlot::PlotLine(std::format("##hatch_{}", index).c_str(),
                         hatch_xs.data(),
                         hatch_ys.data(),
                         static_cast<int>(hatch_xs.size()),
                         hatch_spec);
        for (const std::vector<std::complex<double>> &piece : SampleBoundary(*boundary)) {
            std::vector<double> xs;
            std::vector<double> ys;
            for (const std::complex<double> point : piece) {
                xs.push_back(point.real());
                ys.push_back(point.imag());
            }
            ImPlotSpec spec;
            spec.LineColor = ToImVec4(color);
            spec.LineWeight = CIRCLE_WEIGHT;
            spec.Flags = ImPlotItemFlags_NoFit;
            ImPlot::PlotLine(name, xs.data(), ys.data(), static_cast<int>(xs.size()), spec);
        }
    }
}

/**
 * @brief   Rings the point of a curve nearest to the pointer and reads it in a tooltip, when it is near enough.
 */
void ShowSmithReading(const Core::SParameters &result, const std::vector<SParameterEntry> &entries) {
    if (!ImPlot::IsPlotHovered()) {
        return;
    }
    const ImVec2 pointer = ImGui::GetMousePos();
    float nearest = HOVER_DISTANCE;
    std::optional<std::pair<std::size_t, std::size_t>> hovered;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const std::vector<std::complex<double>> values = ListValues(result, entries[index]);
        const std::size_t point =
            FindNearestReflection(values, {ImPlot::GetPlotMousePos().x, ImPlot::GetPlotMousePos().y});
        const ImVec2 mark = ImPlot::PlotToPixels(values[point].real(), values[point].imag());
        const float distance = std::hypot(mark.x - pointer.x, mark.y - pointer.y);
        if (distance < nearest) {
            nearest = distance;
            hovered = std::make_pair(index, point);
        }
    }
    if (!hovered) {
        return;
    }
    const std::complex<double> value =
        result.Get(hovered->second, entries[hovered->first].Row, entries[hovered->first].Column);
    ImPlot::GetPlotDrawList()->AddCircle(ImPlot::PlotToPixels(value.real(), value.imag()),
                                         READING_RING_RADIUS,
                                         ImGui::GetColorU32(ToImVec4(GetEntryColor(hovered->first))),
                                         0,
                                         CURVE_WEIGHT);
    ImGui::SetTooltip(
        "%s",
        DescribeMatrixReadings(result, hovered->second, {entries[hovered->first]}, SParameterQuantity::S).c_str());
}

} // namespace

/**
 * @brief   The checkbox of the stability circles of a two-port, with the frequency they are drawn at while they are
 *          shown.
 * @param[in]     result     The result shown.
 * @param[in,out] stability  Keeps the frequency of the circles.
 */
void SParameterSmithChart::DrawCircleControls(const Core::SParameters &result, SParameterStability &stability) {
    const bool is_two_port = IsTwoPort(result);
    ImGui::BeginDisabled(!is_two_port);
    ImGui::Checkbox("Stability circles", &m_AreCirclesShown);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s",
                          is_two_port ? "The source and load stability circles at one frequency; the hatching "
                                        "marks the terminations that make the two-port unstable"
                                      : DescribeNeedsTwoPorts(result, "Stability circles").c_str());
    }
    if (is_two_port && m_AreCirclesShown) {
        stability.DrawCirclePicker(result);
    }
}

/**
 * @brief   The Smith chart: the grid of resistance and reactance, a curve per checked entry with a dot at its first
 *          frequency, the stability circles when asked, and the readings of the point nearest to the pointer.
 * @param[in]     result      The result shown.
 * @param[in]     entries     The checked entries of S.
 * @param[in]     stability   The frequency of the circles, and mu there.
 * @param[in,out] is_fitting  Whether the axes fit to the curves this frame; a resized window sets it, and the chart
 *                            clears it once its axes are set up.
 */
void SParameterSmithChart::Draw(const Core::SParameters &result,
                                const std::vector<SParameterEntry> &entries,
                                const SParameterStability &stability,
                                bool &is_fitting) {
    // The room the plot gets, less the axis labels; the axes are equal, so the shorter side shows the extent and the
    // longer one shows more, and the whole chart fits whatever the shape of the window
    const ImVec2 room = ImGui::GetContentRegionAvail();
    if (TakeResized(room)) {
        is_fitting = true;
    }
    const double aspect = GetChartAspect(room);
    if (!ImPlot::BeginPlot("##smith_chart", ImVec2(-1.0f, -1.0f), ImPlotFlags_Equal)) {
        return;
    }
    SetupSmithAxes(std::max(SMITH_MARGIN, FindLargestMagnitude(result, entries) * 1.05), aspect, is_fitting);
    is_fitting = false;
    PlotItems(result, entries, stability);
    ImPlot::EndPlot();
}

/**
 * @brief   Tells whether the chart shows the stability circles, which the export draws too.
 * @return  True when the user asked for them; the result may still not be a two-port.
 */
bool SParameterSmithChart::AreCirclesShown() const {
    return m_AreCirclesShown;
}

/**
 * @brief   Tells whether the room for the chart changed size since its axes were last fitted, and keeps the new size.
 * @param[in] room  The room the chart gets this frame, in pixels.
 * @return  True when either side changed by more than a pixel.
 */
bool SParameterSmithChart::TakeResized(const ImVec2 room) {
    const bool is_resized = std::abs(room.x - m_FittedWidth) > 1.0f || std::abs(room.y - m_FittedHeight) > 1.0f;
    if (is_resized) {
        m_FittedWidth = room.x;
        m_FittedHeight = room.y;
    }
    return is_resized;
}

/**
 * @brief   The grid, the stability circles when they are shown, the curves and the reading under the pointer.
 */
void SParameterSmithChart::PlotItems(const Core::SParameters &result,
                                     const std::vector<SParameterEntry> &entries,
                                     const SParameterStability &stability) const {
    const bool shows_circles = m_AreCirclesShown && IsTwoPort(result);
    PlotGrid();
    if (shows_circles) {
        PlotStabilityCircles(result, stability.GetCircleFrequency());
    }
    PlotSmithCurves(result, entries);
    if (shows_circles) {
        stability.DrawCircleFrequencyLabel();
    }
    ShowSmithReading(result, entries);
}

} // namespace GUI
