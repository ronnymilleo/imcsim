/**
 * @file    theme.h
 * @brief   Visual theme of the interface: ImGui and ImPlot styles, colors and fonts.
 */

#ifndef IMCSIM_THEME_H
#define IMCSIM_THEME_H

#include "imgui.h"

namespace GUI {

// Setup
void ApplyTheme();
void LoadThemeFonts();

// Shared colors and fonts
ImFont *GetMonospaceFont();
ImVec4 GetThemeBackground();
ImVec4 GetErrorTextColor();
ImVec4 GetWarningTextColor();

// Widgets
bool PrimaryButton(const char *label);

} // namespace GUI

#endif // IMCSIM_THEME_H
