/**
 * @file    readout_placement.h
 * @brief   Where the voltage readout of a node sits beside its wire, off the wire and its node dot.
 */

#ifndef IMCSIM_READOUT_PLACEMENT_H
#define IMCSIM_READOUT_PLACEMENT_H

#include "imgui.h"
#include <optional>

namespace GUI {

/// Smallest font size of a readout; below it the pill is left out, as the wire colors still carry the voltages.
inline constexpr float MIN_READOUT_FONT_SIZE = 9.0f;

ImVec2 PlaceReadout(ImVec2 wire_start, ImVec2 wire_end, ImVec2 readout_size, float gap);
std::optional<float> GetReadoutFontSize(float zoom);

} // namespace GUI

#endif // IMCSIM_READOUT_PLACEMENT_H
