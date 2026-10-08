/**
 * @file    plot_export.cpp
 * @brief   Renders plots described as data into SVG images and CSV tables for reports.
 * @details Sizes are designed for a 1600x900 image and scale with it. Text widths are estimated, since the viewer
 *          picks the font that renders the SVG; the estimate only sizes margins and the legend.
 */

#include "plot_export.h"

#include "spice_value.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <string_view>

namespace GUI {

namespace {

// region Theme and sizes

/**
 * @struct  ExportTheme
 * @brief   Colors of the parts of a figure other than its traces.
 */
struct ExportTheme {
    ExportColor Background;
    ExportColor PlotBackground;
    ExportColor Text;
    ExportColor MutedText;
    ExportColor Border;
    ExportColor Grid;
    double GridOpacity;
    double MinorGridOpacity;
};

// Dark follows the warm charcoal of the application; light is plain black on white for print
constexpr ExportTheme DarkTheme = {
    .Background = {31, 25, 26},
    .PlotBackground = {22, 18, 19},
    .Text = {235, 226, 227},
    .MutedText = {160, 142, 145},
    .Border = {66, 52, 55},
    .Grid = {255, 255, 255},
    .GridOpacity = 0.08,
    .MinorGridOpacity = 0.035,
};
constexpr ExportTheme LightTheme = {
    .Background = {255, 255, 255},
    .PlotBackground = {255, 255, 255},
    .Text = {30, 30, 30},
    .MutedText = {85, 85, 85},
    .Border = {150, 150, 150},
    .Grid = {0, 0, 0},
    .GridOpacity = 0.12,
    .MinorGridOpacity = 0.05,
};

// Sizes in pixels of a 1600x900 image
constexpr double DesignWidth = 1600.0;
constexpr double DesignHeight = 900.0;
constexpr double MinScale = 0.45;
constexpr double MaxScale = 3.0;
constexpr double OuterMargin = 18.0;
constexpr double TickFont = 14.0;
constexpr double AxisNameFont = 16.0;
constexpr double TitleFont = 16.0;
constexpr double LegendFont = 14.0;
constexpr double NoteFont = 13.0;
constexpr double TickLength = 5.0;
constexpr double TickGap = 8.0;
constexpr double LineWidth = 2.0;
constexpr double HeavyLineWidth = 2.6;
constexpr double DashLength = 8.0;
constexpr double DashGap = 5.0;
constexpr double LegendSample = 26.0;
constexpr double LegendSpacing = 20.0;
constexpr double MinPlotSize = 40.0;
// Average advance of a sans-serif character, in font sizes
constexpr double CharacterWidth = 0.56;
// Data range added above and below the traces, as a fraction of it
constexpr double YPadding = 0.08;
// Samples closer than this to the last one drawn, in pixels, add nothing to the line
constexpr double MinPointDistance = 0.5;

/**
 * @struct  Prefix
 * @brief   An SI prefix and its factor, as Core::FormatValue() writes them.
 */
struct Prefix {
    std::string_view Text;
    double Factor;
};

constexpr auto Prefixes = std::to_array<Prefix>({
    {"T", 1e12},
    {"G", 1e9},
    {"M", 1e6},
    {"k", 1e3},
    {"", 1.0},
    {"m", 1e-3},
    {"u", 1e-6},
    {"n", 1e-9},
    {"p", 1e-12},
    {"f", 1e-15},
});

// endregion

// region Text and SVG helpers

double EstimateTextWidth(const std::string_view text, const double font_size) {
    return CharacterWidth * font_size * static_cast<double>(text.size());
}

// endregion

// region Ranges and ticks

/**
 * @struct  ValueRange
 * @brief   The lowest and highest value an axis shows.
 */
struct ValueRange {
    double Low = 0.0;
    double High = 1.0;
};

// A logarithmic axis maps decades to equal lengths
double ToAxis(const double value, const bool logarithmic) {
    return logarithmic ? std::log10(value) : value;
}

bool IsShownOnAxis(const double value, const bool logarithmic) {
    return std::isfinite(value) && (!logarithmic || value > 0.0);
}

// A range with no width opens around its value, so a flat trace is drawn across the middle of the plot
ValueRange Widen(const ValueRange range) {
    if (range.High > range.Low) {
        return range;
    }
    const double half_width = range.Low == 0.0 ? 1.0 : std::abs(range.Low) * 0.1;
    return {range.Low - half_width, range.High + half_width};
}

ValueRange FindXRange(const ExportFigure &figure) {
    if (figure.XRange) {
        const double low = std::min(figure.XRange->From, figure.XRange->To);
        const double high = std::max(figure.XRange->From, figure.XRange->To);
        if (IsShownOnAxis(low, figure.XLogarithmic) && IsShownOnAxis(high, figure.XLogarithmic) && high > low) {
            return {low, high};
        }
    }
    ValueRange range{std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (const double x : figure.Xs) {
        if (IsShownOnAxis(x, figure.XLogarithmic)) {
            range.Low = std::min(range.Low, x);
            range.High = std::max(range.High, x);
        }
    }
    if (!(range.Low <= range.High)) {
        return figure.XLogarithmic ? ValueRange{1.0, 10.0} : ValueRange{0.0, 1.0};
    }
    if (figure.XLogarithmic && range.High <= range.Low) {
        return {range.Low / 10.0, range.High * 10.0};
    }
    return Widen(range);
}

// Range of the samples of an axis whose X is shown, with some room above and below
ValueRange FindYRange(const ExportFigure &figure, const ExportPanel &panel, const std::size_t axis,
                      const ValueRange x_range) {
    ValueRange range{std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (const ExportSeries &series : panel.Series) {
        if (series.Axis != axis) {
            continue;
        }
        const std::size_t count = std::min(series.Values.size(), figure.Xs.size());
        for (std::size_t index = 0; index < count; ++index) {
            const double x = figure.Xs[index];
            const double y = series.Values[index];
            if (std::isfinite(y) && x >= x_range.Low && x <= x_range.High) {
                range.Low = std::min(range.Low, y);
                range.High = std::max(range.High, y);
            }
        }
    }
    if (!(range.Low <= range.High)) {
        return {0.0, 1.0};
    }
    range = Widen(range);
    const double padding = (range.High - range.Low) * YPadding;
    return {range.Low - padding, range.High + padding};
}

// The largest prefix that keeps the biggest tick at 1 or more, so all ticks of an axis share it
const Prefix &ChoosePrefix(const std::vector<double> &ticks) {
    double largest = 0.0;
    for (const double tick : ticks) {
        largest = std::max(largest, std::abs(tick));
    }
    for (const Prefix &prefix : Prefixes) {
        if (largest >= prefix.Factor) {
            return prefix;
        }
    }
    return Prefixes.back();
}

std::vector<std::string> FormatLinearTicks(const std::vector<double> &ticks, const ExportAxis &axis) {
    const double step = ticks.size() >= 2 ? ticks[1] - ticks[0] : 1.0;
    std::vector<std::string> labels;
    if (!axis.UsesPrefixes) {
        for (const double tick : ticks) {
            labels.push_back(FormatTick(tick, step));
        }
        return labels;
    }
    const Prefix &prefix = ChoosePrefix(ticks);
    for (const double tick : ticks) {
        labels.push_back(
            std::format("{}{}{}", FormatTick(tick / prefix.Factor, step / prefix.Factor), prefix.Text, axis.Unit));
    }
    return labels;
}

// endregion

// region Layout

/**
 * @struct  YAxisLayout
 * @brief   Range, ticks and tick labels of one Y axis of a panel.
 */
struct YAxisLayout {
    ValueRange Range;
    std::vector<double> Ticks;
    std::vector<std::string> Labels;
    double LabelWidth = 0.0;
};

/**
 * @struct  LegendEntry
 * @brief   A legend item and the row it is placed in.
 */
struct LegendEntry {
    const ExportSeries *Series;
    std::size_t Row;
    double Width;
};

/**
 * @struct  PanelLayout
 * @brief   Where a panel is drawn and what its axes and legend hold.
 */
struct PanelLayout {
    double Top = 0.0;
    double HeaderHeight = 0.0;
    double PlotHeight = 0.0;
    std::vector<YAxisLayout> YAxes;
    std::vector<LegendEntry> Legend;
    std::size_t LegendRows = 0;
};

/**
 * @struct  AxisColumns
 * @brief   Horizontal room of the Y axes, shared by every panel so their axes line up.
 */
struct AxisColumns {
    // Baseline of the name of the left axis, which reads bottom to top
    double LeftNameX = 0.0;
    // Width of each right axis, from the plot outwards
    std::vector<double> RightWidths;
};

/**
 * @struct  Frame
 * @brief   The plot rectangle of a panel in pixels and the ranges it maps.
 */
struct Frame {
    double Left;
    double Top;
    double Width;
    double Height;
    ValueRange X;
    bool XLogarithmic;

    double ToPixelX(const double x) const {
        const double low = ToAxis(X.Low, XLogarithmic);
        const double high = ToAxis(X.High, XLogarithmic);
        return Left + (ToAxis(x, XLogarithmic) - low) / (high - low) * Width;
    }

    double ToPixelY(const double y, const ValueRange &range) const {
        return Top + Height - (y - range.Low) / (range.High - range.Low) * Height;
    }
};

// Places the legend entries in rows no wider than the space given, right-aligned later when drawn
std::size_t PlaceLegend(const ExportPanel &panel, const double max_width, const double scale,
                        std::vector<LegendEntry> &entries) {
    std::size_t row = 0;
    double row_width = 0.0;
    for (const ExportSeries &series : panel.Series) {
        if (series.LegendLabel.empty()) {
            continue;
        }
        const double width = (LegendSample + 6.0) * scale + EstimateTextWidth(series.LegendLabel, LegendFont * scale);
        if (row_width > 0.0 && row_width + LegendSpacing * scale + width > max_width) {
            ++row;
            row_width = 0.0;
        }
        row_width += (row_width > 0.0 ? LegendSpacing * scale : 0.0) + width;
        entries.push_back({&series, row, width});
    }
    return entries.empty() ? 0 : row + 1;
}

std::vector<double> FindXTicks(const ValueRange range, const bool logarithmic, const double plot_width,
                               const double scale) {
    const int count = std::clamp(static_cast<int>(plot_width / (110.0 * scale)), 3, 12);
    return logarithmic ? FindDecadeTicks(range.Low, range.High, count) : FindNiceTicks(range.Low, range.High, count);
}

std::vector<std::string> FormatXTicks(const std::vector<double> &ticks, const ExportFigure &figure) {
    if (!figure.XLogarithmic) {
        return FormatLinearTicks(ticks, figure.XAxis);
    }
    std::vector<std::string> labels;
    for (const double tick : ticks) {
        labels.push_back(Core::FormatValue(tick) + figure.XAxis.Unit);
    }
    return labels;
}

// endregion

// region Drawing

// The visible part of a series in pixels, split where samples are missing; samples just outside the X range are
// kept so the line reaches the edge of the plot
std::vector<std::vector<SvgPoint>> BuildRuns(const ExportFigure &figure, const ExportSeries &series, const Frame &frame,
                                             const ValueRange &y_range) {
    std::vector<std::vector<SvgPoint>> runs(1);
    const std::size_t count = std::min(series.Values.size(), figure.Xs.size());
    const auto in_range = [&](const std::size_t index) {
        const double x = figure.Xs[index];
        return x >= frame.X.Low && x <= frame.X.High;
    };
    for (std::size_t index = 0; index < count; ++index) {
        const double x = figure.Xs[index];
        const double y = series.Values[index];
        const bool near_range =
            in_range(index) || (index > 0 && in_range(index - 1)) || (index + 1 < count && in_range(index + 1));
        if (!near_range || !IsShownOnAxis(x, figure.XLogarithmic) || !std::isfinite(y)) {
            if (!runs.back().empty()) {
                runs.emplace_back();
            }
            continue;
        }
        const SvgPoint point = {frame.ToPixelX(x), frame.ToPixelY(y, y_range)};
        std::vector<SvgPoint> &run = runs.back();
        const bool is_last = index + 1 == count;
        if (run.size() >= 2 && !is_last && std::abs(point.X - run.back().X) < MinPointDistance &&
            std::abs(point.Y - run.back().Y) < MinPointDistance) {
            continue;
        }
        run.push_back(point);
    }
    std::erase_if(runs, [](const auto &run) { return run.size() < 2; });
    return runs;
}

std::string DashPattern(const double scale) {
    return std::format("{} {}", FormatCoordinate(DashLength * scale), FormatCoordinate(DashGap * scale));
}

void DrawSeries(SvgWriter &svg, const ExportFigure &figure, const ExportSeries &series, const Frame &frame,
                const ValueRange &y_range, const ExportColor color, const double scale) {
    const double width = (series.Heavy ? HeavyLineWidth : LineWidth) * scale;
    for (const auto &run : BuildRuns(figure, series, frame, y_range)) {
        svg.Polyline(run, color, width, series.Dashed ? DashPattern(scale) : "");
    }
}

// Notes sit right of their point, or left of it when they would leave the plot
void DrawNote(SvgWriter &svg, const ExportNote &note, const Frame &frame, const ValueRange &y_range,
              const ExportTheme &theme, const double scale) {
    const double x = frame.ToPixelX(note.X);
    const double y = frame.ToPixelY(note.Y, y_range);
    const double font_size = NoteFont * scale;
    const double width = EstimateTextWidth(note.Text, font_size);
    const double padding = 3.0 * scale;
    const bool to_left = x + 4.0 * scale + width + 2.0 * padding > frame.Left + frame.Width;
    const double box_left = to_left ? x - 4.0 * scale - width - 2.0 * padding : x + 4.0 * scale;
    svg.Rectangle({box_left, y - font_size * 0.7 - padding}, width + 2.0 * padding, font_size * 1.4 + padding,
                  theme.Background, 0.85);
    svg.Text({box_left + padding, y + font_size * 0.35}, note.Text, font_size, theme.Text, TextAnchor::Start);
}

void DrawLegend(SvgWriter &svg, const PanelLayout &layout, const Frame &frame, const ExportTheme &theme,
                const bool dark, const double scale) {
    const double row_height = LegendFont * scale * 1.6;
    const double first_row_y = layout.Top + layout.HeaderHeight - static_cast<double>(layout.LegendRows) * row_height;
    for (std::size_t row = 0; row < layout.LegendRows; ++row) {
        double row_width = 0.0;
        for (const LegendEntry &entry : layout.Legend) {
            if (entry.Row == row) {
                row_width += (row_width > 0.0 ? LegendSpacing * scale : 0.0) + entry.Width;
            }
        }
        double x = frame.Left + frame.Width - row_width;
        const double y = first_row_y + (static_cast<double>(row) + 0.5) * row_height;
        for (const LegendEntry &entry : layout.Legend) {
            if (entry.Row != row) {
                continue;
            }
            const ExportSeries &series = *entry.Series;
            const ExportColor color = dark ? series.Color : AdjustColorForPrint(series.Color);
            const double width = (series.Heavy ? HeavyLineWidth : LineWidth) * scale;
            svg.Polyline({{x, y}, {x + LegendSample * scale, y}}, color, width,
                         series.Dashed ? DashPattern(scale) : "");
            svg.Text({x + (LegendSample + 6.0) * scale, y + LegendFont * scale * 0.35}, series.LegendLabel,
                     LegendFont * scale, theme.Text, TextAnchor::Start);
            x += entry.Width + LegendSpacing * scale;
        }
    }
}

void DrawXAxis(SvgWriter &svg, const ExportFigure &figure, const Frame &frame, const ExportTheme &theme,
               const double scale) {
    const std::vector<double> ticks = FindXTicks(frame.X, figure.XLogarithmic, frame.Width, scale);
    const std::vector<std::string> labels = FormatXTicks(ticks, figure);
    const double bottom = frame.Top + frame.Height;
    for (std::size_t index = 0; index < ticks.size(); ++index) {
        const double x = frame.ToPixelX(ticks[index]);
        svg.Line({x, bottom}, {x, bottom + TickLength * scale}, theme.MutedText, scale);
        svg.Text({x, bottom + (TickLength + TickGap) * scale + TickFont * scale * 0.8}, labels[index], TickFont * scale,
                 theme.MutedText, TextAnchor::Middle);
    }
}

// The first axis is on the left; the others stack outwards on the right, each as wide as its widest labels in
// any panel, with their names in the same columns across panels
void DrawYAxes(SvgWriter &svg, const ExportPanel &panel, const PanelLayout &layout, const Frame &frame,
               const AxisColumns &columns, const ExportTheme &theme, const double scale) {
    double right_edge = frame.Left + frame.Width;
    const double name_y = frame.Top + frame.Height / 2.0;
    for (std::size_t axis = 0; axis < layout.YAxes.size(); ++axis) {
        const YAxisLayout &axis_layout = layout.YAxes[axis];
        const bool left = axis == 0;
        const double edge = left ? frame.Left : right_edge;
        const double direction = left ? -1.0 : 1.0;
        if (!left) {
            svg.Line({edge, frame.Top}, {edge, frame.Top + frame.Height}, theme.Border, scale);
        }
        for (std::size_t index = 0; index < axis_layout.Ticks.size(); ++index) {
            const double y = frame.ToPixelY(axis_layout.Ticks[index], axis_layout.Range);
            svg.Line({edge, y}, {edge + direction * TickLength * scale, y}, theme.MutedText, scale);
            svg.Text({edge + direction * (TickLength + 3.0) * scale, y + TickFont * scale * 0.35},
                     axis_layout.Labels[index], TickFont * scale, theme.MutedText,
                     left ? TextAnchor::End : TextAnchor::Start);
        }
        if (left) {
            svg.Text({columns.LeftNameX, name_y}, panel.YAxes[axis].Name, AxisNameFont * scale, theme.MutedText,
                     TextAnchor::Middle, true);
            continue;
        }
        // A vertical name extends left of its baseline by about its ascent
        const double width = columns.RightWidths[axis - 1];
        svg.Text({edge + width - AxisNameFont * scale * 0.4, name_y}, panel.YAxes[axis].Name, AxisNameFont * scale,
                 theme.MutedText, TextAnchor::Middle, true);
        right_edge += width;
    }
}

void DrawGrid(SvgWriter &svg, const ExportFigure &figure, const PanelLayout &layout, const Frame &frame,
              const ExportTheme &theme, const double scale) {
    const std::vector<double> x_ticks = FindXTicks(frame.X, figure.XLogarithmic, frame.Width, scale);
    for (const double tick : x_ticks) {
        const double x = frame.ToPixelX(tick);
        svg.Line({x, frame.Top}, {x, frame.Top + frame.Height}, theme.Grid, scale, theme.GridOpacity);
    }
    // Minor lines at 2 to 9 times each decade, as on log paper, while there are few decades
    if (figure.XLogarithmic && std::log10(frame.X.High / frame.X.Low) <= 8.0) {
        for (double decade = std::pow(10.0, std::floor(std::log10(frame.X.Low))); decade < frame.X.High;
             decade *= 10.0) {
            for (int multiple = 2; multiple <= 9; ++multiple) {
                const double x = decade * multiple;
                if (x > frame.X.Low && x < frame.X.High) {
                    svg.Line({frame.ToPixelX(x), frame.Top}, {frame.ToPixelX(x), frame.Top + frame.Height}, theme.Grid,
                             scale, theme.MinorGridOpacity);
                }
            }
        }
    }
    if (!layout.YAxes.empty()) {
        const YAxisLayout &first_axis = layout.YAxes.front();
        for (const double tick : first_axis.Ticks) {
            const double y = frame.ToPixelY(tick, first_axis.Range);
            svg.Line({frame.Left, y}, {frame.Left + frame.Width, y}, theme.Grid, scale, theme.GridOpacity);
        }
    }
}

void DrawPanel(SvgWriter &svg, const ExportFigure &figure, const ExportPanel &panel, const PanelLayout &layout,
               const Frame &frame, const AxisColumns &columns, const ExportStyle &style, const double scale) {
    const ExportTheme &theme = style.Dark ? DarkTheme : LightTheme;
    svg.Rectangle({frame.Left, frame.Top}, frame.Width, frame.Height, theme.PlotBackground);
    DrawGrid(svg, figure, layout, frame, theme, scale);
    svg.BeginClip({frame.Left, frame.Top}, frame.Width, frame.Height);
    for (const ExportSeries &series : panel.Series) {
        if (series.Axis < layout.YAxes.size()) {
            DrawSeries(svg, figure, series, frame, layout.YAxes[series.Axis].Range,
                       style.Dark ? series.Color : AdjustColorForPrint(series.Color), scale);
        }
    }
    svg.EndClip();
    for (const ExportNote &note : panel.Notes) {
        if (note.Axis < layout.YAxes.size() && note.X >= frame.X.Low && note.X <= frame.X.High) {
            DrawNote(svg, note, frame, layout.YAxes[note.Axis].Range, theme, scale);
        }
    }
    svg.Polyline({{frame.Left, frame.Top},
                  {frame.Left + frame.Width, frame.Top},
                  {frame.Left + frame.Width, frame.Top + frame.Height},
                  {frame.Left, frame.Top + frame.Height},
                  {frame.Left, frame.Top}},
                 theme.Border, scale);
    DrawXAxis(svg, figure, frame, theme, scale);
    DrawYAxes(svg, panel, layout, frame, columns, theme, scale);
    if (!panel.Title.empty()) {
        svg.Text({frame.Left, layout.Top + TitleFont * scale * 1.1}, panel.Title, TitleFont * scale, theme.Text,
                 TextAnchor::Start);
    }
    DrawLegend(svg, layout, frame, theme, style.Dark, scale);
}

// endregion

// region CSV

// Quotes a field that holds a separator, a quote or a line break, doubling its quotes
std::string QuoteCsvField(const std::string &text) {
    if (text.find_first_of(",\"\n") == std::string::npos) {
        return text;
    }
    std::string quoted = "\"";
    for (const char character : text) {
        quoted += character;
        if (character == '"') {
            quoted += '"';
        }
    }
    return quoted + "\"";
}

std::string CsvHeader(const std::string &name, const std::string &unit) {
    return QuoteCsvField(unit.empty() ? name : std::format("{} ({})", name, unit));
}

// Shortest text that reads back as the same number; missing samples stay empty
std::string FormatCsvValue(const double value) {
    return std::isfinite(value) ? std::format("{}", value) : "";
}

// endregion

} // namespace

/**
 * @brief   Renders a figure as an SVG image.
 * @param[in] figure    Panels and samples to draw; it needs at least one panel and one sample.
 * @param[in] style     Size, between MinExportSize and MaxExportSize pixels on each side, and theme.
 * @return  The SVG document, or the reason it cannot be drawn.
 * @note    The X axis shows figure.XRange when set, and each Y axis fits the samples in that range. Missing
 *          samples (NaN) break the lines, and dense ones closer than half a pixel are merged.
 */
std::expected<std::string, std::string> RenderSvg(const ExportFigure &figure, const ExportStyle &style) {
    if (style.Width < MinExportSize || style.Height < MinExportSize || style.Width > MaxExportSize ||
        style.Height > MaxExportSize) {
        return std::unexpected(
            std::format("Width and height must be between {} and {} pixels", MinExportSize, MaxExportSize));
    }
    if (figure.Panels.empty() || figure.Xs.empty()) {
        return std::unexpected("There is nothing to export");
    }

    const auto width = static_cast<double>(style.Width);
    const auto height = static_cast<double>(style.Height);
    const double scale = std::clamp(std::min(width / DesignWidth, height / DesignHeight), MinScale, MaxScale);
    const double outer = OuterMargin * scale;
    const ValueRange x_range = FindXRange(figure);

    // Heights: each panel has a header with its title and legend, and a row of X tick labels below; the last
    // panel also has the name of the X axis. The plots share the rest equally
    std::vector<PanelLayout> layouts(figure.Panels.size());
    const double legend_row = LegendFont * scale * 1.6;
    const double tick_row = (TickLength + TickGap) * scale + TickFont * scale * 1.2;
    const double axis_name_row = AxisNameFont * scale * 1.8;
    double fixed_height = 2.0 * outer + axis_name_row;
    for (std::size_t index = 0; index < figure.Panels.size(); ++index) {
        const ExportPanel &panel = figure.Panels[index];
        PanelLayout &layout = layouts[index];
        // The legend wraps before it reaches the title, or most of the width when there is none
        const double title_width = panel.Title.empty() ? 0.0 : EstimateTextWidth(panel.Title, TitleFont * scale);
        layout.LegendRows = PlaceLegend(panel, (width - 2.0 * outer) * 0.75 - title_width, scale, layout.Legend);
        const double title_height = panel.Title.empty() ? 0.0 : TitleFont * scale * 1.6;
        layout.HeaderHeight = std::max(title_height, static_cast<double>(layout.LegendRows) * legend_row) + 6.0 * scale;
        fixed_height += layout.HeaderHeight + tick_row;
    }
    const double plot_height =
        std::max(MinPlotSize * scale, (height - fixed_height) / static_cast<double>(figure.Panels.size()));

    // Y axes, and from their tick labels the margins: the left axis on the left, the others outwards on the right
    double left_label_width = 0.0;
    AxisColumns columns;
    std::vector<double> &right_widths = columns.RightWidths;
    for (std::size_t index = 0; index < figure.Panels.size(); ++index) {
        const ExportPanel &panel = figure.Panels[index];
        PanelLayout &layout = layouts[index];
        layout.PlotHeight = plot_height;
        const int tick_count = std::clamp(static_cast<int>(plot_height / (60.0 * scale)), 2, 10);
        for (std::size_t axis = 0; axis < panel.YAxes.size(); ++axis) {
            YAxisLayout axis_layout;
            axis_layout.Range = FindYRange(figure, panel, axis, x_range);
            axis_layout.Ticks = FindNiceTicks(axis_layout.Range.Low, axis_layout.Range.High, tick_count);
            axis_layout.Labels = FormatLinearTicks(axis_layout.Ticks, panel.YAxes[axis]);
            for (const std::string &label : axis_layout.Labels) {
                axis_layout.LabelWidth = std::max(axis_layout.LabelWidth, EstimateTextWidth(label, TickFont * scale));
            }
            if (axis == 0) {
                left_label_width = std::max(left_label_width, axis_layout.LabelWidth);
            } else {
                const double axis_width =
                    (TickLength + 3.0 + TickGap) * scale + axis_layout.LabelWidth + AxisNameFont * scale * 1.4;
                right_widths.resize(std::max(right_widths.size(), axis));
                right_widths[axis - 1] = std::max(right_widths[axis - 1], axis_width);
            }
            layout.YAxes.push_back(std::move(axis_layout));
        }
    }
    const double left = outer + AxisNameFont * scale * 1.4 + (TickLength + 3.0 + TickGap) * scale + left_label_width;
    columns.LeftNameX = outer + AxisNameFont * scale;
    double right = outer;
    for (const double axis_width : right_widths) {
        right += axis_width;
    }
    // Room for half of the last X tick label when no axis is on the right
    right = std::max(right, outer + 30.0 * scale);
    const double plot_width = std::max(MinPlotSize * scale, width - left - right);

    const ExportTheme &theme = style.Dark ? DarkTheme : LightTheme;
    SvgWriter svg;
    double top = outer;
    Frame frame{left, 0.0, plot_width, plot_height, x_range, figure.XLogarithmic};
    for (std::size_t index = 0; index < figure.Panels.size(); ++index) {
        PanelLayout &layout = layouts[index];
        layout.Top = top;
        frame.Top = top + layout.HeaderHeight;
        DrawPanel(svg, figure, figure.Panels[index], layout, frame, columns, style, scale);
        top = frame.Top + plot_height + tick_row;
    }
    svg.Text({left + plot_width / 2.0, top + AxisNameFont * scale * 1.1}, figure.XAxis.Name, AxisNameFont * scale,
             theme.MutedText, TextAnchor::Middle);
    return svg.Finish({0.0, 0.0}, width, height, theme.Background);
}

/**
 * @brief   Writes the samples of a figure as a CSV table.
 * @param[in] figure    Figure whose X samples and series become the columns; its X range is ignored.
 * @return  A header row, then one row per X sample: the X value, then each series of each panel in order.
 * @note    Headers carry the unit of their axis, and the panel title when the figure has more than one panel,
 *          since the panels of a Bode plot list the same traces. Missing samples are left empty.
 */
std::string RenderCsv(const ExportFigure &figure) {
    std::vector<const ExportSeries *> columns;
    std::string text = CsvHeader(figure.XAxis.Name, figure.XAxis.Unit);
    for (const ExportPanel &panel : figure.Panels) {
        for (const ExportSeries &series : panel.Series) {
            const std::string name = figure.Panels.size() > 1 && !panel.Title.empty()
                                         ? std::format("{} {}", panel.Title, series.Name)
                                         : series.Name;
            const std::string unit = series.Axis < panel.YAxes.size() ? panel.YAxes[series.Axis].Unit : "";
            text += "," + CsvHeader(name, unit);
            columns.push_back(&series);
        }
    }
    text += "\n";
    for (std::size_t row = 0; row < figure.Xs.size(); ++row) {
        text += FormatCsvValue(figure.Xs[row]);
        for (const ExportSeries *series : columns) {
            text += ",";
            if (row < series->Values.size()) {
                text += FormatCsvValue(series->Values[row]);
            }
        }
        text += "\n";
    }
    return text;
}

/**
 * @brief   Finds round tick positions (1, 2 or 5 times a power of ten) across a range.
 * @param[in] from          Lower end of the range.
 * @param[in] to            Upper end; must be above @p from.
 * @param[in] target_count  About how many intervals are wanted; at least 1.
 * @return  Ticks inside the range in ascending order, at most 64; empty for an invalid range or count.
 */
std::vector<double> FindNiceTicks(const double from, const double to, const int target_count) {
    std::vector<double> ticks;
    if (!(to > from) || !std::isfinite(from) || !std::isfinite(to) || target_count < 1) {
        return ticks;
    }
    const double raw_step = (to - from) / target_count;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw_step)));
    const double normalized = raw_step / magnitude;
    const double step = (normalized <= 1.5   ? 1.0
                         : normalized <= 3.0 ? 2.0
                         : normalized <= 7.0 ? 5.0
                                             : 10.0) *
                        magnitude;
    const double first = std::ceil(from / step - 1e-9);
    for (double index = first; ticks.size() < 64; index += 1.0) {
        const double tick = index * step;
        if (tick > to + step * 1e-9) {
            break;
        }
        // Rounding leaves residues such as 1e-17 where the tick is zero
        ticks.push_back(std::abs(tick) < step * 1e-9 ? 0.0 : tick);
    }
    return ticks;
}

