/**
 * @file    ui_gate.cpp
 * @brief   Schematic symbol of a logic gate.
 */

#include "ui_elements/ui_gate.h"

#include "circuit/components/logic_part.h"
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

namespace GUI {

namespace {

/// A gate of two inputs is 3 units high.
constexpr float TWO_INPUT_GATE_HALF_HEIGHT = 1.5f;
/// The body of a gate reaches half a unit past its first and last input.
constexpr float BODY_MARGIN = 0.5f;
constexpr float GATE_LEFT = -2.0f;
constexpr float GATE_RIGHT = 2.0f;
constexpr float BUBBLE_RADIUS = 0.3f;
/// How far the back of an OR gate curves in.
constexpr float OR_BACK_DEPTH = 0.5f;
/// How far the second curve of an XOR gate is from the first.
constexpr float XOR_GAP = 0.35f;
/// Exponent of the curve from the back corner of an OR gate to its tip.
constexpr float OR_SIDE_EXPONENT = 2.2f;
constexpr int CURVE_SEGMENTS = 12;
constexpr float TRIANGLE_HALF_HEIGHT = 1.3f;
constexpr float TRIANGLE_LEFT = -1.5f;
/// The tip of the triangle of a NOT gate stops short of its bubble.
constexpr float NOT_TIP_GAP = 0.1f;
constexpr float NAME_OFFSET_Y = 0.5f;

bool HasOneInput(const Core::ComponentType type) {
    return type == Core::ComponentType::NOT || type == Core::ComponentType::BUFFER;
}

bool IsInverting(const Core::ComponentType type) {
    return type == Core::ComponentType::NOT || type == Core::ComponentType::NAND || type == Core::ComponentType::NOR
        || type == Core::ComponentType::XNOR;
}

bool IsOrFamily(const Core::ComponentType type) {
    return type == Core::ComponentType::OR || type == Core::ComponentType::NOR || type == Core::ComponentType::XOR
        || type == Core::ComponentType::XNOR;
}

bool IsXorFamily(const Core::ComponentType type) {
    return type == Core::ComponentType::XOR || type == Core::ComponentType::XNOR;
}

/**
 * @brief   What the IEC symbol of a gate writes in its box.
 */
const char *IECQualifier(const Core::ComponentType type) {
    switch (type) {
    case Core::ComponentType::AND:
    case Core::ComponentType::NAND:
        return "&";
    case Core::ComponentType::OR:
    case Core::ComponentType::NOR:
        return "\xE2\x89\xA5"
               "1";
    case Core::ComponentType::XOR:
    case Core::ComponentType::XNOR:
        return "=1";
    default:
        return "1";
    }
}

/**
 * @brief   The x where the body of an ANSI gate ends and the output lead starts: before the bubble of an inverting one.
 */
float OutputStart(const Core::ComponentType type) {
    if (type == Core::ComponentType::NOT) {
        return TRIANGLE_HALF_HEIGHT + NOT_TIP_GAP + 2.0f * BUBBLE_RADIUS;
    }
    if (IsInverting(type)) {
        return GATE_RIGHT - 2.0f * BUBBLE_RADIUS;
    }
    return type == Core::ComponentType::BUFFER ? 1.5f : GATE_RIGHT;
}

/**
 * @brief   Where the back of an OR gate is at a height, the curve its inputs touch.
 */
float OrBackX(const float y, const float half_height) {
    const float ratio = y / half_height;
    return GATE_LEFT + OR_BACK_DEPTH * (1.0f - ratio * ratio);
}

/**
 * @brief   The inputs go down the left side two units apart, A at y = -1, B at 1, C at 3 and so on, so adding an input
 *          leaves the ones already wired where they are and the body only grows downward.
 */
int GateInputY(const int input) {
    return 2 * input - 1;
}

} // namespace

/**
 * @brief   Creates a gate placed on the grid, with its simulation component.
 * @param[in] type      A type for which Core::IsLogicPart() is true and Core::IsFlipFlop() is false.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIGate::UIGate(const Core::ComponentType type, const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::LogicPart>(type), position, rotation) {
}

void UIGate::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const Core::ComponentType type = GetComponent().GetType();
    const std::vector<GridPoint> terminals = GetLocalTerminals();
    const bool ansi_or = IsOrFamily(type);
    const float half_height = GetGateHalfHeight();
    const float center = GetGateCenterY();
    for (std::size_t input = 0; input + 1 < terminals.size(); ++input) {
        const auto y = static_cast<float>(terminals[input].Y);
        // The inputs of an OR gate run to its curved back, and past it to the second curve of an XOR gate
        float end = HasOneInput(type) ? TRIANGLE_LEFT : GATE_LEFT;
        if (ansi_or) {
            end = OrBackX(y - center, half_height);
            if (IsXorFamily(type)) {
                end -= XOR_GAP;
            }
        }
        canvas.AddLine(LocalToScreen(view, -3, y), LocalToScreen(view, end, y), color, LINE_THICKNESS);
    }
    canvas.AddLine(LocalToScreen(view, OutputStart(type), center),
                   LocalToScreen(view, 3, center),
                   color,
                   LINE_THICKNESS);
}

void UIGate::DrawSymbol(SchematicCanvas &canvas,
                        const ViewTransform &view,
                        const ImU32 color,
                        const SymbolStyle style) const {
    if (style == SymbolStyle::IEC) {
        DrawIECGate(canvas, view, color);
    } else {
        DrawAnsiGate(canvas, view, color);
    }
}

/**
 * @brief   The name above the body.
 * @param[in,out] canvas  Where it is drawn: the editor window or an exported image.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     color   Text color.
 */
void UIGate::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const float top = GetGateCenterY() - GetGateHalfHeight();
    DrawLabel(canvas, view, {0.0f, top - NAME_OFFSET_Y}, {0.0f, -1.0f}, GetComponent().GetName(), color);
}

