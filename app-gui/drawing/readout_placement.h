/**
 * @file    readout_placement.h
 * @brief   Where the voltage readout of a node sits beside its wire, off the wire and its node dot.
 */

#ifndef IMCSIM_READOUT_PLACEMENT_H
#define IMCSIM_READOUT_PLACEMENT_H

#include "imgui.h"

namespace GUI {

ImVec2 PlaceReadout(ImVec2 wire_start, ImVec2 wire_end, ImVec2 readout_size, float gap);

} // namespace GUI

#endif // IMCSIM_READOUT_PLACEMENT_H
