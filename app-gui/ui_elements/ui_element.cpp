/**
 * @file    ui_element.cpp
 * @brief   Visual representation of a circuit component inside the schematic editor.
 */

#include "ui_elements/ui_element.h"

#include "simulation/monte_carlo.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace GUI {

namespace {

/// Labels sit just outside the tallest two-terminal symbol, which spans y = -0.8 to 0.8.
constexpr float LABEL_OFFSET = 1.0f;
/// Room, in grid units, between the symbol bounds and the outline that marks it.
constexpr float OUTLINE_PADDING = 0.15f;
/// Half the length of each bar of a plus or minus sign on a symbol.
constexpr float SIGN_HALF_SIZE = 0.15f;

/**
 * @brief   The two corners of the base of an arrow head, in local grid units.
 * @param[in] tip        Point of the arrow.
 * @param[in] direction  Direction the arrow points to; it does not need to be normalized.
 * @param[in] head       Size of the head.
 * @return  The corner to the left of the arrow, looking along it on screen, then the one to its right.
 */
std::array<ImVec2, 2> GetArrowHeadCorners(const ImVec2 tip, const ImVec2 direction, const ArrowHead head) {
    const float length = std::hypot(direction.x, direction.y);
    const ImVec2 along = {direction.x / length, direction.y / length};
    const ImVec2 across = {-along.y, along.x};
    const ImVec2 base = {tip.x - along.x * head.Length, tip.y - along.y * head.Length};
    return {ImVec2{base.x + across.x * head.HalfWidth, base.y + across.y * head.HalfWidth},
            ImVec2{base.x - across.x * head.HalfWidth, base.y - across.y * head.HalfWidth}};
}

} // namespace

/**
 * @brief   Creates an element for a component placed on the grid.
 * @param[in] component  Simulation component owned by this element.
 * @param[in] position   Grid position in world units.
 * @param[in] rotation   Orientation on the grid.
 */
UIElement::UIElement(std::unique_ptr<Core::Component> component, const GridPoint position, const Rotation rotation)
    : m_Component(std::move(component)), m_Position(position), m_Rotation(rotation) {
}

/**
 * @brief   Draws the terminals, the component symbol and its name and value labels.
 * @param[in,out] canvas Where it is drawn: the editor window or an exported image.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Line color, so the same element can be drawn as a placement preview.
 * @param[in] style      Drawing standard for the symbol.
 */
void UIElement::Draw(SchematicCanvas &canvas,
                     const ViewTransform &view,
                     const ImU32 color,
                     const SymbolStyle style) const {
    DrawTerminals(canvas, view, color);
    DrawSymbol(canvas, view, color, style);
    DrawLabels(canvas, view, color);
}

/**
 * @brief   Draws the symbol and its leads, without labels, scaled to fit a square, as an icon of its kind of part.
 * @param[in,out] canvas Where it is drawn, such as a toolbar button.
 * @param[in] center     Middle of the square, in screen pixels.
 * @param[in] size       Side of the square, in pixels.
 * @param[in] color      Line color.
 * @param[in] style      Drawing standard for the symbol.
 * @note    Meant for an unrotated, unmirrored element, such as a newly created one.
 */
void UIElement::DrawIcon(SchematicCanvas &canvas,
                         const ImVec2 center,
                         const float size,
                         const ImU32 color,
                         const SymbolStyle style) const {
    const LocalBounds bounds = GetLocalBounds();
    const ImVec2 extent = {bounds.Max.x - bounds.Min.x, bounds.Max.y - bounds.Min.y};
    const float zoom = size / std::max(extent.x, extent.y);
    const ImVec2 middle = ToVec2(m_Position) + (bounds.Min + bounds.Max) / 2.0f;
    const ViewTransform view({0.0f, 0.0f}, center - middle * zoom, zoom);
    DrawTerminals(canvas, view, color);
    DrawSymbol(canvas, view, color, style);
}

/**
 * @brief   Draws a rectangle around the symbol, leads included, turned with the element.
 * @param[in,out] canvas Where it is drawn: the editor window or an exported image.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Line color.
 * @note    Marks a selected part without recoloring its symbol, so colors the symbol carries stay readable.
 */
