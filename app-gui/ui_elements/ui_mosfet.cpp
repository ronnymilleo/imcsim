/**
 * @file    ui_mosfet.cpp
 * @brief   Schematic symbol of an N- or P-channel enhancement MOSFET.
 */

#include "ui_mosfet.h"

#include "components/mosfet.h"
#include <array>
#include <memory>

namespace GUI {

namespace {

constexpr float GATE_X = -0.6f;
constexpr float CHANNEL_X = -0.3f;
constexpr float PLATE_HALF_HEIGHT = 0.7f;
// Drain and source connect to the ends of the channel
constexpr float CONTACT_Y = 0.55f;
// The channel is broken in three, the sign of an enhancement part
constexpr std::array<std::array<float, 2>, 3> CHANNEL_SEGMENTS = {{{-0.7f, -0.4f}, {-0.15f, 0.15f}, {0.4f, 0.7f}}};
constexpr float NMOS_ARROW_TIP_X = CHANNEL_X + 0.05f;
constexpr float PMOS_ARROW_TIP_X = 0.55f;

} // namespace

/**
 * @brief   Creates a MOSFET placed on the grid, together with its simulation component.
 * @param[in] type      Core::ComponentType::NMOS or PMOS.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIMOSFET::UIMOSFET(const Core::ComponentType type, const GridPoint position, const Rotation rotation)
    : UITransistor(std::make_unique<Core::MOSFET>(type), position, rotation) {
}

void UIMOSFET::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                          SymbolStyle /*style*/) const {
    canvas.AddLine(LocalToScreen(view, -1, 0), LocalToScreen(view, GATE_X, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, GATE_X, -PLATE_HALF_HEIGHT), LocalToScreen(view, GATE_X, PLATE_HALF_HEIGHT),
                   color, LINE_THICKNESS);
    for (const auto &[top, bottom] : CHANNEL_SEGMENTS) {
        canvas.AddLine(LocalToScreen(view, CHANNEL_X, top), LocalToScreen(view, CHANNEL_X, bottom), color,
                       LINE_THICKNESS);
    }

    // Drain on top, source below, and the body joining the source
    canvas.AddLine(LocalToScreen(view, CHANNEL_X, -CONTACT_Y), LocalToScreen(view, 1, -CONTACT_Y), color,
                   LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, 1, -CONTACT_Y), LocalToScreen(view, 1, -1), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, CHANNEL_X, CONTACT_Y), LocalToScreen(view, 1, CONTACT_Y), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, 1, CONTACT_Y), LocalToScreen(view, 1, 1), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, CHANNEL_X, 0), LocalToScreen(view, 1, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, 1, 0), LocalToScreen(view, 1, CONTACT_Y), color, LINE_THICKNESS);

    if (GetComponent().GetType() == Core::ComponentType::NMOS) {
        DrawArrowHead(canvas, view, {NMOS_ARROW_TIP_X, 0.0f}, {-1.0f, 0.0f}, color);
    } else {
        DrawArrowHead(canvas, view, {PMOS_ARROW_TIP_X, 0.0f}, {1.0f, 0.0f}, color);
    }
}

const char *UIMOSFET::GetModelName() const {
    return static_cast<const Core::MOSFET &>(GetComponent()).GetModelName();
}

} // namespace GUI
