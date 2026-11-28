/**
 * @file    complex_plane_plot.cpp
 * @brief   Interactive ImPlot widget for Polar and Nyquist diagrams in the complex plane.
 */

#include "windows/output_tabs/complex_plane_plot.h"

#include "implot.h"
#include "windows/output_tabs/plot_widgets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <utility>

namespace GUI {

namespace {

constexpr float HOVER_DISTANCE = 16.0f;
constexpr float READING_RING_RADIUS = 7.0f;
constexpr float START_MARKER_SIZE = 5.0f;
constexpr float CRITICAL_MARKER_SIZE = 7.0f;
constexpr float LOCUS_WEIGHT = 2.0f;
constexpr float ARROW_PIXELS = 8.0f;
constexpr float GRID_ALPHA = 0.32f;
constexpr float OUTER_ALPHA = 0.65f;
constexpr float FAINT_ALPHA = 0.35f;
constexpr float MIRROR_ALPHA = 0.55f;
constexpr float PLOT_SIDE_MARGIN = 3.0f;
constexpr float PLOT_BOTTOM_MARGIN = 5.0f;
constexpr ImVec4 CRITICAL_COLOR(0.88f, 0.30f, 0.28f, 1.0f);

bool IsFiniteSample(const std::complex<double> value) {
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

double GetChartAspect(const ImVec2 room) {
    const float font_size = ImGui::GetFontSize();
    return std::max(room.x - font_size * PLOT_SIDE_MARGIN, 1.0f)
         / std::max(room.y - font_size * PLOT_BOTTOM_MARGIN, 1.0f);
}

ImU32 WithAlpha(const ImU32 color, const float alpha) {
    ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
    rgba.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(rgba);
}

void SetupComplexAxes(const ComplexPlaneKind kind,
                      const double extent,
                      const double aspect,
                      const bool has_legend,
                      const bool is_fitting) {
    const ImPlotAxisFlags flags = kind == ComplexPlaneKind::POLAR ? ImPlotAxisFlags_NoGridLines : ImPlotAxisFlags_None;
    ImPlot::SetupAxes("Real", "Imaginary", flags, flags);
    if (has_legend) {
        ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_NoButtons);
    }
    const ImPlotCond cond = is_fitting ? ImPlotCond_Always : ImPlotCond_Once;
    const double half_width = extent * std::max(1.0, aspect);
    ImPlot::SetupAxisLimits(ImAxis_X1, -half_width, half_width, cond);
    ImPlot::SetupAxisLimits(ImAxis_Y1, -extent, extent, cond);
}

void PlotPolarGrid(const PolarGrid &grid) {
    for (std::size_t index = 0; index < grid.Spokes.size(); ++index) {
        const PolarGridSpoke &spoke = grid.Spokes[index];
        const std::array<double, 2> xs = {0.0, spoke.End.X};
        const std::array<double, 2> ys = {0.0, spoke.End.Y};
        ImPlotSpec spec;
        spec.LineColor = ImVec4(0.55f, 0.55f, 0.55f, GRID_ALPHA);
        spec.LineWeight = 1.0f;
        spec.Flags = ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
        ImPlot::PlotLine(std::format("##polar_spoke_{}", index).c_str(), xs.data(), ys.data(), 2, spec);
        ImPlot::PlotText(spoke.Label.c_str(), spoke.LabelPosition.X, spoke.LabelPosition.Y);
    }
    for (std::size_t index = 0; index < grid.Circles.size(); ++index) {
        const PolarGridCircle &circle = grid.Circles[index];
        std::vector<double> xs;
        std::vector<double> ys;
        xs.reserve(circle.Points.size());
        ys.reserve(circle.Points.size());
        for (const ComplexPlanePoint &pt : circle.Points) {
            xs.push_back(pt.X);
            ys.push_back(pt.Y);
        }
        const bool is_outer = index + 1 == grid.Circles.size();
        ImPlotSpec spec;
        spec.LineColor = ImVec4(0.55f, 0.55f, 0.55f, is_outer ? OUTER_ALPHA : GRID_ALPHA);
        spec.LineWeight = is_outer ? 1.4f : 1.0f;
        spec.Flags = ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
        ImPlot::PlotLine(std::format("##polar_circle_{}", index).c_str(),
                         xs.data(),
                         ys.data(),
                         static_cast<int>(xs.size()),
                         spec);
        ImPlot::PlotText(circle.Label.c_str(), circle.Radius, 0.0, ImVec2(-12.0f, -8.0f));
    }
}

void PlotNyquistGuides() {
    const std::vector<ComplexPlanePoint> ring = BuildCirclePoints(1.0);
    std::vector<double> xs;
    std::vector<double> ys;
    xs.reserve(ring.size());
    ys.reserve(ring.size());
    for (const ComplexPlanePoint &pt : ring) {
        xs.push_back(pt.X);
        ys.push_back(pt.Y);
    }
    PlotDashedLine("##nyquist_unit_circle", xs, ys, ImGui::GetColorU32(ImVec4(0.60f, 0.60f, 0.60f, 0.55f)));

    const double crit_x = NYQUIST_CRITICAL_POINT.real();
    const double crit_y = NYQUIST_CRITICAL_POINT.imag();
    ImPlotSpec crit_spec;
    crit_spec.Marker = ImPlotMarker_Cross;
    crit_spec.MarkerSize = CRITICAL_MARKER_SIZE;
    crit_spec.LineWeight = 2.0f;
    crit_spec.LineColor = CRITICAL_COLOR;
    crit_spec.MarkerLineColor = CRITICAL_COLOR;
    crit_spec.Flags = ImPlotItemFlags_NoFit;
    ImPlot::PlotScatter("Critical (-1, 0)", &crit_x, &crit_y, 1, crit_spec);
}

void PlotDashedComplexSegments(const std::string &id_prefix,
                               const std::string &legend_label,
                               const std::vector<std::complex<double>> &values,
                               const ImU32 color) {
    if (!legend_label.empty()) {
        ImPlotSpec legend_spec;
        legend_spec.LineColor = ImGui::ColorConvertU32ToFloat4(color);
        ImPlot::PlotDummy(legend_label.c_str(), legend_spec);
    }
    std::vector<double> xs;
    std::vector<double> ys;
    std::size_t piece = 0;
    for (const std::complex<double> value : values) {
        if (!IsFiniteSample(value)) {
            if (xs.size() >= 2) {
                PlotDashedLine(std::format("##{}_{}", id_prefix, piece++), xs, ys, color);
            }
            xs.clear();
            ys.clear();
            continue;
        }
        xs.push_back(value.real());
        ys.push_back(value.imag());
    }
    if (xs.size() >= 2) {
        PlotDashedLine(std::format("##{}_{}", id_prefix, piece), xs, ys, color);
    }
}

void PlotSolidComplexCurve(const std::string &item_label,
                           const std::vector<std::complex<double>> &values,
                           const ImU32 color,
                           const bool in_legend) {
    std::vector<double> xs;
    std::vector<double> ys;
    xs.reserve(values.size());
    ys.reserve(values.size());
    for (const std::complex<double> value : values) {
        xs.push_back(value.real());
        ys.push_back(value.imag());
    }
    ImPlotSpec spec;
    spec.LineColor = ImGui::ColorConvertU32ToFloat4(color);
    spec.LineWeight = LOCUS_WEIGHT;
    if (!in_legend) {
        spec.Flags = ImPlotItemFlags_NoLegend;
    }
    ImPlot::PlotLine(item_label.c_str(), xs.data(), ys.data(), static_cast<int>(xs.size()), spec);
}

double ComputeWorldArrowLength() {
    const ImPlotRect limits = ImPlot::GetPlotLimits(ImAxis_X1, ImAxis_Y1);
    const ImVec2 size = ImPlot::GetPlotSize();
    if (size.x <= 1.0f) {
        return 0.0;
    }
    return static_cast<double>(ARROW_PIXELS) * (limits.X.Max - limits.X.Min) / static_cast<double>(size.x);
}

void PlotStartAndArrows(const std::size_t index,
                        const std::vector<std::complex<double>> &values,
                        const ImU32 color,
                        const double arrow_len,
                        const bool plot_start_dot) {
    const ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
    if (plot_start_dot) {
        for (const std::complex<double> value : values) {
            if (IsFiniteSample(value)) {
                const double sx = value.real();
                const double sy = value.imag();
                ImPlotSpec start_spec;
                start_spec.Marker = ImPlotMarker_Circle;
                start_spec.MarkerSize = START_MARKER_SIZE;
                start_spec.MarkerLineColor = rgba;
                start_spec.MarkerFillColor = rgba;
                start_spec.Flags = ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
                ImPlot::PlotScatter(std::format("##complex_start_{}", index).c_str(), &sx, &sy, 1, start_spec);
                break;
            }
        }
    }
    std::size_t arrow_idx = 0;
    for (const DirectionArrow &arrow : BuildDirectionArrows(values, arrow_len)) {
        const std::array<double, 3> xs = {arrow.LeftWing.X, arrow.Tip.X, arrow.RightWing.X};
        const std::array<double, 3> ys = {arrow.LeftWing.Y, arrow.Tip.Y, arrow.RightWing.Y};
        ImPlotSpec arrow_spec;
        arrow_spec.LineColor = rgba;
        arrow_spec.LineWeight = LOCUS_WEIGHT;
        arrow_spec.Flags = ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
        ImPlot::PlotLine(std::format("##complex_arrow_{}_{}", index, arrow_idx++).c_str(),
                         xs.data(),
                         ys.data(),
                         3,
                         arrow_spec);
    }
}

void ShowHoveredLocusReading(const std::vector<ComplexLocus> &loci,
                             const ComplexPlanePlot::HoverFormatter &describe_hover) {
    if (!ImPlot::IsPlotHovered() || !describe_hover) {
        return;
    }
    const ImVec2 pointer = ImGui::GetMousePos();
    const ImPlotPoint mouse_plot = ImPlot::GetPlotMousePos();
    const std::complex<double> query(mouse_plot.x, mouse_plot.y);
    float nearest = HOVER_DISTANCE;
    std::optional<std::pair<std::size_t, std::size_t>> hovered;

    for (std::size_t index = 0; index < loci.size(); ++index) {
        if (loci[index].IsFaint) {
            continue;
        }
        const std::optional<std::size_t> point = FindNearestComplexPoint(loci[index].Values, query);
        if (!point) {
            continue;
        }
        const std::complex<double> sample = loci[index].Values[*point];
        const ImVec2 mark = ImPlot::PlotToPixels(sample.real(), sample.imag());
        const float distance = std::hypot(mark.x - pointer.x, mark.y - pointer.y);
        if (distance < nearest) {
            nearest = distance;
            hovered = std::make_pair(index, *point);
        }
    }
    if (!hovered) {
        return;
    }
    const ComplexLocus &locus = loci[hovered->first];
    const std::complex<double> sample = locus.Values[hovered->second];
    ImPlot::GetPlotDrawList()->AddCircle(ImPlot::PlotToPixels(sample.real(), sample.imag()),
                                         READING_RING_RADIUS,
                                         locus.Color,
                                         0,
                                         LOCUS_WEIGHT);
    const std::string text = describe_hover(hovered->first, hovered->second);
    if (!text.empty()) {
        ImGui::SetTooltip("%s", text.c_str());
    }
}

} // namespace

/**
 * @brief   Draws a Polar or Nyquist plot on equal real and imaginary axes, preserving user zoom between fits.
 * @param[in]     id                           ImPlot widget identifier.
 * @param[in]     kind                         `POLAR` or `NYQUIST`.
 * @param[in]     loci                         Complex traces to plot.
 * @param[in]     mirror_negative_frequencies  Whether Nyquist draws the conjugate negative-frequency locus.
 * @param[in]     has_legend                   Whether ImPlot shows a legend in the upper-right corner.
 * @param[in,out] is_fitting                   Fits axes when true (or after resize) and clears itself once fitted.
 * @param[in]     describe_hover               Builds the tooltip text for the sample nearest to the pointer.
 */
void ComplexPlanePlot::Draw(const char *id,
                            const ComplexPlaneKind kind,
                            const std::vector<ComplexLocus> &loci,
                            const bool mirror_negative_frequencies,
                            const bool has_legend,
                            bool &is_fitting,
                            const HoverFormatter &describe_hover) {
    const ImVec2 room = ImGui::GetContentRegionAvail();
    if (TakeResized(room)) {
        is_fitting = true;
    }
    std::vector<std::vector<std::complex<double>>> curves;
    curves.reserve(loci.size());
    for (const ComplexLocus &locus : loci) {
        curves.push_back(locus.Values);
    }
    const double max_mag = FindMaxComplexMagnitude(curves);
    const PolarGrid polar_grid =
        kind == ComplexPlaneKind::POLAR ? BuildPolarGrid(max_mag) : PolarGrid{.OuterRadius = 1.0};
    const double extent =
        kind == ComplexPlaneKind::POLAR ? polar_grid.OuterRadius * 1.14 : ChooseNyquistExtent(max_mag);

    const ImPlotFlags plot_flags = ImPlotFlags_Equal | (has_legend ? ImPlotFlags_None : ImPlotFlags_NoLegend);
    if (!ImPlot::BeginPlot(id, ImVec2(-1.0f, -1.0f), plot_flags)) {
        return;
    }
    SetupComplexAxes(kind, extent, GetChartAspect(room), has_legend, is_fitting);
    is_fitting = false;

    if (kind == ComplexPlaneKind::POLAR) {
        PlotPolarGrid(polar_grid);
    } else {
        PlotNyquistGuides();
    }

    const double arrow_len = ComputeWorldArrowLength();
    for (std::size_t index = 0; index < loci.size(); ++index) {
        const ComplexLocus &locus = loci[index];
        if (!locus.IsFaint) {
            continue;
        }
        const ImU32 faint = WithAlpha(locus.Color, FAINT_ALPHA);
        if (locus.IsDashed) {
            PlotDashedComplexSegments(std::format("faint_dash_{}", index), "", locus.Values, faint);
        } else {
            PlotSolidComplexCurve(std::format("##faint_{}", index), locus.Values, faint, false);
        }
    }
    for (std::size_t index = 0; index < loci.size(); ++index) {
        const ComplexLocus &locus = loci[index];
        if (locus.IsFaint) {
            continue;
        }
        if (kind == ComplexPlaneKind::NYQUIST && mirror_negative_frequencies) {
            const std::vector<std::complex<double>> mirrored = MirrorNegativeFrequencies(locus.Values);
            const ImU32 mirror_color = WithAlpha(locus.Color, MIRROR_ALPHA);
            PlotDashedComplexSegments(std::format("mirror_{}", index), "", mirrored, mirror_color);
            PlotStartAndArrows(index + loci.size(), mirrored, mirror_color, arrow_len, false);
        }
        const std::string label = locus.LegendLabel.empty() ? std::format("##locus_{}", index) : locus.LegendLabel;
        if (locus.IsDashed) {
            PlotDashedComplexSegments(std::format("locus_dash_{}", index),
                                      has_legend ? locus.LegendLabel : "",
                                      locus.Values,
                                      locus.Color);
        } else {
            PlotSolidComplexCurve(label, locus.Values, locus.Color, has_legend && !locus.LegendLabel.empty());
        }
        PlotStartAndArrows(index, locus.Values, locus.Color, arrow_len, true);
    }

    ShowHoveredLocusReading(loci, describe_hover);
    ImPlot::EndPlot();
}

bool ComplexPlanePlot::TakeResized(const ImVec2 room) {
    const bool is_resized = std::abs(room.x - m_FittedWidth) > 1.0f || std::abs(room.y - m_FittedHeight) > 1.0f;
    if (is_resized) {
        m_FittedWidth = room.x;
        m_FittedHeight = room.y;
    }
    return is_resized;
}

} // namespace GUI
