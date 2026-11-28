/**
 * @file    plot_export_complex.cpp
 * @brief   Draws a Polar or Nyquist plot in the complex plane as an SVG image for the plot export.
 */

#include "plots/plot_export_complex.h"

#include "plots/plot_export_style.h"
#include "plots/plot_ticks.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace GUI {

namespace {

constexpr double PLANE_LABEL_FONT = 12.0;
constexpr double MARKER_RADIUS = 4.0;
constexpr double CRITICAL_CROSS_HALF = 6.0;
constexpr double ARROW_PIXELS = 8.0;
constexpr double LABEL_ROOM_FACTOR = 3.6;
constexpr double FAINT_OPACITY = 0.35;
constexpr double MIRROR_OPACITY = 0.55;
constexpr ExportColor CRITICAL_COLOR{220, 75, 70};

/**
 * @struct  PlaneFrame
 * @brief   Maps complex-plane coordinates into square SVG pixel coordinates.
 */
struct PlaneFrame {
    SvgPoint Center;
    double HalfSide = 1.0;
    double PixelsPerUnit = 1.0;

    SvgPoint ToPixel(const double x, const double y) const {
        return {Center.X + x * PixelsPerUnit, Center.Y - y * PixelsPerUnit};
    }
};

std::string DashPattern(const double scale) {
    return std::format("{} {}", FormatCoordinate(DASH_LENGTH * scale), FormatCoordinate(DASH_GAP * scale));
}

ExportColor BlendWithBackground(const ExportColor foreground, const ExportColor background, const double alpha) {
    const auto blend = [alpha](const std::uint8_t fg, const std::uint8_t bg) {
        return static_cast<std::uint8_t>(std::clamp(std::lround(fg * alpha + bg * (1.0 - alpha)), 0L, 255L));
    };
    return {blend(foreground.Red, background.Red),
            blend(foreground.Green, background.Green),
            blend(foreground.Blue, background.Blue)};
}

std::vector<SvgPoint> ToSvgPoints(const std::vector<ComplexPlanePoint> &points, const PlaneFrame &frame) {
    std::vector<SvgPoint> pixels;
    pixels.reserve(points.size());
    for (const ComplexPlanePoint &point : points) {
        pixels.push_back(frame.ToPixel(point.X, point.Y));
    }
    return pixels;
}

double FindMaxTraceMagnitude(const ExportComplexPlane &plane) {
    std::vector<std::vector<std::complex<double>>> curves;
    curves.reserve(plane.Traces.size());
    for (const ExportComplexTrace &trace : plane.Traces) {
        curves.push_back(trace.Values);
    }
    return FindMaxComplexMagnitude(curves);
}

void DrawPolarPlaneGrid(SvgWriter &svg,
                        const PolarGrid &grid,
                        const PlaneFrame &frame,
                        const ExportTheme &theme,
                        const double scale) {
    const double font_size = PLANE_LABEL_FONT * scale;
    for (const PolarGridSpoke &spoke : grid.Spokes) {
        svg.Line(frame.ToPixel(0.0, 0.0), frame.ToPixel(spoke.End.X, spoke.End.Y), theme.Grid, scale);
        const SvgPoint label_pos = frame.ToPixel(spoke.LabelPosition.X, spoke.LabelPosition.Y);
        svg.Text({label_pos.X, label_pos.Y + font_size * 0.35},
                 spoke.Label,
                 font_size,
                 theme.MutedText,
                 TextAnchor::MIDDLE);
    }
    for (std::size_t index = 0; index < grid.Circles.size(); ++index) {
        const PolarGridCircle &circle = grid.Circles[index];
        const bool is_outer = index + 1 == grid.Circles.size();
        svg.Polyline(ToSvgPoints(circle.Points, frame),
                     is_outer ? theme.Border : theme.Grid,
                     (is_outer ? 1.5 : 1.0) * scale);
        const SvgPoint tick_pos = frame.ToPixel(circle.Radius, 0.0);
        svg.Text({tick_pos.X - 3.0 * scale, tick_pos.Y - 4.0 * scale},
                 circle.Label,
                 font_size,
                 theme.MutedText,
                 TextAnchor::END);
    }
}

void DrawNyquistCartesianGrid(SvgWriter &svg,
                              const double extent,
                              const PlaneFrame &frame,
                              const ExportTheme &theme,
                              const double scale) {
    const double font_size = PLANE_LABEL_FONT * scale;
    const std::vector<double> ticks = FindNiceTicks(-extent, extent, 6);
    const double step = ticks.size() >= 2 ? (ticks[1] - ticks[0]) : extent;
    const double left = frame.Center.X - frame.HalfSide;
    const double right = frame.Center.X + frame.HalfSide;
    const double top = frame.Center.Y - frame.HalfSide;
    const double bottom = frame.Center.Y + frame.HalfSide;

    for (const double tick : ticks) {
        if (std::abs(tick) > extent * 1.001) {
            continue;
        }
        const SvgPoint x_px = frame.ToPixel(tick, 0.0);
        const SvgPoint y_px = frame.ToPixel(0.0, tick);
        const bool is_zero = std::abs(tick) <= step * 1e-6;
        svg.Line({x_px.X, top}, {x_px.X, bottom}, is_zero ? theme.Border : theme.Grid, (is_zero ? 1.4 : 1.0) * scale);
        svg.Line({left, y_px.Y}, {right, y_px.Y}, is_zero ? theme.Border : theme.Grid, (is_zero ? 1.4 : 1.0) * scale);
        const std::string label = FormatTick(tick, step);
        svg.Text({x_px.X, bottom + font_size * 1.15}, label, font_size, theme.MutedText, TextAnchor::MIDDLE);
        svg.Text({left - 6.0 * scale, y_px.Y + font_size * 0.35}, label, font_size, theme.MutedText, TextAnchor::END);
    }
    svg.Polyline({{left, top}, {right, top}, {right, bottom}, {left, bottom}}, theme.Border, scale, "", true);
    svg.Polyline(ToSvgPoints(BuildCirclePoints(1.0), frame), theme.MutedText, scale, DashPattern(scale));

    const double axis_font = AXIS_NAME_FONT * scale;
    svg.Text({frame.Center.X, bottom + font_size * 1.3 + axis_font * 1.25},
             "Real",
             axis_font,
             theme.Text,
             TextAnchor::MIDDLE);
    svg.Text({left - 34.0 * scale, frame.Center.Y}, "Imaginary", axis_font, theme.Text, TextAnchor::MIDDLE, true);
}

void DrawCriticalPoint(SvgWriter &svg,
                       const std::complex<double> point,
                       const PlaneFrame &frame,
                       const ExportStyle &style,
                       const double scale) {
    const ExportColor color = style.Dark ? CRITICAL_COLOR : AdjustColorForPrint(CRITICAL_COLOR);
    const SvgPoint center = frame.ToPixel(point.real(), point.imag());
    const double arm = CRITICAL_CROSS_HALF * scale;
    svg.Line({center.X - arm, center.Y - arm}, {center.X + arm, center.Y + arm}, color, 2.0 * scale);
    svg.Line({center.X - arm, center.Y + arm}, {center.X + arm, center.Y - arm}, color, 2.0 * scale);
}

void DrawComplexRun(SvgWriter &svg,
                    const std::vector<std::complex<double>> &values,
                    const PlaneFrame &frame,
                    const ExportColor color,
                    const double scale,
                    const std::string &dash,
                    const bool draw_markers) {
    std::vector<SvgPoint> run;
    for (const std::complex<double> value : values) {
        if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
            svg.Polyline(run, color, LINE_WIDTH * scale, dash);
            run.clear();
            continue;
        }
        run.push_back(frame.ToPixel(value.real(), value.imag()));
    }
    svg.Polyline(run, color, LINE_WIDTH * scale, dash);
    if (!draw_markers) {
        return;
    }
    for (const std::complex<double> value : values) {
        if (std::isfinite(value.real()) && std::isfinite(value.imag())) {
            svg.FilledCircle(frame.ToPixel(value.real(), value.imag()), MARKER_RADIUS * scale, color);
            break;
        }
    }
    const double arrow_len = ARROW_PIXELS * scale / std::max(frame.PixelsPerUnit, 1e-12);
    for (const DirectionArrow &arrow : BuildDirectionArrows(values, arrow_len)) {
        const SvgPoint tip = frame.ToPixel(arrow.Tip.X, arrow.Tip.Y);
        svg.Polyline({frame.ToPixel(arrow.LeftWing.X, arrow.LeftWing.Y),
                      tip,
                      frame.ToPixel(arrow.RightWing.X, arrow.RightWing.Y)},
                     color,
                     LINE_WIDTH * scale);
    }
}

