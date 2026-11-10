/**
 * @file    ui_behavioral_source.cpp
 * @brief   Schematic symbol of a behavioral source, whose voltage or current is an expression.
 */

#include "ui_elements/ui_behavioral_source.h"

#include <format>
#include <memory>
#include <string>

namespace GUI {

namespace {

/// The diamond reaches this far from the middle along both axes.
constexpr float DIAMOND_RADIUS = 0.8f;
constexpr float SIGN_HALF_SIZE = 0.15f;
/// Distance of the signs inside the diamond above and below its middle.
constexpr float INNER_SIGN_OFFSET = 0.4f;
constexpr float ARROW_HALF_LENGTH = 0.45f;
constexpr float ARROW_HEAD_LENGTH = 0.2f;
constexpr float ARROW_HEAD_HALF_WIDTH = 0.15f;
constexpr float LABEL_OFFSET_X = 1.1f;
constexpr float LABEL_OFFSET_Y = 0.5f;
/// Characters of the expression the schematic shows before it cuts it, so a long one does not cover its neighbors.
constexpr std::size_t MAX_LABEL_CHARACTERS = 24;
/// The ellipsis, encoded in UTF-8.
constexpr std::string_view ELLIPSIS = "\xE2\x80\xA6";

} // namespace

/**
 * @brief   Creates a behavioral source placed on the grid, together with its simulation component.
 * @param[in] type      BEHAVIORAL_VOLTAGE or BEHAVIORAL_CURRENT.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIBehavioralSource::UIBehavioralSource(const Core::ComponentType type, const GridPoint position,
                                       const Rotation rotation)
    : UIElement(std::make_unique<Core::BehavioralSource>(type), position, rotation) {
}

/**
 * @brief   The leads reach the diamond from both terminals.
 */
void UIBehavioralSource::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, -2), LocalToScreen(view, 0, -DIAMOND_RADIUS), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, 0, DIAMOND_RADIUS), LocalToScreen(view, 0, 2), color, LINE_THICKNESS);
}

/**
 * @brief   A source that sets a voltage shows its polarity, + toward terminal 1; one that sets a current shows the way
 *          it pushes it, from terminal 1 to terminal 2 through itself, as SPICE does.
 */
void UIBehavioralSource::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                                    SymbolStyle /*style*/) const {
    const ImVec2 diamond[4] = {LocalToScreen(view, 0, -DIAMOND_RADIUS), LocalToScreen(view, DIAMOND_RADIUS, 0),
                               LocalToScreen(view, 0, DIAMOND_RADIUS), LocalToScreen(view, -DIAMOND_RADIUS, 0)};
    canvas.AddPolyline(diamond, 4, color, ImDrawFlags_Closed, LINE_THICKNESS);
    if (GetComponent().GetType() == Core::ComponentType::BEHAVIORAL_VOLTAGE) {
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

/**
 * @brief   The name above, the expression below, both to the right of the diamond.
 */
void UIBehavioralSource::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const Core::BehavioralSource &source = GetSource();
    DrawLabel(canvas, view, {LABEL_OFFSET_X, -LABEL_OFFSET_Y}, {1.0f, 0.0f}, source.GetName(), color);
    DrawLabel(canvas, view, {LABEL_OFFSET_X, LABEL_OFFSET_Y}, {1.0f, 0.0f}, FormatBehavioralLabel(source), color);
}

/**
 * @brief   Returns the terminals in component order, in local grid units before rotation.
 * @return  Terminal 1 (+) at (0, -2) and terminal 2 (-) at (0, 2).
 */
std::vector<GridPoint> UIBehavioralSource::GetLocalTerminals() const {
    return {{0, -2}, {0, 2}};
}

/**
 * @brief   Returns the pick area of the source, in local grid units before rotation.
 * @return  A box around the diamond and its leads.
 */
LocalBounds UIBehavioralSource::GetLocalBounds() const {
    return {{-DIAMOND_RADIUS, -2.0f}, {DIAMOND_RADIUS, 2.0f}};
}

const Core::BehavioralSource &UIBehavioralSource::GetSource() const {
    return static_cast<const Core::BehavioralSource &>(GetComponent());
}

void UIBehavioralSource::DrawSign(SchematicCanvas &canvas, const ViewTransform &view, const ImVec2 center,
                                  const bool plus, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, center.x - SIGN_HALF_SIZE, center.y),
                   LocalToScreen(view, center.x + SIGN_HALF_SIZE, center.y), color, LINE_THICKNESS);
    if (plus) {
        canvas.AddLine(LocalToScreen(view, center.x, center.y - SIGN_HALF_SIZE),
                       LocalToScreen(view, center.x, center.y + SIGN_HALF_SIZE), color, LINE_THICKNESS);
    }
}

/**
 * @brief   Writes what a behavioral source drives, as the schematic labels it.
 * @param[in] source  The source.
 * @return  Such as "V=V(IN)*2" or "I=1m"; an expression past MAX_LABEL_CHARACTERS is cut and ends with an ellipsis.
 */
std::string FormatBehavioralLabel(const Core::BehavioralSource &source) {
    const char *quantity = source.GetType() == Core::ComponentType::BEHAVIORAL_VOLTAGE ? "V" : "I";
    const std::string &expression = source.GetExpression();
    if (expression.size() <= MAX_LABEL_CHARACTERS) {
        return std::format("{}={}", quantity, expression);
    }
    return std::format("{}={}{}", quantity, std::string_view(expression).substr(0, MAX_LABEL_CHARACTERS), ELLIPSIS);
}

} // namespace GUI
