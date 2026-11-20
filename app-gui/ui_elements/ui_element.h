/**
 * @file    ui_element.h
 * @brief   Visual representation of a circuit component inside the schematic editor.
 */

#ifndef IMCSIM_UI_ELEMENT_H
#define IMCSIM_UI_ELEMENT_H

#include "base/helpers.h"
#include "canvas/schematic_canvas.h"
#include "circuit/components/component.h"
#include "imgui.h"
#include <memory>
#include <string>
#include <vector>

namespace GUI {

/**
 * @enum    SymbolStyle
 * @brief   Drawing standard for schematic symbols.
 * @details Only symbols that differ between standards use it; the resistor is a rectangle in IEC and a zig-zag
 *          in ANSI.
 */
enum class SymbolStyle {
    IEC,
    ANSI
};

/**
 * @struct  LocalBounds
 * @brief   Axis-aligned box around a symbol, in local grid units before rotation.
 */
struct LocalBounds {
    ImVec2 Min;
    ImVec2 Max;
};

/**
 * @struct  WorldBounds
 * @brief   Axis-aligned box around a placed symbol, terminals included, in world grid units.
 */
struct WorldBounds {
    ImVec2 Min;
    ImVec2 Max;
};

/**
 * @struct  ArrowHead
 * @brief   The size of the head of an arrow drawn on a symbol, in local grid units.
 */
struct ArrowHead {
    /// From the tip back to the base, along the arrow.
    float Length = 0.0f;
    /// From the axis of the arrow to each corner of the base.
    float HalfWidth = 0.0f;
};

/**
 * @struct  CoilTurns
 * @brief   A row of half-circle turns, as on an inductor or a transformer, in local grid units.
 */
struct CoilTurns {
    /// Where the first turn starts, on the axis of the coil.
    ImVec2 Start;
    /// Unit vector along the coil, from the first turn to the last.
    ImVec2 Axis;
    /// Unit vector toward the side the turns bulge to.
    ImVec2 Bulge;
    int TurnCount = 0;
    float TurnRadius = 0.0f;
};

/**
 * @class   UIElement
 * @brief   A component placed on the schematic grid, with its position, rotation and mirroring.
 * @details Owns its Core::Component. By default an element has two terminals at local x = -2 and x = +2 and
 *          derived classes only draw the symbol between them; single-terminal symbols derive from
 *          UISingleTerminalElement. A mirrored element is flipped across its local vertical axis (x becomes -x)
 *          before it is rotated, so mirroring and the four rotations give every orientation on the grid.
 */
class UIElement {
public:
    UIElement(std::unique_ptr<Core::Component> component, GridPoint position, Rotation rotation);
    virtual ~UIElement() = default;

    // Drawing
    void Draw(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const;
    void DrawIcon(SchematicCanvas &canvas, ImVec2 center, float size, ImU32 color, SymbolStyle style) const;
    void DrawOutline(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const;

    // Component and placement
    const Core::Component &GetComponent() const;
    Core::Component &GetComponent();
    GridPoint GetPosition() const;
    void SetPosition(GridPoint position);
    Rotation GetRotation() const;
    void SetRotation(Rotation rotation);
    bool IsMirrored() const;
    void SetMirrored(bool mirrored);

    // Geometry in world units
    ImVec2 LocalToWorld(ImVec2 local) const;
    GridPoint LocalToWorld(GridPoint local) const;
    ImVec2 WorldToLocal(ImVec2 world) const;
    std::vector<GridPoint> GetTerminals() const;
    GridPoint GetTerminalInward(std::size_t terminal) const;
    bool Contains(ImVec2 world_pos) const;
    WorldBounds GetWorldBounds() const;
    bool Intersects(ImVec2 min, ImVec2 max) const;

protected:
    // Parts of the symbol, in the order Draw() draws them
    virtual void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const;
    /**
     * @brief   Draws the body of the symbol between the leads, in local grid units.
     * @param[in,out] canvas Where it is drawn: the editor window or an exported image.
     * @param[in] view       Transform of the current frame.
     * @param[in] color      Line color, so the same element can be drawn as a placement preview.
     * @param[in] style      Drawing standard for the symbol; parts drawn alike in both ignore it.
     */
    virtual void
    DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const = 0;
    virtual void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const;

    // Geometry in local grid units, before rotation and translation
    virtual std::vector<GridPoint> GetLocalTerminals() const;
    virtual GridPoint GetLocalTerminalInward(std::size_t terminal) const;
    virtual LocalBounds GetLocalBounds() const;

    // Helpers for derived classes
    void DrawLabel(SchematicCanvas &canvas,
                   const ViewTransform &view,
                   ImVec2 local_anchor,
                   ImVec2 local_direction,
                   const std::string &text,
                   ImU32 color) const;
    ImVec2 LocalToScreen(const ViewTransform &view, float x, float y) const;
    ImVec2 LocalToScreen(const ViewTransform &view, ImVec2 local) const;
    float LocalLengthToScreen(const ViewTransform &view, float length) const;

    // Parts of symbols, for derived classes
    void DrawCircleAt(SchematicCanvas &canvas,
                      const ViewTransform &view,
                      ImVec2 local_center,
                      float local_radius,
                      ImU32 color) const;
    void
    DrawSign(SchematicCanvas &canvas, const ViewTransform &view, ImVec2 local_center, bool is_plus, ImU32 color) const;
    void DrawArrowHead(SchematicCanvas &canvas,
                       const ViewTransform &view,
                       ImVec2 local_tip,
                       ImVec2 local_direction,
                       ArrowHead head,
                       ImU32 color) const;
    void DrawArrow(SchematicCanvas &canvas,
                   const ViewTransform &view,
                   ImVec2 local_start,
                   ImVec2 local_tip,
                   ArrowHead head,
                   ImU32 color) const;
    void DrawFilledArrowHead(SchematicCanvas &canvas,
                             const ViewTransform &view,
                             ImVec2 local_tip,
                             ImVec2 local_direction,
                             ArrowHead head,
                             ImU32 color) const;
    void DrawCoilTurns(SchematicCanvas &canvas, const ViewTransform &view, const CoilTurns &coil, ImU32 color) const;

private:
    std::unique_ptr<Core::Component> m_Component;
    GridPoint m_Position;
    Rotation m_Rotation;
    bool m_Mirrored = false;
};

// Labels
std::string GetValueLabel(const Core::Component &component);

} // namespace GUI

#endif // IMCSIM_UI_ELEMENT_H
