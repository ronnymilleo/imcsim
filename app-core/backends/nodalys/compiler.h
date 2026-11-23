/**
 * @file    compiler.h
 * @brief   Single-pass circuit compiler: translates a `Core::Circuit` into an autonomous `Nodalys::Circuit`.
 */

#ifndef IMCSIM_NODALYS_COMPILER_H
#define IMCSIM_NODALYS_COMPILER_H

#include "circuit/circuit.h"
#include <expected>
#include <nodalys/nodalys.h>
#include <optional>
#include <string>
#include <vector>

namespace Core {

class SubcircuitInstance;

// Compiling
[[nodiscard]] std::expected<Nodalys::Circuit, std::string> CompileCircuit(const Circuit &circuit,
                                                                          std::vector<std::string> *warnings = nullptr);
[[nodiscard]] std::optional<std::string> FindSpiceBlockProblem(const Circuit &circuit,
                                                               const SubcircuitInstance &instance);

} // namespace Core

#endif // IMCSIM_NODALYS_COMPILER_H