void UIElement::DrawOutline(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const LocalBounds bounds = GetLocalBounds();
    const float left = bounds.Min.x - OUTLINE_PADDING;
    const float top = bounds.Min.y - OUTLINE_PADDING;
    const float right = bounds.Max.x + OUTLINE_PADDING;
    const float bottom = bounds.Max.y + OUTLINE_PADDING;
    const std::array<ImVec2, 4> corners = {LocalToScreen(view, left, top),
                                           LocalToScreen(view, right, top),
                                           LocalToScreen(view, right, bottom),
                                           LocalToScreen(view, left, bottom)};
    canvas.AddPolyline(corners.data(), static_cast<int>(corners.size()), color, ImDrawFlags_Closed, LINE_THICKNESS);
}

/**
 * @brief   Returns the simulation component behind this element.
 * @return  The owned component.
 */
const Core::Component &UIElement::GetComponent() const {
    return *m_Component;
}

/**
 * @brief   Returns the simulation component behind this element, for editing its name or value.
 * @return  The owned component.
 */
Core::Component &UIElement::GetComponent() {
    return *m_Component;
}

/**
 * @brief   Returns the grid position of the element.
 * @return  Position in world units.
 */
GridPoint UIElement::GetPosition() const {
    return m_Position;
}

/**
 * @brief   Moves the element to another grid position.
 * @param[in] position  New grid position in world units.
 */
void UIElement::SetPosition(const GridPoint position) {
    m_Position = position;
}

/**
 * @brief   Returns the orientation of the element.
 * @return  The element rotation.
 */
Rotation UIElement::GetRotation() const {
    return m_Rotation;
}

/**
 * @brief   Changes the orientation of the element.
 * @param[in] rotation  New orientation on the grid.
 */
void UIElement::SetRotation(const Rotation rotation) {
    m_Rotation = rotation;
}

/**
 * @brief   Tells whether the element is flipped across its local vertical axis.
 * @return  True for a mirrored element.
 */
bool UIElement::IsMirrored() const {
    return m_Mirrored;
}

/**
 * @brief   Flips the element across its local vertical axis, or back.
 * @param[in] mirrored  Whether the element is mirrored; it applies before the rotation.
 */
void UIElement::SetMirrored(const bool mirrored) {
    m_Mirrored = mirrored;
}

/**
 * @brief   Converts a point of the symbol to world units: mirrored, then rotated, then moved to the position.
 * @param[in] local  Point in local grid units.
 * @return  The point in world grid units.
 */
ImVec2 UIElement::LocalToWorld(const ImVec2 local) const {
    return ToVec2(m_Position) + Rotate(ImVec2{m_Mirrored ? -local.x : local.x, local.y}, m_Rotation);
}

/**
 * @brief   Converts a grid point of the symbol to world units: mirrored, then rotated, then moved to the position.
 * @param[in] local  Point in local grid units.
 * @return  The point in world grid units, exact on the grid.
 */
GridPoint UIElement::LocalToWorld(const GridPoint local) const {
    return m_Position + Rotate(GridPoint{m_Mirrored ? -local.X : local.X, local.Y}, m_Rotation);
}

/**
 * @brief   Converts a world point to the local grid units of the symbol, undoing LocalToWorld().
 * @param[in] world  Point in world grid units.
 * @return  The point in local grid units.
 */
ImVec2 UIElement::WorldToLocal(const ImVec2 world) const {
    const ImVec2 local = Rotate(world - ToVec2(m_Position), InverseRotation(m_Rotation));
    return {m_Mirrored ? -local.x : local.x, local.y};
}

/**
 * @brief   Returns the terminals of the element on the grid, after mirroring, rotation and translation.
 * @return  Connection points in world units, where wires can attach.
 */
std::vector<GridPoint> UIElement::GetTerminals() const {
    std::vector<GridPoint> terminals = GetLocalTerminals();
    for (GridPoint &terminal : terminals) {
        terminal = LocalToWorld(terminal);
    }
    return terminals;
}

/**
 * @brief   Returns the direction from a terminal into the part, along its lead, in world units.
 * @param[in] terminal  Index in GetTerminals().
 * @return  A unit step along the X or Y axis, mirrored and rotated with the element.
 */
GridPoint UIElement::GetTerminalInward(const std::size_t terminal) const {
    // A direction does not move with the element, so its position is taken off again
    return LocalToWorld(GetLocalTerminalInward(terminal)) - m_Position;
}

