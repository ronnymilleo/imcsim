/**
 * @file    analyses.h
 * @brief   The analyses the native backend runs on a compiled circuit: the operating point, the DC sweep and the
 *          transient with trapezoidal integration.
 * @details The operating point and the sweep are Newton-Raphson solves (newton.h). The transient integrates with
 *          the trapezoidal rule and local truncation error control, as in Vlach & Singhal, "Computer Methods for
 *          Circuit Analysis and Design", and Kundert, "The Designer's Guide to SPICE and Spectre", chapter 5: the
 *          LTE of a trapezoidal step is estimated from the distance between the solution and the linear predictor,
 *          LTE ≈ |x − x̂| / 4 for the constant step, and the step grows or shrinks to keep it under the tolerance.
 */

#ifndef IMCSIM_NATIVE_ANALYSES_H
#define IMCSIM_NATIVE_ANALYSES_H

#include "native/compiler.h"
#include "native/newton.h"
#include "simulation/simulation_backend.h"
#include <expected>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace Core::Native {

/**
 * @brief   Formats a diagnostic message reporting how many Newton-Raphson iterations a DC solve took (and which
 *          homotopy fallback was used, if any).
 */
inline void DescribeOutcome(const NewtonOutcome &outcome, const char *analysis,
                            std::vector<SimulatorMessage> &messages) {
    std::string text = std::format("native: {} converged in {} iterations", analysis, outcome.Iterations);
    if (!outcome.Strategy.empty()) {
        text += std::format(" (with {})", outcome.Strategy);
    }
    messages.push_back({std::move(text), false});
}

/**
 * @brief   Computes the DC operating point of a compiled circuit.
 * @param[in,out] compiled  The compiled circuit; its devices keep the converged solution as the seed of later runs.
 * @param[in]     options   Convergence tolerances.
 * @param[out]    messages  The lines a run prints, like the iteration count.
 * @return  The operating point, or the reason no solution was found.
 */
std::expected<OperatingPoint, std::string> ComputeOperatingPoint(CompiledCircuit &compiled,
                                                                 const ConvergenceOptions &options,
                                                                 std::vector<SimulatorMessage> &messages);

/**
 * @brief   Sweeps a source through DC values and solves the operating point at each one.
 * @param[in]     circuit   The circuit the compiled one was built from, for the units and names of the sweep.
 * @param[in,out] compiled  The compiled circuit.
 * @param[in]     settings  Swept source and range, and optionally a second source stepped once per curve.
 * @param[in]     options   Convergence tolerances.
 * @param[out]    messages  The lines a run prints.
 * @return  The node voltages and currents along the sweep, one curve per stepped value, or the reason it failed.
 * @note    Each point warm-starts from the previous solution, sweeping up or down, and AC or pulse sources sweep
 *          through their DC value, as ngspice does.
 */
std::expected<DCSweep, std::string> ComputeDCSweep(const Circuit &circuit, CompiledCircuit &compiled,
                                                   const DCSweepSettings &settings, const ConvergenceOptions &options,
                                                   std::vector<SimulatorMessage> &messages);

/**
 * @brief   Simulates the circuit over time with trapezoidal integration and LTE step control.
 * @param[in,out] compiled               The compiled circuit.
 * @param[in]     settings               Stop time and largest step between output points.
 * @param[in]     has_initial_conditions The circuit starts from the initial conditions (uic) instead of the
 *                                       operating point.
 * @param[in]     monitor                Where to look for a stop request, report progress and receive the points as
 *                                       they are made.
 * @param[in]     options                Convergence tolerances.
 * @param[out]    messages               The lines a run prints.
 * @return  The values at every output point, or the reason the run failed.
 * @note    The internal step adapts to the truncation error and never exceeds the output step; output points come
 *          at exact multiples of the output step by linear interpolation, as ngspice reports them. A run that is
 *          stopped returns the points made until then, with IsComplete false.
 */
std::expected<Transient, std::string> ComputeTransient(CompiledCircuit &compiled, const TransientSettings &settings,
                                                       bool has_initial_conditions, const RunMonitor &monitor,
                                                       const ConvergenceOptions &options,
                                                       std::vector<SimulatorMessage> &messages);

/**
 * @brief   Computes the small-signal AC frequency response (Bode magnitude and phase) around the DC operating point.
 * @param[in,out] compiled  The compiled circuit.
 * @param[in]     settings  Start frequency, stop frequency, and points per decade (decade-logarithmic spacing).
 * @param[in]     options   DC operating-point convergence tolerances.
 * @param[out]    messages  Diagnostic messages from the DC operating point and AC sweep.
 * @return  Node voltage and component current magnitudes (dB) and phases (degrees), or the reason the sweep failed.
 */
std::expected<ACSweep, std::string> ComputeACSweep(CompiledCircuit &compiled, const ACSweepSettings &settings,
                                                   const ConvergenceOptions &options,
                                                   std::vector<SimulatorMessage> &messages);

/**
 * @brief   Computes the small-signal noise spectral density and integrated RMS noise at an output node, referred to
 *          an input AC source, around the DC operating point (SPICE `.noise`).
 */
std::expected<Noise, std::string> ComputeNoise(const Circuit &circuit, CompiledCircuit &compiled,
                                               const NoiseSettings &settings, const ConvergenceOptions &options,
                                               std::vector<SimulatorMessage> &messages);

/**
 * @brief   Computes the small-signal DC transfer function (gain, input impedance, output impedance) from an
 *          independent input source to an output node around the DC operating point.
 */
std::expected<TransferFunction, std::string> ComputeTransferFunction(const Circuit &circuit, CompiledCircuit &compiled,
                                                                     const TransferFunctionSettings &settings,
                                                                     const ConvergenceOptions &options,
                                                                     std::vector<SimulatorMessage> &messages);

} // namespace Core::Native

#endif // IMCSIM_NATIVE_ANALYSES_H
