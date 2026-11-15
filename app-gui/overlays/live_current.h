/**
 * @file    live_current.h
 * @brief   Draws the live current as arrows flowing along the wires of a schematic.
 */

#ifndef IMCSIM_LIVE_CURRENT_H
#define IMCSIM_LIVE_CURRENT_H

#include "canvas/schematic_canvas.h"
#include "results/circuit_snapshot.h"
#include "schematic/schematic.h"

namespace GUI {

void DrawLiveCurrent(SchematicCanvas &canvas, const ViewTransform &view, const Schematic &schematic,
                     const Core::OperatingPoint &operating_point, const HeatScale &scale, double time);

} // namespace GUI

#endif // IMCSIM_LIVE_CURRENT_H