/**
 * @brief   Returns the terminals in component order, in local grid units before rotation.
 * @return  The inputs, then the output, as the class description places them.
 */
std::vector<GridPoint> UIGate::GetLocalTerminals() const {
    if (HasOneInput(GetComponent().GetType())) {
        return {{-3, 0}, {3, 0}};
    }
    std::vector<GridPoint> terminals;
    const int input_count = static_cast<const Core::LogicPart &>(GetComponent()).GetInputCount();
    for (int input = 0; input < input_count; ++input) {
        terminals.push_back({-3, GateInputY(input)});
    }
    // The output sits at the middle of the body, which is a whole unit for every count
    terminals.push_back({3, static_cast<int>(GetGateCenterY())});
    return terminals;
}

/**
 * @brief   The leads of a gate run along x, whichever input they are, and the default direction would follow the longer
 *          axis.
 * @param[in] terminal  Index in `GetLocalTerminals()`.
 * @return  `{1, 0}` for an input on the left, `{-1, 0}` for the output on the right.
 */
GridPoint UIGate::GetLocalTerminalInward(const std::size_t terminal) const {
    return GetLocalTerminals()[terminal].X < 0 ? GridPoint{1, 0} : GridPoint{-1, 0};
}

/**
 * @brief   Returns the pick area of the gate, in local grid units before rotation.
 * @return  The box around the leads, as high as the body.
 */
LocalBounds UIGate::GetLocalBounds() const {
    const float center = GetGateCenterY();
    return {{-3.0f, center - GetGateHalfHeight()}, {3.0f, center + GetGateHalfHeight()}};
}

/**
 * @brief   The body of a gate goes from half a unit above its first input to half a unit below its last, so it grows
 *          downward with the inputs and its middle, where the output is, moves down a unit with each one.
 * @details A gate of one input is as high as one of two.
 */
