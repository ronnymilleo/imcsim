/**
 * @file    glow_falloff.cpp
 * @brief   The texture of a glow: white texels whose alpha falls off from the center.
 */

#include "overlays/glow_falloff.h"

#include <algorithm>
#include <cmath>

namespace GUI {

namespace {

/// Share of the broad halo in the falloff of a glow; with the narrow core it adds up to 1, the alpha at the center.
constexpr float HALO_WEIGHT = 0.65f;
/// Share of the narrow core in the falloff, so a glow looks hot in the middle.
constexpr float CORE_WEIGHT = 0.35f;
/// Width of the core, as a fraction of the radius.
constexpr float CORE_WIDTH = 0.2f;
/// Largest value of an 8-bit channel.
constexpr float CHANNEL_MAX = 255.0f;

} // namespace

/**
 * @brief   Alpha of a glow at a distance from its center: a broad halo with a narrow hot core.
 * @param[in] distance  Distance from the center, as a fraction of the radius.
 * @return  1 at the center, falling to 0 at the radius and beyond.
 */
float GetGlowFalloff(const float distance) {
    if (distance >= 1.0f) {
        return 0.0f;
    }
    const float halo = (1.0f - distance) * (1.0f - distance);
    const float core = std::exp(-(distance / CORE_WIDTH) * (distance / CORE_WIDTH));
    return HALO_WEIGHT * halo + CORE_WEIGHT * core;
}

/**
 * @brief   White texels whose alpha falls off from the center, so the vertex color of each glow tints them.
 * @return  The texels, row by row; the radius of the glow reaches the middle of each edge.
 */
GlowTexels BuildFalloffTexels() {
    GlowTexels texels{};
    for (std::uint32_t row = 0; row < GLOW_TEXTURE_SIZE; ++row) {
        for (std::uint32_t column = 0; column < GLOW_TEXTURE_SIZE; ++column) {
            // Texel centers, from -1 to 1 across the texture
            const float x = (static_cast<float>(column) + 0.5f) / GLOW_TEXTURE_SIZE * 2.0f - 1.0f;
            const float y = (static_cast<float>(row) + 0.5f) / GLOW_TEXTURE_SIZE * 2.0f - 1.0f;
            const float alpha = std::clamp(GetGlowFalloff(std::sqrt(x * x + y * y)), 0.0f, 1.0f);
            std::uint8_t *texel = &texels[(std::size_t{row} * GLOW_TEXTURE_SIZE + column) * GLOW_TEXEL_BYTES];
            texel[0] = texel[1] = texel[2] = 255;
            texel[3] = static_cast<std::uint8_t>(std::lround(alpha * CHANNEL_MAX));
        }
    }
    return texels;
}

} // namespace GUI
