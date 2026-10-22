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

// The box the square wave sits in, and the leads from the default terminals at x = -2 and x = 2
constexpr float BoxX = 1.0f;
constexpr float BoxY = 0.9f;
constexpr float WaveHalfHeight = 0.35f;
constexpr float WaveLow = WaveHalfHeight;
constexpr float WaveHigh = -WaveHalfHeight;
// The high and low halves of one cycle of the wave inside the box
constexpr float FirstRise = -0.2f;
constexpr float FirstFall = 0.3f;
// The wave starts and ends on the low level, a little inside the box
constexpr float WaveLeft = -0.7f;
constexpr float WaveRight = 0.7f;
constexpr float LabelOffset = 1.05f;

} // namespace

/**
 * @brief   Creates a clock placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIClock::UIClock(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Clock>(), position, rotation) {
}

void UIClock::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                         const SymbolStyle /*style*/) const {
    const std::array<ImVec2, 4> box = {LocalToScreen(view, -BoxX, -BoxY), LocalToScreen(view, BoxX, -BoxY),
                                       LocalToScreen(view, BoxX, BoxY), LocalToScreen(view, -BoxX, BoxY)};
    canvas.AddPolyline(box.data(), static_cast<int>(box.size()), color, ImDrawFlags_Closed, LineThickness);
    const std::array<ImVec2, 6> wave = {
        LocalToScreen(view, WaveLeft, WaveLow),   LocalToScreen(view, FirstRise, WaveLow),
        LocalToScreen(view, FirstRise, WaveHigh), LocalToScreen(view, FirstFall, WaveHigh),
        LocalToScreen(view, FirstFall, WaveLow),  LocalToScreen(view, WaveRight, WaveLow)};
    canvas.AddPolyline(wave.data(), static_cast<int>(wave.size()), color, ImDrawFlags_None, LineThickness);
}

// The name above the box and the frequency with the duty cycle below it
void UIClock::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const Core::ClockParameters &figures = static_cast<const Core::Clock &>(GetComponent()).GetParameters();
    DrawLabel(canvas, view, {0.0f, -BoxY - LabelOffset}, {0.0f, -1.0f}, GetComponent().GetName(), color);
    const std::string description =
        std::format("{}Hz {}%", Core::FormatValue(figures.Frequency), Core::FormatValue(figures.DutyCycle * 100.0));
    DrawLabel(canvas, view, {0.0f, BoxY + LabelOffset}, {0.0f, 1.0f}, description, color);
}

} // namespace GUI