/**
 * @brief   Finds the powers of ten across a range, for a logarithmic axis.
 * @param[in] from      Lower end of the range; must be positive.
 * @param[in] to        Upper end; must be above @p from.
 * @param[in] max_count Most ticks wanted; when there are more decades, every second, third... decade is kept.
 * @return  Powers of ten inside the range in ascending order; empty for an invalid range.
 */
std::vector<double> FindDecadeTicks(const double from, const double to, const int max_count) {
    std::vector<double> ticks;
    if (!(from > 0.0) || !(to > from) || !std::isfinite(to) || max_count < 1) {
        return ticks;
    }
    const auto first = static_cast<int>(std::ceil(std::log10(from) - 1e-9));
    const auto last = static_cast<int>(std::floor(std::log10(to) + 1e-9));
    const int stride = std::max(1, (last - first + max_count) / max_count);
    for (int exponent = first; exponent <= last; exponent += stride) {
        ticks.push_back(std::pow(10.0, exponent));
    }
    return ticks;
}

/**
 * @brief   Formats a tick label for an axis whose ticks are a given step apart.
 * @param[in] value Tick value.
 * @param[in] step  Distance between ticks; sets how many decimals are shown.
 * @return  "0" for values that round to zero, never "-0"; exponent notation such as "2e-5" below 1e-3 or from 1e6
 *          in magnitude; otherwise the fewest decimals that show the step exactly.
 */
std::string FormatTick(const double value, const double step) {
    const double magnitude = std::abs(value);
    if (magnitude == 0.0 || magnitude < std::abs(step) * 1e-6) {
        return "0";
    }
    if (magnitude >= 1e6 || magnitude < 1e-3) {
        const auto value_exponent = static_cast<int>(std::floor(std::log10(magnitude)));
        const auto step_exponent = static_cast<int>(std::floor(std::log10(std::abs(step)) + 1e-9));
        const std::string text = std::format("{:.{}e}", value, std::clamp(value_exponent - step_exponent, 0, 6));
        const std::size_t exponent_position = text.find('e');
        return std::format("{}e{}", text.substr(0, exponent_position), std::stoi(text.substr(exponent_position + 1)));
    }
    const double step_magnitude = std::abs(step);
    int decimals = 0;
    while (decimals < 8) {
        const double scaled = step_magnitude * std::pow(10.0, decimals);
        if (std::abs(scaled - std::round(scaled)) <= 1e-6 * scaled) {
            break;
        }
        ++decimals;
    }
    const std::string text = std::format("{:.{}f}", value, decimals);
    return text.find_first_not_of("-0.") == std::string::npos ? "0" : text;
}

} // namespace GUI
