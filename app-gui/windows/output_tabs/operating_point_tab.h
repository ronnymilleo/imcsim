/**
 * @file    operating_point_tab.h
 * @brief   The Operating point tab of the Output window: the DC values of the last operating point, and the measured
 *          traces against the step value of a stepped one.
 */

#ifndef IMCSIM_OPERATING_POINT_TAB_H
#define IMCSIM_OPERATING_POINT_TAB_H

#include "plots/trace_fit_state.h"
#include "schematic/schematic.h"
#include "simulation/simulation_results.h"
#include <cstddef>
#include <optional>

namespace GUI {

/**
 * @class   OperatingPointTab
 * @brief   Lists the node voltages and the currents of the operating point the schematic shows, and for a stepped run
 *          picks the step and plots every measured trace against the step value, so V(OUT) against R1 reads at once.
 * @details A Monte Carlo run picks its run here too, while its spread shows in the Monte Carlo tab, since a trace
 *          against the run number tells nothing.
 */
class OperatingPointTab {
public:
    explicit OperatingPointTab(Schematic &schematic);

    void Update();
    void Draw();
    const Core::DCSweep *GetStepSweep() const;

private:
    Schematic &m_Schematic;
    /// The steps laid out as a sweep of the parameter, for a run stepped through a parameter.
    std::optional<Core::DCSweep> m_StepSweep;
    /// The result version m_StepSweep was laid out from.
    std::optional<std::size_t> m_SweptVersion;
    /// Changes whenever the values of m_StepSweep change, which picking another step does not do.
    std::size_t m_StepSweepVersion = 0;
    TraceFitState m_Fit;

    void DrawStepPlot(const Core::DCSweep &sweep);
};

} // namespace GUI

#endif // IMCSIM_OPERATING_POINT_TAB_H
