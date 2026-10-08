/**
 * @file    theme.h
 * @brief   Visual theme of the interface: ImGui and ImPlot styles, colors and fonts.
 */

#ifndef IMCSIM_THEME_H
#define IMCSIM_THEME_H

#include "imgui.h"
#include <array>
#include <cstddef>
#include <span>

namespace GUI {

/**
 * @struct  ThemePalette
 * @brief   Colors of one theme: the interface surfaces and accent, and the schematic canvas.
 * @details Surfaces go from Background, the darkest on a dark theme, through Surface and Raised to RaisedHot, the
 *          hovered state. The accent marks the active and primary items; AccentDim fills them, Accent and AccentHot
 *          are their hovered and pressed states. Node, current and math trace colors are shared by every theme;
 *          a light theme darkens them through AdaptToBackground().
 */
struct ThemePalette {
    const char *Name;
    bool Light;

    // Interface
    ImVec4 Background;
    ImVec4 Surface;
    ImVec4 Raised;
    ImVec4 RaisedHot;
    ImVec4 Border;
    ImVec4 Text;
    ImVec4 TextMuted;
    ImVec4 Accent;
    ImVec4 AccentHot;
    ImVec4 AccentDim;
    ImVec4 ErrorText;
    ImVec4 WarningText;
    ImVec4 PlotGrid;

    // Schematic canvas
    ImU32 CanvasBackground;
    ImU32 GridDot;
    ImU32 Element;
    ImU32 Wire;
    ImU32 Selected;
    ImU32 CanvasText;
    ImU32 VoltageLabel;
    // Heat scale of voltages and currents, from the lowest value to the highest
    std::array<ImVec4, 4> HeatStops;
};

// Setup
void ApplyTheme();
void LoadThemeFonts();
void RegisterThemeSettingsHandler();

// Themes
std::span<const ThemePalette> GetThemes();
std::size_t GetThemeIndex();
void SetTheme(std::size_t index);
const ThemePalette &GetPalette();
ImU32 AdaptToBackground(ImU32 color);

// Shared colors and fonts
ImFont *GetMonospaceFont();
ImVec4 GetThemeBackground();
ImVec4 GetErrorTextColor();
ImVec4 GetWarningTextColor();
ImU32 GetPreviewColor();
ImU32 GetHighlightColor();

// Widgets
bool PrimaryButton(const char *label, ImVec2 size = ImVec2(0.0f, 0.0f));

} // namespace GUI

#endif // IMCSIM_THEME_H
