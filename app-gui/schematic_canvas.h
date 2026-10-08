/**
 * @file    schematic_canvas.h
 * @brief   Where schematic symbols are drawn: the editor's ImGui draw list or an SVG image.
 */

#ifndef IMCSIM_SCHEMATIC_CANVAS_H
#define IMCSIM_SCHEMATIC_CANVAS_H

#include "imgui.h"
#include "svg_writer.h"
#include <limits>

namespace GUI {

/**
 * @class   SchematicCanvas
 * @brief   The drawing calls that schematic symbols, wires and probes use.
 * @details The methods copy the names and parameters of the ImDrawList calls they stand for, so drawing code reads
 *          the same whether it goes to the screen or to an exported image. A new kind of shape needs a method here
 *          and in both implementations.
 */
class SchematicCanvas {
public:
    virtual ~SchematicCanvas() = default;

    virtual void AddLine(ImVec2 start, ImVec2 end, ImU32 color, float thickness) = 0;
    virtual void AddPolyline(const ImVec2 *points, int count, ImU32 color, ImDrawFlags flags, float thickness) = 0;
    virtual void AddTriangle(ImVec2 first, ImVec2 second, ImVec2 third, ImU32 color, float thickness) = 0;
    virtual void AddTriangleFilled(ImVec2 first, ImVec2 second, ImVec2 third, ImU32 color) = 0;
    virtual void AddRectFilled(ImVec2 min, ImVec2 max, ImU32 color) = 0;
    virtual void AddCircle(ImVec2 center, float radius, ImU32 color, int segments, float thickness) = 0;
    virtual void AddCircleFilled(ImVec2 center, float radius, ImU32 color) = 0;
    virtual void AddText(ImFont *font, float font_size, ImVec2 position, ImU32 color, const char *text) = 0;
    virtual void AddText(ImVec2 position, ImU32 color, const char *text) = 0;
};

/**
 * @class   DrawListCanvas
 * @brief   Draws on the screen by passing every call to an ImGui draw list unchanged.
 */
class DrawListCanvas final : public SchematicCanvas {
public:
    explicit DrawListCanvas(ImDrawList *draw_list);

    void AddLine(ImVec2 start, ImVec2 end, ImU32 color, float thickness) override;
    void AddPolyline(const ImVec2 *points, int count, ImU32 color, ImDrawFlags flags, float thickness) override;
    void AddTriangle(ImVec2 first, ImVec2 second, ImVec2 third, ImU32 color, float thickness) override;
    void AddTriangleFilled(ImVec2 first, ImVec2 second, ImVec2 third, ImU32 color) override;
    void AddRectFilled(ImVec2 min, ImVec2 max, ImU32 color) override;
    void AddCircle(ImVec2 center, float radius, ImU32 color, int segments, float thickness) override;
    void AddCircleFilled(ImVec2 center, float radius, ImU32 color) override;
    void AddText(ImFont *font, float font_size, ImVec2 position, ImU32 color, const char *text) override;
    void AddText(ImVec2 position, ImU32 color, const char *text) override;

private:
    ImDrawList *m_DrawList;
};

/**
 * @class   SvgCanvas
 * @brief   Draws into an SVG image, keeping track of the area the drawing covers so the image can be cropped to it.
 * @details Text is measured with the ImGui font, as on screen, and anchored at its middle, so it stays centered
 *          where the symbol placed it even when the viewer renders it with another font. For print, colors too
 *          light for white paper are darkened, as in exported plots, so probes keep the colors of their traces.
 */
class SvgCanvas final : public SchematicCanvas {
public:
    SvgCanvas(SvgWriter &svg, bool for_print);

    void AddLine(ImVec2 start, ImVec2 end, ImU32 color, float thickness) override;
    void AddPolyline(const ImVec2 *points, int count, ImU32 color, ImDrawFlags flags, float thickness) override;
    void AddTriangle(ImVec2 first, ImVec2 second, ImVec2 third, ImU32 color, float thickness) override;
    void AddTriangleFilled(ImVec2 first, ImVec2 second, ImVec2 third, ImU32 color) override;
    void AddRectFilled(ImVec2 min, ImVec2 max, ImU32 color) override;
    void AddCircle(ImVec2 center, float radius, ImU32 color, int segments, float thickness) override;
    void AddCircleFilled(ImVec2 center, float radius, ImU32 color) override;
    void AddText(ImFont *font, float font_size, ImVec2 position, ImU32 color, const char *text) override;
    void AddText(ImVec2 position, ImU32 color, const char *text) override;

    // Area covered so far, in the same pixels as the drawing calls
    ImVec2 GetMin() const;
    ImVec2 GetMax() const;

private:
    SvgWriter &m_Svg;
    bool m_ForPrint;
    ImVec2 m_Min = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    ImVec2 m_Max = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

    ExportColor Convert(ImU32 color) const;
    void Cover(ImVec2 point, float margin);
};

} // namespace GUI

#endif // IMCSIM_SCHEMATIC_CANVAS_H
