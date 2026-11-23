/**
 * @file    opamp_macromodels.h
 * @brief   Composite op-amp macromodels (ideal dominant-pole and Boyle) lowered via CircuitCompiler primitives.
 */

#ifndef NODALYS_COMPILER_OPAMP_MACROMODELS_H
#define NODALYS_COMPILER_OPAMP_MACROMODELS_H

#include "compiler/circuit_description.h"

namespace Nodalys {

class CircuitCompiler;

// Op-amp macromodel lowering
void CompileBoyleOpAmp(CircuitCompiler &compiler, const BoyleOpAmpSpec &spec);
void CompileIdealOpAmp(CircuitCompiler &compiler, const IdealOpAmpSpec &spec);

} // namespace Nodalys

#endif // NODALYS_COMPILER_OPAMP_MACROMODELS_H
