/**
 * @file    helpers.h
 * @brief   Geometry helpers for the schematic editor: rotations, grid snapping and world/screen conversion.
 */

#ifndef IMCSIM_HELPERS_H
#define IMCSIM_HELPERS_H

#include "imgui.h"
#include <cstdint>

namespace GUI {

/**
 * @enum    Rotation
 * @brief   Orientation of an element on the grid, in clockwise steps of 90 degrees.
 */
enum class Rotation : uint8_t {
    R0,
    R90,
    R180,
    R270
};

/**
 * @class   ViewTransform
 * @brief   Converts between world coordinates (grid units) and screen pixels for one frame of the canvas.
 */
class ViewTransform {
public:
    ViewTransform(ImVec2 origin, ImVec2 pan, float zoom);

    ImVec2 ToScreen(ImVec2 world_pos) const;
    ImVec2 ToWorld(ImVec2 screen_pos) const;

private:
    ImVec2 m_Origin;
    ImVec2 m_Pan;
    float m_Zoom;
};

Rotation NextRotation(Rotation rotation);
ImVec2 Rotate(ImVec2 point, Rotation rotation);
ImVec2 Snap(ImVec2 world_pos);

} // namespace GUI

#endif // IMCSIM_HELPERS_H
