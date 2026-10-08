/**
 * @file    schematic_canvas.cpp
 * @brief   Where schematic symbols are drawn: the editor's ImGui draw list or an SVG image.
 */

#include "schematic_canvas.h"

#include <algorithm>
#include <cfloat>
#include <vector>

namespace GUI {

namespace {

// ImGui sizes a font by its full height, from ascent to descent; SVG and CSS by its em square, which is smaller.
// Common sans fonts span about 1.33 em (Noto Sans 1.36, Inter 1.21, DejaVu Sans 1.16), so this keeps exported text
// close to the size it has on screen
constexpr float ImGuiToSvgFontSize = 0.75f;

SvgPoint ToSvg(const ImVec2 point) {
    return {point.x, point.y};
}

std::vector<SvgPoint> ToSvg(const ImVec2 *points, const int count) {
    std::vector<SvgPoint> converted;
    for (int index = 0; index < count; ++index) {
        converted.push_back(ToSvg(points[index]));
    }
    return converted;
}

} // namespace

/**
 * @brief   Creates a canvas that draws into an ImGui draw list.
 * @param[in] draw_list  Draw list of the window; it must outlive the canvas.
 */
DrawListCanvas::DrawListCanvas(ImDrawList *draw_list) : m_DrawList(draw_list) {
}

void DrawListCanvas::AddLine(const ImVec2 start, const ImVec2 end, const ImU32 color, const float thickness) {
    m_DrawList->AddLine(start, end, color, thickness);
}

void DrawListCanvas::AddPolyline(const ImVec2 *points, const int count, const ImU32 color, const ImDrawFlags flags,
                                 const float thickness) {
    m_DrawList->AddPolyline(points, count, color, flags, thickness);
}

void DrawListCanvas::AddTriangle(const ImVec2 first, const ImVec2 second, const ImVec2 third, const ImU32 color,
                                 const float thickness) {
    m_DrawList->AddTriangle(first, second, third, color, thickness);
}

void DrawListCanvas::AddTriangleFilled(const ImVec2 first, const ImVec2 second, const ImVec2 third, const ImU32 color) {
    m_DrawList->AddTriangleFilled(first, second, third, color);
}

void DrawListCanvas::AddRectFilled(const ImVec2 min, const ImVec2 max, const ImU32 color) {
    m_DrawList->AddRectFilled(min, max, color);
}

void DrawListCanvas::AddCircle(const ImVec2 center, const float radius, const ImU32 color, const int segments,
                               const float thickness) {
    m_DrawList->AddCircle(center, radius, color, segments, thickness);
}

void DrawListCanvas::AddCircleFilled(const ImVec2 center, const float radius, const ImU32 color) {
    m_DrawList->AddCircleFilled(center, radius, color);
}

void DrawListCanvas::AddText(ImFont *font, const float font_size, const ImVec2 position, const ImU32 color,
                             const char *text) {
    m_DrawList->AddText(font, font_size, position, color, text);
}

void DrawListCanvas::AddText(const ImVec2 position, const ImU32 color, const char *text) {
    m_DrawList->AddText(position, color, text);
}

/**
 * @brief   Creates a canvas that draws into an SVG image.
 * @param[in] svg        Writer that collects the elements; it must outlive the canvas.
 * @param[in] for_print  Darkens colors that would be too light on white paper.
 */
SvgCanvas::SvgCanvas(SvgWriter &svg, const bool for_print) : m_Svg(svg), m_ForPrint(for_print) {
}

void SvgCanvas::AddLine(const ImVec2 start, const ImVec2 end, const ImU32 color, const float thickness) {
    m_Svg.Line(ToSvg(start), ToSvg(end), Convert(color), thickness);
    Cover(start, thickness);
    Cover(end, thickness);
}

void SvgCanvas::AddPolyline(const ImVec2 *points, const int count, const ImU32 color, const ImDrawFlags flags,
                            const float thickness) {
    m_Svg.Polyline(ToSvg(points, count), Convert(color), thickness, "", (flags & ImDrawFlags_Closed) != 0);
    for (int index = 0; index < count; ++index) {
        Cover(points[index], thickness);
    }
}

void SvgCanvas::AddTriangle(const ImVec2 first, const ImVec2 second, const ImVec2 third, const ImU32 color,
                            const float thickness) {
    AddPolyline(std::vector<ImVec2>{first, second, third}.data(), 3, color, ImDrawFlags_Closed, thickness);
}

void SvgCanvas::AddTriangleFilled(const ImVec2 first, const ImVec2 second, const ImVec2 third, const ImU32 color) {
    m_Svg.Polygon({ToSvg(first), ToSvg(second), ToSvg(third)}, Convert(color));
    for (const ImVec2 corner : {first, second, third}) {
        Cover(corner, 0.0f);
    }
}

void SvgCanvas::AddRectFilled(const ImVec2 min, const ImVec2 max, const ImU32 color) {
    m_Svg.Rectangle(ToSvg(min), max.x - min.x, max.y - min.y, Convert(color));
    Cover(min, 0.0f);
    Cover(max, 0.0f);
}

void SvgCanvas::AddCircle(const ImVec2 center, const float radius, const ImU32 color, const int /*segments*/,
                          const float thickness) {
    m_Svg.Circle(ToSvg(center), radius, Convert(color), thickness);
    Cover(center, radius + thickness);
}

void SvgCanvas::AddCircleFilled(const ImVec2 center, const float radius, const ImU32 color) {
    m_Svg.FilledCircle(ToSvg(center), radius, Convert(color));
    Cover(center, radius);
}

// ImGui places text by its top left corner; SVG by a point on the baseline. The ascent of the ImGui font puts the
// baseline where the screen has it
void SvgCanvas::AddText(ImFont *font, const float font_size, const ImVec2 position, const ImU32 color,
                        const char *text) {
    const ImVec2 size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text);
    const float ascent = font->GetFontBaked(font_size)->Ascent;
    m_Svg.Text({position.x + size.x / 2.0f, position.y + ascent}, text, font_size * ImGuiToSvgFontSize, Convert(color),
               TextAnchor::Middle);
    Cover(position, 0.0f);
    Cover(position + size, 0.0f);
}

