/**
 * @file    ui_logic_part.cpp
 * @brief   Schematic symbol of a logic gate and of a flip-flop.
 */

#include "ui_logic_part.h"

#include "components/logic_part.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace GUI {

namespace {

// The body of a gate reaches half a unit past its first and last input, so a gate of two inputs is 3 units high
constexpr float TwoInputGateHalfHeight = 1.5f;
constexpr float BodyMargin = 0.5f;
constexpr float GateLeft = -2.0f;
constexpr float GateRight = 2.0f;
constexpr float BubbleRadius = 0.3f;
// How far the back of an OR gate curves in, and how far the second curve of an XOR gate is from the first
constexpr float OrBackDepth = 0.5f;
constexpr float XorGap = 0.35f;
// Exponent of the curve from the back corner of an OR gate to its tip
constexpr float OrSideExponent = 2.2f;
constexpr int CurveSegments = 12;
constexpr float TriangleHalfHeight = 1.3f;
constexpr float TriangleLeft = -1.5f;
constexpr float FlipFlopHalfWidth = 2.0f;
constexpr float ClockMarkHalfHeight = 0.3f;
constexpr float ClockMarkDepth = 0.4f;
constexpr float PinLabelInset = 0.2f;
constexpr float NameOffsetY = 0.5f;

bool HasOneInput(const Core::ComponentType type) {
    return type == Core::ComponentType::Not || type == Core::ComponentType::Buffer;
}

bool IsInverting(const Core::ComponentType type) {
    return type == Core::ComponentType::Not || type == Core::ComponentType::Nand || type == Core::ComponentType::Nor ||
           type == Core::ComponentType::Xnor;
}

bool IsOrFamily(const Core::ComponentType type) {
    return type == Core::ComponentType::Or || type == Core::ComponentType::Nor || type == Core::ComponentType::Xor ||
           type == Core::ComponentType::Xnor;
}

bool HasTwoDataInputs(const Core::ComponentType type) {
    return type == Core::ComponentType::JKFlipFlop || type == Core::ComponentType::SRFlipFlop;
}

// Half of the height of the box of a flip-flop: the inputs of a JK or SR one are two apart, a D or T one's one
float FlipFlopHalfHeight(const Core::ComponentType type) {
    return HasTwoDataInputs(type) ? 3.0f : 2.0f;
}

// What the IEC symbol of a gate writes in its box
const char *IECQualifier(const Core::ComponentType type) {
    switch (type) {
    case Core::ComponentType::And:
    case Core::ComponentType::Nand:
        return "&";
    case Core::ComponentType::Or:
    case Core::ComponentType::Nor:
        return "\xE2\x89\xA5"
               "1";
    case Core::ComponentType::Xor:
    case Core::ComponentType::Xnor:
        return "=1";
    default:
        return "1";
    }
}

// The x where the body of an ANSI gate ends and the output lead starts: before the bubble of an inverting one
float OutputStart(const Core::ComponentType type) {
    if (type == Core::ComponentType::Not) {
        return TriangleHalfHeight + 0.1f + 2.0f * BubbleRadius;
    }
    if (IsInverting(type)) {
        return GateRight - 2.0f * BubbleRadius;
    }
    return type == Core::ComponentType::Buffer ? 1.5f : GateRight;
}

// Where the back of an OR gate is at a height, the curve its inputs touch
float OrBackX(const float y, const float half_height) {
    const float ratio = y / half_height;
    return GateLeft + OrBackDepth * (1.0f - ratio * ratio);
}

// The inputs go down the left side two units apart, A at y = -1, B at 1, C at 3 and so on, so adding an input leaves
// the ones already wired where they are and the body only grows downward
int GateInputY(const int input) {
    return 2 * input - 1;
}

} // namespace

