/**
 * @file    trace_swatch.cpp
 * @brief   A row of a trace list: its checkbox and a short line in the color and style the trace is plotted with.
 */

#include "base/trace_swatch.h"

#include <algorithm>

namespace GUI {

namespace {

// Swatch sizes, in font sizes
constexpr float SWATCH_LENGTH = 1.3f;
constexpr float DASH_LENGTH = 0.35f;
constexpr float DASH_GAP = 0.2f;

// Line weights, in pixels, as the plots draw them
constexpr float LINE_WEIGHT = 2.0f;
constexpr float HEAVY_WEIGHT = 3.5f;

/// A trace that is not shown keeps its swatch, faded, so the list tells what it would look like.
constexpr float HIDDEN_ALPHA = 0.45f;

/**
 * @brief   Draws the swatch line across a box of the current line, dashed for a current.
 */
void DrawSwatch(const ImVec2 corner, const ImVec2 size, const ImU32 color, const TraceStyle style) {
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const float y = corner.y + size.y / 2.0f;
    const float weight = style == TraceStyle::HEAVY ? HEAVY_WEIGHT : LINE_WEIGHT;
    if (style != TraceStyle::DASHED) {
        draw_list->AddLine({corner.x, y}, {corner.x + size.x, y}, color, weight);
        return;
    }
    const float font_size = ImGui::GetFontSize();
    const float dash = font_size * DASH_LENGTH;
    const float gap = font_size * DASH_GAP;
    const int dash_count = static_cast<int>(size.x / (dash + gap)) + 1;
    for (int index = 0; index < dash_count; ++index) {
        const float start = static_cast<float>(index) * (dash + gap);
        const float end = std::min(start + dash, size.x);
        draw_list->AddLine({corner.x + start, y}, {corner.x + end, y}, color, weight);
    }
}

} // namespace

/**
 * @brief   Draws a checkbox for a trace with a swatch of its color and line style between the box and its name, so the
 *          list doubles as the legend of the plots; clicking the name toggles it too.
 * @param[in]     label     Name of the trace, also its ImGui ID within the current scope.
 * @param[in,out] is_shown  Whether the trace is plotted.
 * @param[in]     color     Color of the trace.
 * @param[in]     style     How the plots draw it.
 * @return  True when the user toggled it this frame.
 */
bool DrawTraceCheckbox(const char *label, bool *is_shown, const ImU32 color, const TraceStyle style) {
    ImGui::PushID(label);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, color);
    bool has_changed = ImGui::Checkbox("##shown", is_shown);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    const ImVec2 size = {ImGui::GetFontSize() * SWATCH_LENGTH, ImGui::GetFrameHeight()};
    ImGui::Dummy(size);
    const ImU32 swatch_color = *is_shown ? color : ImGui::GetColorU32(color, HIDDEN_ALPHA);
    DrawSwatch(corner, size, swatch_color, style);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (ImGui::IsItemClicked()) {
        *is_shown = !*is_shown;
        has_changed = true;
    }
    ImGui::PopID();
    return has_changed;
}

} // namespace GUI