void SvgCanvas::AddText(const ImVec2 position, const ImU32 color, const char *text) {
    AddText(ImGui::GetFont(), ImGui::GetFontSize(), position, color, text);
}

/**
 * @brief   Returns the top left corner of the area drawn so far.
 * @return  Corner in drawing pixels; meaningless until something is drawn.
 */
ImVec2 SvgCanvas::GetMin() const {
    return m_Min;
}

/**
 * @brief   Returns the bottom right corner of the area drawn so far.
 * @return  Corner in drawing pixels; meaningless until something is drawn.
 */
ImVec2 SvgCanvas::GetMax() const {
    return m_Max;
}

// Transparency is dropped: exported schematics have no overlapping translucent shapes
ExportColor SvgCanvas::Convert(const ImU32 color) const {
    const ExportColor converted = {static_cast<std::uint8_t>((color >> IM_COL32_R_SHIFT) & 0xFF),
                                   static_cast<std::uint8_t>((color >> IM_COL32_G_SHIFT) & 0xFF),
                                   static_cast<std::uint8_t>((color >> IM_COL32_B_SHIFT) & 0xFF)};
    return m_ForPrint ? AdjustColorForPrint(converted) : converted;
}

void SvgCanvas::Cover(const ImVec2 point, const float margin) {
    m_Min = {std::min(m_Min.x, point.x - margin), std::min(m_Min.y, point.y - margin)};
    m_Max = {std::max(m_Max.x, point.x + margin), std::max(m_Max.y, point.y + margin)};
}

} // namespace GUI
