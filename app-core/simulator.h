/**
 * @file    simulator.h
 * @brief   Checks a request and runs it on the simulation backend, then returns the results.
 */

#ifndef IMCSIM_SIMULATOR_H
#define IMCSIM_SIMULATOR_H

#include "analysis_settings.h"
#include "circuit.h"
#include "circuit_checks.h"
#include "run_control.h"
#include "simulation_backend.h"
#include "simulation_results.h"
#include "stepped_circuit.h"
#include <optional>

namespace Core {

OperatingPointRun RunOperatingPoint(const Circuit &circuit);
TransientRun RunTransient(const Circuit &circuit, const TransientSettings &settings,
                          const std::optional<ParameterValue> &parameter = std::nullopt,
                          const RunMonitor &monitor = {});
ACSweepRun RunACSweep(const Circuit &circuit, const ACSweepSettings &settings,
                      const std::optional<ParameterValue> &parameter = std::nullopt);
DCSweepRun RunDCSweep(const Circuit &circuit, const DCSweepSettings &settings,
                      const std::optional<ParameterValue> &parameter = std::nullopt);
NoiseRun RunNoise(const Circuit &circuit, const NoiseSettings &settings);
TransferFunctionRun RunTransferFunction(const Circuit &circuit, const TransferFunctionSettings &settings);
TruthTableRun RunTruthTable(const Circuit &circuit, const TruthTableSettings &settings, RunControl *control = nullptr);

} // namespace Core

#endif // IMCSIM_SIMULATOR_H