/**
 * @brief   Creates a gate or flip-flop placed on the grid, with its simulation component.
 * @param[in] type      A type for which Core::IsLogicPart() is true.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UILogicPart::UILogicPart(const Core::ComponentType type, const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::LogicPart>(type), position, rotation) {
}

void UILogicPart::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    if (IsFlipFlop()) {
        DrawFlipFlopTerminals(canvas, view, color);
    } else {
        DrawGateTerminals(canvas, view, color);
    }
}

void UILogicPart::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                             const SymbolStyle style) const {
    if (IsFlipFlop()) {
        DrawFlipFlop(canvas, view, color);
    } else if (style == SymbolStyle::IEC) {
        DrawIECGate(canvas, view, color);
    } else {
        DrawAnsiGate(canvas, view, color);
    }
}

// The name above the body, and for a flip-flop the names of its pins inside it
void UILogicPart::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const Core::ComponentType type = GetComponent().GetType();
    // A flip-flop's name stands above the end of its SET lead, which is a unit over the box
    const float top = IsFlipFlop() ? -FlipFlopHalfHeight(type) - 1.0f : GetGateCenterY() - GetGateHalfHeight();
    DrawLabel(canvas, view, {0.0f, top - NameOffsetY}, {0.0f, -1.0f}, GetComponent().GetName(), color);
    if (!IsFlipFlop()) {
        return;
    }
    const std::vector<std::string> inputs = Core::GetLogicInputLabels(type);
    const std::vector<GridPoint> terminals = GetLocalTerminals();
    for (std::size_t input = 0; input < inputs.size(); ++input) {
        const GridPoint pin = terminals[input];
        if (pin.X < 0) {
            // A clock input shows its marker instead of a name
            if (inputs[input] != "CLK") {
                DrawLabel(canvas, view, {-FlipFlopHalfWidth + PinLabelInset, static_cast<float>(pin.Y)}, {1.0f, 0.0f},
                          inputs[input], color);
            }
        } else if (pin.Y < 0) {
            DrawLabel(canvas, view, {0.0f, -FlipFlopHalfHeight(type) + PinLabelInset}, {0.0f, 1.0f}, inputs[input],
                      color);
        } else {
            DrawLabel(canvas, view, {0.0f, FlipFlopHalfHeight(type) - PinLabelInset}, {0.0f, -1.0f}, inputs[input],
                      color);
        }
    }
    const std::size_t first_output = inputs.size();
    DrawLabel(canvas, view, {FlipFlopHalfWidth - PinLabelInset, static_cast<float>(terminals[first_output].Y)},
              {-1.0f, 0.0f}, "Q", color);
    DrawLabel(canvas, view, {FlipFlopHalfWidth - PinLabelInset, static_cast<float>(terminals[first_output + 1].Y)},
              {-1.0f, 0.0f}, "QN", color);
}

/**
 * @brief   Returns the terminals in component order, in local grid units before rotation.
 * @return  The inputs, then the outputs, as the class description places them.
 */
