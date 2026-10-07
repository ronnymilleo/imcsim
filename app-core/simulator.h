/**
 * @file    simulator.h
 * @brief   Runs circuits through the ngspice shared library and returns their results.
 */

#ifndef IMCSIM_SIMULATOR_H
#define IMCSIM_SIMULATOR_H

#include "circuit.h"
#include <expected>
#include <string>
#include <vector>

namespace Core {

/**
 * @struct  SimulatorMessage
 * @brief   One line printed by ngspice while it ran.
 * @details ngspice prints warnings and errors on its error stream, such as "singular matrix" for a node with
 *          no DC path to ground, which often explains odd results even when the run succeeds.
 */
struct SimulatorMessage {
    std::string Text;
    bool FromErrorStream = false;
};

/**
 * @struct  OperatingPoint
 * @brief   DC operating point of a circuit: the voltage of every node.
 */
struct OperatingPoint {
    // Indexed by node number; node 0 is ground and always 0 V
    std::vector<double> NodeVoltages;
};

/**
 * @struct  OperatingPointRun
 * @brief   Outcome of an operating point simulation, with everything ngspice printed.
 */
struct OperatingPointRun {
    std::expected<OperatingPoint, std::string> Result;
    std::vector<SimulatorMessage> Messages;
};

OperatingPointRun RunOperatingPoint(const Circuit &circuit);

} // namespace Core

#endif // IMCSIM_SIMULATOR_H
