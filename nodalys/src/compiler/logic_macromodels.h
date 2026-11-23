/**
 * @file    logic_macromodels.h
 * @brief   Composite smooth-logic macromodels (SR latch and 7474 edge-triggered D flip-flop).
 */

#ifndef NODALYS_COMPILER_LOGIC_MACROMODELS_H
#define NODALYS_COMPILER_LOGIC_MACROMODELS_H

#include "compiler/circuit_description.h"
#include "nodalys/types.h"
#include <string>

namespace Nodalys {

class CircuitCompiler;

// Logic macromodels
void AppendSRLatch(CircuitDescription &description,
                   const std::string &name,
                   NodeId q,
                   NodeId q_bar,
                   NodeId set,
                   NodeId reset,
                   const SmoothLogicParameters &params);
void CompileDFlipFlop(CircuitCompiler &compiler, const DFlipFlopSpec &spec);

} // namespace Nodalys

#endif // NODALYS_COMPILER_LOGIC_MACROMODELS_H
