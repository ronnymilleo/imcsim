/**
 * @file    glow_falloff_tests.cpp
 * @brief   Tests for the texture of a glow, whose alpha falls off from the center.
 */

#include "overlays/glow_falloff.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>

namespace {

std::uint8_t GetAlpha(const GUI::GlowTexels &texels, const std::size_t row, const std::size_t column) {
    return texels[(row * GUI::GLOW_TEXTURE_SIZE + column) * GUI::GLOW_TEXEL_BYTES + 3];
}

} // namespace

TEST_CASE("A glow is fully opaque at its center and fades out to nothing at its radius", "[glow_falloff]") {
    CHECK_THAT(GUI::GetGlowFalloff(0.0f), Catch::Matchers::WithinAbs(1.0, 1e-6));
    CHECK(GUI::GetGlowFalloff(0.5f) < GUI::GetGlowFalloff(0.1f));
    CHECK(GUI::GetGlowFalloff(0.9f) < GUI::GetGlowFalloff(0.5f));
    CHECK(GUI::GetGlowFalloff(1.0f) == 0.0f);
    CHECK(GUI::GetGlowFalloff(1.5f) == 0.0f);
}

TEST_CASE("The falloff texture is white, round and brightest in the middle", "[glow_falloff]") {
    const GUI::GlowTexels texels = GUI::BuildFalloffTexels();
    for (std::size_t texel = 0; texel < texels.size(); texel += GUI::GLOW_TEXEL_BYTES) {
        REQUIRE(texels[texel] == 255);
        REQUIRE(texels[texel + 1] == 255);
        REQUIRE(texels[texel + 2] == 255);
    }
    constexpr std::size_t LAST = GUI::GLOW_TEXTURE_SIZE - 1;
    constexpr std::size_t MIDDLE = GUI::GLOW_TEXTURE_SIZE / 2;
    // The corners are past the radius
    CHECK(GetAlpha(texels, 0, 0) == 0);
    CHECK(GetAlpha(texels, LAST, LAST) == 0);
    CHECK(GetAlpha(texels, MIDDLE, MIDDLE) > 240);
    CHECK(GetAlpha(texels, MIDDLE, MIDDLE) > GetAlpha(texels, MIDDLE, MIDDLE / 2));
    // The same at the same distance from the middle, whichever way
    CHECK(GetAlpha(texels, 10, 20) == GetAlpha(texels, 20, 10));
    CHECK(GetAlpha(texels, 10, 20) == GetAlpha(texels, LAST - 10, 20));
    CHECK(GetAlpha(texels, 10, 20) == GetAlpha(texels, 10, LAST - 20));
}
