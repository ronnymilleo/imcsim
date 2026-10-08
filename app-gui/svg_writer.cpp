/**
 * @file    svg_writer.cpp
 * @brief   Writes the SVG elements that exported plots and schematics are made of.
 */

#include "svg_writer.h"

#include <cmath>
#include <format>

namespace GUI {

namespace {

// The fonts the application itself prefers, then common fallbacks; the viewer picks the first it has
constexpr std::string_view FontFamily =
    "Inter, 'Noto Sans', 'DejaVu Sans', 'Liberation Sans', Helvetica, Arial, sans-serif";

// Trace colors are picked for a dark screen; on white, the lighter ones (yellow, pale gray) are darkened to this
// relative luminance so they stay readable in print
constexpr double MaxPrintLuminance = 0.5;

std::string FormatColor(const ExportColor color) {
    return std::format("#{:02x}{:02x}{:02x}", color.Red, color.Green, color.Blue);
}

std::string FormatOpacity(const char *name, const double opacity) {
    return opacity >= 0.999 ? "" : std::format(" {}=\"{}\"", name, FormatCoordinate(opacity));
}

std::string FormatPoints(const std::vector<SvgPoint> &points) {
    std::string text;
    for (const SvgPoint &point : points) {
        text += std::format("{}{},{}", text.empty() ? "" : " ", FormatCoordinate(point.X), FormatCoordinate(point.Y));
    }
    return text;
}

std::string EscapeXml(const std::string_view text) {
    std::string escaped;
    for (const char character : text) {
        switch (character) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        default:
            escaped += character;
        }
    }
    return escaped;
}

} // namespace

/**
 * @brief   Wraps the elements written so far in an SVG document.
 * @param[in] corner        Top left corner of the area the image shows.
 * @param[in] width         Width of that area, which is also the image width in pixels.
 * @param[in] height        Height of that area, which is also the image height in pixels.
 * @param[in] background    Color that fills the whole area under the elements; none leaves it transparent.
 * @return  The SVG document.
 */
std::string SvgWriter::Finish(const SvgPoint corner, const double width, const double height,
                              const std::optional<ExportColor> background) const {
    std::string document =
        std::format("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{}\" height=\"{}\" viewBox=\"{} {} {} {}\" "
                    "font-family=\"{}\">\n",
                    FormatCoordinate(width), FormatCoordinate(height), FormatCoordinate(corner.X),
                    FormatCoordinate(corner.Y), FormatCoordinate(width), FormatCoordinate(height), FontFamily);
    if (background) {
        document += std::format("<rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" fill=\"{}\"/>\n",
                                FormatCoordinate(corner.X), FormatCoordinate(corner.Y), FormatCoordinate(width),
                                FormatCoordinate(height), FormatColor(*background));
    }
    return document + m_Body + "</svg>\n";
}

/**
 * @brief   Adds a filled rectangle.
 * @param[in] corner    Top left corner.
 * @param[in] width     Width in pixels.
 * @param[in] height    Height in pixels.
 * @param[in] fill      Fill color.
 * @param[in] opacity   Fill opacity, from 0 to 1.
 */
void SvgWriter::Rectangle(const SvgPoint corner, const double width, const double height, const ExportColor fill,
                          const double opacity) {
    m_Body += std::format("<rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" fill=\"{}\"{}/>\n",
                          FormatCoordinate(corner.X), FormatCoordinate(corner.Y), FormatCoordinate(width),
                          FormatCoordinate(height), FormatColor(fill), FormatOpacity("fill-opacity", opacity));
}

/**
 * @brief   Adds a straight line.
 * @param[in] start     First end.
 * @param[in] end       Second end.
 * @param[in] color     Stroke color.
 * @param[in] width     Stroke width in pixels.
 * @param[in] opacity   Stroke opacity, from 0 to 1.
 */
void SvgWriter::Line(const SvgPoint start, const SvgPoint end, const ExportColor color, const double width,
                     const double opacity) {
    m_Body += std::format("<line x1=\"{}\" y1=\"{}\" x2=\"{}\" y2=\"{}\" stroke=\"{}\" stroke-width=\"{}\"{}/>\n",
                          FormatCoordinate(start.X), FormatCoordinate(start.Y), FormatCoordinate(end.X),
                          FormatCoordinate(end.Y), FormatColor(color), FormatCoordinate(width),
                          FormatOpacity("stroke-opacity", opacity));
}

/**
 * @brief   Adds a line through several points, with round joins and caps.
 * @param[in] points        Points in drawing order.
 * @param[in] color         Stroke color.
 * @param[in] width         Stroke width in pixels.
 * @param[in] dash_pattern  SVG dash lengths such as "8 5"; empty for a solid line.
 * @param[in] closed        Joins the last point back to the first.
 */
void SvgWriter::Polyline(const std::vector<SvgPoint> &points, const ExportColor color, const double width,
                         const std::string &dash_pattern, const bool closed) {
    m_Body += std::format("<{} fill=\"none\" stroke=\"{}\" stroke-width=\"{}\" stroke-linejoin=\"round\" "
                          "stroke-linecap=\"round\"",
                          closed ? "polygon" : "polyline", FormatColor(color), FormatCoordinate(width));
    if (!dash_pattern.empty()) {
        m_Body += std::format(" stroke-dasharray=\"{}\"", dash_pattern);
    }
    m_Body += std::format(" points=\"{}\"/>\n", FormatPoints(points));
}

