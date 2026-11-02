/**
 * @file    ui_controlled_source.cpp
 * @brief   Schematic symbol of a controlled source: VCVS, VCCS, CCCS or CCVS.
 */

#include "ui_controlled_source.h"

#include "spice_value.h"
#include <format>
#include <memory>

namespace GUI {

namespace {

// The diamond reaches this far from the middle along both axes
constexpr float DIAMOND_RADIUS = 0.8f;
// Control leads stop short of the diamond, which they do not touch
constexpr float CONTROL_LEAD_END = -1.2f;
constexpr float SIGN_HALF_SIZE = 0.15f;
// Signs inside the diamond, above and below its middle, and beside the control leads
constexpr float INNER_SIGN_OFFSET = 0.4f;
constexpr ImVec2 CONTROL_PLUS_SIGN = {-1.6f, -1.4f};
constexpr ImVec2 CONTROL_MINUS_SIGN = {-1.6f, 1.4f};
constexpr float ARROW_HALF_LENGTH = 0.45f;
constexpr float ARROW_HEAD_LENGTH = 0.2f;
constexpr float ARROW_HEAD_HALF_WIDTH = 0.15f;
constexpr float LABEL_OFFSET_X = 1.1f;
constexpr float LABEL_OFFSET_Y = 0.5f;

bool SetsVoltage(const Core::ComponentType type) {
    return type == Core::ComponentType::VCVS || type == Core::ComponentType::CCVS;
}

} // namespace

/**
 * @brief   Creates a controlled source placed on the grid, together with its simulation component.
 * @param[in] type      VCVS, VCCS, CCCS or CCVS.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIControlledSource::UIControlledSource(const Core::ComponentType type, const GridPoint position,
                                       const Rotation rotation)
    : UIElement(std::make_unique<Core::ControlledSource>(type), position, rotation) {
}

// The output leads reach the diamond; the control leads end short of it, with their polarity beside them
void UIControlledSource::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, -2), LocalToScreen(view, 0, -DIAMOND_RADIUS), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, 0, DIAMOND_RADIUS), LocalToScreen(view, 0, 2), color, LINE_THICKNESS);
    if (Core::IsCurrentControlled(GetComponent().GetType())) {
        return;
    }
    canvas.AddLine(LocalToScreen(view, -2, -1), LocalToScreen(view, CONTROL_LEAD_END, -1), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, -2, 1), LocalToScreen(view, CONTROL_LEAD_END, 1), color, LINE_THICKNESS);
    DrawSign(canvas, view, CONTROL_PLUS_SIGN, true, color);
    DrawSign(canvas, view, CONTROL_MINUS_SIGN, false, color);
}

// A source that sets a voltage shows its polarity, + toward terminal 1; one that sets a current shows the way it
// pushes it, from terminal 1 to terminal 2 through itself, as SPICE does
void UIControlledSource::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                                    SymbolStyle /*style*/) const {
    const ImVec2 diamond[4] = {LocalToScreen(view, 0, -DIAMOND_RADIUS), LocalToScreen(view, DIAMOND_RADIUS, 0),
                               LocalToScreen(view, 0, DIAMOND_RADIUS), LocalToScreen(view, -DIAMOND_RADIUS, 0)};
    canvas.AddPolyline(diamond, 4, color, ImDrawFlags_Closed, LINE_THICKNESS);
    if (SetsVoltage(GetComponent().GetType())) {
        DrawSign(canvas, view, {0.0f, -INNER_SIGN_OFFSET}, true, color);
        DrawSign(canvas, view, {0.0f, INNER_SIGN_OFFSET}, false, color);
        return;
    }
    const ImVec2 tip = LocalToScreen(view, 0, ARROW_HALF_LENGTH);
    canvas.AddLine(LocalToScreen(view, 0, -ARROW_HALF_LENGTH), tip, color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, -ARROW_HEAD_HALF_WIDTH, ARROW_HALF_LENGTH - ARROW_HEAD_LENGTH), tip, color,
                   LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, ARROW_HEAD_HALF_WIDTH, ARROW_HALF_LENGTH - ARROW_HEAD_LENGTH), tip, color,
                   LINE_THICKNESS);
}

// The gain reads with its unit, such as "10V/V"; a current-controlled source adds the current it follows, as
// "10A/A·I(R1)", or "I(?)" until one is picked
void UIControlledSource::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const Core::ControlledSource &source = GetSource();
    std::string gain = Core::FormatValue(source.GetValue()) + source.GetUnit();
    if (Core::IsCurrentControlled(source.GetType())) {
        const std::string &current = source.GetControllingCurrent();
        gain += std::format("·I({})", current.empty() ? "?" : current);
    }
    DrawLabel(canvas, view, {LABEL_OFFSET_X, -LABEL_OFFSET_Y}, {1.0f, 0.0f}, source.GetName(), color);
    DrawLabel(canvas, view, {LABEL_OFFSET_X, LABEL_OFFSET_Y}, {1.0f, 0.0f}, gain, color);
}

/**
 * @brief   Returns the terminals in component order, in local grid units before rotation.
 * @return  Output terminal 1 at (0, -2) and 2 at (0, 2); then, for a voltage-controlled source, control + at
 *          (-2, -1) and control - at (-2, 1).
 */
std::vector<GridPoint> UIControlledSource::GetLocalTerminals() const {
    if (Core::IsCurrentControlled(GetComponent().GetType())) {
        return {{0, -2}, {0, 2}};
    }
    return {{0, -2}, {0, 2}, {-2, -1}, {-2, 1}};
}

/**
 * @brief   Returns the pick area of the source, in local grid units before rotation.
 * @return  A box around the diamond and the output leads, reaching the control terminals when there are some.
 */
LocalBounds UIControlledSource::GetLocalBounds() const {
    const float left = Core::IsCurrentControlled(GetComponent().GetType()) ? -DIAMOND_RADIUS : -2.0f;
    return {{left, -2.0f}, {DIAMOND_RADIUS, 2.0f}};
}

const Core::ControlledSource &UIControlledSource::GetSource() const {
    return static_cast<const Core::ControlledSource &>(GetComponent());
}

void UIControlledSource::DrawSign(SchematicCanvas &canvas, const ViewTransform &view, const ImVec2 center,
                                  const bool plus, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, center.x - SIGN_HALF_SIZE, center.y),
                   LocalToScreen(view, center.x + SIGN_HALF_SIZE, center.y), color, LINE_THICKNESS);
    if (plus) {
        canvas.AddLine(LocalToScreen(view, center.x, center.y - SIGN_HALF_SIZE),
                       LocalToScreen(view, center.x, center.y + SIGN_HALF_SIZE), color, LINE_THICKNESS);
    }
}

} // namespace GUI
