/**
 * @file    compiler.h
 * @brief   Single-pass circuit compiler: translates a `Core::Circuit` into an autonomous `nodalys::Circuit`.
 */

#ifndef IMCSIM_NATIVE_COMPILER_H
#define IMCSIM_NATIVE_COMPILER_H

#include "model/circuit.h"
#include <expected>
#include <nodalys/nodalys.h>
#include <string>

namespace Core::Native {

/**
 * @brief   Compiles a `Core::Circuit` for the Nodalys solver using `nodalys::CircuitBuilder`.
 * @param[in] circuit  Circuit to compile.
 * @return  The compiled Nodalys circuit, or a descriptive error if a component or expression is unsupported.
 */
std::expected<nodalys::Circuit, std::string> CompileCircuit(const Circuit &circuit);

} // namespace Core::Native

#endif // IMCSIM_NATIVE_COMPILER_H
