/**
 * @file    helpers.cpp
 * @brief   Geometry helpers for the schematic editor: rotations, grid snapping and world/screen conversion.
 */

#include "helpers.h"

#include <cmath>

namespace GUI {

/**
 * @brief   Creates the transform for the current canvas state.
 * @param[in] origin  Top-left corner of the canvas in screen pixels.
 * @param[in] pan     Pan offset in pixels.
 * @param[in] zoom    Pixels per grid unit.
 */
ViewTransform::ViewTransform(const ImVec2 origin, const ImVec2 pan, const float zoom)
    : m_Origin(origin), m_Pan(pan), m_Zoom(zoom) {
}

/**
 * @brief   Converts a world position to screen pixels.
 * @param[in] world_pos  Position in grid units.
 * @return  Position in screen pixels.
 */
ImVec2 ViewTransform::ToScreen(const ImVec2 world_pos) const {
    return m_Origin + m_Pan + world_pos * m_Zoom;
}

/**
 * @brief   Converts a screen position to world coordinates.
 * @param[in] screen_pos  Position in screen pixels.
 * @return  Position in grid units.
 */
ImVec2 ViewTransform::ToWorld(const ImVec2 screen_pos) const {
    return (screen_pos - m_Origin - m_Pan) / m_Zoom;
}

/**
 * @brief   Returns the next orientation, turning 90 degrees clockwise.
 * @param[in] rotation  Current orientation.
 * @return  The orientation after one step, wrapping from R270 back to R0.
 */
Rotation NextRotation(const Rotation rotation) {
    switch (rotation) {
    case Rotation::R0:
        return Rotation::R90;
    case Rotation::R90:
        return Rotation::R180;
    case Rotation::R180:
        return Rotation::R270;
    case Rotation::R270:
        return Rotation::R0;
    }
    return Rotation::R0;
}

/**
 * @brief   Rotates a point around the origin.
 * @param[in] point     Point in local grid units.
 * @param[in] rotation  Orientation to apply.
 * @return  The rotated point.
 * @note    Screen Y grows downward, so each step turns clockwise on screen.
 */
ImVec2 Rotate(const ImVec2 point, const Rotation rotation) {
    switch (rotation) {
    case Rotation::R0:
        return point;
    case Rotation::R90:
        return {-point.y, point.x};
    case Rotation::R180:
        return {-point.x, -point.y};
    case Rotation::R270:
        return {point.y, -point.x};
    }
    return point;
}

/**
 * @brief   Rounds a world position to the nearest grid point.
 * @param[in] world_pos  Position in grid units.
 * @return  The closest grid point.
 */
ImVec2 Snap(const ImVec2 world_pos) {
    return {std::round(world_pos.x), std::round(world_pos.y)};
}

} // namespace GUI
