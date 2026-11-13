/**
 * @file    check_transient.cpp
 * @brief   Checks the settings of a transient before it runs.
 */

#include "simulation/check_transient.h"

#include "circuit/circuit.h"
#include "circuit/components/source.h"
#include "simulation/analysis_settings.h"
#include <format>
#include <optional>
#include <string>

namespace Core {

/**
 * @brief   Checks the time span and step of a transient.
 * @param[in] settings  Stop time and time step.
 * @return  The reason they cannot run, such as a step longer than the stop time or too many points; no value when they
 *          can.
 */
std::optional<std::string> CheckTransientSettings(const TransientSettings &settings) {
    if (settings.StopTime <= 0.0) {
        return "The stop time must be positive";
    }
    if (settings.TimeStep <= 0.0) {
        return "The time step must be positive";
    }
    if (settings.TimeStep > settings.StopTime) {
        return "The time step must not be longer than the stop time";
    }
    if (settings.StopTime / settings.TimeStep > MAX_TRANSIENT_POINTS) {
        return std::format("The time step is too small for the stop time: it would produce more than {} points",
                           MAX_TRANSIENT_POINTS);
    }
    return std::nullopt;
}

/**
 * @brief   Finds the shortest interval at which a noise or random source of the circuit changes.
 * @param[in] circuit  Circuit to look through, blocks included.
 * @return  The shortest time step of its noise and random sources; no value when none has one.
 */
std::optional<double> FindFinestSignalStep(const Circuit &circuit) {
    std::optional<double> finest;
    for (const Component *part : CollectParts(circuit)) {
        if (!IsSource(part->GetType())) {
            continue;
        }
        const std::optional<double> step = static_cast<const Source &>(*part).GetSignalStep();
        if (step && (!finest || *step < *finest)) {
            finest = step;
        }
    }
    return finest;
}

/**
 * @brief   Checks that the noise and random sources of a circuit keep a transient within the points it may span.
 * @param[in] circuit   Circuit to simulate.
 * @param[in] settings  Stop time and time step.
 * @return  The reason it would span too many points, since ngspice stops at every sample of a noise or random source
 *          and not only at the time step; no value when it does not.
 */
std::optional<std::string> CheckSourceSteps(const Circuit &circuit, const TransientSettings &settings) {
    const std::optional<double> finest = FindFinestSignalStep(circuit);
    if (finest && *finest > 0.0 && settings.StopTime / *finest > MAX_TRANSIENT_POINTS) {
        return std::format("The time step of a noise or random source is too small for the stop time: it would "
                           "produce more than {} points",
                           MAX_TRANSIENT_POINTS);
    }
    return std::nullopt;
}

} // namespace Core
