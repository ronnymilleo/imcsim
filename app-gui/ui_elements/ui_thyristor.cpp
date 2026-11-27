/**
 * @file    ui_thyristor.cpp
 * @brief   Schematic symbol of a thyristor (SCR), a TRIAC or a GTO.
 */

#include "ui_elements/ui_thyristor.h"

#include "circuit/components/thyristor.h"
#include "spice/spice_value.h"
#include <array>
#include <format>
#include <memory>
#include <utility>

namespace GUI {

namespace {

/// The triangle spans x = -0.5 to 0.5 and the bar sits at its tip, as a diode.
constexpr float BODY_HALF_LENGTH = 0.5f;
constexpr float BODY_HALF_HEIGHT = 0.6f;
/// Where the gate lead leaves the cathode bar, and the terminal it reaches below the symbol.
constexpr float GATE_LEAD_X = BODY_HALF_LENGTH;
constexpr float GATE_LEAD_Y = BODY_HALF_HEIGHT;
/// A TRIAC draws two anti-parallel triangles, their tips this far off the axis and their bases this tall.
constexpr float TRIAC_TIP_OFFSET = 0.25f;
constexpr float TRIAC_BASE_HALF_HEIGHT = 0.35f;
/// Labels sit just outside the symbol.
constexpr float LABEL_OFFSET = 1.0f;
/// A GTO marks its gate lead with two arrowheads, one each way: the lead is this far from the bar to the terminal in
/// each axis, the heads sit at these shares of its length and their wings are this long and this wide.
constexpr float GATE_LEAD_DX = -0.5f;
constexpr float GATE_LEAD_DY = 1.4f;
constexpr float GATE_LEAD_LENGTH = 1.4866f;
constexpr float OUTWARD_HEAD_SHARE = 0.35f;
constexpr float INWARD_HEAD_SHARE = 0.75f;
constexpr float HEAD_LENGTH = 0.25f;
constexpr float HEAD_HALF_WIDTH = 0.12f;

/**
 * @brief   The points of an arrowhead on the gate lead of a GTO, in local grid units.
 * @param[in] share      How far along the lead from the bar to the terminal the tip sits, from 0 to 1.
 * @param[in] direction  1 for a head pointing to the terminal, -1 for one pointing to the bar.
 * @return  One wing end, the tip and the other wing end, which two lines join.
 */
std::array<ImVec2, 3> GateArrowhead(const float share, const float direction) {
    const float along_x = GATE_LEAD_DX / GATE_LEAD_LENGTH * direction;
    const float along_y = GATE_LEAD_DY / GATE_LEAD_LENGTH * direction;
    const ImVec2 tip{GATE_LEAD_X + GATE_LEAD_DX * share, GATE_LEAD_Y + GATE_LEAD_DY * share};
    const ImVec2 base{tip.x - along_x * HEAD_LENGTH, tip.y - along_y * HEAD_LENGTH};
    const ImVec2 sideways{-along_y * HEAD_HALF_WIDTH, along_x * HEAD_HALF_WIDTH};
    return {ImVec2{base.x - sideways.x, base.y - sideways.y}, tip, ImVec2{base.x + sideways.x, base.y + sideways.y}};
}

} // namespace

/**
 * @brief   Creates a thyristor part placed on the grid, together with its simulation component.
 * @param[in] type      Core::ComponentType::THYRISTOR for an SCR, TRIAC for a bidirectional one, GTO for one that a
 *                      negative gate turns off.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIThyristor::UIThyristor(const Core::ComponentType type, const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Thyristor>(type), position, rotation) {
}

/**
 * @brief   The anode (MT2) and cathode (MT1) leads run along the axis; the gate lead runs from the cathode bar down to
 *          its terminal.
 * @param[in,out] canvas  Where it is drawn: the editor window or an exported image.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     color   Line color.
 */
void UIThyristor::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, -2, 0), LocalToScreen(view, -1, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, 1, 0), LocalToScreen(view, 2, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, GATE_LEAD_X, GATE_LEAD_Y), LocalToScreen(view, 0, 2), color, LINE_THICKNESS);
}