std::vector<GridPoint> UILogicPart::GetLocalTerminals() const {
    const Core::ComponentType type = GetComponent().GetType();
    if (!IsFlipFlop()) {
        if (HasOneInput(type)) {
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
    if (HasTwoDataInputs(type)) {
        return {{-3, -2}, {-3, 2}, {-3, 0}, {0, -4}, {0, 4}, {3, -2}, {3, 2}};
    }
    return {{-3, -1}, {-3, 1}, {0, -3}, {0, 3}, {3, -1}, {3, 1}};
}

// The leads of a gate run along x, whichever input they are, and the default direction would follow the longer axis
GridPoint UILogicPart::GetLocalTerminalInward(const std::size_t terminal) const {
    if (IsFlipFlop()) {
        return UIElement::GetLocalTerminalInward(terminal);
    }
    return GetLocalTerminals()[terminal].X < 0 ? GridPoint{1, 0} : GridPoint{-1, 0};
}

/**
 * @brief   Returns the pick area of the part, in local grid units before rotation.
 * @return  The box around the leads, as high as the body.
 */
LocalBounds UILogicPart::GetLocalBounds() const {
    const Core::ComponentType type = GetComponent().GetType();
    if (IsFlipFlop()) {
        const float height = FlipFlopHalfHeight(type) + 1.0f;
        return {{-3.0f, -height}, {3.0f, height}};
    }
    const float center = GetGateCenterY();
    return {{-3.0f, center - GetGateHalfHeight()}, {3.0f, center + GetGateHalfHeight()}};
}

bool UILogicPart::IsFlipFlop() const {
    return Core::IsFlipFlop(GetComponent().GetType());
}

// The body of a gate goes from half a unit above its first input to half a unit below its last, so it grows downward
// with the inputs and its middle, where the output is, moves down a unit with each one. A gate of one input is
// as high as one of two
float UILogicPart::GetGateHalfHeight() const {
    if (HasOneInput(GetComponent().GetType())) {
        return TwoInputGateHalfHeight;
    }
    const int input_count = static_cast<const Core::LogicPart &>(GetComponent()).GetInputCount();
    return (static_cast<float>(GateInputY(input_count - 1) - GateInputY(0)) + 2.0f * BodyMargin) / 2.0f;
}

float UILogicPart::GetGateCenterY() const {
    if (HasOneInput(GetComponent().GetType())) {
        return 0.0f;
    }
    const int input_count = static_cast<const Core::LogicPart &>(GetComponent()).GetInputCount();
    return static_cast<float>(GateInputY(0) + GateInputY(input_count - 1)) / 2.0f;
}

void UILogicPart::DrawGateTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const Core::ComponentType type = GetComponent().GetType();
    const std::vector<GridPoint> terminals = GetLocalTerminals();
    const bool ansi_or = IsOrFamily(type);
    const float half_height = GetGateHalfHeight();
    const float center = GetGateCenterY();
    for (std::size_t input = 0; input + 1 < terminals.size(); ++input) {
        const auto y = static_cast<float>(terminals[input].Y);
        // The inputs of an OR gate run to its curved back, and past it to the second curve of an XOR gate
        float end = HasOneInput(type) ? TriangleLeft : GateLeft;
        if (ansi_or) {
            end = OrBackX(y - center, half_height);
            if (type == Core::ComponentType::Xor || type == Core::ComponentType::Xnor) {
                end -= XorGap;
            }
        }
        canvas.AddLine(LocalToScreen(view, -3, y), LocalToScreen(view, end, y), color, LineThickness);
    }
    canvas.AddLine(LocalToScreen(view, OutputStart(type), center), LocalToScreen(view, 3, center), color,
                   LineThickness);
}

void UILogicPart::DrawFlipFlopTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const Core::ComponentType type = GetComponent().GetType();
    const float half_height = FlipFlopHalfHeight(type);
    for (const GridPoint pin : GetLocalTerminals()) {
        const auto x = static_cast<float>(pin.X);
        const auto y = static_cast<float>(pin.Y);
        if (pin.X != 0) {
            const float edge = pin.X < 0 ? -FlipFlopHalfWidth : FlipFlopHalfWidth;
            canvas.AddLine(LocalToScreen(view, x, y), LocalToScreen(view, edge, y), color, LineThickness);
        } else {
            const float edge = pin.Y < 0 ? -half_height : half_height;
            canvas.AddLine(LocalToScreen(view, x, y), LocalToScreen(view, x, edge), color, LineThickness);
        }
    }
}

// The distinctive shapes: a flat back and a round front for AND, a curved back and a pointed front for OR, a
// second curve for XOR, a triangle for NOT and the buffer, and a bubble on every inverting one
void UILogicPart::DrawAnsiGate(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const Core::ComponentType type = GetComponent().GetType();
    const float tip = OutputStart(type);
    const float half_height = GetGateHalfHeight();
    const float center = GetGateCenterY();
    std::vector<ImVec2> outline;
    if (HasOneInput(type)) {
        const ImVec2 triangle[3] = {LocalToScreen(view, TriangleLeft, -TriangleHalfHeight),
                                    LocalToScreen(view, tip - (type == Core::ComponentType::Not ? 0.1f : 0.0f), center),
                                    LocalToScreen(view, TriangleLeft, TriangleHalfHeight)};
        canvas.AddPolyline(triangle, 3, color, ImDrawFlags_Closed, LineThickness);
    } else if (type == Core::ComponentType::And || type == Core::ComponentType::Nand) {
        // The sides run straight to the middle and the front is half an ellipse reaching the tip
        const float arc_start = GateLeft + (tip - GateLeft) / 2.0f;
        outline.push_back(LocalToScreen(view, GateLeft, center + half_height));
        outline.push_back(LocalToScreen(view, GateLeft, center - half_height));
        outline.push_back(LocalToScreen(view, arc_start, center - half_height));
        for (int step = 1; step < CurveSegments; ++step) {
            const float angle = std::numbers::pi_v<float> * (0.5f - static_cast<float>(step) / CurveSegments);
            outline.push_back(LocalToScreen(view, arc_start + (tip - arc_start) * std::cos(angle),
                                            center + half_height * -std::sin(angle)));
        }
        outline.push_back(LocalToScreen(view, arc_start, center + half_height));
        canvas.AddPolyline(outline.data(), static_cast<int>(outline.size()), color, ImDrawFlags_Closed, LineThickness);
    } else {
        // The top edge goes from the back corner to the tip, the bottom is its mirror, and the back curves in
        const auto side = [tip, half_height, center](const float sign, const float ratio) {
            const float curve = 1.0f - std::pow(ratio, OrSideExponent);
            return ImVec2{GateLeft + (tip - GateLeft) * ratio, center + sign * half_height * curve};
        };
        for (int step = 0; step <= CurveSegments; ++step) {
            const ImVec2 point = side(-1.0f, static_cast<float>(step) / CurveSegments);
            outline.push_back(LocalToScreen(view, point.x, point.y));
        }
        for (int step = CurveSegments - 1; step >= 0; --step) {
            const ImVec2 point = side(1.0f, static_cast<float>(step) / CurveSegments);
            outline.push_back(LocalToScreen(view, point.x, point.y));
        }
        for (int step = 1; step < CurveSegments; ++step) {
            const float y = half_height * (1.0f - 2.0f * static_cast<float>(step) / CurveSegments);
            outline.push_back(LocalToScreen(view, OrBackX(y, half_height), center + y));
        }
        canvas.AddPolyline(outline.data(), static_cast<int>(outline.size()), color, ImDrawFlags_Closed, LineThickness);
        if (type == Core::ComponentType::Xor || type == Core::ComponentType::Xnor) {
            std::vector<ImVec2> second;
            for (int step = 0; step <= CurveSegments; ++step) {
                const float y = half_height * (-1.0f + 2.0f * static_cast<float>(step) / CurveSegments);
                second.push_back(LocalToScreen(view, OrBackX(y, half_height) - XorGap, center + y));
            }
            canvas.AddPolyline(second.data(), static_cast<int>(second.size()), color, ImDrawFlags_None, LineThickness);
        }
    }
    if (IsInverting(type)) {
        DrawBubble(canvas, view, tip + BubbleRadius, color);
    }
}

