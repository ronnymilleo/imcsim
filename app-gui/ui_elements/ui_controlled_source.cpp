/**
 * @file    ui_controlled_source.cpp
 * @brief   Schematic symbol of a controlled source: VCVS, VCCS, CCCS or CCVS.
 */

#include "ui_elements/ui_controlled_source.h"

#include "spice/spice_value.h"
#include <format>
#include <memory>

namespace GUI {

namespace {

/// Control leads stop short of the diamond, which they do not touch.
constexpr float CONTROL_LEAD_END = -1.2f;
/// Plus sign beside the control leads.
constexpr ImVec2 CONTROL_PLUS_SIGN = {-1.6f, -1.4f};
/// Minus sign beside the control leads.
constexpr ImVec2 CONTROL_MINUS_SIGN = {-1.6f, 1.4f};

} // namespace

/**
 * @brief   Creates a controlled source placed on the grid, together with its simulation component.
 * @param[in] type      VCVS, VCCS, CCCS or CCVS.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIControlledSource::UIControlledSource(const Core::ComponentType type,
                                       const GridPoint position,
                                       const Rotation rotation)
    : UIDependentSource(std::make_unique<Core::ControlledSource>(type), position, rotation) {
}

/**
 * @brief   The output leads reach the diamond; the control leads end short of it, with their polarity beside them.
 * @param[in,out] canvas  Where it is drawn: the editor window or an exported image.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     color   Line color.
 */
void UIControlledSource::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    UIDependentSource::DrawTerminals(canvas, view, color);
    if (Core::IsCurrentControlled(GetComponent().GetType())) {
        return;
    }
    canvas.AddLine(LocalToScreen(view, -2, -1), LocalToScreen(view, CONTROL_LEAD_END, -1), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, -2, 1), LocalToScreen(view, CONTROL_LEAD_END, 1), color, LINE_THICKNESS);
    DrawSign(canvas, view, CONTROL_PLUS_SIGN, true, color);
    DrawSign(canvas, view, CONTROL_MINUS_SIGN, false, color);
}

/**
 * @brief   The gain reads with its unit, such as "10V/V"; a current-controlled source adds the current it follows, as
 *          "10A/A·I(R1)", or "I(?)" until one is picked.
 * @param[in,out] canvas  Where it is drawn: the editor window or an exported image.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     color   Text color.
 */
void UIControlledSource::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const Core::ControlledSource &source = GetSource();
    std::string gain = Core::FormatValue(source.GetValue()) + source.GetUnit();
    if (Core::IsCurrentControlled(source.GetType())) {
        const std::string &current = source.GetControllingCurrent();
        gain += std::format("·I({})", current.empty() ? "?" : current);
    }
    DrawNameAndValue(canvas, view, gain, color);
}

/**
 * @brief   Returns the terminals in component order, in local grid units before rotation.
 * @return  Output terminal 1 at (0, -2) and 2 at (0, 2); then, for a voltage-controlled source, control + at
 *          (-2, -1) and control - at (-2, 1).
 */
std::vector<GridPoint> UIControlledSource::GetLocalTerminals() const {
    std::vector<GridPoint> terminals = UIDependentSource::GetLocalTerminals();
    if (!Core::IsCurrentControlled(GetComponent().GetType())) {
        terminals.insert(terminals.end(), {{-2, -1}, {-2, 1}});
    }
    return terminals;
}

/**
 * @brief   Returns the pick area of the source, in local grid units before rotation.
 * @return  A box around the diamond and the output leads, reaching the control terminals when there are some.
 */
LocalBounds UIControlledSource::GetLocalBounds() const {
    const float left = Core::IsCurrentControlled(GetComponent().GetType()) ? -DIAMOND_RADIUS : -2.0f;
    return {{left, -2.0f}, {DIAMOND_RADIUS, 2.0f}};
}

/**
 * @brief   A VCVS and a CCVS set a voltage, a VCCS and a CCCS a current.
 * @return  True for `VCVS` or `CCVS`, false for `VCCS` or `CCCS`.
 */
bool UIControlledSource::SetsVoltage() const {
    const Core::ComponentType type = GetComponent().GetType();
    return type == Core::ComponentType::VCVS || type == Core::ComponentType::CCVS;
}

const Core::ControlledSource &UIControlledSource::GetSource() const {
    return static_cast<const Core::ControlledSource &>(GetComponent());
}

} // namespace GUI
