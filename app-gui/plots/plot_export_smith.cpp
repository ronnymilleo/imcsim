/**
 * @file    plot_export_smith.cpp
 * @brief   Draws a Smith chart of reflection coefficients as an SVG image for the plot export.
 */

#include "plots/plot_export_smith.h"

#include "plots/plot_export_style.h"
#include "plots/smith_chart.h"
#include <algorithm>
#include <cmath>

namespace GUI {

namespace {

constexpr double SMITH_LABEL_FONT = 12.0;
constexpr double SMITH_MARKER_RADIUS = 4.0;
/// Room left around the unit circle for the labels of the reactance arcs, in label font sizes.
constexpr double SMITH_LABEL_ROOM = 3.0;
/// The chart reaches this far past the farthest point when a trace leaves the unit circle, as a gain above 1 does.
constexpr double SMITH_EXTENT_MARGIN = 1.05;

/**
 * @struct  SmithFrame
 * @brief   Where the reflection plane lies in the image: the pixel of its origin and the pixels of one unit.
 */
struct SmithFrame {
    SvgPoint Center;
    double PixelsPerUnit = 1.0;

    SvgPoint ToPixel(const double x, const double y) const {
        return {Center.X + x * PixelsPerUnit, Center.Y - y * PixelsPerUnit};
    }
};

/**
 * @brief   Half the side of the plane the chart shows: the unit circle, or more when a trace leaves it.
 */
double FindSmithExtent(const ExportSmith &smith) {
    double extent = 1.0;
    for (const ExportSmithTrace &trace : smith.Traces) {
        for (const std::complex<double> value : trace.Values) {
            if (std::isfinite(std::abs(value))) {
                extent = std::max(extent, std::abs(value) * SMITH_EXTENT_MARGIN);
            }
        }
    }
    return extent;
}

std::vector<SvgPoint> ToPixels(const std::vector<SmithPoint> &points, const SmithFrame &frame) {
    std::vector<SvgPoint> pixels;
    pixels.reserve(points.size());
    for (const SmithPoint &point : points) {
        pixels.push_back(frame.ToPixel(point.X, point.Y));
    }
    return pixels;
}

/**
 * @brief   Names a curve of the grid: a resistance circle above the real axis where it crosses it, a reactance arc
 *          outside the outer circle where it starts.
 */
void DrawSmithLabel(SvgWriter &svg, const SmithCurve &curve, const SmithFrame &frame, const ExportTheme &theme,
                    const double scale) {
    if (curve.Label.empty()) {
        return;
    }
    const double font_size = SMITH_LABEL_FONT * scale;
    SvgPoint anchor = frame.ToPixel(curve.LabelPosition.X, curve.LabelPosition.Y);
    if (curve.IsResistance) {
        svg.Text({anchor.X - 2.0 * scale, anchor.Y - 3.0 * scale}, curve.Label, font_size, theme.MutedText,
                 TextAnchor::END);
        return;
    }
    // Pushed outward along the radius, so the label clears the outer circle on either side
    const double radius = std::hypot(curve.LabelPosition.X, curve.LabelPosition.Y);
    const double push = font_size * 0.9 / frame.PixelsPerUnit / std::max(radius, 1e-9);
    anchor = frame.ToPixel(curve.LabelPosition.X * (1.0 + push), curve.LabelPosition.Y * (1.0 + push));
    svg.Text({anchor.X, anchor.Y + font_size * 0.35}, curve.Label, font_size, theme.MutedText, TextAnchor::MIDDLE);
}

void DrawSmithGrid(SvgWriter &svg, const SmithFrame &frame, const ExportTheme &theme, const double scale) {
    svg.Line(frame.ToPixel(-1.0, 0.0), frame.ToPixel(1.0, 0.0), theme.Border, scale);
    for (const SmithCurve &curve : BuildSmithGrid(SMITH_CURVE_SEGMENTS)) {
        const bool is_outer = curve.Label.empty();
        svg.Polyline(ToPixels(curve.Points, frame), theme.Border, (is_outer ? 1.6 : 1.0) * scale);
        DrawSmithLabel(svg, curve, frame, theme, scale);
    }
}

void DrawSmithTraces(SvgWriter &svg, const ExportSmith &smith, const SmithFrame &frame, const ExportStyle &style,
                     const double scale) {
    for (const ExportSmithTrace &trace : smith.Traces) {
        const ExportColor color = style.Dark ? trace.Color : AdjustColorForPrint(trace.Color);
        std::vector<SvgPoint> run;
        for (const std::complex<double> value : trace.Values) {
            if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
                svg.Polyline(run, color, LINE_WIDTH * scale);
                run.clear();
                continue;
            }
            run.push_back(frame.ToPixel(value.real(), value.imag()));
        }
        svg.Polyline(run, color, LINE_WIDTH * scale);
        // The first frequency is marked with a dot, so the direction the curve runs can be told
        if (!trace.Values.empty() && std::isfinite(std::abs(trace.Values.front()))) {
            svg.FilledCircle(frame.ToPixel(trace.Values.front().real(), trace.Values.front().imag()),
                             SMITH_MARKER_RADIUS * scale, color);
        }
    }
}

double GetSmithLegendWidth(const ExportSmith &smith, const double scale) {
    double legend_width = 0.0;
    for (const ExportSmithTrace &trace : smith.Traces) {
        legend_width +=
            (LEGEND_SAMPLE + 6.0) * scale + EstimateTextWidth(trace.Name, LEGEND_FONT * scale) + LEGEND_SPACING * scale;
    }
    return legend_width;
}

void DrawSmithLegend(SvgWriter &svg, const ExportSmith &smith, const double baseline, const double right,
                     const ExportStyle &style, const ExportTheme &theme, const double scale) {
    double x = right - GetSmithLegendWidth(smith, scale) + LEGEND_SPACING * scale;
    for (const ExportSmithTrace &trace : smith.Traces) {
        const ExportColor color = style.Dark ? trace.Color : AdjustColorForPrint(trace.Color);
        svg.Polyline({{x, baseline}, {x + LEGEND_SAMPLE * scale, baseline}}, color, LINE_WIDTH * scale);
        svg.Text({x + (LEGEND_SAMPLE + 6.0) * scale, baseline + LEGEND_FONT * scale * 0.35}, trace.Name,
                 LEGEND_FONT * scale, theme.Text, TextAnchor::START);
        x +=
            (LEGEND_SAMPLE + 6.0) * scale + EstimateTextWidth(trace.Name, LEGEND_FONT * scale) + LEGEND_SPACING * scale;
    }
}

} // namespace

