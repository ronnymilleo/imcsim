#ifndef NODALYS_RESULTS_H
#define NODALYS_RESULTS_H

#include <string>
#include <vector>

namespace nodalys {

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

} // namespace nodalys

#endif // NODALYS_RESULTS_H
