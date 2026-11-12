/**
 * @file    ac_sweep_tab.h
 * @brief   The AC sweep tab of the Output window: the Bode plots of the measured traces and their response.
 */

#ifndef IMCSIM_AC_SWEEP_TAB_H
#define IMCSIM_AC_SWEEP_TAB_H

#include "plots/plot_export.h"
#include "plots/plot_types.h"
#include "plots/trace_fit_state.h"
#include "results/result_store.h"
#include "schematic/schematic.h"
#include "simulation/simulation_results.h"
#include "windows/output_tabs/frame_results.h"
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   ACSweepTab
 * @brief   Plots the last AC sweep as magnitude and phase on a logarithmic frequency axis, with the response of every
 *          shown trace on the right, as a network analyzer would.
 * @details Both plots share their frequency range and cursors, so zooming one zooms the other. Currents go on the
 *          secondary axis of both, relative to 1 A instead of 1 V, and math channels of any other unit on a third. With
 * a pole-zero result, the plots can mark the natural frequency of every pole and zero.
 */
class ACSweepTab {
public:
    ACSweepTab(Schematic &schematic, ResultStore &results);

    void Draw(const FrameResults &frame);

    // Export
    bool CanExport(const FrameResults &frame) const;
    ExportFigure BuildFigure(const FrameResults &frame) const;

private:
    /**
     * @struct  BodePlot
     * @brief   What sets one of the two plots apart: its axes, the values of the sweep it reads and its math traces.
     */
    struct BodePlot {
        const char *Id;
        const char *VoltageAxis;
        const char *CurrentAxis;
        std::string ThirdAxis;
        /// Unit of the cursor readout, such as " dB".
        const char *Unit;
        std::vector<std::vector<double>> Core::ACSweep::*Nodes;
        std::vector<Core::ComponentTrace> Core::ACSweep::*Currents;
        const std::vector<MathTrace> *Math;
        /// The magnitude plot labels the steps and the poles and zeros; the phase plot only marks them.
        bool HasLabels;
    };

    /**
     * @struct  PlotFits
     * @brief   What both plots share in a frame: which axes fit and which traces they show.
     */
    struct PlotFits {
        bool IsNewResult;
        bool HaveVoltagesChanged;
        bool HaveCurrentsChanged;
        bool HasMathChanged;
        bool ShowsCurrents;
        bool HasCurrentAxis;
        ImPlotAxisFlags VoltageAxisFlags;
        float Height;
    };

    Schematic &m_Schematic;
    ResultStore &m_Results;
    /// What the plots showed last frame, so their axes fit for a new result and when their traces change.
    TraceFitState m_Fit;
    /// Shared by both plots.
    PlotCursors m_Cursors;
    /// The visible frequency range, shared by both plots.
    PlotSpan m_View{.From = 1.0, .To = 1e6};
    /// Whether the plots mark the poles and zeros of the last pole-zero analysis, when there is one.
    bool m_ShowPoleZero = false;

    void DrawPlots(const Core::ACSweep &sweep, const ACMathTraces &math);
    void DrawPlot(const Core::ACSweep &sweep, const BodePlot &plot, const PlotFits &fits);
    void DrawRoots(bool has_labels);
    void DrawStatistics(const Core::ACSweep &sweep, const ACMathTraces &math);
};

} // namespace GUI

#endif // IMCSIM_AC_SWEEP_TAB_H