/**
 * @brief   Checks whether a world position falls on the element, for picking it with the mouse.
 * @param[in] world_pos  Position in world units, usually the cursor.
 * @return  True when the position is inside the symbol bounds, terminals included.
 */
bool UIElement::Contains(const ImVec2 world_pos) const {
    const ImVec2 local = WorldToLocal(world_pos);
    const LocalBounds bounds = GetLocalBounds();
    return local.x >= bounds.Min.x && local.x <= bounds.Max.x && local.y >= bounds.Min.y && local.y <= bounds.Max.y;
}

/**
 * @brief   Finds the box the element takes on the grid, after its rotation and mirroring.
 * @return  The symbol bounds, terminals included, in world units.
 */
WorldBounds UIElement::GetWorldBounds() const {
    const LocalBounds bounds = GetLocalBounds();
    WorldBounds world_bounds = {.Min = {FLT_MAX, FLT_MAX}, .Max = {-FLT_MAX, -FLT_MAX}};
    for (const ImVec2 corner :
         {bounds.Min, ImVec2{bounds.Max.x, bounds.Min.y}, bounds.Max, ImVec2{bounds.Min.x, bounds.Max.y}}) {
        const ImVec2 world = LocalToWorld(corner);
        world_bounds.Min = {std::min(world_bounds.Min.x, world.x), std::min(world_bounds.Min.y, world.y)};
        world_bounds.Max = {std::max(world_bounds.Max.x, world.x), std::max(world_bounds.Max.y, world.y)};
    }
    return world_bounds;
}

/**
 * @brief   Checks whether the element touches a rectangle in world space, for picking it with a selection box.
 * @param[in] min  Corner of the rectangle with the lowest X and Y.
 * @param[in] max  Corner with the highest X and Y.
 * @return  True when the symbol bounds, terminals included, overlap the rectangle.
 */
bool UIElement::Intersects(const ImVec2 min, const ImVec2 max) const {
    const WorldBounds bounds = GetWorldBounds();
    return bounds.Min.x <= max.x && bounds.Max.x >= min.x && bounds.Min.y <= max.y && bounds.Max.y >= min.y;
}

/**
 * @brief   Draws the leads of a two-terminal component, from x = -2 to -1 and from x = 1 to 2.
 * @param[in,out] canvas Where it is drawn: the editor window or an exported image.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Line color.
 */
void UIElement::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, -2, 0), LocalToScreen(view, -1, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, 1, 0), LocalToScreen(view, 2, 0), color, LINE_THICKNESS);
}

/**
 * @brief   Draws the component name and its value on one line above the symbol, in local orientation, such as "R1 1k",
 *          so the part reads in one glance and the side below stays free; the value is the parameter it follows, such
 * as
 *          "{R}", for a part inside a block.
 * @param[in,out] canvas Where it is drawn: the editor window or an exported image.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Text color.
 */
void UIElement::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    std::string text = m_Component->GetName();
    if (m_Component->HasValue()) {
        text += " " + GetValueLabel(*m_Component);
    }
    DrawLabel(canvas, view, {0.0f, -LABEL_OFFSET}, {0.0f, -1.0f}, text, color);
}

/**
 * @brief   Returns the terminals of a two-terminal component, in local grid units before rotation.
 * @return  The points at x = -2 and x = +2.
 */
std::vector<GridPoint> UIElement::GetLocalTerminals() const {
    return {{-2, 0}, {2, 0}};
}

/**
 * @brief   Returns the direction from a terminal into the part, before rotation.
 * @param[in] terminal  Index in GetLocalTerminals().
 * @return  A unit step from the terminal toward the middle of the symbol, along the axis the terminal is furthest
 *          on, which is the axis of its lead for every symbol whose terminals are away from its middle.
 */
GridPoint UIElement::GetLocalTerminalInward(const std::size_t terminal) const {
    const GridPoint position = GetLocalTerminals()[terminal];
    const auto step_toward_zero = [](const int value) { return value > 0 ? -1 : value < 0 ? 1 : 0; };
    if (std::abs(position.X) >= std::abs(position.Y)) {
        return {step_toward_zero(position.X), 0};
    }
    return {0, step_toward_zero(position.Y)};
}

/**
 * @brief   Returns the pick area of a two-terminal component, in local grid units before rotation.
 * @return  A box from terminal to terminal, as tall as the tallest symbol.
 */
