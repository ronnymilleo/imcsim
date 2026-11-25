/**
 * @file    ui_transmission_line.cpp
 * @brief   Schematic symbol of a two-port coaxial TEM transmission line.
 */

#include "ui_elements/ui_transmission_line.h"

#include "circuit/components/transmission_line.h"
#include "spice/spice_value.h"
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>

namespace GUI {

namespace {

constexpr float BODY_HALF_WIDTH = 1.05f;
constexpr float ELLIPSE_RX = 0.22f;
constexpr float SHIELD_RY = 0.55f;
constexpr float INNER_LEAD_X = 1.55f;
constexpr float SHIELD_TAP_X = 0.8f;
constexpr int ELLIPSE_SEGMENTS = 24;
constexpr int HALF_ELLIPSE_SEGMENTS = 12;
constexpr float NAME_OFFSET_Y = 1.4f;

} // namespace

/**
 * @brief   Creates a transmission line placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UITransmissionLine::UITransmissionLine(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::TransmissionLine>(), position, rotation) {
}

/**
 * @brief   Draws the port 1 and port 2 leads: + terminals bend to the center conductor axis, - terminals tap the bottom
 *          of the outer shield.
 */
void UITransmissionLine::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const std::array<ImVec2, 3> left_signal = {LocalToScreen(view, -2.0f, -1.0f),
                                               LocalToScreen(view, -INNER_LEAD_X, -1.0f),
                                               LocalToScreen(view, -INNER_LEAD_X, 0.0f)};
    canvas.AddPolyline(left_signal.data(),
                       static_cast<int>(left_signal.size()),
                       color,
                       ImDrawFlags_None,
                       LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, -INNER_LEAD_X, 0.0f),
                   LocalToScreen(view, -BODY_HALF_WIDTH, 0.0f),
                   color,
                   LINE_THICKNESS);

    const std::array<ImVec2, 3> right_signal = {LocalToScreen(view, 2.0f, -1.0f),
                                                LocalToScreen(view, INNER_LEAD_X, -1.0f),
                                                LocalToScreen(view, INNER_LEAD_X, 0.0f)};
    canvas.AddPolyline(right_signal.data(),
                       static_cast<int>(right_signal.size()),
                       color,
                       ImDrawFlags_None,
                       LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, BODY_HALF_WIDTH + ELLIPSE_RX, 0.0f),
                   LocalToScreen(view, INNER_LEAD_X, 0.0f),
                   color,
                   LINE_THICKNESS);

    const std::array<ImVec2, 3> left_shield = {LocalToScreen(view, -2.0f, 1.0f),
                                               LocalToScreen(view, -SHIELD_TAP_X, 1.0f),
                                               LocalToScreen(view, -SHIELD_TAP_X, SHIELD_RY)};
    canvas.AddPolyline(left_shield.data(),
                       static_cast<int>(left_shield.size()),
                       color,
                       ImDrawFlags_None,
                       LINE_THICKNESS);

    const std::array<ImVec2, 3> right_shield = {LocalToScreen(view, 2.0f, 1.0f),
                                                LocalToScreen(view, SHIELD_TAP_X, 1.0f),
                                                LocalToScreen(view, SHIELD_TAP_X, SHIELD_RY)};
    canvas.AddPolyline(right_shield.data(),
                       static_cast<int>(right_shield.size()),
                       color,
                       ImDrawFlags_None,
                       LINE_THICKNESS);
}

void UITransmissionLine::DrawSymbol(SchematicCanvas &canvas,
                                    const ViewTransform &view,
                                    const ImU32 color,
                                    const SymbolStyle /*style*/) const {
    canvas.AddLine(LocalToScreen(view, -BODY_HALF_WIDTH, -SHIELD_RY),
                   LocalToScreen(view, BODY_HALF_WIDTH, -SHIELD_RY),
                   color,
                   LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, -BODY_HALF_WIDTH, SHIELD_RY),
                   LocalToScreen(view, BODY_HALF_WIDTH, SHIELD_RY),
                   color,
                   LINE_THICKNESS);

    std::array<ImVec2, ELLIPSE_SEGMENTS> left_cap{};
    for (int i = 0; i < ELLIPSE_SEGMENTS; ++i) {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / ELLIPSE_SEGMENTS;
        left_cap[static_cast<std::size_t>(i)] =
            LocalToScreen(view, -BODY_HALF_WIDTH + ELLIPSE_RX * std::cos(angle), SHIELD_RY * std::sin(angle));
    }
    canvas.AddPolyline(left_cap.data(), ELLIPSE_SEGMENTS, color, ImDrawFlags_Closed, LINE_THICKNESS);

    std::array<ImVec2, HALF_ELLIPSE_SEGMENTS + 1> right_cap{};
    for (int i = 0; i <= HALF_ELLIPSE_SEGMENTS; ++i) {
        const float angle = -0.5f * std::numbers::pi_v<float>
                          + std::numbers::pi_v<float> * static_cast<float>(i) / HALF_ELLIPSE_SEGMENTS;
        right_cap[static_cast<std::size_t>(i)] =
            LocalToScreen(view, BODY_HALF_WIDTH + ELLIPSE_RX * std::cos(angle), SHIELD_RY * std::sin(angle));
    }
    canvas.AddPolyline(right_cap.data(), HALF_ELLIPSE_SEGMENTS + 1, color, ImDrawFlags_None, LINE_THICKNESS);
}

/**
 * @brief   The name above, and the characteristic impedance with propagation delay (such as "50 Ohm 1ns") below.
 */
void UITransmissionLine::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const auto &line = static_cast<const Core::TransmissionLine &>(GetComponent());
    DrawLabel(canvas, view, {0.0f, -NAME_OFFSET_Y}, {0.0f, -1.0f}, line.GetName(), color);
    DrawLabel(
        canvas,
        view,
        {0.0f, NAME_OFFSET_Y},
        {0.0f, 1.0f},
        std::format("{} Ohm {}s", Core::FormatValue(line.GetValue()), Core::FormatValue(line.GetParameters().Delay)),
        color);
}

/**
 * @brief   Returns the terminals in component order, in local grid units before rotation.
 * @return  Port 1 + at (-2, -1) and - at (-2, 1), then port 2 + at (2, -1) and - at (2, 1).
 */
std::vector<GridPoint> UITransmissionLine::GetLocalTerminals() const {
    return {{-2, -1}, {-2, 1}, {2, -1}, {2, 1}};
}

/**
 * @brief   Returns the pick area of the transmission line, in local grid units before rotation.
 * @return  A box from the port 1 leads to the port 2 leads around the coaxial cylinder.
 */
LocalBounds UITransmissionLine::GetLocalBounds() const {
    return {{-2.0f, -1.5f}, {2.0f, 1.5f}};
}

} // namespace GUI
