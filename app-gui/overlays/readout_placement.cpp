/**
 * @file    readout_placement.cpp
 * @brief   Where the voltage readout of a node sits beside its wire, off the wire and its node dot.
 */

#include "overlays/readout_placement.h"

#include "base/helpers.h"

#include <cmath>

namespace GUI {

/**
 * @brief   Places a readout beside the middle of a wire: under a level wire, centered on it, and right of an upright
 * one, so it never covers the wire, its node dot or the name of a probe, which sits over the wire.
 * @param[in] wire_start    One end of the wire on screen, in pixels.
 * @param[in] wire_end      The other end; the same point for a node with no wire, read at a terminal.
 * @param[in] readout_size  Size of the readout, in pixels.
 * @param[in] gap           Space between the wire and the readout, in pixels.
 * @return  The top left corner of the readout on screen. A terminal takes it under and right of the point, as a level
 *          wire would after its end.
 */
ImVec2 PlaceReadout(const ImVec2 wire_start, const ImVec2 wire_end, const ImVec2 readout_size, const float gap) {
    const ImVec2 middle = {(wire_start.x + wire_end.x) / 2.0f, (wire_start.y + wire_end.y) / 2.0f};
    const float width = std::abs(wire_end.x - wire_start.x);
    const float height = std::abs(wire_end.y - wire_start.y);
    if (width == 0.0f && height == 0.0f) {
        return {middle.x + gap, middle.y + gap};
    }
    if (height > width) {
        return {middle.x + gap, middle.y - readout_size.y / 2.0f};
    }
    return {middle.x - readout_size.x / 2.0f, middle.y + gap};
}

/**
 * @brief   The font size of the readouts at a zoom: that of the part labels, so a readout keeps its size next to the
 *          parts and never grows over them when zoomed out.
 * @param[in] zoom  Pixels per grid unit.
 * @return  The font size in pixels, or none below MIN_READOUT_FONT_SIZE, where the readouts are left out.
 */
std::optional<float> GetReadoutFontSize(const float zoom) {
    const float font_size = zoom * LABEL_SCALE;
    if (font_size < MIN_READOUT_FONT_SIZE) {
        return std::nullopt;
    }
    return font_size;
}

} // namespace GUI