/**
 * @brief   Adds a filled polygon without outline.
 * @param[in] points    Corners in order.
 * @param[in] fill      Fill color.
 */
void SvgWriter::Polygon(const std::vector<SvgPoint> &points, const ExportColor fill) {
    m_Body += std::format("<polygon fill=\"{}\" points=\"{}\"/>\n", FormatColor(fill), FormatPoints(points));
}

/**
 * @brief   Adds the outline of a circle.
 * @param[in] center    Center.
 * @param[in] radius    Radius in pixels.
 * @param[in] color     Stroke color.
 * @param[in] width     Stroke width in pixels.
 */
void SvgWriter::Circle(const SvgPoint center, const double radius, const ExportColor color, const double width) {
    m_Body += std::format("<circle cx=\"{}\" cy=\"{}\" r=\"{}\" fill=\"none\" stroke=\"{}\" stroke-width=\"{}\"/>\n",
                          FormatCoordinate(center.X), FormatCoordinate(center.Y), FormatCoordinate(radius),
                          FormatColor(color), FormatCoordinate(width));
}

/**
 * @brief   Adds a filled circle without outline.
 * @param[in] center    Center.
 * @param[in] radius    Radius in pixels.
 * @param[in] fill      Fill color.
 */
void SvgWriter::FilledCircle(const SvgPoint center, const double radius, const ExportColor fill) {
    m_Body += std::format("<circle cx=\"{}\" cy=\"{}\" r=\"{}\" fill=\"{}\"/>\n", FormatCoordinate(center.X),
                          FormatCoordinate(center.Y), FormatCoordinate(radius), FormatColor(fill));
}

/**
 * @brief   Adds a line of text.
 * @param[in] baseline  Point on the baseline that the anchor refers to.
 * @param[in] text      UTF-8 text; XML characters are escaped.
 * @param[in] font_size Font size in pixels.
 * @param[in] color     Fill color.
 * @param[in] anchor    Whether the point is the start, the middle or the end of the text.
 * @param[in] vertical  Turns the text a quarter turn counter-clockwise, to read bottom to top.
 */
void SvgWriter::Text(const SvgPoint baseline, const std::string_view text, const double font_size,
                     const ExportColor color, const TextAnchor anchor, const bool vertical) {
    const char *anchor_name = anchor == TextAnchor::Start ? "start" : anchor == TextAnchor::Middle ? "middle" : "end";
    m_Body += std::format("<text x=\"{}\" y=\"{}\" font-size=\"{}\" fill=\"{}\" text-anchor=\"{}\"",
                          FormatCoordinate(baseline.X), FormatCoordinate(baseline.Y), FormatCoordinate(font_size),
                          FormatColor(color), anchor_name);
    if (vertical) {
        m_Body +=
            std::format(" transform=\"rotate(-90 {} {})\"", FormatCoordinate(baseline.X), FormatCoordinate(baseline.Y));
    }
    m_Body += std::format(">{}</text>\n", EscapeXml(text));
}

/**
 * @brief   Clips the elements added until EndClip() to a rectangle.
 * @param[in] corner    Top left corner.
 * @param[in] width     Width in pixels.
 * @param[in] height    Height in pixels.
 */
void SvgWriter::BeginClip(const SvgPoint corner, const double width, const double height) {
    const std::string id = std::format("clip{}", m_NextClip++);
    m_Body += std::format("<clipPath id=\"{}\"><rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\"/></clipPath>\n"
                          "<g clip-path=\"url(#{})\">\n",
                          id, FormatCoordinate(corner.X), FormatCoordinate(corner.Y), FormatCoordinate(width),
                          FormatCoordinate(height), id);
}

/**
 * @brief   Ends the clipping started by BeginClip().
 */
void SvgWriter::EndClip() {
    m_Body += "</g>\n";
}

/**
 * @brief   Formats a coordinate or a size for an SVG attribute.
 * @param[in] value Value in pixels.
 * @return  The value with two decimals at most, without trailing zeros, and never "-0".
 */
std::string FormatCoordinate(const double value) {
    std::string text = std::format("{:.2f}", value);
    while (text.back() == '0') {
        text.pop_back();
    }
    if (text.back() == '.') {
        text.pop_back();
    }
    return text == "-0" ? "0" : text;
}

/**
 * @brief   Darkens a color that would be too light to read on white paper.
 * @param[in] color Color picked for the dark screen.
 * @return  The same color when it is dark enough; otherwise the same hue scaled down to a readable luminance.
 */
ExportColor AdjustColorForPrint(const ExportColor color) {
    const double luminance = (0.2126 * color.Red + 0.7152 * color.Green + 0.0722 * color.Blue) / 255.0;
    if (luminance <= MaxPrintLuminance) {
        return color;
    }
    const double factor = MaxPrintLuminance / luminance;
    const auto darken = [factor](const std::uint8_t channel) {
        return static_cast<std::uint8_t>(std::lround(channel * factor));
    };
    return {darken(color.Red), darken(color.Green), darken(color.Blue)};
}

} // namespace GUI
