/**
 * @file    live_run_plan.cpp
 * @brief   How a live transient runs: the pace that keeps it to the clock, the stop time that caps it, and the warning
 *          it leaves when it reaches that cap.
 */

#include "analysis/live_run_plan.h"

#include "schematic/simulation_settings.h"
#include "simulation/check_transient.h"
#include <algorithm>

namespace GUI {

/**
 * @brief   Plans a live transient, which covers the stop time of the settings in the live seconds and goes on until it
 *          is stopped or reaches its limit.
 * @param[in] settings      Stop time and time step the user set.
 * @param[in] circuit       Circuit to simulate, whose noise and random sources may step finer than the time step.
 * @param[in] live_seconds  Wall seconds the run takes to cover the stop time of the settings.
 * @return  The pace, and the stop time the run is given: LIVE_RUN_LIMIT_SECONDS on the clock, or one step short of
 *          the most a transient may span, which the check would refuse after rounding, whichever comes first.
 */
LiveRunPlan
PlanLiveRun(const Core::TransientSettings &settings, const Core::Circuit &circuit, const double live_seconds) {
    const double pace = settings.StopTime / live_seconds;
    // A noise or random source steps the transient on its own, so the finest step counts for the points
    const double finest_step =
        std::min(settings.TimeStep, Core::FindFinestSignalStep(circuit).value_or(settings.TimeStep));
    return {.StopTime = std::min(pace * LIVE_RUN_LIMIT_SECONDS,
                                 finest_step * static_cast<double>(Core::MAX_TRANSIENT_POINTS - 1)),
            .Pace = pace};
}

/**
 * @brief   Warns that a live run reached its limit, since a live run only completes when it does.
 * @param[in,out] run  The live run that ended; it gains the warning when its result is complete.
 */
void AddLiveLimitWarning(Core::TransientRun &run) {
    if (run.Result && run.Result->IsComplete) {
        run.Result->Warnings.emplace_back("The live run reached its limit, an hour on the clock or a million time "
                                          "steps, whichever comes first; Run starts a new one");
    }
}

} // namespace GUI
