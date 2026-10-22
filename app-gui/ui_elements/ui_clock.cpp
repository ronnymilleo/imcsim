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
constexpr float LeadLength = 0.6f;
constexpr float BoxHalfWidth = 1.0f;
constexpr float BoxHeight = 1.6f;
constexpr float BoxTop = -(LeadLength + BoxHeight);
constexpr float BoxBottom = -LeadLength;
constexpr float BoxMiddle = (BoxTop + BoxBottom) / 2.0f;
constexpr float WaveHalfHeight = 0.35f;
constexpr float WaveLow = BoxMiddle + WaveHalfHeight;
constexpr float WaveHigh = BoxMiddle - WaveHalfHeight;
// The high and low halves of one cycle of the wave inside the box
constexpr float FirstRise = -0.2f;
constexpr float FirstFall = 0.3f;
// The wave starts and ends on the low level, a little inside the box
constexpr float WaveLeft = -0.7f;
constexpr float WaveRight = 0.7f;
constexpr float LabelOffset = 0.3f;

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
    canvas.AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, 0, BoxBottom), color, LineThickness);
}

void UIClock::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                         const SymbolStyle /*style*/) const {
    const std::array<ImVec2, 4> box = {
        LocalToScreen(view, -BoxHalfWidth, BoxTop), LocalToScreen(view, BoxHalfWidth, BoxTop),
        LocalToScreen(view, BoxHalfWidth, BoxBottom), LocalToScreen(view, -BoxHalfWidth, BoxBottom)};
    canvas.AddPolyline(box.data(), static_cast<int>(box.size()), color, ImDrawFlags_Closed, LineThickness);
    const std::array<ImVec2, 6> wave = {
        LocalToScreen(view, WaveLeft, WaveLow),   LocalToScreen(view, FirstRise, WaveLow),
        LocalToScreen(view, FirstRise, WaveHigh), LocalToScreen(view, FirstFall, WaveHigh),
        LocalToScreen(view, FirstFall, WaveLow),  LocalToScreen(view, WaveRight, WaveLow)};
    canvas.AddPolyline(wave.data(), static_cast<int>(wave.size()), color, ImDrawFlags_None, LineThickness);
}

// The name above the box and the frequency with the duty cycle beside it
void UIClock::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const Core::ClockParameters &figures = static_cast<const Core::Clock &>(GetComponent()).GetParameters();
    DrawLabel(canvas, view, {0.0f, BoxTop - LabelOffset}, {0.0f, -1.0f}, GetComponent().GetName(), color);
    const std::string description =
        std::format("{}Hz {}%", Core::FormatValue(figures.Frequency), Core::FormatValue(figures.DutyCycle * 100.0));
    DrawLabel(canvas, view, {BoxHalfWidth + LabelOffset, BoxMiddle}, {1.0f, 0.0f}, description, color);
}

std::vector<GridPoint> UIClock::GetLocalTerminals() const {
    return {{0, 0}};
}

// The terminal is at the origin and its lead goes up to the box
GridPoint UIClock::GetLocalTerminalInward(std::size_t /*terminal*/) const {
    return {0, -1};
}

LocalBounds UIClock::GetLocalBounds() const {
    return {{-BoxHalfWidth, BoxTop}, {BoxHalfWidth, 0.0f}};
}

} // namespace GUI
