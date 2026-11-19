/**
 * @file    s_parameter_widgets.h
 * @brief   What the views of the S-parameters tab share: the colors of the figures as ImGui colors, the weight of
 *          their curves and the notes under the controls.
 */

#ifndef IMCSIM_S_PARAMETER_WIDGETS_H
#define IMCSIM_S_PARAMETER_WIDGETS_H

#include "base/svg_writer.h"
#include "imgui.h"
#include "simulation/simulation_results.h"
#include <string>

namespace GUI {

/// The curves of the S-parameters tab are heavier than the grid and the marker lines.
inline constexpr float CURVE_WEIGHT = 2.0f;

// Colors
ImVec4 ToImVec4(ExportColor color);
ImVec4 ToImVec4(ExportColor color, float alpha);

// Notes
std::string DescribeNeedsTwoPorts(const Core::SParameters &result, const char *what);
void ShowWrapped(const std::string &text);

} // namespace GUI

#endif // IMCSIM_S_PARAMETER_WIDGETS_H
