/**
 * @file    nodalys.cpp
 * @brief   Public entry point forwarding simulation requests to internal solvers.
 */

#include "nodalys/nodalys.h"

#include "analyses/analyses.h"
#include "api/circuit_impl.h"

namespace Nodalys {

/**
 * @brief   Computes the DC operating point (`.op`) of a compiled circuit.
 * @param[in]  circuit   Immutable compiled circuit to simulate.
 * @param[in]  options   Newton-Raphson tolerances and fallback settings.
 * @param[out] messages  Optional sink for diagnostic messages.
 * @return  DC node voltages and branch currents on success, or an error message.
 */
std::expected<OperatingPointResult, std::string> ComputeOperatingPoint(const Circuit &circuit,
                                                                       const ConvergenceOptions &options,
                                                                       std::vector<DiagnosticMessage> *messages) {
    std::expected<CompiledCircuit, std::string> run = CircuitAccess::CloneForRun(circuit);
    if (!run) {
        return std::unexpected(run.error());
    }
    return ComputeOperatingPoint(*run, options, messages);
}

/**
 * @brief   Runs a 1D or nested 2D DC transfer sweep (`.dc`) on a compiled circuit.
 * @param[in]  circuit   Immutable compiled circuit to simulate.
 * @param[in]  config    Swept and optional stepped source sweep bounds.
 * @param[in]  options   Newton-Raphson tolerances and fallback settings.
 * @param[out] messages  Optional sink for diagnostic messages.
 * @return  DC sweep trajectories on success, or an error message.
 */
std::expected<DcSweepResult, std::string> ComputeDCSweep(const Circuit &circuit,
                                                         const DcSweepConfig &config,
                                                         const ConvergenceOptions &options,
                                                         std::vector<DiagnosticMessage> *messages) {
    std::expected<CompiledCircuit, std::string> run = CircuitAccess::CloneForRun(circuit);
    if (!run) {
        return std::unexpected(run.error());
    }
    return ComputeDCSweep(*run, config, options, messages);
}

/**
 * @brief   Runs a time-domain transient simulation (`.tran`) on a compiled circuit.
 * @param[in]  circuit                 Immutable compiled circuit to simulate.
 * @param[in]  config                  Stop time, step bounds, integration method, and startup options.
 * @param[in]  has_initial_conditions  True to honor element and `.ic` initial conditions (`uic`).
 * @param[in]  monitor                 Optional streaming callbacks for live waveform observation and control.
 * @param[in]  options                 Newton-Raphson tolerances and fallback settings.
 * @param[out] messages                Optional sink for diagnostic messages.
 * @return  Time-domain trajectories on success, or an error message.
 */
std::expected<TransientResult, std::string> ComputeTransient(const Circuit &circuit,
                                                             const TransientConfig &config,
                                                             const bool has_initial_conditions,
                                                             const RunMonitor &monitor,
                                                             const ConvergenceOptions &options,
                                                             std::vector<DiagnosticMessage> *messages) {
    std::expected<CompiledCircuit, std::string> run = CircuitAccess::CloneForRun(circuit);
    if (!run) {
        return std::unexpected(run.error());
    }
    return ComputeTransient(*run,
                            config,
                            has_initial_conditions || config.UseInitialConditions,
                            monitor,
                            options,
                            messages);
}

/**
 * @brief   Runs a small-signal AC frequency sweep (`.ac`) linearized around the DC operating point.
 * @param[in]  circuit   Immutable compiled circuit to simulate.
 * @param[in]  config    Frequency sweep type, range, and point density.
 * @param[in]  options   Newton-Raphson tolerances used for the DC bias solve.
 * @param[out] messages  Optional sink for diagnostic messages.
 * @return  Complex node voltage and branch current phasors on success, or an error message.
 */
std::expected<AcSweepResult, std::string> ComputeACSweep(const Circuit &circuit,
                                                         const AcSweepConfig &config,
                                                         const ConvergenceOptions &options,
                                                         std::vector<DiagnosticMessage> *messages) {
    std::expected<CompiledCircuit, std::string> run = CircuitAccess::CloneForRun(circuit);
    if (!run) {
        return std::unexpected(run.error());
    }
    return ComputeACSweep(*run, config, options, messages);
}

/**
 * @brief   Computes the small-signal DC transfer function (`.tf`) gain, input impedance, and output impedance.
 * @param[in]  circuit   Immutable compiled circuit to simulate.
 * @param[in]  config    Output node pair and input source specification.
 * @param[in]  options   Newton-Raphson tolerances used for the DC bias solve.
 * @param[out] messages  Optional sink for diagnostic messages.
 * @return  Transfer function metrics on success, or an error message.
 */
std::expected<TransferFunctionResult, std::string> ComputeTransferFunction(const Circuit &circuit,
                                                                           const TransferFunctionConfig &config,
                                                                           const ConvergenceOptions &options,
                                                                           std::vector<DiagnosticMessage> *messages) {
    std::expected<CompiledCircuit, std::string> run = CircuitAccess::CloneForRun(circuit);
    if (!run) {
        return std::unexpected(run.error());
    }
    return ComputeTransferFunction(*run, config, options, messages);
}

/**
 * @brief   Runs small-signal AC noise spectral density analysis (`.noise`) using the adjoint method.
 * @param[in]  circuit   Immutable compiled circuit to simulate.
 * @param[in]  config    Output nodes, input reference source, and frequency sweep settings.
 * @param[in]  options   Newton-Raphson tolerances used for the DC bias solve.
 * @param[out] messages  Optional sink for diagnostic messages.
 * @return  Output/input noise spectra and integrated noise on success, or an error message.
 */
std::expected<NoiseResult, std::string> ComputeNoise(const Circuit &circuit,
                                                     const NoiseConfig &config,
                                                     const ConvergenceOptions &options,
                                                     std::vector<DiagnosticMessage> *messages) {
    std::expected<CompiledCircuit, std::string> run = CircuitAccess::CloneForRun(circuit);
    if (!run) {
        return std::unexpected(run.error());
    }
    return ComputeNoise(*run, config, options, messages);
}

/**
 * @brief   Computes multiport small-signal scattering, impedance, and admittance parameters (`.sp`).
 * @param[in]  circuit   Immutable compiled circuit to simulate.
 * @param[in]  config    Port definitions and frequency sweep settings.
 * @param[in]  options   Newton-Raphson tolerances used for the DC bias solve.
 * @param[out] messages  Optional sink for diagnostic messages.
 * @return  S/Z/Y port matrices across frequency on success, or an error message.
 */
std::expected<SParameterResult, std::string> ComputeSParameters(const Circuit &circuit,
                                                                const SParameterConfig &config,
                                                                const ConvergenceOptions &options,
                                                                std::vector<DiagnosticMessage> *messages) {
    std::expected<CompiledCircuit, std::string> run = CircuitAccess::CloneForRun(circuit);
    if (!run) {
        return std::unexpected(run.error());
    }
    return ComputeSParameters(*run, config, options, messages);
}

/**
 * @brief   Computes small-signal poles and zeros (`.pz`) via generalized eigenvalue decomposition.
 * @param[in]  circuit   Immutable compiled circuit to simulate.
 * @param[in]  config    Input source, output node, and pole/zero selection flags.
 * @param[in]  options   Newton-Raphson tolerances used for the DC bias solve.
 * @param[out] messages  Optional sink for diagnostic messages.
 * @return  Complex pole and zero frequencies on success, or an error message.
 */
std::expected<PoleZeroResult, std::string> ComputePoleZero(const Circuit &circuit,
                                                           const PoleZeroConfig &config,
                                                           const ConvergenceOptions &options,
                                                           std::vector<DiagnosticMessage> *messages) {
    std::expected<CompiledCircuit, std::string> run = CircuitAccess::CloneForRun(circuit);
    if (!run) {
        return std::unexpected(run.error());
    }
    return ComputePoleZero(*run, config, options, messages);
}

/**
 * @brief   Computes first-order DC sensitivities (`.sens`) of an output voltage with respect to every sensitive
 * element.
 * @param[in]  circuit   Immutable compiled circuit to simulate.
 * @param[in]  config    Output node pair configuration.
 * @param[in]  options   Newton-Raphson tolerances used for the DC bias solve.
 * @param[out] messages  Optional sink for diagnostic messages.
 * @return  Map of element sensitivities on success, or an error message.
 */
std::expected<SensitivityResult, std::string> ComputeSensitivity(const Circuit &circuit,
                                                                 const SensitivityConfig &config,
                                                                 const ConvergenceOptions &options,
                                                                 std::vector<DiagnosticMessage> *messages) {
    std::expected<CompiledCircuit, std::string> run = CircuitAccess::CloneForRun(circuit);
    if (!run) {
        return std::unexpected(run.error());
    }
    return ComputeSensitivity(*run, config, options, messages);
}

/**
 * @brief   Computes small-signal AC sensitivities (`.sens ... ac`) across a frequency sweep.
 * @param[in]  circuit   Immutable compiled circuit to simulate.
 * @param[in]  config    Output node pair, input source, and frequency sweep settings.
 * @param[in]  options   Newton-Raphson tolerances used for the DC bias solve.
 * @param[out] messages  Optional sink for diagnostic messages.
 * @return  Per-element complex AC sensitivities across frequency on success, or an error message.
 */
std::expected<AcSensitivityResult, std::string> ComputeAcSensitivity(const Circuit &circuit,
                                                                     const AcSensitivityConfig &config,
                                                                     const ConvergenceOptions &options,
                                                                     std::vector<DiagnosticMessage> *messages) {
    std::expected<CompiledCircuit, std::string> run = CircuitAccess::CloneForRun(circuit);
    if (!run) {
        return std::unexpected(run.error());
    }
    return ComputeAcSensitivity(*run, config, options, messages);
}

/**
 * @brief   Computes periodic steady-state waveforms via Harmonic Balance (`.hb`).
 * @param[in]  circuit   Immutable compiled circuit to simulate.
 * @param[in]  config    Fundamental frequency, harmonic order, sample count, and homotopy settings.
 * @param[in]  options   Newton-Raphson tolerances and convergence controls.
 * @param[out] messages  Optional sink for diagnostic messages.
 * @return  Harmonic phasors and time-domain waveforms on success, or an error message.
 */
std::expected<HarmonicBalanceResult, std::string> ComputeHarmonicBalance(const Circuit &circuit,
                                                                         const HarmonicBalanceConfig &config,
                                                                         const ConvergenceOptions &options,
                                                                         std::vector<DiagnosticMessage> *messages) {
    std::expected<CompiledCircuit, std::string> run = CircuitAccess::CloneForRun(circuit);
    if (!run) {
        return std::unexpected(run.error());
    }
    return ComputeHarmonicBalance(*run, config, options, messages);
}

} // namespace Nodalys
