/**
 * @file    dc_sweep_tab.h
 * @brief   The DC sweep tab of the Output window: the measured traces against the swept source, and their statistics.
 */

#ifndef IMCSIM_DC_SWEEP_TAB_H
#define IMCSIM_DC_SWEEP_TAB_H

#include "document/schematic.h"
#include "plots/math_channels.h"
#include "plots/plot_export.h"
#include "plots/plot_types.h"
#include "plots/trace_fit_state.h"
#include "simulation/simulation_results.h"
#include "windows/output_tabs/frame_results.h"
#include <vector>

namespace GUI {

/**
 * @class   DCSweepTab
 * @brief   Plots the last DC sweep against its swept source, one curve per step of a stepped source, with two cursors
 *          and the value, change and slope of every shown trace on the right, as a curve tracer would.
 */
class DCSweepTab {
public:
    explicit DCSweepTab(Schematic &schematic);

    void Draw(const FrameResults &frame);

    // Export
    bool CanExport(const FrameResults &frame) const;
    ExportFigure BuildFigure(const FrameResults &frame) const;

private:
    Schematic &m_Schematic;
    /// What the plot showed last frame, so its axes fit for a new result and when its traces change.
    TraceFitState m_Fit;
    PlotCursors m_Cursors;
    /// The visible range of the swept source, which the statistics measure.
    PlotSpan m_View;

    void DrawPlot(const Core::DCSweep &sweep, const std::vector<std::vector<MathTrace>> &math);
    void DrawStatistics(const Core::DCSweep &sweep, const std::vector<std::vector<MathTrace>> &math);
};

} // namespace GUI

#endif // IMCSIM_DC_SWEEP_TAB_H
