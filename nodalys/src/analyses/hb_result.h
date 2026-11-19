/**
 * @file    hb_result.h
 * @brief   Turns the harmonics a balance found into spectra, waveforms and the currents of the circuit.
 */

#ifndef NODALYS_HB_RESULT_H
#define NODALYS_HB_RESULT_H

#include "analyses/hb_solve.h"
#include "analyses/hb_workspace.h"
#include "compiler/compiled_circuit.h"
#include "nodalys/analyses.h"
#include "nodalys/results.h"
#include <string>
#include <vector>

namespace Nodalys {

// Result assembly
HarmonicBalanceResult BuildHarmonicBalanceResult(HbWorkspace &ws,
                                                 const CompiledCircuit &compiled,
                                                 const HarmonicBalanceConfig &config,
                                                 const std::vector<double> &x_hb,
                                                 const HbSolveOutcome &outcome,
                                                 std::vector<std::string> warnings);

} // namespace Nodalys

#endif // NODALYS_HB_RESULT_H
