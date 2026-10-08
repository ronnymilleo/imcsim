/**
 * @file    plot_export.h
 * @brief   Renders plots described as data into SVG images and CSV tables for reports.
 * @details The Output window describes what it plots as an ExportFigure; this file draws it again on its own, so
 *          the image does not depend on the screen, its size or its theme.
 */

#ifndef IMCSIM_PLOT_EXPORT_H
#define IMCSIM_PLOT_EXPORT_H

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  ExportColor
 * @brief   An 8-bit sRGB color.
 */
struct ExportColor {
    std::uint8_t Red = 0;
    std::uint8_t Green = 0;
    std::uint8_t Blue = 0;
};

/**
 * @struct  ExportAxis
 * @brief   Name and unit of an axis, and how its ticks are written.
 * @details With prefixes, ticks read like the values the user types followed by the unit ("10ms", "1kHz");
 *          decibels and degrees read better as plain numbers, so their unit is left to the name.
 */
struct ExportAxis {
    std::string Name;
    std::string Unit;
    bool UsesPrefixes = true;
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
 * @struct  ExportRange
 * @brief   The part of the X axis that the image shows.
 */
struct ExportRange {
    double From = 0.0;
    double To = 0.0;
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
