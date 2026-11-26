/**
 * @file    igbt_macromodel.h
 * @brief   The IGBT as its conduction device, the capacitances of its datasheet and its optional co-packaged diode.
 */

#ifndef NODALYS_COMPILER_IGBT_MACROMODEL_H
#define NODALYS_COMPILER_IGBT_MACROMODEL_H

#include "compiler/circuit_description.h"

#include <optional>
#include <string>

namespace Nodalys {

class CircuitCompiler;

// IGBT
std::optional<std::string> CheckIgbt(const IgbtSpec &spec);
void CompileIgbt(CircuitCompiler &compiler, const IgbtSpec &spec);

} // namespace Nodalys

#endif // NODALYS_COMPILER_IGBT_MACROMODEL_H
