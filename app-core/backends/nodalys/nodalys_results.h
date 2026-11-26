/**
 * @file    nodalys_results.h
 * @brief   Conversions from Nodalys engine analysis outputs to imcsim Core simulation results.
 */

#ifndef IMCSIM_NODALYS_RESULTS_H
#define IMCSIM_NODALYS_RESULTS_H

#include "circuit/circuit.h"
#include "simulation/live_pace.h"
#include "simulation/run_control.h"
#include "simulation/simulation_backend.h"
#include "simulation/simulation_results.h"
#include <expected>
#include <nodalys/analyses.h>
#include <nodalys/results.h>
#include <string>

namespace Core {

// Monitor bridging
[[nodiscard]] Nodalys::RunMonitor
ToNodalysMonitor(const RunMonitor &monitor, LivePacer &live_pacer, const Circuit &circuit);

// Result conversion
[[nodiscard]] OperatingPoint ToOperatingPoint(const Circuit &circuit, Nodalys::OperatingPointResult result);
[[nodiscard]] Transient ToTransient(Nodalys::TransientResult result, int node_count);
[[nodiscard]] Transient
ToFinishedTransient(const Circuit &circuit, Nodalys::TransientResult result, bool has_initial_conditions);
[[nodiscard]] DCSweep ToDCSweep(Nodalys::DcSweepResult result, int node_count);
[[nodiscard]] ACSweep ToACSweep(Nodalys::AcSweepResult result, int node_count);
[[nodiscard]] Noise ToNoise(Nodalys::NoiseResult result);
[[nodiscard]] TransferFunction ToTransferFunction(Nodalys::TransferFunctionResult result);
[[nodiscard]] ACSensitivity
ToACSensitivity(const Circuit &circuit, const ACSensitivitySettings &settings, Nodalys::AcSensitivityResult result);
[[nodiscard]] std::expected<PoleZero, std::string>
ToPoleZero(const Circuit &circuit, const PoleZeroSettings &settings, const Nodalys::PoleZeroResult &result);
[[nodiscard]] SParameters ToSParameters(Nodalys::SParameterResult result);
[[nodiscard]] HarmonicBalance
ToHarmonicBalance(const HarmonicBalanceSettings &settings, Nodalys::HarmonicBalanceResult result, int node_count);

} // namespace Core

#endif // IMCSIM_NODALYS_RESULTS_H
