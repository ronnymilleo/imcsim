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
constexpr float DiamondRadius = 0.8f;
// Control leads stop short of the diamond, which they do not touch
constexpr float ControlLeadEnd = -1.2f;
constexpr float SignHalfSize = 0.15f;
// Signs inside the diamond, above and below its middle, and beside the control leads
constexpr float InnerSignOffset = 0.4f;
constexpr ImVec2 ControlPlusSign = {-1.6f, -1.4f};
constexpr ImVec2 ControlMinusSign = {-1.6f, 1.4f};
constexpr float ArrowHalfLength = 0.45f;
constexpr float ArrowHeadLength = 0.2f;
constexpr float ArrowHeadHalfWidth = 0.15f;
constexpr float LabelOffsetX = 1.1f;
constexpr float LabelOffsetY = 0.5f;

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
    canvas.AddLine(LocalToScreen(view, 0, -2), LocalToScreen(view, 0, -DiamondRadius), color, LineThickness);
    canvas.AddLine(LocalToScreen(view, 0, DiamondRadius), LocalToScreen(view, 0, 2), color, LineThickness);
    if (Core::IsCurrentControlled(GetComponent().GetType())) {
        return;
    }
    canvas.AddLine(LocalToScreen(view, -2, -1), LocalToScreen(view, ControlLeadEnd, -1), color, LineThickness);
    canvas.AddLine(LocalToScreen(view, -2, 1), LocalToScreen(view, ControlLeadEnd, 1), color, LineThickness);
    DrawSign(canvas, view, ControlPlusSign, true, color);
    DrawSign(canvas, view, ControlMinusSign, false, color);
}

// A source that sets a voltage shows its polarity, + toward terminal 1; one that sets a current shows the way it
// pushes it, from terminal 1 to terminal 2 through itself, as SPICE does
void UIControlledSource::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                                    SymbolStyle /*style*/) const {
    const ImVec2 diamond[4] = {LocalToScreen(view, 0, -DiamondRadius), LocalToScreen(view, DiamondRadius, 0),
                               LocalToScreen(view, 0, DiamondRadius), LocalToScreen(view, -DiamondRadius, 0)};
    canvas.AddPolyline(diamond, 4, color, ImDrawFlags_Closed, LineThickness);
    if (SetsVoltage(GetComponent().GetType())) {
        DrawSign(canvas, view, {0.0f, -InnerSignOffset}, true, color);
        DrawSign(canvas, view, {0.0f, InnerSignOffset}, false, color);
        return;
    }
    const ImVec2 tip = LocalToScreen(view, 0, ArrowHalfLength);
    canvas.AddLine(LocalToScreen(view, 0, -ArrowHalfLength), tip, color, LineThickness);
    canvas.AddLine(LocalToScreen(view, -ArrowHeadHalfWidth, ArrowHalfLength - ArrowHeadLength), tip, color,
                   LineThickness);
    canvas.AddLine(LocalToScreen(view, ArrowHeadHalfWidth, ArrowHalfLength - ArrowHeadLength), tip, color,
                   LineThickness);
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
    DrawLabel(canvas, view, {LabelOffsetX, -LabelOffsetY}, {1.0f, 0.0f}, source.GetName(), color);
    DrawLabel(canvas, view, {LabelOffsetX, LabelOffsetY}, {1.0f, 0.0f}, gain, color);
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
    const float left = Core::IsCurrentControlled(GetComponent().GetType()) ? -DiamondRadius : -2.0f;
    return {{left, -2.0f}, {DiamondRadius, 2.0f}};
}

const Core::ControlledSource &UIControlledSource::GetSource() const {
    return static_cast<const Core::ControlledSource &>(GetComponent());
}

void UIControlledSource::DrawSign(SchematicCanvas &canvas, const ViewTransform &view, const ImVec2 center,
                                  const bool plus, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, center.x - SignHalfSize, center.y),
                   LocalToScreen(view, center.x + SignHalfSize, center.y), color, LineThickness);
    if (plus) {
        canvas.AddLine(LocalToScreen(view, center.x, center.y - SignHalfSize),
                       LocalToScreen(view, center.x, center.y + SignHalfSize), color, LineThickness);
    }
}

} // namespace GUI
