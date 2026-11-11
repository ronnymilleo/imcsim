/**
 * @file    analyses.h
 * @brief   Internal solver interfaces and implementations for circuit analyses.
 */

#ifndef NODALYS_INTERNAL_ANALYSES_H
#define NODALYS_INTERNAL_ANALYSES_H

#include "compiler/compiled_circuit.h"
#include "newton/newton.h"
#include "nodalys/analyses.h"
#include "nodalys/results.h"
#include <expected>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace nodalys {

inline void DescribeOutcome(const NewtonOutcome &outcome, const char *analysis,
                            std::vector<DiagnosticMessage> *messages) {
    if (!messages) {
        return;
    }
    std::string text = std::format("nodalys: {} converged in {} iterations", analysis, outcome.Iterations);
    if (!outcome.Strategy.empty()) {
        text += std::format(" (with {})", outcome.Strategy);
    }
    messages->push_back({std::move(text), false});
}

std::expected<OperatingPointResult, std::string> ComputeOperatingPoint(CompiledCircuit &compiled,
                                                                       const ConvergenceOptions &options,
                                                                       std::vector<DiagnosticMessage> *messages);

std::expected<DcSweepResult, std::string> ComputeDCSweep(CompiledCircuit &compiled, const DcSweepConfig &settings,
                                                         const ConvergenceOptions &options,
                                                         std::vector<DiagnosticMessage> *messages);

std::expected<TransientResult, std::string> ComputeTransient(CompiledCircuit &compiled, const TransientConfig &settings,
                                                             bool has_initial_conditions, const RunMonitor &monitor,
                                                             const ConvergenceOptions &options,
                                                             std::vector<DiagnosticMessage> *messages);

std::expected<AcSweepResult, std::string> ComputeACSweep(CompiledCircuit &compiled, const AcSweepConfig &settings,
                                                         const ConvergenceOptions &options,
                                                         std::vector<DiagnosticMessage> *messages);

std::expected<TransferFunctionResult, std::string> ComputeTransferFunction(CompiledCircuit &compiled,
                                                                           const TransferFunctionConfig &settings,
                                                                           const ConvergenceOptions &options,
                                                                           std::vector<DiagnosticMessage> *messages);

std::expected<NoiseResult, std::string> ComputeNoise(CompiledCircuit &compiled, const NoiseConfig &settings,
                                                     const ConvergenceOptions &options,
                                                     std::vector<DiagnosticMessage> *messages);

std::expected<SParameterResult, std::string> ComputeSParameters(CompiledCircuit &compiled,
                                                                const SParameterConfig &settings,
                                                                const ConvergenceOptions &options,
                                                                std::vector<DiagnosticMessage> *messages);

std::expected<PoleZeroResult, std::string> ComputePoleZero(CompiledCircuit &compiled, const PoleZeroConfig &settings,
                                                           const ConvergenceOptions &options,
                                                           std::vector<DiagnosticMessage> *messages);

std::expected<SensitivityResult, std::string> ComputeSensitivity(CompiledCircuit &compiled,
                                                                 const SensitivityConfig &settings,
                                                                 const ConvergenceOptions &options,
                                                                 std::vector<DiagnosticMessage> *messages);

} // namespace nodalys

#endif // NODALYS_INTERNAL_ANALYSES_H