LocalBounds UIElement::GetLocalBounds() const {
    return {{-2.0f, -0.8f}, {2.0f, 0.8f}};
}

/**
 * @brief   ImGui text cannot rotate, so the text stays upright and is pushed from the anchor along the rotated
 *          direction until it no longer overlaps the anchor, whatever the element rotation.
 */
void UIElement::DrawLabel(SchematicCanvas &canvas,
                          const ViewTransform &view,
                          const ImVec2 local_anchor,
                          const ImVec2 local_direction,
                          const std::string &text,
                          const ImU32 color) const {
    if (text.empty()) {
        return;
    }
    const ImVec2 anchor = LocalToScreen(view, local_anchor.x, local_anchor.y);
    const ImVec2 step = LocalToScreen(view, local_anchor.x + local_direction.x, local_anchor.y + local_direction.y);
    // The local direction is one grid unit long, so its length on screen is the current zoom
    const ImVec2 offset = step - anchor;
    const float zoom = std::hypot(offset.x, offset.y);
    const float font_size = zoom * LABEL_SCALE;
    if (font_size < MIN_LABEL_SIZE) {
        return;
    }

    const ImVec2 direction = offset / zoom;
    ImFont *font = ImGui::GetFont();
    const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text.c_str());
    const float half_extent = (std::abs(direction.x) * text_size.x + std::abs(direction.y) * text_size.y) / 2.0f;
    const ImVec2 center = anchor + direction * half_extent;
    canvas.AddText(font, font_size, center - text_size / 2.0f, color, text.c_str());
}

/**
 * @brief   Converts a point of the symbol, in local grid units, to screen pixels.
 * @param[in] view  Transform of the current frame.
 * @param[in] x     Local X before mirroring and rotation.
 * @param[in] y     Local Y before rotation.
 * @return  Position in screen pixels.
 */
ImVec2 UIElement::LocalToScreen(const ViewTransform &view, const float x, const float y) const {
    return view.ToScreen(LocalToWorld(ImVec2{x, y}));
}

/**
 * @brief   Converts a point of the symbol, in local grid units, to screen pixels.
 * @param[in] view   Transform of the current frame.
 * @param[in] local  Point before mirroring and rotation.
 * @return  Position in screen pixels.
 */
ImVec2 UIElement::LocalToScreen(const ViewTransform &view, const ImVec2 local) const {
    return LocalToScreen(view, local.x, local.y);
}

/**
 * @brief   How long a length of the symbol is on screen, such as the radius of a circle drawn on it.
 * @param[in] view    Transform of the current frame.
 * @param[in] length  Length in local grid units.
 * @return  The length in pixels; rotation and mirroring do not change it.
 */
float UIElement::LocalLengthToScreen(const ViewTransform &view, const float length) const {
    const ImVec2 unit = LocalToScreen(view, 1.0f, 0.0f) - LocalToScreen(view, 0.0f, 0.0f);
    return std::hypot(unit.x, unit.y) * length;
}

/**
 * @brief   Draws the outline of a circle on the symbol, such as a contact or a bubble.
 * @param[in,out] canvas        Where it is drawn: the editor window or an exported image.
 * @param[in]     view          Transform of the current frame.
 * @param[in]     local_center  Center of the circle, in local grid units.
 * @param[in]     local_radius  Radius of the circle, in local grid units.
 * @param[in]     color         Line color.
 */
void UIElement::DrawCircleAt(SchematicCanvas &canvas,
                             const ViewTransform &view,
                             const ImVec2 local_center,
                             const float local_radius,
                             const ImU32 color) const {
    canvas.AddCircle(LocalToScreen(view, local_center),
                     LocalLengthToScreen(view, local_radius),
                     color,
                     0,
                     LINE_THICKNESS);
}

/**
 * @brief   Draws a plus or a minus sign on the symbol, such as the polarity of a source; the bars stay along the local
 *          axes, so the sign turns with the element.
 * @param[in,out] canvas        Where it is drawn: the editor window or an exported image.
 * @param[in]     view          Transform of the current frame.
 * @param[in]     local_center  Middle of the sign, in local grid units.
 * @param[in]     is_plus       True for a plus, false for a minus.
 * @param[in]     color         Line color.
 */
