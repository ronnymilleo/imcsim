/**
 * @file    live_current.h
 * @brief   Draws the live current as arrows flowing along the wires of a schematic.
 */

#ifndef IMCSIM_LIVE_CURRENT_H
#define IMCSIM_LIVE_CURRENT_H

#include "canvas/schematic_canvas.h"
#include "imgui.h"
#include "results/circuit_snapshot.h"
#include "results/current_flow.h"
#include "schematic/schematic.h"
#include <vector>

namespace GUI {

/// Distance between flowing-current arrows along the wires, in grid units, so they follow the zoom. The arrows only
/// show direction, so they all have the same size and scroll at one speed; the magnitude stays on the heat color.
inline constexpr float FLOW_ARROW_SPACING = 0.60f;
inline constexpr float FLOW_ARROW_LENGTH = 0.20f;

// Arrows
std::vector<ImVec2> ListArrowCenters(const WireCurrent &piece, ImVec2 flow, float phase);

// Drawing
void DrawLiveCurrent(SchematicCanvas &canvas,
                     const ViewTransform &view,
                     const Schematic &schematic,
                     const Core::OperatingPoint &operating_point,
                     const HeatScale &scale,
                     double time);

} // namespace GUI

#endif // IMCSIM_LIVE_CURRENT_H
