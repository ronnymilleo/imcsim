/**
 * @file    s_parameter_widgets.cpp
 * @brief   What the views of the S-parameters tab share: the colors of the figures as ImGui colors, the weight of
 *          their curves and the notes under the controls.
 */

#include "windows/output_tabs/s_parameter_widgets.h"

#include <format>

namespace GUI {

namespace {

/// The channels of an export color run from 0 to this.
constexpr float COLOR_CHANNEL_MAX = 255.0f;

} // namespace

/**
 * @brief   The color of a figure, which the export shares, as ImGui draws it.
 * @param[in] color  The color, opaque.
 * @return  The same color, opaque.
 */
ImVec4 ToImVec4(const ExportColor color) {
    return {static_cast<float>(color.Red) / COLOR_CHANNEL_MAX,
            static_cast<float>(color.Green) / COLOR_CHANNEL_MAX,
            static_cast<float>(color.Blue) / COLOR_CHANNEL_MAX,
            1.0f};
}

/**
 * @brief   The color of a figure as ImGui draws it, see-through.
 * @param[in] color  The color, opaque.
 * @param[in] alpha  Its opacity, from 0 to 1.
 * @return  The color with that opacity.
 */
ImVec4 ToImVec4(const ExportColor color, const float alpha) {
    ImVec4 converted = ToImVec4(color);
    converted.w = alpha;
    return converted;
}

/**
 * @brief   Why a view of a result that does not have two ports is off.
 * @param[in] result  The result shown.
 * @param[in] what    What needs two ports, such as "Stability circles".
 * @return  The sentence, with the number of ports of the result.
 */
std::string DescribeNeedsTwoPorts(const Core::SParameters &result, const char *what) {
    return std::format("{} need exactly two ports; this result has {}", what, result.GetPortCount());
}

/**
 * @brief   Writes a note that wraps at the edge of the window.
 * @param[in] text  The note.
 */
void ShowWrapped(const std::string &text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
}

} // namespace GUI
