/**
 * @file    helpers.cpp
 * @brief   Geometry helpers for the schematic editor: grid points, rotations, snapping, view transform and framing.
 */

#include "helpers.h"

#include <algorithm>
#include <cmath>

namespace GUI {

namespace {

// Grid units left around the framed points, since symbols reach a little past their terminals
constexpr float FrameMargin = 2.0f;

} // namespace

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
 * @brief   Adds two grid points component-wise.
 * @param[in] first   First point.
 * @param[in] second  Second point.
 * @return  The sum of both points.
 */
GridPoint operator+(const GridPoint first, const GridPoint second) {
    return {first.X + second.X, first.Y + second.Y};
}

/**
 * @brief   Subtracts two grid points component-wise.
 * @param[in] first   Point to subtract from.
 * @param[in] second  Point to subtract.
 * @return  The offset from the second point to the first.
 */
GridPoint operator-(const GridPoint first, const GridPoint second) {
    return {first.X - second.X, first.Y - second.Y};
}

/**
 * @brief   Converts a grid point to world coordinates for drawing.
 * @param[in] point  Point on the grid.
 * @return  The same position as floats.
 */
ImVec2 ToVec2(const GridPoint point) {
    return {static_cast<float>(point.X), static_cast<float>(point.Y)};
}

/**
 * @brief   Rounds a world position to the nearest grid point.
 * @param[in] world_pos  Position in grid units.
 * @return  The closest grid point.
 */
GridPoint Snap(const ImVec2 world_pos) {
    return {static_cast<int>(std::lround(world_pos.x)), static_cast<int>(std::lround(world_pos.y))};
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
 * @brief   Returns the rotation that undoes the given one.
 * @param[in] rotation  Orientation to undo.
 * @return  The opposite orientation, so applying both leaves a point unchanged.
 */
Rotation InverseRotation(const Rotation rotation) {
    switch (rotation) {
    case Rotation::R0:
        return Rotation::R0;
    case Rotation::R90:
        return Rotation::R270;
    case Rotation::R180:
        return Rotation::R180;
    case Rotation::R270:
        return Rotation::R90;
    }
    return Rotation::R0;
}

/**
 * @brief   Converts an orientation to clockwise degrees.
 * @param[in] rotation  Orientation to convert.
 * @return  0, 90, 180 or 270.
 */
int ToDegrees(const Rotation rotation) {
    switch (rotation) {
    case Rotation::R0:
        return 0;
    case Rotation::R90:
        return 90;
    case Rotation::R180:
        return 180;
    case Rotation::R270:
        return 270;
    }
    return 0;
}

/**
 * @brief   Converts clockwise degrees to an orientation.
 * @param[in] degrees  Angle in degrees.
 * @return  The orientation for 0, 90, 180 or 270, or no value for any other angle.
 */
std::optional<Rotation> RotationFromDegrees(const int degrees) {
    switch (degrees) {
    case 0:
        return Rotation::R0;
    case 90:
        return Rotation::R90;
    case 180:
        return Rotation::R180;
    case 270:
        return Rotation::R270;
    default:
        return std::nullopt;
    }
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
 * @brief   Rotates a grid point around the origin.
 * @param[in] point     Point in local grid units.
 * @param[in] rotation  Orientation to apply.
 * @return  The rotated point, still on the grid.
 */
GridPoint Rotate(const GridPoint point, const Rotation rotation) {
    switch (rotation) {
    case Rotation::R0:
        return point;
    case Rotation::R90:
        return {-point.Y, point.X};
    case Rotation::R180:
        return {-point.X, -point.Y};
    case Rotation::R270:
        return {point.Y, -point.X};
    }
    return point;
}

/**
 * @brief   Finds the pan and zoom that center a set of points on a canvas, such as a schematic just opened.
 * @param[in] points       Grid points to show, such as terminals and wire ends.
 * @param[in] canvas_size  Size of the canvas in pixels.
 * @param[in] min_zoom     Smallest zoom allowed, in pixels per grid unit.
 * @param[in] max_zoom     Largest zoom to use; small circuits are centered at it instead of filling the canvas.
 * @return  The view frame; with no points, the origin at the top-left corner and the largest zoom.
 */
ViewFrame FramePoints(const std::span<const GridPoint> points, const ImVec2 canvas_size, const float min_zoom,
                      const float max_zoom) {
    if (points.empty()) {
        return {.Pan = {0.0f, 0.0f}, .Zoom = max_zoom};
    }
    const auto [min_x, max_x] = std::ranges::minmax(points, {}, &GridPoint::X);
    const auto [min_y, max_y] = std::ranges::minmax(points, {}, &GridPoint::Y);
    return FrameBounds(ToVec2({min_x.X, min_y.Y}), ToVec2({max_x.X, max_y.Y}), canvas_size, min_zoom, max_zoom);
}

/**
 * @brief   Finds the pan and zoom that center an area on a canvas, such as everything a schematic draws.
 * @param[in] min          Top left corner of the area, in grid units.
 * @param[in] max          Bottom right corner of the area, in grid units.
 * @param[in] canvas_size  Size of the canvas in pixels.
 * @param[in] min_zoom     Smallest zoom allowed, in pixels per grid unit.
 * @param[in] max_zoom     Largest zoom to use; small areas are centered at it instead of filling the canvas.
 * @return  The view frame, with a margin of two grid units around the area.
 */
ViewFrame FrameBounds(const ImVec2 min, const ImVec2 max, const ImVec2 canvas_size, const float min_zoom,
                      const float max_zoom) {
    const float width = max.x - min.x + 2.0f * FrameMargin;
    const float height = max.y - min.y + 2.0f * FrameMargin;
    const float zoom = std::clamp(std::min(canvas_size.x / width, canvas_size.y / height), min_zoom, max_zoom);
    const ImVec2 center = (min + max) / 2.0f;
    return {.Pan = {canvas_size.x / 2.0f - center.x * zoom, canvas_size.y / 2.0f - center.y * zoom}, .Zoom = zoom};
}

} // namespace GUI
