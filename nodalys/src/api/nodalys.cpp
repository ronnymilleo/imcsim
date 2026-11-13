/**
 * @file    nodalys.cpp
 * @brief   Public entry point forwarding simulation requests to internal solvers.
 */

#include "nodalys/nodalys.h"

#include "analyses/analyses.h"
#include "api/circuit_impl.h"

namespace Nodalys {

namespace {

CompiledCircuit &PrepareCircuit(const Circuit &circuit) {
    CompiledCircuit &compiled = const_cast<Circuit &>(circuit).Impl().Compiled();
    for (const auto &device : compiled.Devices) {
        device->SetTemperature(compiled.Thermal);
    }
    return compiled;
}

} // namespace

std::expected<OperatingPointResult, std::string> ComputeOperatingPoint(const Circuit &circuit,
                                                                       const ConvergenceOptions &options,
                                                                       std::vector<DiagnosticMessage> *messages) {
    return ComputeOperatingPoint(PrepareCircuit(circuit), options, messages);
}

std::expected<DcSweepResult, std::string> ComputeDCSweep(const Circuit &circuit, const DcSweepConfig &config,
                                                         const ConvergenceOptions &options,
                                                         std::vector<DiagnosticMessage> *messages) {
    return ComputeDCSweep(PrepareCircuit(circuit), config, options, messages);
}

std::expected<TransientResult, std::string> ComputeTransient(const Circuit &circuit, const TransientConfig &config,
                                                             const bool has_initial_conditions,
                                                             const RunMonitor &monitor,
                                                             const ConvergenceOptions &options,
                                                             std::vector<DiagnosticMessage> *messages) {
    return ComputeTransient(PrepareCircuit(circuit), config, has_initial_conditions || config.UseInitialConditions,
                            monitor, options, messages);
}

std::expected<AcSweepResult, std::string> ComputeACSweep(const Circuit &circuit, const AcSweepConfig &config,
                                                         const ConvergenceOptions &options,
                                                         std::vector<DiagnosticMessage> *messages) {
    return ComputeACSweep(PrepareCircuit(circuit), config, options, messages);
}

std::expected<TransferFunctionResult, std::string> ComputeTransferFunction(const Circuit &circuit,
                                                                           const TransferFunctionConfig &config,
                                                                           const ConvergenceOptions &options,
                                                                           std::vector<DiagnosticMessage> *messages) {
    return ComputeTransferFunction(PrepareCircuit(circuit), config, options, messages);
}

std::expected<NoiseResult, std::string> ComputeNoise(const Circuit &circuit, const NoiseConfig &config,
                                                     const ConvergenceOptions &options,
                                                     std::vector<DiagnosticMessage> *messages) {
    return ComputeNoise(PrepareCircuit(circuit), config, options, messages);
}

std::expected<SParameterResult, std::string> ComputeSParameters(const Circuit &circuit, const SParameterConfig &config,
                                                                const ConvergenceOptions &options,
                                                                std::vector<DiagnosticMessage> *messages) {
    return ComputeSParameters(PrepareCircuit(circuit), config, options, messages);
}

std::expected<PoleZeroResult, std::string> ComputePoleZero(const Circuit &circuit, const PoleZeroConfig &config,
                                                           const ConvergenceOptions &options,
                                                           std::vector<DiagnosticMessage> *messages) {
    return ComputePoleZero(PrepareCircuit(circuit), config, options, messages);
}

std::expected<SensitivityResult, std::string> ComputeSensitivity(const Circuit &circuit,
                                                                 const SensitivityConfig &config,
                                                                 const ConvergenceOptions &options,
                                                                 std::vector<DiagnosticMessage> *messages) {
    return ComputeSensitivity(PrepareCircuit(circuit), config, options, messages);
}

std::expected<HarmonicBalanceResult, std::string> ComputeHarmonicBalance(const Circuit &circuit,
                                                                         const HarmonicBalanceConfig &config,
                                                                         const ConvergenceOptions &options,
                                                                         std::vector<DiagnosticMessage> *messages) {
    return ComputeHarmonicBalance(PrepareCircuit(circuit), config, options, messages);
}

} // namespace Nodalys
