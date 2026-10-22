/**
 * @file    ui_voltage_source.cpp
 * @brief   Schematic symbol of an independent voltage source.
 */

#include "ui_voltage_source.h"

#include "components/voltage_source.h"
#include <memory>

namespace GUI {

namespace {

constexpr float SIGN_HALF_SIZE = 0.15f;
constexpr float SIGN_OFFSET = 0.4f;
constexpr float IEC_PLUS_POSITION = 1.4f;
constexpr float IEC_PLUS_HEIGHT = 0.4f;

} // namespace

/**
 * @brief   Creates a voltage source placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIVoltageSource::UIVoltageSource(const GridPoint position, const Rotation rotation)
    : UISource(std::make_unique<Core::VoltageSource>(), position, rotation) {
}

// The positive terminal is the first one, at x = -2. AC and pulse sources show their waveform instead of signs
void UIVoltageSource::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                                 const SymbolStyle style) const {
    DrawCircle(canvas, view, color);
    if (DrawWaveform(canvas, view, color)) {
        return;
    }

    if (style == SymbolStyle::IEC) {
        // IEC draws the conductor through the circle and marks the positive side outside it
        canvas.AddLine(LocalToScreen(view, -CIRCLE_RADIUS, 0), LocalToScreen(view, CIRCLE_RADIUS, 0), color,
                       LINE_THICKNESS);
        canvas.AddLine(LocalToScreen(view, -IEC_PLUS_POSITION - SIGN_HALF_SIZE, -IEC_PLUS_HEIGHT),
                       LocalToScreen(view, -IEC_PLUS_POSITION + SIGN_HALF_SIZE, -IEC_PLUS_HEIGHT), color,
                       LINE_THICKNESS);
        canvas.AddLine(LocalToScreen(view, -IEC_PLUS_POSITION, -IEC_PLUS_HEIGHT - SIGN_HALF_SIZE),
                       LocalToScreen(view, -IEC_PLUS_POSITION, -IEC_PLUS_HEIGHT + SIGN_HALF_SIZE), color,
                       LINE_THICKNESS);
        return;
    }

    // The minus bar is drawn across the axis, so it reads as a minus when the source stands vertically
    canvas.AddLine(LocalToScreen(view, -SIGN_OFFSET - SIGN_HALF_SIZE, 0),
                   LocalToScreen(view, -SIGN_OFFSET + SIGN_HALF_SIZE, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, -SIGN_OFFSET, -SIGN_HALF_SIZE),
                   LocalToScreen(view, -SIGN_OFFSET, SIGN_HALF_SIZE), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, SIGN_OFFSET, -SIGN_HALF_SIZE), LocalToScreen(view, SIGN_OFFSET, SIGN_HALF_SIZE),
                   color, LINE_THICKNESS);
}

} // namespace GUI
