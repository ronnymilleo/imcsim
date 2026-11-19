/**
 * @file    live_run_plan.h
 * @brief   How a live transient runs: the pace that keeps it to the clock, the stop time that caps it, and the warning
 *          it leaves when it reaches that cap.
 */

#ifndef IMCSIM_LIVE_RUN_PLAN_H
#define IMCSIM_LIVE_RUN_PLAN_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/simulation_results.h"

namespace GUI {

/**
 * @struct  LiveRunPlan
 * @brief   The stop time a transient is given and the pace it keeps against the clock.
 */
struct LiveRunPlan {
    double StopTime = 0.0;
    /// Simulated seconds per second of the clock, or 0 for a run that is not live and goes as fast as it can.
    double Pace = 0.0;
};

// Planning
LiveRunPlan PlanLiveRun(const Core::TransientSettings &settings, const Core::Circuit &circuit, double live_seconds);

// Limit
void AddLiveLimitWarning(Core::TransientRun &run);

} // namespace GUI

#endif // IMCSIM_LIVE_RUN_PLAN_H
