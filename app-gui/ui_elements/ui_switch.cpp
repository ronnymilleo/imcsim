/**
 * @file    ui_switch.cpp
 * @brief   Schematic symbol of a switch and of a push button.
 */

#include "ui_switch.h"

#include "components/switch.h"
#include <cmath>
#include <memory>

namespace GUI {

namespace {

constexpr float CONTACT_X = 1.0f;
constexpr float CONTACT_RADIUS = 0.12f;
// A lever that is open ends here, above the second contact
constexpr ImVec2 OPEN_LEVER_TIP = {0.9f, -0.9f};
// A push button's bar rests this high above the contacts, and is pushed down onto them
constexpr float RAISED_BAR_Y = -0.7f;
constexpr float BAR_HALF_LENGTH = 0.7f;
constexpr float STEM_LENGTH = 0.6f;
constexpr float CAP_HALF_LENGTH = 0.4f;
constexpr float NAME_OFFSET_Y = 0.5f;

} // namespace

/**
 * @brief   Creates a switch or a push button placed on the grid, together with its simulation component.
 * @param[in] type      ComponentType::Switch or ComponentType::PushButton.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UISwitch::UISwitch(const Core::ComponentType type, const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Switch>(type), position, rotation) {
}

void UISwitch::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                          const SymbolStyle /*style*/) const {
    const ImVec2 origin = LocalToScreen(view, 0, 0);
    const ImVec2 unit = LocalToScreen(view, 1, 0) - origin;
    const float radius = std::hypot(unit.x, unit.y) * CONTACT_RADIUS;
    canvas.AddCircle(LocalToScreen(view, -CONTACT_X, 0), radius, color, 0, LINE_THICKNESS);
    canvas.AddCircle(LocalToScreen(view, CONTACT_X, 0), radius, color, 0, LINE_THICKNESS);

    if (!IsPushButton()) {
        // The lever swings from the first contact, down onto the second when closed
        const ImVec2 tip = IsClosed() ? ImVec2{CONTACT_X - CONTACT_RADIUS, 0.0f} : OPEN_LEVER_TIP;
        canvas.AddLine(LocalToScreen(view, -CONTACT_X + CONTACT_RADIUS, 0), LocalToScreen(view, tip.x, tip.y), color,
                       LINE_THICKNESS);
        return;
    }
    const float bar_y = IsClosed() ? 0.0f : RAISED_BAR_Y;
    canvas.AddLine(LocalToScreen(view, -BAR_HALF_LENGTH, bar_y), LocalToScreen(view, BAR_HALF_LENGTH, bar_y), color,
                   LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, 0, bar_y), LocalToScreen(view, 0, bar_y - STEM_LENGTH), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, -CAP_HALF_LENGTH, bar_y - STEM_LENGTH),
                   LocalToScreen(view, CAP_HALF_LENGTH, bar_y - STEM_LENGTH), color, LINE_THICKNESS);
}

// The name sits above whatever is drawn highest, and the state under the contacts
void UISwitch::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const float top = IsPushButton() ? RAISED_BAR_Y - STEM_LENGTH : OPEN_LEVER_TIP.y;
    DrawLabel(canvas, view, {0.0f, top - 0.3f}, {0.0f, -1.0f}, GetComponent().GetName(), color);
    const char *state = IsPushButton() ? (IsClosed() ? "pressed" : "") : (IsClosed() ? "closed" : "open");
    DrawLabel(canvas, view, {0.0f, NAME_OFFSET_Y}, {0.0f, 1.0f}, state, color);
}

/**
 * @brief   Returns the pick area of the switch, in local grid units before rotation.
 * @return  A box from terminal to terminal, as tall as the lever or the button.
 */
LocalBounds UISwitch::GetLocalBounds() const {
    return {{-2.0f, IsPushButton() ? RAISED_BAR_Y - STEM_LENGTH : OPEN_LEVER_TIP.y}, {2.0f, 0.8f}};
}

bool UISwitch::IsPushButton() const {
    return GetComponent().GetType() == Core::ComponentType::PUSH_BUTTON;
}

bool UISwitch::IsClosed() const {
    return static_cast<const Core::Switch &>(GetComponent()).IsClosed();
}

} // namespace GUI
