/**
 * @file    stability_view.h
 * @brief   What the Output window shows of the stability of a two-port: the curves of K, mu and the gain bound, and the
 *          figures of the plots for the export, with the stability circles on the Smith chart.
 * @details Pure functions with no ImGui, so the tests run them; the drawing is in the S-parameters tab.
 */

#ifndef IMCSIM_STABILITY_VIEW_H
#define IMCSIM_STABILITY_VIEW_H

#include "plots/plot_export.h"
#include "plots/s_parameter_view.h"
#include "results/stability_circles.h"
#include "results/two_port_metrics.h"
#include "simulation/simulation_results.h"
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/// The highest value the plot of K and mu reaches at first; a K of 40 at low frequency would flatten the rest.
constexpr double FACTOR_AXIS_CEILING = 3.0;

/// The least the plot of K and mu shows above 1, so a factor of 1.05 still has room around the line at 1.
constexpr double FACTOR_AXIS_MINIMUM_CEILING = 1.5;

/// The colors of the curves of the Stability view and of the two circles, so the window and the export agree.
constexpr ExportColor STABILITY_K_COLOR{86, 156, 214};
constexpr ExportColor STABILITY_MU_COLOR{200, 120, 200};
constexpr ExportColor STABILITY_LINE_AT_ONE_COLOR{150, 150, 150};
constexpr ExportColor STABILITY_MAG_COLOR{120, 190, 100};
constexpr ExportColor STABILITY_MSG_COLOR{230, 126, 80};
constexpr ExportColor SOURCE_CIRCLE_COLOR{38, 166, 154};
constexpr ExportColor LOAD_CIRCLE_COLOR{216, 67, 150};

/**
 * @struct  StabilityCurves
 * @brief   The curves of the Stability view, one value per frequency, not a number where there is nothing to draw.
 * @details K is infinite where S12 is 0, and nothing is drawn there. The gain is one curve in two kinds: MAG where the
 *          two-port is unconditionally stable and MSG where it is not, which meet at K = 1. The MSG curve also holds
 *          the stable point next to each unstable one, so the two kinds join instead of leaving a gap between samples.
 */
struct StabilityCurves {
    std::vector<double> Frequencies;
    std::vector<double> K;
    std::vector<double> Mu;
    /// MAG, or the unilateral gain where S12 is 0, in decibels.
    std::vector<double> MaximumAvailableGainDecibels;
    /// MSG in decibels.
    std::vector<double> MaximumStableGainDecibels;
    std::string MaximumAvailableGainLabel;
    std::string MaximumStableGainLabel;
    /// The range of the Y axis of the factors.
    ExportRange FactorRange;
};

// Curves
StabilityCurves BuildStabilityCurves(const TwoPortSweep &sweep);

// Figures
ExportFigure BuildStabilityFigure(const TwoPortSweep &sweep, const std::optional<ExportRange> &view);
ExportFigure BuildStabilityTableFigure(const TwoPortSweep &sweep);
ExportFigure BuildSmithFigureWithCircles(const Core::SParameters &result, const std::vector<SParameterEntry> &entries,
                                         std::size_t frequency);

} // namespace GUI

#endif // IMCSIM_STABILITY_VIEW_H
