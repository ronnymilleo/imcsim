/**
 * @file    ui_flip_flop.cpp
 * @brief   Schematic symbol of a flip-flop.
 */

#include "ui_elements/ui_flip_flop.h"

#include "circuit/components/logic_part.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace GUI {

namespace {

constexpr float FLIP_FLOP_HALF_WIDTH = 2.0f;
constexpr float CLOCK_MARK_HALF_HEIGHT = 0.3f;
constexpr float CLOCK_MARK_DEPTH = 0.4f;
constexpr float PIN_LABEL_INSET = 0.2f;
constexpr float NAME_OFFSET_Y = 0.5f;

/**
 * @struct  PinLabelAnchor
 * @brief   Where the name of a pin starts inside the box, and the way it is pushed from there.
 */
struct PinLabelAnchor {
    ImVec2 Point;
    ImVec2 Direction;
};

bool HasTwoDataInputs(const Core::ComponentType type) {
    return type == Core::ComponentType::JK_FLIP_FLOP || type == Core::ComponentType::SR_FLIP_FLOP;
}

/**
 * @brief   The name of an input sits just inside the box, next to where its lead meets it: on the left edge for the
 *          inputs on the left, under the top edge for SET and over the bottom edge for RESET.
 */
PinLabelAnchor GetPinLabelAnchor(const GridPoint pin, const float half_height) {
    if (pin.X < 0) {
        return {.Point = {-FLIP_FLOP_HALF_WIDTH + PIN_LABEL_INSET, static_cast<float>(pin.Y)},
                .Direction = {1.0f, 0.0f}};
    }
    if (pin.Y < 0) {
        return {.Point = {0.0f, -half_height + PIN_LABEL_INSET}, .Direction = {0.0f, 1.0f}};
    }
    return {.Point = {0.0f, half_height - PIN_LABEL_INSET}, .Direction = {0.0f, -1.0f}};
}

} // namespace

/**
 * @brief   Creates a flip-flop placed on the grid, with its simulation component.
 * @param[in] type      A type for which Core::IsFlipFlop() is true.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIFlipFlop::UIFlipFlop(const Core::ComponentType type, const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::LogicPart>(type), position, rotation) {
}

void UIFlipFlop::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const float half_height = GetHalfHeight();
    for (const GridPoint pin : GetLocalTerminals()) {
        const auto x = static_cast<float>(pin.X);
        const auto y = static_cast<float>(pin.Y);
        if (pin.X != 0) {
            const float edge = pin.X < 0 ? -FLIP_FLOP_HALF_WIDTH : FLIP_FLOP_HALF_WIDTH;
            canvas.AddLine(LocalToScreen(view, x, y), LocalToScreen(view, edge, y), color, LINE_THICKNESS);
        } else {
            const float edge = pin.Y < 0 ? -half_height : half_height;
            canvas.AddLine(LocalToScreen(view, x, y), LocalToScreen(view, x, edge), color, LINE_THICKNESS);
        }
    }
}

/**
 * @brief   The box, with the small triangle that marks the clock input.
 * @param[in,out] canvas  Where it is drawn: the editor window or an exported image.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     color   Line color.
 * @param[in]     style   Drawing standard (unused; flip-flop box is identical in IEC and ANSI).
 */
void UIFlipFlop::DrawSymbol(SchematicCanvas &canvas,
                            const ViewTransform &view,
                            const ImU32 color,
                            [[maybe_unused]] const SymbolStyle style) const {
    const float half_height = GetHalfHeight();
    const ImVec2 box[4] = {LocalToScreen(view, -FLIP_FLOP_HALF_WIDTH, -half_height),
                           LocalToScreen(view, FLIP_FLOP_HALF_WIDTH, -half_height),
                           LocalToScreen(view, FLIP_FLOP_HALF_WIDTH, half_height),
                           LocalToScreen(view, -FLIP_FLOP_HALF_WIDTH, half_height)};
    canvas.AddPolyline(box, 4, color, ImDrawFlags_Closed, LINE_THICKNESS);
    const std::vector<std::string> inputs = Core::GetLogicInputLabels(GetComponent().GetType());
    const std::vector<GridPoint> terminals = GetLocalTerminals();
    const auto clock = static_cast<std::size_t>(std::ranges::find(inputs, "CLK") - inputs.begin());
    const auto y = static_cast<float>(terminals[clock].Y);
    const ImVec2 mark[3] = {LocalToScreen(view, -FLIP_FLOP_HALF_WIDTH, y - CLOCK_MARK_HALF_HEIGHT),
                            LocalToScreen(view, -FLIP_FLOP_HALF_WIDTH + CLOCK_MARK_DEPTH, y),
                            LocalToScreen(view, -FLIP_FLOP_HALF_WIDTH, y + CLOCK_MARK_HALF_HEIGHT)};
    canvas.AddPolyline(mark, 3, color, ImDrawFlags_None, LINE_THICKNESS);
}

/**
 * @brief   The name above the end of the SET lead, which is a unit over the box, and the names of the pins inside the
 *          box; the clock input shows its marker instead of a name.
 * @param[in,out] canvas  Where it is drawn: the editor window or an exported image.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     color   Text color.
 */
void UIFlipFlop::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const float half_height = GetHalfHeight();
    DrawLabel(canvas,
              view,
              {0.0f, -half_height - 1.0f - NAME_OFFSET_Y},
              {0.0f, -1.0f},
              GetComponent().GetName(),
              color);
    const std::vector<std::string> inputs = Core::GetLogicInputLabels(GetComponent().GetType());
    const std::vector<GridPoint> terminals = GetLocalTerminals();
    for (std::size_t input = 0; input < inputs.size(); ++input) {
        if (inputs[input] == "CLK") {
            continue;
        }
        const PinLabelAnchor anchor = GetPinLabelAnchor(terminals[input], half_height);
        DrawLabel(canvas, view, anchor.Point, anchor.Direction, inputs[input], color);
    }
    const std::size_t first_output = inputs.size();
    for (const auto &[offset, name] : {std::pair{std::size_t{0}, "Q"}, std::pair{std::size_t{1}, "QN"}}) {
        DrawLabel(canvas,
                  view,
                  {FLIP_FLOP_HALF_WIDTH - PIN_LABEL_INSET, static_cast<float>(terminals[first_output + offset].Y)},
                  {-1.0f, 0.0f},
                  name,
                  color);
    }
}

/**
 * @brief   Returns the terminals in component order, in local grid units before rotation.
 * @return  The inputs, then the outputs, as the class description places them.
 */
std::vector<GridPoint> UIFlipFlop::GetLocalTerminals() const {
    if (HasTwoDataInputs(GetComponent().GetType())) {
        return {{-3, -2}, {-3, 2}, {-3, 0}, {0, -4}, {0, 4}, {3, -2}, {3, 2}};
    }
    return {{-3, -1}, {-3, 1}, {0, -3}, {0, 3}, {3, -1}, {3, 1}};
}

/**
 * @brief   Returns the pick area of the flip-flop, in local grid units before rotation.
 * @return  The box around the leads, SET and RESET included.
 */
LocalBounds UIFlipFlop::GetLocalBounds() const {
    const float height = GetHalfHeight() + 1.0f;
    return {{-3.0f, -height}, {3.0f, height}};
}

/**
 * @brief   Half of the height of the box: the inputs of a JK or SR flip-flop are two apart, a D or T one's one.
 */
float UIFlipFlop::GetHalfHeight() const {
    return HasTwoDataInputs(GetComponent().GetType()) ? 3.0f : 2.0f;
}

} // namespace GUI
