/**
 * @file    s_parameter_smith.h
 * @brief   The Smith chart of the S-parameters tab: the grid, a curve per checked entry, the stability circles of a
 *          two-port and the reading of the point under the pointer.
 */

#ifndef IMCSIM_S_PARAMETER_SMITH_H
#define IMCSIM_S_PARAMETER_SMITH_H

#include "imgui.h"
#include "plots/s_parameter_view.h"
#include "simulation/simulation_results.h"
#include "windows/output_tabs/s_parameter_stability.h"
#include <vector>

namespace GUI {

/**
 * @class   SParameterSmithChart
 * @brief   Draws the checked entries of S on a Smith chart, on equal axes that fit the whole chart whatever the shape
 * of the window, with the source and load stability circles of a two-port when the user asks for them.
 */
class SParameterSmithChart {
public:
    void DrawCircleControls(const Core::SParameters &result, SParameterStability &stability);
    void Draw(const Core::SParameters &result,
              const std::vector<SParameterEntry> &entries,
              const SParameterStability &stability,
              bool &is_fitting);
    bool AreCirclesShown() const;

private:
    /// Size of the room for the chart when its axes were last fitted, so a resized window fits them again.
    float m_FittedWidth = 0.0f;
    float m_FittedHeight = 0.0f;
    bool m_AreCirclesShown = false; ///< Whether the chart shows the stability circles of a two-port.

    bool TakeResized(ImVec2 room);
    void PlotItems(const Core::SParameters &result,
                   const std::vector<SParameterEntry> &entries,
                   const SParameterStability &stability) const;
};

} // namespace GUI

#endif // IMCSIM_S_PARAMETER_SMITH_H