void DrawPlaneTraces(SvgWriter &svg,
                     const ExportComplexPlane &plane,
                     const PlaneFrame &frame,
                     const ExportStyle &style,
                     const ExportTheme &theme,
                     const double scale) {
    const std::string dashed = DashPattern(scale);
    for (const ExportComplexTrace &trace : plane.Traces) {
        if (!trace.Faint) {
            continue;
        }
        const ExportColor base = style.Dark ? trace.Color : AdjustColorForPrint(trace.Color);
        const ExportColor faint = BlendWithBackground(base, theme.Background, FAINT_OPACITY);
        DrawComplexRun(svg, trace.Values, frame, faint, scale, trace.Dashed ? dashed : "", false);
    }
    for (const ExportComplexTrace &trace : plane.Traces) {
        if (trace.Faint) {
            continue;
        }
        const ExportColor color = style.Dark ? trace.Color : AdjustColorForPrint(trace.Color);
        if (plane.Kind == ComplexPlaneKind::NYQUIST && plane.MirrorNegativeFrequencies) {
            const ExportColor mirror_color = BlendWithBackground(color, theme.Background, MIRROR_OPACITY);
            DrawComplexRun(svg, MirrorNegativeFrequencies(trace.Values), frame, mirror_color, scale, dashed, true);
        }
        DrawComplexRun(svg, trace.Values, frame, color, scale, trace.Dashed ? dashed : "", true);
    }
}

