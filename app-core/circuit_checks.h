/**
 * @file    circuit_checks.h
 * @brief   Checks of a circuit and of the settings of an analysis, and the lists of parts an analysis can use.
 * @details Independent of the simulation backend: whatever runs the circuit, these say whether it can be asked to.
 */

#ifndef IMCSIM_CIRCUIT_CHECKS_H
#define IMCSIM_CIRCUIT_CHECKS_H

#include "analysis_settings.h"
#include "circuit.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

// Most time steps a transient may span, which keeps a mistyped setting from running for minutes or exhausting memory
constexpr int MAX_TRANSIENT_POINTS = 1000000;

// What is wrong with a circuit or with settings, in words for the user, or no value when nothing is
std::optional<std::string> CheckCircuit(const Circuit &circuit);
std::optional<std::string> CheckTransientSettings(const TransientSettings &settings);
std::optional<std::string> CheckACSweepSettings(const Circuit &circuit, const ACSweepSettings &settings);
std::optional<std::string> CheckDCSweepSettings(const Circuit &circuit, const DCSweepSettings &settings);
std::optional<std::string> CheckNoiseSettings(const Circuit &circuit, const NoiseSettings &settings);
std::optional<std::string> CheckTransferFunctionSettings(const Circuit &circuit,
                                                         const TransferFunctionSettings &settings);
std::optional<std::string> CheckTruthTableSettings(const Circuit &circuit, const TruthTableSettings &settings);

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

#endif // IMCSIM_CIRCUIT_CHECKS_H
