/**
 * @file    solution_readout.h
 * @brief   Reads the quantities that the results hold out of the solution vector of an analysis.
 */

#ifndef NODALYS_SOLUTION_READOUT_H
#define NODALYS_SOLUTION_READOUT_H

#include "compiler/compiled_circuit.h"
#include <vector>

namespace Nodalys {

// Currents
std::vector<double> ReadCurrentValues(const CompiledCircuit &compiled, const std::vector<double> &x);

} // namespace Nodalys

#endif // NODALYS_SOLUTION_READOUT_H
