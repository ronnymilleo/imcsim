/**
 * @file    helpers.h
 * @brief   Geometry helpers for the schematic editor: grid points, rotations, snapping and world/screen conversion.
 */

#ifndef IMCSIM_HELPERS_H
#define IMCSIM_HELPERS_H

#include "imgui.h"
#include <compare>
#include <cstdint>
#include <optional>

namespace GUI {

inline constexpr float LineThickness = 2.0f;

/**
 * @struct  GridPoint
 * @brief   A point on the schematic grid, in integer world units.
 * @details Connections are found by comparing points exactly, which is reliable with integers but not with floats.
 */
struct GridPoint {
    int X = 0;
    int Y = 0;

    // Ordering lets grid points be keys of std::map
    auto operator<=>(const GridPoint &) const = default;
};

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

GridPoint operator+(GridPoint first, GridPoint second);
GridPoint operator-(GridPoint first, GridPoint second);
ImVec2 ToVec2(GridPoint point);
Rotation NextRotation(Rotation rotation);
Rotation InverseRotation(Rotation rotation);
int ToDegrees(Rotation rotation);
std::optional<Rotation> RotationFromDegrees(int degrees);
ImVec2 Rotate(ImVec2 point, Rotation rotation);
GridPoint Rotate(GridPoint point, Rotation rotation);
GridPoint Snap(ImVec2 world_pos);

} // namespace GUI

#endif // IMCSIM_HELPERS_H
