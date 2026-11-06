/**
 * @file    ac_sweep_tab.h
 * @brief   The AC sweep tab of the Output window: the Bode plots of the measured traces and their response.
 */

#ifndef IMCSIM_AC_SWEEP_TAB_H
#define IMCSIM_AC_SWEEP_TAB_H

#include "document/schematic.h"
#include "plots/plot_export.h"
#include "plots/plot_types.h"
#include "plots/trace_fit_state.h"
#include "simulation/simulation_results.h"
#include "windows/output_tabs/frame_results.h"

namespace GUI {

/**
 * @class   ACSweepTab
 * @brief   Plots the last AC sweep as magnitude and phase on a logarithmic frequency axis, with the response of every
 *          shown trace on the right, as a network analyzer would.
 * @details Both plots share their frequency range and cursors, so zooming one zooms the other. Currents go on the
 *          secondary axis of both, relative to 1 A instead of 1 V. With a pole-zero result, the plots can mark the
 *          natural frequency of every pole and zero.
 */
class ACSweepTab {
public:
    explicit ACSweepTab(Schematic &schematic);

    void Draw(const FrameResults &frame);

    // Export
    bool CanExport(const FrameResults &frame) const;
    ExportFigure BuildFigure() const;

private:
    Schematic &m_Schematic;
    /// What the plots showed last frame, so their axes fit for a new result and when their traces change.
    TraceFitState m_Fit;
    /// Shared by both plots.
    PlotCursors m_Cursors;
    /// The visible frequency range, shared by both plots.
    PlotSpan m_View{.From = 1.0, .To = 1e6};
    /// Whether the plots mark the poles and zeros of the last pole-zero analysis, when there is one.
    bool m_ShowPoleZero = false;

    void DrawPlots(const Core::ACSweep &sweep);
    void DrawRoots(bool has_labels);
    void DrawStatistics(const Core::ACSweep &sweep);
};

} // namespace GUI

#endif // IMCSIM_AC_SWEEP_TAB_H
