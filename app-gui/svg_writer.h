/**
 * @file    svg_writer.h
 * @brief   Writes the SVG elements that exported plots and schematics are made of.
 */

#ifndef IMCSIM_SVG_WRITER_H
#define IMCSIM_SVG_WRITER_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
 * @struct  SvgPoint
 * @brief   A point of an SVG image, in pixels with y down.
 */
struct SvgPoint {
    double X = 0.0;
    double Y = 0.0;
};

/**
 * @enum    TextAnchor
 * @brief   Which point of a text its position gives: its start, its middle or its end.
 */
enum class TextAnchor {
    Start,
    Middle,
    End
};

/**
 * @class   SvgWriter
 * @brief   Collects SVG elements and wraps them in a document once the area they cover is known.
 */
class SvgWriter {
public:
    // Document
    std::string Finish(SvgPoint corner, double width, double height,
                       std::optional<ExportColor> background = std::nullopt) const;

    // Shapes
    void Rectangle(SvgPoint corner, double width, double height, ExportColor fill, double opacity = 1.0);
    void Line(SvgPoint start, SvgPoint end, ExportColor color, double width, double opacity = 1.0);
    void Polyline(const std::vector<SvgPoint> &points, ExportColor color, double width,
                  const std::string &dash_pattern = "", bool closed = false);
    void Polygon(const std::vector<SvgPoint> &points, ExportColor fill);
    void Circle(SvgPoint center, double radius, ExportColor color, double width);
    void FilledCircle(SvgPoint center, double radius, ExportColor fill);
    void Text(SvgPoint baseline, std::string_view text, double font_size, ExportColor color, TextAnchor anchor,
              bool vertical = false);

    // Clipping
    void BeginClip(SvgPoint corner, double width, double height);
    void EndClip();

private:
    std::string m_Body;
    int m_NextClip = 0;
};

// Formatting
std::string FormatCoordinate(double value);
ExportColor AdjustColorForPrint(ExportColor color);

} // namespace GUI

#endif // IMCSIM_SVG_WRITER_H
