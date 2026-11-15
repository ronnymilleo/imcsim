/**
 * @file    results.h
 * @brief   Output data structures for Nodalys numerical simulation analyses.
 */

#ifndef NODALYS_RESULTS_H
#define NODALYS_RESULTS_H

#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace Nodalys {

/**
 * @struct  ComponentCurrent
 * @brief   Current through one circuit component at the DC operating point.
 */
struct ComponentCurrent {
    std::string Name;
    double Current = 0.0;
};

/**
 * @struct  ComponentTrace
 * @brief   A series of numerical values for one component across sweep/time steps.
 */
struct ComponentTrace {
    std::string Name;
    std::vector<double> Values;
};

/**
 * @struct  OperatingPointResult
 * @brief   DC Operating Point (.op) node voltages and branch currents.
 */
struct OperatingPointResult {
    /// Node voltages indexed by NodeId (node 0 is ground and always 0 V).
    std::vector<double> NodeVoltages;
    /// Component currents.
    std::vector<ComponentCurrent> Currents;
    /// Warnings emitted during solution.
    std::vector<std::string> Warnings;
};

/**
 * @struct  TransientResult
 * @brief   Time-domain Transient (.tran) node voltage and branch current trajectories.
 */
struct TransientResult {
    std::vector<double> Times;
    /// Indexed by node number, then by time point.
    std::vector<std::vector<double>> NodeVoltages;
    /// Currents for each monitored component over time.
    std::vector<ComponentTrace> Currents;
    /// Diagnostic warnings.
    std::vector<std::string> Warnings;
    /// False if simulation was interrupted or cut short.
    bool IsComplete = true;
};

/**
 * @struct  DcSweepCurve
 * @brief   A single curve of a DC sweep for one value of the stepped source.
 */
struct DcSweepCurve {
    double StepValue = 0.0;
    std::vector<std::vector<double>> NodeVoltages;
    std::vector<ComponentTrace> Currents;
};

/**
 * @struct  DcSweepResult
 * @brief   DC Sweep (.dc) 1D or 2D results.
 */
struct DcSweepResult {
    std::string SweptSource;
    std::string SweptUnit;
    std::vector<double> SweptValues;
    std::string SteppedSource;
    std::string SteppedUnit;
    std::vector<DcSweepCurve> Curves;
};

/**
 * @struct  AcSweepResult
 * @brief   Small-Signal AC (.ac) frequency response (dB magnitude and unwrapped degrees phase).
 */
struct AcSweepResult {
    std::vector<double> Frequencies;
    std::vector<std::vector<double>> NodeMagnitudesDecibels;
    std::vector<std::vector<double>> NodePhasesDegrees;
    std::vector<ComponentTrace> CurrentMagnitudesDecibels;
    std::vector<ComponentTrace> CurrentPhasesDegrees;
};

/**
 * @struct  TransferFunctionResult
 * @brief   Small-Signal DC Transfer Function (.tf) gain, input impedance, and output impedance.
 */
struct TransferFunctionResult {
    std::string InputSource;
    int OutputPositiveNode = 0;
    int OutputNegativeNode = 0;
    double Gain = 0.0;
    std::string GainUnit;
    double InputImpedance = 0.0;
    double OutputImpedance = 0.0;
};

/**
 * @struct  NoiseContribution
 * @brief   Individual component spectral noise density and integrated total RMS noise.
 */
struct NoiseContribution {
    std::string Name;
    std::vector<double> OutputDensities;
    double OutputTotal = 0.0;
};

/**
 * @struct  NoiseResult
 * @brief   Small-Signal AC Noise (.noise) analysis output and input-referred results.
 */
struct NoiseResult {
    int OutputNode = 0;
    std::string InputSource;
    std::string InputUnit;
    std::vector<double> Frequencies;
    std::vector<double> OutputDensities;
    std::vector<double> InputDensities;
    double OutputTotal = 0.0;
    double InputTotal = 0.0;
    std::vector<NoiseContribution> Contributions;
};

/**
 * @struct  SParameterResult
 * @brief   Multiport Scattering Parameters (S-matrix) and derived RF figures of merit across frequency.
 */
struct SParameterResult {
    std::vector<double> Frequencies;
    /// Reference impedance in Ohms per port, ordered by 1-based port index.
    std::vector<double> ReferenceOhms;
    /// Flattened complex S-matrix values stored as: [freq_idx][row - 1][col - 1].
    std::vector<std::complex<double>> Values;
    /// Diagnostic warnings.
    std::vector<std::string> Warnings;

    int GetPortCount() const { return static_cast<int>(ReferenceOhms.size()); }