double GetLegendEntryWidth(const std::string &label, const double scale) {
    return (LEGEND_SAMPLE + 6.0) * scale + EstimateTextWidth(label, LEGEND_FONT * scale) + LEGEND_SPACING * scale;
}

void DrawPlaneLegend(SvgWriter &svg,
                     const ExportComplexPlane &plane,
                     const double baseline,
                     const double right,
                     const ExportStyle &style,
                     const ExportTheme &theme,
                     const double scale) {
    double total_width = 0.0;
    for (const ExportComplexTrace &trace : plane.Traces) {
        if (!trace.LegendLabel.empty()) {
            total_width += GetLegendEntryWidth(trace.LegendLabel, scale);
        }
    }
    double x = right - total_width + LEGEND_SPACING * scale;
    const std::string dashed = DashPattern(scale);
    for (const ExportComplexTrace &trace : plane.Traces) {
        if (trace.LegendLabel.empty()) {
            continue;
        }
        const ExportColor color = style.Dark ? trace.Color : AdjustColorForPrint(trace.Color);
        svg.Polyline({{x, baseline}, {x + LEGEND_SAMPLE * scale, baseline}},
                     color,
                     LINE_WIDTH * scale,
                     trace.Dashed ? dashed : "");
        svg.Text({x + (LEGEND_SAMPLE + 6.0) * scale, baseline + LEGEND_FONT * scale * 0.35},
                 trace.LegendLabel,
                 LEGEND_FONT * scale,
                 theme.Text,
                 TextAnchor::START);
        x += GetLegendEntryWidth(trace.LegendLabel, scale);
    }
}

} // namespace

/**
 * @brief   Renders a Polar or Nyquist diagram inside the largest square the image leaves under its header.
 * @param[in] plane  Complex traces, plane kind, and Nyquist options to draw.
 * @param[in] style  Image dimensions and dark or print theme.
 * @return  The complete SVG document, or an error message when the image is too small.
 */
std::expected<std::string, std::string> RenderComplexPlane(const ExportComplexPlane &plane, const ExportStyle &style) {
    const auto width = static_cast<double>(style.Width);
    const auto height = static_cast<double>(style.Height);
    const double scale = std::clamp(std::min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT), MIN_SCALE, MAX_SCALE);
    const double outer = OUTER_MARGIN * scale;
    const double header = std::max(plane.Title.empty() ? 0.0 : TITLE_FONT * scale * 1.6, LEGEND_FONT * scale * 1.6);
    const double room = PLANE_LABEL_FONT * scale * LABEL_ROOM_FACTOR;
    const double side = std::min(width - 2.0 * (outer + room), height - 2.0 * outer - header - 2.0 * room);
    if (side < MIN_PLOT_SIZE * scale) {
        return std::unexpected("The image is too small for the complex-plane plot");
    }

    const ExportTheme &theme = style.Dark ? DARK_THEME : LIGHT_THEME;
    const double max_mag = FindMaxTraceMagnitude(plane);
    const PolarGrid polar_grid =
        plane.Kind == ComplexPlaneKind::POLAR ? BuildPolarGrid(max_mag) : PolarGrid{.OuterRadius = 1.0};
    const double extent = plane.Kind == ComplexPlaneKind::POLAR ? polar_grid.OuterRadius : ChooseNyquistExtent(max_mag);
    const PlaneFrame frame{.Center = {width / 2.0, outer + header + room + side / 2.0},
                           .HalfSide = side / 2.0,
                           .PixelsPerUnit = side / 2.0 / extent};

    SvgWriter svg;
    if (plane.Kind == ComplexPlaneKind::POLAR) {
        DrawPolarPlaneGrid(svg, polar_grid, frame, theme, scale);
    } else {
        DrawNyquistCartesianGrid(svg, extent, frame, theme, scale);
    }
    svg.BeginClip({frame.Center.X - frame.HalfSide - 2.0 * scale, frame.Center.Y - frame.HalfSide - 2.0 * scale},
                  side + 4.0 * scale,
                  side + 4.0 * scale);
    if (plane.Kind == ComplexPlaneKind::NYQUIST && plane.CriticalPoint) {
        DrawCriticalPoint(svg, *plane.CriticalPoint, frame, style, scale);
    }
    DrawPlaneTraces(svg, plane, frame, style, theme, scale);
    svg.EndClip();

    if (!plane.Title.empty()) {
        svg.Text({outer, outer + TITLE_FONT * scale * 1.1},
                 plane.Title,
                 TITLE_FONT * scale,
                 theme.Text,
                 TextAnchor::START);
    }
    DrawPlaneLegend(svg, plane, outer + header / 2.0, width - outer, style, theme, scale);
    return svg.Finish({0.0, 0.0}, width, height, theme.Background);
}

} // namespace GUI
