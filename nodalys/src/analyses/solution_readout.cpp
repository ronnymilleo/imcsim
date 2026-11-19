/**
 * @file    solution_readout.cpp
 * @brief   Reads the quantities that the results hold out of the solution vector of an analysis.
 */

#include "analyses/solution_readout.h"

#include "devices/devices.h"

namespace Nodalys {

/**
 * @brief   Reads the current of every component that the circuit reports, at a solution.
 * @param[in] compiled  Compiled circuit with the terminals of each current.
 * @param[in] x         Solution vector of the DC or transient solve.
 * @return  The currents in amperes, in the order of the circuit's currents; each is the sum of the terminal currents
 *          that make it up.
 */
std::vector<double> ReadCurrentValues(const CompiledCircuit &compiled, const std::vector<double> &x) {
    std::vector<double> currents;
    currents.reserve(compiled.Currents.size());
    for (const CurrentRef &ref : compiled.Currents) {
        double value = 0.0;
        for (const CurrentContribution &contribution : ref.Contributions) {
            value += contribution.Element->TerminalCurrent(contribution.Terminal, x);
        }
        currents.push_back(value);
    }
    return currents;
}

} // namespace Nodalys
