/**
 * @file    speed_selector.cpp
 * @brief   Compact speed multiplier selector shared by transient playback and live simulation controls.
 */

#include "windows/speed_selector.h"

#include "imgui.h"
#include "results/playback.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace GUI {

namespace {

/// Tolerance when matching the current speed against one of the preset multipliers.
constexpr double SPEED_MATCH_TOLERANCE = 1e-9;

} // namespace

/**
 * @brief   Fixed width of the compact speed selector combo, sized to the widest preset label.
 * @return  Width in pixels.
 */
float GetSpeedSelectorWidth() {
    float widest = 0.0f;
    for (const double preset : PLAYBACK_SPEED_PRESETS) {
        widest = std::max(widest, ImGui::CalcTextSize(FormatPlaybackSpeed(preset).c_str()).x);
    }
    const ImGuiStyle &style = ImGui::GetStyle();
    return ImGui::GetFrameHeight() + widest + style.FramePadding.x * 2.0f + style.ItemInnerSpacing.x;
}

/**
 * @brief   Draws a compact dropdown for picking a playback or live simulation speed multiplier.
 * @param[in]     id       Unique ImGui widget ID (typically "##...").
 * @param[in,out] speed    Current speed multiplier, updated when the user picks a preset.
 * @param[in]     tooltip  Hover tooltip text, or null for none.
 * @return  True in the frame a new speed preset was picked.
 */
bool DrawSpeedSelector(const char *id, double &speed, const char *tooltip) {
    const std::string preview = FormatPlaybackSpeed(speed);
    ImGui::SetNextItemWidth(GetSpeedSelectorWidth());
    bool has_changed = false;
    if (ImGui::BeginCombo(id, preview.c_str())) {
        for (const double preset : PLAYBACK_SPEED_PRESETS) {
            const std::string label = FormatPlaybackSpeed(preset);
            const bool is_selected = std::abs(speed - preset) < SPEED_MATCH_TOLERANCE;
            if (ImGui::Selectable(label.c_str(), is_selected)) {
                speed = preset;
                has_changed = true;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (tooltip != nullptr) {
        ImGui::SetItemTooltip("%s", tooltip);
    }
    return has_changed;
}

} // namespace GUI
