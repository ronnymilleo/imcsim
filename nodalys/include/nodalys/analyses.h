/**
 * @file    analyses.h
 * @brief   Declarations of simulation analyses and configurations for Nodalys.
 */

#ifndef NODALYS_ANALYSES_H
#define NODALYS_ANALYSES_H

#include "nodalys/results.h"
#include "nodalys/types.h"
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace nodalys {

class Circuit;

/**
 * @struct  ConvergenceOptions
 * @brief   Numerical tolerances and iteration limits for Newton-Raphson and LTE step control.
 */
struct ConvergenceOptions {
    double RelTol = 1e-3;
    double VoltageAbsTol = 1e-6;
    double CurrentAbsTol = 1e-12;
    int MaxIterations = 150;
    double Gmin = 1e-12;
    int GminSteps = 10;
    int SourceSteps = 10;
    double NormalStepFactor = 2.0;
    double MaxStepFactor = 8.0;
    double CutbackFactor = 0.125;
};

/**
 * @struct  DcSweepConfig
 * @brief   Configuration for a 1D or 2D DC sweep (.dc).
 */
struct DcSweepConfig {
    std::string SweptSource;
    double SweptStart = 0.0;
    double SweptStop = 0.0;
    double SweptStep = 0.0;
    std::string SteppedSource;
    double SteppedStart = 0.0;
    double SteppedStop = 0.0;
    double SteppedStep = 0.0;
};

/**
 * @struct  TransientConfig
 * @brief   Configuration for time-domain transient analysis (.tran).
 */
struct TransientConfig {
    double StopTime = 0.0;
    double TimeStep = 0.0;
    double MaxStep = 0.0;
    bool UseInitialConditions = false;
};

/**
 * @struct  AcSweepConfig
 * @brief   Configuration for small-signal AC frequency sweep (.ac).
 */
struct AcSweepConfig {
    double StartFrequency = 1.0;
    double StopFrequency = 1e6;
    int PointsPerDecade = 10;
};

/**
 * @struct  TransferFunctionConfig
 * @brief   Configuration for small-signal DC transfer function (.tf).
 */
struct TransferFunctionConfig {
    std::string InputSource;
    int OutputPositiveNode = 0;
    int OutputNegativeNode = 0;
};

/**
 * @struct  NoiseConfig
 * @brief   Configuration for small-signal AC noise analysis (.noise).
 */
struct NoiseConfig {
    int OutputPositiveNode = 0;
    int OutputNegativeNode = 0;
    std::string InputSource;
    double StartFrequency = 1.0;
    double StopFrequency = 1e6;
    int PointsPerDecade = 10;
};

/**
 * @enum    FrequencySweepType
 * @brief   Frequency spacing mode for AC and S-parameter frequency sweeps.
 */
enum class FrequencySweepType {
    Linear,
    Decade,
    Octave
};

/**
 * @struct  SParameterConfig
 * @brief   Configuration for multiport small-signal scattering parameter analysis (.sp).
 */
struct SParameterConfig {
    FrequencySweepType SweepType = FrequencySweepType::Decade;
    double StartFrequency = 1e6;
    double StopFrequency = 1e9;
    int Points = 100;
    std::vector<CircuitPort> Ports;
};

/**
 * @struct  PoleZeroConfig
 * @brief   Configuration for small-signal pole-zero analysis (.pz).
 */
struct PoleZeroConfig {
    int OutputNode = 0;
    std::string InputSource;
    bool FindPoles = true;
    bool FindZeros = true;
};

/**
 * @struct  RunMonitor
 * @brief   Asynchronous control and streaming callbacks for interactive/live simulation.
 */
struct RunMonitor {
    std::function<bool()> ShouldStop = nullptr;
    std::function<void(double progress)> OnProgress = nullptr;
    std::function<std::vector<LiveChange>()> TakeLiveChanges = nullptr;
    std::function<void(const TransientResult &chunk)> PostTransientChunk = nullptr;
};

// Top-level analysis entry points
std::expected<OperatingPointResult, std::string>
ComputeOperatingPoint(const Circuit &circuit, const ConvergenceOptions &options = {},
                      std::vector<DiagnosticMessage> *messages = nullptr);

std::expected<DcSweepResult, std::string> ComputeDCSweep(const Circuit &circuit, const DcSweepConfig &config,
                                                         const ConvergenceOptions &options = {},
                                                         std::vector<DiagnosticMessage> *messages = nullptr);

std::expected<TransientResult, std::string> ComputeTransient(const Circuit &circuit, const TransientConfig &config,
                                                             bool has_initial_conditions = false,
                                                             const RunMonitor &monitor = {},
                                                             const ConvergenceOptions &options = {},
                                                             std::vector<DiagnosticMessage> *messages = nullptr);

std::expected<AcSweepResult, std::string> ComputeACSweep(const Circuit &circuit, const AcSweepConfig &config,
                                                         const ConvergenceOptions &options = {},
                                                         std::vector<DiagnosticMessage> *messages = nullptr);

std::expected<TransferFunctionResult, std::string>
ComputeTransferFunction(const Circuit &circuit, const TransferFunctionConfig &config,
                        const ConvergenceOptions &options = {}, std::vector<DiagnosticMessage> *messages = nullptr);

std::expected<NoiseResult, std::string> ComputeNoise(const Circuit &circuit, const NoiseConfig &config,
                                                     const ConvergenceOptions &options = {},
                                                     std::vector<DiagnosticMessage> *messages = nullptr);

std::expected<SParameterResult, std::string> ComputeSParameters(const Circuit &circuit, const SParameterConfig &config,
                                                                const ConvergenceOptions &options = {},
                                                                std::vector<DiagnosticMessage> *messages = nullptr);

std::expected<PoleZeroResult, std::string> ComputePoleZero(const Circuit &circuit, const PoleZeroConfig &config,
                                                           const ConvergenceOptions &options = {},
                                                           std::vector<DiagnosticMessage> *messages = nullptr);

} // namespace nodalys

#endif // NODALYS_ANALYSES_H
