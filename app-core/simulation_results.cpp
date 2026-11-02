/**
 * @file    simulation_results.cpp
 * @brief   What a simulation returns: the results of each analysis, and the outcome of a run with its messages.
 */

#include "simulation_results.h"

namespace Core {

/**
 * @brief   Tells whether the combination has output voltages.
 * @return  True when the simulator found an operating point; false when it did not, as for a latch that depends on
 *          its history.
 */
bool TruthTableRow::HasOperatingPoint() const {
    return !OutputVoltages.empty();
}

} // namespace Core