    std::complex<double> Get(const std::size_t frequency_idx, const int row, const int column) const {
        const auto ports = static_cast<std::size_t>(GetPortCount());
        return Values[(frequency_idx * ports + static_cast<std::size_t>(row - 1)) * ports +
                      static_cast<std::size_t>(column - 1)];
    }

    std::optional<double> ReturnLossDb(const std::size_t frequency_idx, const int port) const {
        constexpr double MIN_MAG = 1e-15;
        const auto s = Get(frequency_idx, port, port);
        const double mag = std::abs(s);
        if (mag < MIN_MAG) {
            return std::nullopt;
        }
        return -20.0 * std::log10(mag);
    }

    std::optional<double> InsertionLossDb(const std::size_t frequency_idx, const int to_port,
                                          const int from_port) const {
        constexpr double MIN_MAG = 1e-15;
        const auto s = Get(frequency_idx, to_port, from_port);
        const double mag = std::abs(s);
        if (mag < MIN_MAG) {
            return std::nullopt;
        }
        return -20.0 * std::log10(mag);
    }

    std::optional<double> Vswr(const std::size_t frequency_idx, const int port) const {
        constexpr double TOL = 1e-12;
        const double mag = std::abs(Get(frequency_idx, port, port));
        if (mag >= 1.0 - TOL) {
            return std::nullopt;
        }
        return (1.0 + mag) / (1.0 - mag);
    }

    std::optional<std::complex<double>> InputImpedance(const std::size_t frequency_idx, const int port) const {
        constexpr double TOL = 1e-12;
        if (port < 1 || static_cast<std::size_t>(port) > ReferenceOhms.size()) {
            return std::nullopt;
        }
        const double z0 = ReferenceOhms[static_cast<std::size_t>(port - 1)];
        const auto s = Get(frequency_idx, port, port);
        const std::complex<double> denom = 1.0 - s;
        if (std::abs(denom) < TOL) {
            return std::nullopt;
        }
        return z0 * (1.0 + s) / denom;
    }
};

/**
 * @struct  PoleZeroResult
 * @brief   Small-Signal Pole-Zero (.pz) finite poles and zeros in rad/s.
 */
struct PoleZeroResult {
    std::vector<std::complex<double>> Poles;
    std::vector<std::complex<double>> Zeros;
};

/**
 * @struct  SensitivityResult
 * @brief   Small-signal DC Sensitivity (.sens) vectors for circuit components and subcircuits.
 */
struct SensitivityResult {
    int OutputPositiveNode = 0;
    int OutputNegativeNode = 0;
    std::map<std::string, double> Vectors;
};

/**
 * @struct  ComponentPhasors
 * @brief   Complex peak harmonic phasors for one monitored component current (indexed 0..H).
 */
struct ComponentPhasors {
    std::string Name;
    std::vector<std::complex<double>> Phasors;
};

/**
 * @struct  HarmonicBalanceResult
 * @brief   Periodic steady-state Harmonic Balance (.hb) spectra, one-cycle waveforms, and diagnostics.
 * @details Phasors use peak amplitude convention: $x(t) = X_0 + \sum_{k=1}^{H} \text{Re}(X_k e^{j k \omega_0 t})$,
 *          where DC ($k = 0$) has zero imaginary part.
 */
struct HarmonicBalanceResult {
    std::vector<double> Frequencies;
    /// Complex peak phasors indexed [node][harmonic], node 0 is ground.
    std::vector<std::vector<std::complex<double>>> NodePhasors;
    /// Complex peak current phasors per monitored component.
    std::vector<ComponentPhasors> CurrentPhasors;
    /// Reconstructed time points over one period [0, T0).
    std::vector<double> Times;
    /// Reconstructed periodic node voltage waveforms indexed [node][sample].
    std::vector<std::vector<double>> NodeWaveforms;
    /// Reconstructed periodic component current waveforms.
    std::vector<ComponentTrace> CurrentWaveforms;
    /// Diagnostic warnings emitted during analysis.
    std::vector<std::string> Warnings;
    int Iterations = 0;
    /// Recovery strategy used when plain Newton did not converge ("", "source stepping", or "gmin stepping").
    std::string Strategy;
};

[[nodiscard]] double ComputeThd(std::span<const std::complex<double>> phasors);

using SimulationResult =
    std::variant<OperatingPointResult, TransientResult, DcSweepResult, AcSweepResult, TransferFunctionResult,
                 NoiseResult, SParameterResult, PoleZeroResult, SensitivityResult, HarmonicBalanceResult>;

} // namespace Nodalys

#endif // NODALYS_RESULTS_H