/**
 * @brief   Draws the chart in the largest square the image leaves under its header, with room around it for the labels
 *          of the reactance arcs.
 */
std::expected<std::string, std::string> RenderSmith(const ExportSmith &smith, const ExportStyle &style) {
    const auto width = static_cast<double>(style.Width);
    const auto height = static_cast<double>(style.Height);
    const double scale = std::clamp(std::min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT), MIN_SCALE, MAX_SCALE);
    const double outer = OUTER_MARGIN * scale;
    const double header = std::max(smith.Title.empty() ? 0.0 : TITLE_FONT * scale * 1.6, LEGEND_FONT * scale * 1.6);
    const double room = SMITH_LABEL_FONT * scale * SMITH_LABEL_ROOM;
    const double side = std::min(width - 2.0 * (outer + room), height - 2.0 * outer - header - 2.0 * room);
    if (side < MIN_PLOT_SIZE * scale) {
        return std::unexpected("The image is too small for the Smith chart");
    }
    const ExportTheme &theme = style.Dark ? DARK_THEME : LIGHT_THEME;
    const double extent = FindSmithExtent(smith);
    const SmithFrame frame{.Center = {width / 2.0, outer + header + room + side / 2.0},
                           .PixelsPerUnit = side / 2.0 / extent};
    SvgWriter svg;
    DrawSmithGrid(svg, frame, theme, scale);
    svg.BeginClip({frame.Center.X - side / 2.0 - room, frame.Center.Y - side / 2.0 - room}, side + 2.0 * room,
                  side + 2.0 * room);
    DrawSmithTraces(svg, smith, frame, style, scale);
    svg.EndClip();
    if (!smith.Title.empty()) {
        svg.Text({outer, outer + TITLE_FONT * scale * 1.1}, smith.Title, TITLE_FONT * scale, theme.Text,
                 TextAnchor::START);
    }
    DrawSmithLegend(svg, smith, outer + header / 2.0, width - outer, style, theme, scale);
    return svg.Finish({0.0, 0.0}, width, height, theme.Background);
}

} // namespace GUI
