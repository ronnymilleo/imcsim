/**
 * @file    schematic_drawing.h
 * @brief   Schematic drawing shared by the editor and the SVG export: colors, probe markers and the export itself.
 */

#ifndef IMCSIM_SCHEMATIC_DRAWING_H
#define IMCSIM_SCHEMATIC_DRAWING_H

#include "helpers.h"
#include "imgui.h"
#include "schematic.h"
#include "schematic_canvas.h"
#include "ui_elements/ui_element.h"
#include <expected>
#include <string>

namespace GUI {

// Warm tones that match the theme of the editor
inline constexpr ImU32 SchematicBackgroundColor = IM_COL32(18, 15, 16, 255);
inline constexpr ImU32 ElementColor = IM_COL32(220, 220, 220, 255);
inline constexpr ImU32 WireColor = IM_COL32(158, 192, 120, 255);
inline constexpr float MeasurementMarkerRadius = 4.0f;
// Terminal rings on screen, relative to the zoom like junction dots: 4 pixels at the default zoom
inline constexpr float ScreenTerminalRingScale = 0.2f;

// Annotations
void DrawMeasurementMarkers(SchematicCanvas &canvas, const ViewTransform &view, Schematic &schematic);
void DrawTerminalNumbers(SchematicCanvas &canvas, const ViewTransform &view, const Schematic &schematic,
                         float ring_scale);

// Export
std::expected<std::string, std::string> RenderSchematicSvg(Schematic &schematic, SymbolStyle style,
                                                           bool terminal_numbers, bool dark);

} // namespace GUI

#endif // IMCSIM_SCHEMATIC_DRAWING_H
