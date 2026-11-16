/**
 * @file    initial_voltage_targets.h
 * @brief   Finds the node each initial voltage marker sets in the circuit as every instance of a block expands it.
 */

#ifndef IMCSIM_INITIAL_VOLTAGE_TARGETS_H
#define IMCSIM_INITIAL_VOLTAGE_TARGETS_H

#include "circuit/circuit.h"
#include <string>
#include <vector>

namespace Core {

/**
 * @struct  InitialVoltageTarget
 * @brief   One initial voltage as one instance of the circuit expands it: the node it sets and where the marker sits.
 * @details A marker inside a block gives one target per instance, nested instances included. A marker on a pin of the
 *          block sets the outer node that instance connects the pin to, so its Path is that of the outer circuit.
 */
struct InitialVoltageTarget {
    /// Names of the instances from the top circuit down to the block whose numbering Node uses; empty at the top.
    std::vector<std::string> Path;
    int Node = 0;
    double Volts = 0.0;
    /// Names of the instances from the top circuit down to the block that holds the marker; empty at the top.
    std::vector<std::string> MarkerPath;
    /// The circuit whose numbering Node uses: the top circuit or the body of a definition. Not owned.
    const Circuit *Owner = nullptr;
};

// Expanding
std::vector<InitialVoltageTarget> ResolveInitialVoltages(const Circuit &circuit);

// Naming
std::string FormatInstancePath(const std::vector<std::string> &path);

} // namespace Core

#endif // IMCSIM_INITIAL_VOLTAGE_TARGETS_H