void UIThyristor::DrawSymbol(SchematicCanvas &canvas,
                             const ViewTransform &view,
                             const ImU32 color,
                             SymbolStyle /*style*/) const {
    canvas.AddLine(LocalToScreen(view, -1, 0), LocalToScreen(view, -BODY_HALF_LENGTH, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, BODY_HALF_LENGTH, 0), LocalToScreen(view, 1, 0), color, LINE_THICKNESS);
    if (GetComponent().GetType() == Core::ComponentType::TRIAC) {
        // Two anti-parallel triangles, one each way, sharing the MT1 bar: the upper conducts MT2 to MT1, the lower
        // MT1 to MT2
        canvas.AddTriangle(LocalToScreen(view, -BODY_HALF_LENGTH, -TRIAC_TIP_OFFSET - TRIAC_BASE_HALF_HEIGHT),
                           LocalToScreen(view, -BODY_HALF_LENGTH, -TRIAC_TIP_OFFSET + TRIAC_BASE_HALF_HEIGHT),
                           LocalToScreen(view, BODY_HALF_LENGTH, -TRIAC_TIP_OFFSET),
                           color,
                           LINE_THICKNESS);
        canvas.AddTriangle(LocalToScreen(view, BODY_HALF_LENGTH, TRIAC_TIP_OFFSET - TRIAC_BASE_HALF_HEIGHT),
                           LocalToScreen(view, BODY_HALF_LENGTH, TRIAC_TIP_OFFSET + TRIAC_BASE_HALF_HEIGHT),
                           LocalToScreen(view, -BODY_HALF_LENGTH, TRIAC_TIP_OFFSET),
                           color,
                           LINE_THICKNESS);
    } else {
        canvas.AddTriangle(LocalToScreen(view, -BODY_HALF_LENGTH, -BODY_HALF_HEIGHT),
                           LocalToScreen(view, -BODY_HALF_LENGTH, BODY_HALF_HEIGHT),
                           LocalToScreen(view, BODY_HALF_LENGTH, 0),
                           color,
                           LINE_THICKNESS);
    }
    canvas.AddLine(LocalToScreen(view, BODY_HALF_LENGTH, -BODY_HALF_HEIGHT),
                   LocalToScreen(view, BODY_HALF_LENGTH, BODY_HALF_HEIGHT),
                   color,
                   LINE_THICKNESS);
    if (GetComponent().GetType() == Core::ComponentType::GTO) {
        for (const auto &[share, direction] :
             {std::pair{INWARD_HEAD_SHARE, 1.0f}, std::pair{OUTWARD_HEAD_SHARE, -1.0f}}) {
            const std::array<ImVec2, 3> head = GateArrowhead(share, direction);
            canvas.AddLine(LocalToScreen(view, head[0]), LocalToScreen(view, head[1]), color, LINE_THICKNESS);
            canvas.AddLine(LocalToScreen(view, head[1]), LocalToScreen(view, head[2]), color, LINE_THICKNESS);
        }
    }
}

/**
 * @brief   Draws the thyristor name and its holding current above the symbol, in local orientation.
 * @details Both sit on the side away from the gate, as one label like the name and value of a two-terminal part, so
 *          neither covers the gate lead or a wire that goes on from its terminal.
 * @param[in,out] canvas Where it is drawn: the editor window or an exported image.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Text color.
 */
void UIThyristor::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const auto &thyristor = static_cast<const Core::Thyristor &>(GetComponent());
    DrawLabel(canvas,
              view,
              {0.0f, -LABEL_OFFSET},
              {0.0f, -1.0f},
              std::format("{} IH {}", thyristor.GetName(), Core::FormatValue(thyristor.GetParameters().HoldingCurrent)),
              color);
}

/**
 * @brief   Returns the terminals in component order, in local grid units before rotation.
 * @return  Anode (MT2) at (-2, 0), cathode (MT1) at (2, 0) and gate at (0, 2).
 */
std::vector<GridPoint> UIThyristor::GetLocalTerminals() const {
    return {{-2, 0}, {2, 0}, {0, 2}};
}

/**
 * @brief   Returns the pick area of the part, in local grid units before rotation.
 * @return  The two-terminal box, as tall below as the gate lead.
 */
LocalBounds UIThyristor::GetLocalBounds() const {
    return {{-2.0f, -0.8f}, {2.0f, 2.0f}};
}

} // namespace GUI