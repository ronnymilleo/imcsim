/**
 * @file    plot_export.h
 * @brief   Renders plots described as data into SVG images and CSV tables for reports.
 * @details The Output window describes what it plots as an ExportFigure; this file draws it again on its own, so
 *          the image does not depend on the screen, its size or its theme.
 */

#ifndef IMCSIM_PLOT_EXPORT_H
#define IMCSIM_PLOT_EXPORT_H

#include "svg_writer.h"
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  ExportRange
 * @brief   The part of an axis that the image shows.
 */
struct ExportRange {
    double From = 0.0;
    double To = 0.0;
};

/**
 * @struct  ExportTick
 * @brief   A tick of an axis with a name in place of its value, such as a lane of logic signals.
 */
struct ExportTick {
    double Value = 0.0;
    std::string Label;
};

/**
 * @struct  ExportAxis
 * @brief   Name and unit of an axis, and how its ticks are written.
 * @details With prefixes, ticks read like the values the user types followed by the unit ("10ms", "1kHz");
 *          decibels and degrees read better as plain numbers, so their unit is left to the name. A logarithmic Y
 *          axis, as for noise densities, has its ticks at decades and leaves out values that are not positive; the X
 *          axis takes its scale from the figure instead. An axis may also hold named ticks and a fixed range, as the
 *          lanes of logic signals do.
 */
struct ExportAxis {
    std::string Name;
    std::string Unit;
    bool UsesPrefixes = true;
    bool Logarithmic = false;
    // Empty: round ticks across the range of the samples, or decades on a logarithmic axis
    std::vector<ExportTick> Ticks;
    // Empty: the range of the samples, with some room
    std::optional<ExportRange> Range;
};

/**
 * @struct  ExportSeries
 * @brief   One trace of a panel, sampled at the X values of its figure.
 */
struct ExportSeries {
    // Heads its CSV column, with the unit of its axis
    std::string Name;
    // Empty: no legend entry, as for the second and later curves of a DC sweep family
    std::string LegendLabel;
    std::vector<double> Values;
    // Empty: the CSV lists the values. Otherwise it lists these, for values that only stand for something, such as
    // the heights of a logic signal in its lane standing for its voltages
    std::vector<double> Samples;
    // Index in the Y axes of the panel
    std::size_t Axis = 0;
    ExportColor Color;
    bool Dashed = false;
    bool Heavy = false;
};

/**
 * @struct  ExportNote
 * @brief   A short text placed at a point of a panel, such as the stepped value of a DC sweep curve.
 */
struct ExportNote {
    double X = 0.0;
    double Y = 0.0;
    std::size_t Axis = 0;
    std::string Text;
    // Centered on its point instead of beside it, as the bit labels of a logic lane are
    bool Centered = false;
};

/**
 * @struct  ExportPanel
 * @brief   One set of axes with its traces; the first Y axis is on the left and any others on the right.
 */
struct ExportPanel {
    std::string Title;
    std::vector<ExportAxis> YAxes;
    std::vector<ExportSeries> Series;
    std::vector<ExportNote> Notes;
};

/**
 * @struct  ExportFigure
 * @brief   Panels stacked top to bottom that share one X axis and its samples.
 */
struct ExportFigure {
    ExportAxis XAxis;
    bool XLogarithmic = false;
    std::vector<double> Xs;
    // Empty: the whole X data
    std::optional<ExportRange> XRange;
    std::vector<ExportPanel> Panels;
};

/**
 * @struct  ExportStyle
 * @brief   Image size in pixels and theme: light for print, or the dark look of the application.
 */
struct ExportStyle {
    int Width = 1600;
    int Height = 900;
    bool Dark = false;
};

constexpr int MinExportSize = 200;
constexpr int MaxExportSize = 8192;

// Rendering
std::expected<std::string, std::string> RenderSvg(const ExportFigure &figure, const ExportStyle &style);
std::string RenderCsv(const ExportFigure &figure);

// Axis ticks
std::vector<double> FindNiceTicks(double from, double to, int target_count);
std::vector<double> FindDecadeTicks(double from, double to, int max_count);
std::string FormatTick(double value, double step);

} // namespace GUI

#endif // IMCSIM_PLOT_EXPORT_H
