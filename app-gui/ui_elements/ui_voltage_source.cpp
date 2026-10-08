/**
 * @file    ui_voltage_source.cpp
 * @brief   Schematic symbol of an independent voltage source.
 */

#include "ui_voltage_source.h"

#include "components/voltage_source.h"
#include <memory>

namespace GUI {

namespace {

constexpr float SignHalfSize = 0.15f;
constexpr float SignOffset = 0.4f;
constexpr float IECPlusPosition = 1.4f;
constexpr float IECPlusHeight = 0.4f;

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
        canvas.AddLine(LocalToScreen(view, -CircleRadius, 0), LocalToScreen(view, CircleRadius, 0), color,
                       LineThickness);
        canvas.AddLine(LocalToScreen(view, -IECPlusPosition - SignHalfSize, -IECPlusHeight),
                       LocalToScreen(view, -IECPlusPosition + SignHalfSize, -IECPlusHeight), color, LineThickness);
        canvas.AddLine(LocalToScreen(view, -IECPlusPosition, -IECPlusHeight - SignHalfSize),
                       LocalToScreen(view, -IECPlusPosition, -IECPlusHeight + SignHalfSize), color, LineThickness);
        return;
    }

    // The minus bar is drawn across the axis, so it reads as a minus when the source stands vertically
    canvas.AddLine(LocalToScreen(view, -SignOffset - SignHalfSize, 0),
                   LocalToScreen(view, -SignOffset + SignHalfSize, 0), color, LineThickness);
    canvas.AddLine(LocalToScreen(view, -SignOffset, -SignHalfSize), LocalToScreen(view, -SignOffset, SignHalfSize),
                   color, LineThickness);
    canvas.AddLine(LocalToScreen(view, SignOffset, -SignHalfSize), LocalToScreen(view, SignOffset, SignHalfSize), color,
                   LineThickness);
}

} // namespace GUI
