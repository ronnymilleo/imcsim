/**
 * @file    compiler.h
 * @brief   Single-pass circuit compiler: translates a `Core::Circuit` into an autonomous `Nodalys::Circuit`.
 */

#ifndef IMCSIM_NODALYS_COMPILER_H
#define IMCSIM_NODALYS_COMPILER_H

#include "circuit/circuit.h"
#include <expected>
#include <nodalys/nodalys.h>
#include <string>

namespace Core {

/**
 * @brief   Compiles a `Core::Circuit` for the Nodalys solver using `Nodalys::CircuitBuilder`.
 * @param[in] circuit  Circuit to compile.
 * @return  The compiled Nodalys circuit, or a descriptive error if a component or expression is unsupported.
 */
std::expected<Nodalys::Circuit, std::string> CompileCircuit(const Circuit &circuit);

} // namespace Core

#endif // IMCSIM_NODALYS_COMPILER_H
