/**
 * @file    theme_contrast_tests.cpp
 * @brief   Tests that the text and trace colors of every theme read against what they are drawn on.
 */

#include "drawing/svg_writer.h"
#include "drawing/theme.h"
#include "test_printers.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <string>

namespace {

/// WCAG contrast for body text; muted text, warnings and errors are text too.
constexpr double MIN_TEXT_CONTRAST = 4.5;
/// WCAG contrast for graphics, such as a trace or a wire, which are thicker than the strokes of text.
constexpr double MIN_GRAPHIC_CONTRAST = 3.0;

/**
 * @brief   The relative luminance of a color, as WCAG defines it from sRGB.
 */
double GetLuminance(const ImVec4 color) {
    const auto linear = [](const float channel) {
        const double value = channel;
        return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.x) + 0.7152 * linear(color.y) + 0.0722 * linear(color.z);
}

double GetContrast(const ImVec4 first, const ImVec4 second) {
    const double lighter = std::max(GetLuminance(first), GetLuminance(second));
    const double darker = std::min(GetLuminance(first), GetLuminance(second));
    return (lighter + 0.05) / (darker + 0.05);
}

ImVec4 ToVec4(const ImU32 color) {
    constexpr float CHANNEL_MAX = 255.0f;
    return {static_cast<float>((color >> IM_COL32_R_SHIFT) & 0xFF) / CHANNEL_MAX,
            static_cast<float>((color >> IM_COL32_G_SHIFT) & 0xFF) / CHANNEL_MAX,
            static_cast<float>((color >> IM_COL32_B_SHIFT) & 0xFF) / CHANNEL_MAX, 1.0f};
}

} // namespace

TEST_CASE("Every text color of every theme reads on the surfaces it is drawn on", "[theme]") {
    for (const GUI::ThemePalette &palette : GUI::GetThemes()) {
        CAPTURE(std::string(palette.Name));
        for (const ImVec4 surface : {palette.Background, palette.Surface}) {
            CHECK(GetContrast(palette.Text, surface) >= MIN_TEXT_CONTRAST);
            CHECK(GetContrast(palette.TextMuted, surface) >= MIN_TEXT_CONTRAST);
            CHECK(GetContrast(palette.WarningText, surface) >= MIN_TEXT_CONTRAST);
            CHECK(GetContrast(palette.ErrorText, surface) >= MIN_TEXT_CONTRAST);
        }
        // Selected tabs, the Run button and checked boxes fill with the dim accent under plain text
        CHECK(GetContrast(palette.Text, palette.AccentDim) >= MIN_TEXT_CONTRAST);
    }
}

TEST_CASE("The canvas text and voltages read on the canvas of every theme", "[theme]") {
    for (const GUI::ThemePalette &palette : GUI::GetThemes()) {
        CAPTURE(std::string(palette.Name));
        const ImVec4 canvas = ToVec4(palette.CanvasBackground);
        CHECK(GetContrast(ToVec4(palette.CanvasText), canvas) >= MIN_TEXT_CONTRAST);
        // The voltages sit on a box of the surface color
        CHECK(GetContrast(ToVec4(palette.VoltageLabel), palette.Surface) >= MIN_TEXT_CONTRAST);
        CHECK(GetContrast(ToVec4(palette.Wire), canvas) >= MIN_GRAPHIC_CONTRAST);
    }
}

TEST_CASE("The node colors a light theme darkens stand out from its canvas", "[theme]") {
    // The colors of nodes 1 to 6, which AdaptToBackground() darkens through AdjustColorForPrint() on a light theme
    const auto node_colors = {GUI::ExportColor{230, 120, 100}, GUI::ExportColor{110, 170, 240},
                              GUI::ExportColor{240, 200, 90},  GUI::ExportColor{190, 130, 230},
                              GUI::ExportColor{100, 210, 190}, GUI::ExportColor{240, 150, 200}};
    for (const GUI::ThemePalette &palette : GUI::GetThemes()) {
        if (!palette.Light) {
            continue;
        }
        CAPTURE(std::string(palette.Name));
        for (const GUI::ExportColor color : node_colors) {
            const GUI::ExportColor adjusted = GUI::AdjustColorForPrint(color);
            const ImVec4 trace = {static_cast<float>(adjusted.Red) / 255.0f,
                                  static_cast<float>(adjusted.Green) / 255.0f,
                                  static_cast<float>(adjusted.Blue) / 255.0f, 1.0f};
            CAPTURE(adjusted.Red, adjusted.Green, adjusted.Blue);
            CHECK(GetContrast(trace, ToVec4(palette.CanvasBackground)) >= MIN_GRAPHIC_CONTRAST);
        }
    }
}