float UIGate::GetGateHalfHeight() const {
    if (HasOneInput(GetComponent().GetType())) {
        return TWO_INPUT_GATE_HALF_HEIGHT;
    }
    const int input_count = static_cast<const Core::LogicPart &>(GetComponent()).GetInputCount();
    return (static_cast<float>(GateInputY(input_count - 1) - GateInputY(0)) + 2.0f * BODY_MARGIN) / 2.0f;
}

float UIGate::GetGateCenterY() const {
    if (HasOneInput(GetComponent().GetType())) {
        return 0.0f;
    }
    const int input_count = static_cast<const Core::LogicPart &>(GetComponent()).GetInputCount();
    return static_cast<float>(GateInputY(0) + GateInputY(input_count - 1)) / 2.0f;
}

UIGate::GateBody UIGate::GetAnsiBody() const {
    return {.Tip = OutputStart(GetComponent().GetType()),
            .HalfHeight = GetGateHalfHeight(),
            .Center = GetGateCenterY()};
}

/**
 * @brief   The distinctive shapes: a flat back and a round front for AND, a curved back and a pointed front for OR, a
 *          second curve for XOR, a triangle for NOT and the buffer, and a bubble on every inverting one.
 */
void UIGate::DrawAnsiGate(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const Core::ComponentType type = GetComponent().GetType();
    const GateBody body = GetAnsiBody();
    if (HasOneInput(type)) {
        DrawBufferShape(canvas, view, body, color);
    } else if (IsOrFamily(type)) {
        DrawOrShape(canvas, view, body, color);
        if (IsXorFamily(type)) {
            DrawXorSecondCurve(canvas, view, body, color);
        }
    } else {
        DrawAndShape(canvas, view, body, color);
    }
    if (IsInverting(type)) {
        DrawBubble(canvas, view, body.Tip + BUBBLE_RADIUS, color);
    }
}

/**
 * @brief   The triangle of NOT and the buffer; the one of NOT stops short of its bubble.
 */
void UIGate::DrawBufferShape(SchematicCanvas &canvas,
                             const ViewTransform &view,
                             const GateBody &body,
                             const ImU32 color) const {
    const float tip = body.Tip - (GetComponent().GetType() == Core::ComponentType::NOT ? NOT_TIP_GAP : 0.0f);
    const ImVec2 triangle[3] = {LocalToScreen(view, TRIANGLE_LEFT, -TRIANGLE_HALF_HEIGHT),
                                LocalToScreen(view, tip, body.Center),
                                LocalToScreen(view, TRIANGLE_LEFT, TRIANGLE_HALF_HEIGHT)};
    canvas.AddPolyline(triangle, 3, color, ImDrawFlags_Closed, LINE_THICKNESS);
}

/**
 * @brief   The sides of an AND gate run straight to the middle and the front is half an ellipse reaching the tip.
 */
void UIGate::DrawAndShape(SchematicCanvas &canvas,
                          const ViewTransform &view,
                          const GateBody &body,
                          const ImU32 color) const {
    const float arc_start = GATE_LEFT + (body.Tip - GATE_LEFT) / 2.0f;
    std::vector<ImVec2> outline;
    outline.push_back(LocalToScreen(view, GATE_LEFT, body.Center + body.HalfHeight));
    outline.push_back(LocalToScreen(view, GATE_LEFT, body.Center - body.HalfHeight));
    outline.push_back(LocalToScreen(view, arc_start, body.Center - body.HalfHeight));
    for (int step = 1; step < CURVE_SEGMENTS; ++step) {
        const float angle = std::numbers::pi_v<float> * (0.5f - static_cast<float>(step) / CURVE_SEGMENTS);
        outline.push_back(LocalToScreen(view,
                                        arc_start + (body.Tip - arc_start) * std::cos(angle),
                                        body.Center + body.HalfHeight * -std::sin(angle)));
    }
    outline.push_back(LocalToScreen(view, arc_start, body.Center + body.HalfHeight));
    canvas.AddPolyline(outline.data(), static_cast<int>(outline.size()), color, ImDrawFlags_Closed, LINE_THICKNESS);
}

