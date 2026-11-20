/**
 * @file    glow_falloff.h
 * @brief   The texture of a glow: white texels whose alpha falls off from the center.
 */

#ifndef IMCSIM_GLOW_FALLOFF_H
#define IMCSIM_GLOW_FALLOFF_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace GUI {

/// Side of the falloff texture, in texels; the glow is smooth, so a small texture scales up without steps.
inline constexpr std::uint32_t GLOW_TEXTURE_SIZE = 64;
/// Bytes of one texel: red, green, blue and alpha.
inline constexpr std::size_t GLOW_TEXEL_BYTES = 4;

/// The texels of the falloff texture, row by row, in RGBA with 8 bits a channel.
using GlowTexels = std::array<std::uint8_t, std::size_t{GLOW_TEXTURE_SIZE} * GLOW_TEXTURE_SIZE * GLOW_TEXEL_BYTES>;

// Falloff
float GetGlowFalloff(float distance);
GlowTexels BuildFalloffTexels();

} // namespace GUI

#endif // IMCSIM_GLOW_FALLOFF_H
