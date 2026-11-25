/**
 * @file    compiler_parts.h
 * @brief   Translates passive, source, semiconductor, switching, transformer, and amplifier entries into
 *          `Nodalys::CircuitBuilder`.
 */

#ifndef IMCSIM_NODALYS_COMPILER_PARTS_H
#define IMCSIM_NODALYS_COMPILER_PARTS_H

#include "backends/nodalys/compiler_context.h"
#include <expected>
#include <nodalys/nodalys.h>
#include <string>

namespace Core {

[[nodiscard]] std::expected<void, std::string> AddPassivePart(Nodalys::CircuitBuilder &builder,
                                                              const EntryContext &entry);
[[nodiscard]] std::expected<void, std::string> AddSourcePart(Nodalys::CircuitBuilder &builder,
                                                             const EntryContext &entry);
[[nodiscard]] std::expected<void, std::string>
AddControlledSource(Nodalys::CircuitBuilder &builder, const EntryContext &entry, const CompileScope &scope);
[[nodiscard]] std::expected<void, std::string>
AddBehavioralSource(Nodalys::CircuitBuilder &builder, const EntryContext &entry, const CompileScope &scope);
[[nodiscard]] std::expected<void, std::string>
AddSemiconductor(Nodalys::CircuitBuilder &builder, const Circuit &top_circuit, const EntryContext &entry);
[[nodiscard]] std::expected<void, std::string> AddSwitchingPart(Nodalys::CircuitBuilder &builder,
                                                                const EntryContext &entry);
void AddTransformer(Nodalys::CircuitBuilder &builder, const EntryContext &entry);
void AddTransmissionLine(Nodalys::CircuitBuilder &builder, const EntryContext &entry);
void AddOpAmp(Nodalys::CircuitBuilder &builder, const EntryContext &entry);

} // namespace Core

#endif // IMCSIM_NODALYS_COMPILER_PARTS_H
