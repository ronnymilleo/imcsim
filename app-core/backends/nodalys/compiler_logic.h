/**
 * @file    compiler_logic.h
 * @brief   Compiles logic gates and flip-flop macromodels from `Core::Circuit` entries into `Nodalys::CircuitBuilder`.
 */

#ifndef IMCSIM_NODALYS_COMPILER_LOGIC_H
#define IMCSIM_NODALYS_COMPILER_LOGIC_H

#include "backends/nodalys/compiler_context.h"
#include <nodalys/nodalys.h>

namespace Core {

void AddGatePart(Nodalys::CircuitBuilder &builder, const EntryContext &entry);
void AddFlipFlopPart(Nodalys::CircuitBuilder &builder, const EntryContext &entry);

} // namespace Core

#endif // IMCSIM_NODALYS_COMPILER_LOGIC_H
