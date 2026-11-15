/**
 * @file    plot_export_style.h
 * @brief   Colors and sizes that the SVG images of the plot export share, for the cartesian panels and the Smith chart.
 */

#ifndef IMCSIM_PLOT_EXPORT_STYLE_H
#define IMCSIM_PLOT_EXPORT_STYLE_H

#include "base/svg_writer.h"
#include <string_view>

namespace GUI {

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

/// Follows Graphite, the default theme of the application.
inline constexpr ExportTheme DARK_THEME = {
    .Background = {30, 34, 41},
    .PlotBackground = {22, 25, 30},
    .Text = {225, 230, 237},
    .MutedText = {139, 148, 160},
    .Border = {61, 68, 80},
    .Grid = {255, 255, 255},
    .GridOpacity = 0.08,
    .MinorGridOpacity = 0.035,
};
/// Plain black on white, for print.
inline constexpr ExportTheme LIGHT_THEME = {
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
inline constexpr double DESIGN_WIDTH = 1600.0;
inline constexpr double DESIGN_HEIGHT = 900.0;
inline constexpr double MIN_SCALE = 0.45;
inline constexpr double MAX_SCALE = 3.0;
inline constexpr double OUTER_MARGIN = 18.0;
inline constexpr double TICK_FONT = 14.0;
inline constexpr double AXIS_NAME_FONT = 16.0;
inline constexpr double TITLE_FONT = 16.0;
inline constexpr double LEGEND_FONT = 14.0;
inline constexpr double NOTE_FONT = 13.0;
inline constexpr double TICK_LENGTH = 5.0;
inline constexpr double TICK_GAP = 8.0;
inline constexpr double LINE_WIDTH = 2.0;
inline constexpr double HEAVY_LINE_WIDTH = 2.6;
inline constexpr double DASH_LENGTH = 8.0;
inline constexpr double DASH_GAP = 5.0;
inline constexpr double LEGEND_SAMPLE = 26.0;
inline constexpr double LEGEND_SPACING = 20.0;
inline constexpr double MIN_PLOT_SIZE = 40.0;
/// Average advance of a sans-serif character, in font sizes.
inline constexpr double CHARACTER_WIDTH = 0.56;
/// Data range added above and below the traces, as a fraction of it.
inline constexpr double Y_PADDING = 0.08;
/// Samples closer than this to the last one drawn, in pixels, add nothing to the line.
inline constexpr double MIN_POINT_DISTANCE = 0.5;

// Text
double EstimateTextWidth(std::string_view text, double font_size);

} // namespace GUI

#endif // IMCSIM_PLOT_EXPORT_STYLE_H