void UIElement::DrawSign(SchematicCanvas &canvas,
                         const ViewTransform &view,
                         const ImVec2 local_center,
                         const bool is_plus,
                         const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, local_center.x - SIGN_HALF_SIZE, local_center.y),
                   LocalToScreen(view, local_center.x + SIGN_HALF_SIZE, local_center.y),
                   color,
                   LINE_THICKNESS);
    if (is_plus) {
        canvas.AddLine(LocalToScreen(view, local_center.x, local_center.y - SIGN_HALF_SIZE),
                       LocalToScreen(view, local_center.x, local_center.y + SIGN_HALF_SIZE),
                       color,
                       LINE_THICKNESS);
    }
}

/**
 * @brief   Draws an open arrow head, two lines that meet at the tip.
 * @param[in,out] canvas           Where it is drawn: the editor window or an exported image.
 * @param[in]     view             Transform of the current frame.
 * @param[in]     local_tip        Point of the arrow, in local grid units.
 * @param[in]     local_direction  Direction the arrow points to, in local grid units; it does not need to be
 *                                 normalized.
 * @param[in]     head             Size of the head.
 * @param[in]     color            Line color.
 */
void UIElement::DrawArrowHead(SchematicCanvas &canvas,
                              const ViewTransform &view,
                              const ImVec2 local_tip,
                              const ImVec2 local_direction,
                              const ArrowHead head,
                              const ImU32 color) const {
    const ImVec2 tip = LocalToScreen(view, local_tip);
    for (const ImVec2 corner : GetArrowHeadCorners(local_tip, local_direction, head)) {
        canvas.AddLine(LocalToScreen(view, corner), tip, color, LINE_THICKNESS);
    }
}

/**
 * @brief   Draws an arrow: a line with an open head at its tip.
 * @param[in,out] canvas       Where it is drawn: the editor window or an exported image.
 * @param[in]     view         Transform of the current frame.
 * @param[in]     local_start  Tail of the arrow, in local grid units.
 * @param[in]     local_tip    Point of the arrow, in local grid units.
 * @param[in]     head         Size of the head.
 * @param[in]     color        Line color.
 */
void UIElement::DrawArrow(SchematicCanvas &canvas,
                          const ViewTransform &view,
                          const ImVec2 local_start,
                          const ImVec2 local_tip,
                          const ArrowHead head,
                          const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, local_start), LocalToScreen(view, local_tip), color, LINE_THICKNESS);
    DrawArrowHead(canvas, view, local_tip, local_tip - local_start, head, color);
}

/**
 * @brief   Draws a filled arrow head, such as the one on an emitter.
 * @param[in,out] canvas           Where it is drawn: the editor window or an exported image.
 * @param[in]     view             Transform of the current frame.
 * @param[in]     local_tip        Point of the arrow, in local grid units.
 * @param[in]     local_direction  Direction the arrow points to, in local grid units; it does not need to be
 *                                 normalized.
 * @param[in]     head             Size of the head.
 * @param[in]     color            Fill color.
 */
void UIElement::DrawFilledArrowHead(SchematicCanvas &canvas,
                                    const ViewTransform &view,
                                    const ImVec2 local_tip,
                                    const ImVec2 local_direction,
                                    const ArrowHead head,
                                    const ImU32 color) const {
    const std::array<ImVec2, 2> corners = GetArrowHeadCorners(local_tip, local_direction, head);
    canvas.AddTriangleFilled(LocalToScreen(view, local_tip),
                             LocalToScreen(view, corners[0]),
                             LocalToScreen(view, corners[1]),
                             color);
}

/**
 * @brief   Returns the text a symbol shows for the value of its part.
 * @param[in] component  Part with a value.
 * @return  The value with its scale suffix, such as "4.7k", and the tolerance when a Monte Carlo analysis varies the
 *          part, such as "4.7k ±5%"; the parameter it follows, such as "{R}", for a part inside a block, since its
 *          own value is not used then.
 */
std::string GetValueLabel(const Core::Component &component) {
    if (const std::optional<std::string> &expression = component.GetValueExpression()) {
        return *expression;
    }
    const std::string value = Core::FormatValue(component.GetValue());
    if (const std::optional<double> tolerance = component.GetTolerance(); tolerance && Core::IsVaried(component)) {
        return std::format("{} ±{}%", value, Core::FormatValue(*tolerance));
    }
    return value;
}

} // namespace GUI
