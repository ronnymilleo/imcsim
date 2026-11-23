/**
 * @file    electromechanical_macromodels.h
 * @brief   Composite electromechanical and passive macromodels (relay, transformer, potentiometer).
 */

#ifndef NODALYS_COMPILER_ELECTROMECHANICAL_MACROMODELS_H
#define NODALYS_COMPILER_ELECTROMECHANICAL_MACROMODELS_H

#include "compiler/circuit_description.h"

namespace Nodalys {

class CircuitCompiler;

// Electromechanical and passive macromodels
void CompilePotentiometer(CircuitCompiler &compiler, const PotentiometerSpec &spec);
void CompileRelay(CircuitCompiler &compiler, const RelaySpec &spec);
void CompileTransformer(CircuitCompiler &compiler, const TransformerSpec &spec);

} // namespace Nodalys

#endif // NODALYS_COMPILER_ELECTROMECHANICAL_MACROMODELS_H
