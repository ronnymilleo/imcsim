/**
 * @file    noise_tab.h
 * @brief   The Noise tab of the Output window: the noise densities at the output and at the input, and the share of
 *          each part.
 */

#ifndef IMCSIM_NOISE_TAB_H
#define IMCSIM_NOISE_TAB_H

#include "plots/plot_export.h"
#include "plots/plot_types.h"
#include "plots/trace_fit_state.h"
#include "schematic/schematic.h"
#include "simulation/simulation_results.h"
#include <set>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   NoiseTab
 * @brief   Plots the noise density at the output and referred to the input on log-log axes, with the RMS totals and the
 *          share of each part on the right; checking a part plots its share.
 */
class NoiseTab {
public:
    explicit NoiseTab(Schematic &schematic);

    void Draw();

    // Export
    bool CanExport() const;
    ExportFigure BuildFigure() const;

private:
    Schematic &m_Schematic;
    /// The noise result the axes of its plot were fitted to.
    TraceFitState m_Fit;
    /// Visible range of the noise plot.
    PlotSpan m_View;
    /// The parts whose share the noise plot shows.
    std::set<std::string> m_ShownContributions;
    /// The shares the plot drew last frame, so its axes fit when they change.
    std::vector<std::string> m_PlottedContributions;

    void DrawPlot(const Core::Noise &noise);
    void SetupAxes(const Core::Noise &noise, bool input_shown, const std::string &output_unit,
                   const std::string &input_unit);
    void DrawStatistics(const Core::Noise &noise);
};

} // namespace GUI

#endif // IMCSIM_NOISE_TAB_H
