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

constexpr float GateX = -0.6f;
constexpr float ChannelX = -0.3f;
constexpr float PlateHalfHeight = 0.7f;
// Drain and source connect to the ends of the channel
constexpr float ContactY = 0.55f;
// The channel is broken in three, the sign of an enhancement part
constexpr std::array<std::array<float, 2>, 3> ChannelSegments = {{{-0.7f, -0.4f}, {-0.15f, 0.15f}, {0.4f, 0.7f}}};
constexpr float NMOSArrowTipX = ChannelX + 0.05f;
constexpr float PMOSArrowTipX = 0.55f;

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

void UIMOSFET::DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color,
                          SymbolStyle /*style*/) const {
    draw_list->AddLine(LocalToScreen(view, -1, 0), LocalToScreen(view, GateX, 0), color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, GateX, -PlateHalfHeight), LocalToScreen(view, GateX, PlateHalfHeight), color,
                       LineThickness);
    for (const auto &[top, bottom] : ChannelSegments) {
        draw_list->AddLine(LocalToScreen(view, ChannelX, top), LocalToScreen(view, ChannelX, bottom), color,
                           LineThickness);
    }

    // Drain on top, source below, and the body joining the source
    draw_list->AddLine(LocalToScreen(view, ChannelX, -ContactY), LocalToScreen(view, 1, -ContactY), color,
                       LineThickness);
    draw_list->AddLine(LocalToScreen(view, 1, -ContactY), LocalToScreen(view, 1, -1), color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, ChannelX, ContactY), LocalToScreen(view, 1, ContactY), color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, 1, ContactY), LocalToScreen(view, 1, 1), color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, ChannelX, 0), LocalToScreen(view, 1, 0), color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, 1, 0), LocalToScreen(view, 1, ContactY), color, LineThickness);

    if (GetComponent().GetType() == Core::ComponentType::NMOS) {
        DrawArrowHead(draw_list, view, {NMOSArrowTipX, 0.0f}, {-1.0f, 0.0f}, color);
    } else {
        DrawArrowHead(draw_list, view, {PMOSArrowTipX, 0.0f}, {1.0f, 0.0f}, color);
    }
}

const char *UIMOSFET::GetModelName() const {
    return static_cast<const Core::MOSFET &>(GetComponent()).GetModelName();
}

} // namespace GUI