void UILogicPart::DrawIECGate(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const Core::ComponentType type = GetComponent().GetType();
    const float tip = IsInverting(type) ? GateRight - 2.0f * BubbleRadius : GateRight;
    const float half_height = GetGateHalfHeight();
    const float center = GetGateCenterY();
    const ImVec2 box[4] = {
        LocalToScreen(view, GateLeft, center - half_height), LocalToScreen(view, tip, center - half_height),
        LocalToScreen(view, tip, center + half_height), LocalToScreen(view, GateLeft, center + half_height)};
    canvas.AddPolyline(box, 4, color, ImDrawFlags_Closed, LineThickness);
    if (IsInverting(type)) {
        DrawBubble(canvas, view, tip + BubbleRadius, color);
    }
    DrawLabel(canvas, view, {(GateLeft + tip) / 2.0f, center - 0.4f}, {0.0f, 1.0f}, IECQualifier(type), color);
}

void UILogicPart::DrawFlipFlop(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const float half_height = FlipFlopHalfHeight(GetComponent().GetType());
    const ImVec2 box[4] = {
        LocalToScreen(view, -FlipFlopHalfWidth, -half_height), LocalToScreen(view, FlipFlopHalfWidth, -half_height),
        LocalToScreen(view, FlipFlopHalfWidth, half_height), LocalToScreen(view, -FlipFlopHalfWidth, half_height)};
    canvas.AddPolyline(box, 4, color, ImDrawFlags_Closed, LineThickness);
    // The clock input is the one with the small triangle
    const std::vector<std::string> inputs = Core::GetLogicInputLabels(GetComponent().GetType());
    const std::vector<GridPoint> terminals = GetLocalTerminals();
    const auto clock = static_cast<std::size_t>(std::ranges::find(inputs, "CLK") - inputs.begin());
    const auto y = static_cast<float>(terminals[clock].Y);
    const ImVec2 mark[3] = {LocalToScreen(view, -FlipFlopHalfWidth, y - ClockMarkHalfHeight),
                            LocalToScreen(view, -FlipFlopHalfWidth + ClockMarkDepth, y),
                            LocalToScreen(view, -FlipFlopHalfWidth, y + ClockMarkHalfHeight)};
    canvas.AddPolyline(mark, 3, color, ImDrawFlags_None, LineThickness);
}

void UILogicPart::DrawBubble(SchematicCanvas &canvas, const ViewTransform &view, const float center_x,
                             const ImU32 color) const {
    const ImVec2 origin = LocalToScreen(view, 0, 0);
    const ImVec2 unit = LocalToScreen(view, 1, 0) - origin;
    canvas.AddCircle(LocalToScreen(view, center_x, GetGateCenterY()), std::hypot(unit.x, unit.y) * BubbleRadius, color,
                     0, LineThickness);
}

} // namespace GUI
