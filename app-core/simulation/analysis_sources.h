/**
 * @file    analysis_sources.h
 * @brief   The parts of a circuit an analysis can take as its source or input, found by name or listed.
 */

#ifndef IMCSIM_ANALYSIS_SOURCES_H
#define IMCSIM_ANALYSIS_SOURCES_H

#include "circuit/circuit.h"
#include <string>
#include <string_view>
#include <vector>

namespace Core {

/// The parts a DC sweep or a transfer function can drive, as messages name them.
constexpr std::string_view SWEEPABLE_SOURCES = "a voltage or current source, VCC, clock or logic input";

// Parts by name
const Component *FindSweepableSource(const Circuit &circuit, std::string_view name);
const Component *FindNoiseInput(const Circuit &circuit, std::string_view name);
const Component *FindTransferFunctionInput(const Circuit &circuit, std::string_view name);
const Component *FindLogicInput(const Circuit &circuit, std::string_view name);
const char *SourceUnit(const Component &source);

// The parts an analysis offers as its sources
std::vector<std::string> GetSweepableSources(const Circuit &circuit);
std::vector<std::string> GetNoiseInputSources(const Circuit &circuit);
std::vector<std::string> GetTransferFunctionInputSources(const Circuit &circuit);
std::vector<std::string> GetTruthTableInputs(const Circuit &circuit);

} // namespace Core

#endif // IMCSIM_ANALYSIS_SOURCES_H
