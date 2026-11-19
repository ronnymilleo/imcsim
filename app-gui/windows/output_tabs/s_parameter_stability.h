/**
 * @file    s_parameter_stability.h
 * @brief   The stability view of the S-parameters tab: K and mu, the gain bound, and the frequency of the stability
 *          circles of a two-port.
 */

#ifndef IMCSIM_S_PARAMETER_STABILITY_H
#define IMCSIM_S_PARAMETER_STABILITY_H

#include "plots/plot_types.h"
#include "plots/stability_view.h"
#include "results/two_port_metrics.h"
#include "simulation/simulation_results.h"
#include <cstddef>
#include <string>

namespace GUI {

/**
 * @class   SParameterStability
 * @brief   What the S-parameters tab works out of a two-port for its stability: the metrics at every frequency, their
 *          curves, the sentence that says where it can oscillate, and the frequency the Smith chart draws the stability
 *          circles at.
 * @details The factor and gain plots and the circle controls of the Smith chart share that frequency, so a click on
 *          either plot moves the circles. A result that is not a two-port leaves everything empty.
 */
class SParameterStability {
public:
    void StartResult(const Core::SParameters &result);
    void Draw(PlotSpan &view, bool is_fitting);
    const std::string &GetSummary() const;

    // Circles
    std::size_t GetCircleFrequency() const;
    void DrawCirclePicker(const Core::SParameters &result);
    void DrawCircleFrequencyLabel() const;

private:
    TwoPortSweep m_Sweep;
    StabilityCurves m_Curves;
    std::string m_Summary;
    /// Index of the frequency of the circles, which each new result sets where mu is lowest.
    std::size_t m_CircleFrequency = 0;

    // Plots
    void DrawFactorPlot(PlotSpan &view, bool is_fitting, float height);
    void DrawGainPlot(PlotSpan &view, bool is_fitting, float height);
    void MarkCircleFrequency();
    void DrawMetricsTooltip() const;
};

} // namespace GUI

#endif // IMCSIM_S_PARAMETER_STABILITY_H