/**
 * @brief   The top edge of an OR gate goes from the back corner to the tip, the bottom is its mirror, and the back
 *          curves in.
 */
void UIGate::DrawOrShape(SchematicCanvas &canvas,
                         const ViewTransform &view,
                         const GateBody &body,
                         const ImU32 color) const {
    const auto side = [&body](const float sign, const float ratio) {
        const float curve = 1.0f - std::pow(ratio, OR_SIDE_EXPONENT);
        return ImVec2{GATE_LEFT + (body.Tip - GATE_LEFT) * ratio, body.Center + sign * body.HalfHeight * curve};
    };
    std::vector<ImVec2> outline;
    for (int step = 0; step <= CURVE_SEGMENTS; ++step) {
        outline.push_back(LocalToScreen(view, side(-1.0f, static_cast<float>(step) / CURVE_SEGMENTS)));
    }
    for (int step = CURVE_SEGMENTS - 1; step >= 0; --step) {
        outline.push_back(LocalToScreen(view, side(1.0f, static_cast<float>(step) / CURVE_SEGMENTS)));
    }
    for (int step = 1; step < CURVE_SEGMENTS; ++step) {
        const float y = body.HalfHeight * (1.0f - 2.0f * static_cast<float>(step) / CURVE_SEGMENTS);
        outline.push_back(LocalToScreen(view, OrBackX(y, body.HalfHeight), body.Center + y));
    }
    canvas.AddPolyline(outline.data(), static_cast<int>(outline.size()), color, ImDrawFlags_Closed, LINE_THICKNESS);
}

/**
 * @brief   The second curve of an XOR gate, behind its back.
 */
void UIGate::DrawXorSecondCurve(SchematicCanvas &canvas,
                                const ViewTransform &view,
                                const GateBody &body,
                                const ImU32 color) const {
    std::vector<ImVec2> curve;
    for (int step = 0; step <= CURVE_SEGMENTS; ++step) {
        const float y = body.HalfHeight * (-1.0f + 2.0f * static_cast<float>(step) / CURVE_SEGMENTS);
        curve.push_back(LocalToScreen(view, OrBackX(y, body.HalfHeight) - XOR_GAP, body.Center + y));
    }
    canvas.AddPolyline(curve.data(), static_cast<int>(curve.size()), color, ImDrawFlags_None, LINE_THICKNESS);
}

void UIGate::DrawIECGate(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const Core::ComponentType type = GetComponent().GetType();
    const float tip = IsInverting(type) ? GATE_RIGHT - 2.0f * BUBBLE_RADIUS : GATE_RIGHT;
    const float half_height = GetGateHalfHeight();
    const float center = GetGateCenterY();
    const ImVec2 box[4] = {LocalToScreen(view, GATE_LEFT, center - half_height),
                           LocalToScreen(view, tip, center - half_height),
                           LocalToScreen(view, tip, center + half_height),
                           LocalToScreen(view, GATE_LEFT, center + half_height)};
    canvas.AddPolyline(box, 4, color, ImDrawFlags_Closed, LINE_THICKNESS);
    if (IsInverting(type)) {
        DrawBubble(canvas, view, tip + BUBBLE_RADIUS, color);
    }
    DrawLabel(canvas, view, {(GATE_LEFT + tip) / 2.0f, center - 0.4f}, {0.0f, 1.0f}, IECQualifier(type), color);
}

void UIGate::DrawBubble(SchematicCanvas &canvas,
                        const ViewTransform &view,
                        const float center_x,
                        const ImU32 color) const {
    DrawCircleAt(canvas, view, {center_x, GetGateCenterY()}, BUBBLE_RADIUS, color);
}

} // namespace GUI
