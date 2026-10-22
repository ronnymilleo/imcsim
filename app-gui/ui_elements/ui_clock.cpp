/**
 * @file    ui_clock.cpp
 * @brief   Schematic symbol of a digital clock.
 */

#include "ui_clock.h"

#include "components/clock.h"
#include "spice_value.h"
#include <array>
#include <format>
#include <memory>

namespace GUI {

namespace {

// The lead runs from the terminal up to the box, and the square wave sits in the middle of the box
constexpr float LEAD_LENGTH = 0.6f;
constexpr float BOX_HALF_WIDTH = 1.0f;
constexpr float BOX_HEIGHT = 1.6f;
constexpr float BOX_TOP = -(LEAD_LENGTH + BOX_HEIGHT);
constexpr float BOX_BOTTOM = -LEAD_LENGTH;
constexpr float BOX_MIDDLE = (BOX_TOP + BOX_BOTTOM) / 2.0f;
constexpr float WAVE_HALF_HEIGHT = 0.35f;
constexpr float WAVE_LOW = BOX_MIDDLE + WAVE_HALF_HEIGHT;
constexpr float WAVE_HIGH = BOX_MIDDLE - WAVE_HALF_HEIGHT;
// The high and low halves of one cycle of the wave inside the box
constexpr float FIRST_RISE = -0.2f;
constexpr float FIRST_FALL = 0.3f;
// The wave starts and ends on the low level, a little inside the box
constexpr float WAVE_LEFT = -0.7f;
constexpr float WAVE_RIGHT = 0.7f;
constexpr float LABEL_OFFSET = 0.3f;

} // namespace

/**
 * @brief   Creates a clock placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIClock::UIClock(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Clock>(), position, rotation) {
}

void UIClock::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, 0, BOX_BOTTOM), color, LINE_THICKNESS);
}

void UIClock::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                         const SymbolStyle /*style*/) const {
    const std::array<ImVec2, 4> box = {
        LocalToScreen(view, -BOX_HALF_WIDTH, BOX_TOP), LocalToScreen(view, BOX_HALF_WIDTH, BOX_TOP),
        LocalToScreen(view, BOX_HALF_WIDTH, BOX_BOTTOM), LocalToScreen(view, -BOX_HALF_WIDTH, BOX_BOTTOM)};
    canvas.AddPolyline(box.data(), static_cast<int>(box.size()), color, ImDrawFlags_Closed, LINE_THICKNESS);
    const std::array<ImVec2, 6> wave = {
        LocalToScreen(view, WAVE_LEFT, WAVE_LOW),   LocalToScreen(view, FIRST_RISE, WAVE_LOW),
        LocalToScreen(view, FIRST_RISE, WAVE_HIGH), LocalToScreen(view, FIRST_FALL, WAVE_HIGH),
        LocalToScreen(view, FIRST_FALL, WAVE_LOW),  LocalToScreen(view, WAVE_RIGHT, WAVE_LOW)};
    canvas.AddPolyline(wave.data(), static_cast<int>(wave.size()), color, ImDrawFlags_None, LINE_THICKNESS);
}

// The name above the box and the frequency with the duty cycle beside it
void UIClock::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const Core::ClockParameters &figures = static_cast<const Core::Clock &>(GetComponent()).GetParameters();
    DrawLabel(canvas, view, {0.0f, BOX_TOP - LABEL_OFFSET}, {0.0f, -1.0f}, GetComponent().GetName(), color);
    const std::string description =
        std::format("{}Hz {}%", Core::FormatValue(figures.Frequency), Core::FormatValue(figures.DutyCycle * 100.0));
    DrawLabel(canvas, view, {BOX_HALF_WIDTH + LABEL_OFFSET, BOX_MIDDLE}, {1.0f, 0.0f}, description, color);
}

std::vector<GridPoint> UIClock::GetLocalTerminals() const {
    return {{0, 0}};
}

// The terminal is at the origin and its lead goes up to the box
GridPoint UIClock::GetLocalTerminalInward(std::size_t /*terminal*/) const {
    return {0, -1};
}

LocalBounds UIClock::GetLocalBounds() const {
    return {{-BOX_HALF_WIDTH, BOX_TOP}, {BOX_HALF_WIDTH, 0.0f}};
}

} // namespace GUI
