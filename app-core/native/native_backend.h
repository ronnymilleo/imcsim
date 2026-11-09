/**
 * @file    native_backend.h
 * @brief   The simulation backend that solves circuits with the native solver of imcsim, a clean-room engine written
 *          from published circuit theory to sit beside the ngspice backend.
 * @details The backend compiles the Circuit into its equation system (compiler.h) and runs the analysis on it
 *          (analyses.h). The analyses it has so far are the DC operating point, the DC sweep and the transient; the
 *          AC sweep, the noise and the transfer function, and the logic parts, are on the roadmap and reported as
 *          unsupported.
 */

#ifndef IMCSIM_NATIVE_BACKEND_H
#define IMCSIM_NATIVE_BACKEND_H

#include "simulation/simulation_backend.h"

namespace Core {

/**
 * @class   NativeBackend
 * @brief   The imcsim-native circuit simulator as a SimulationBackend.
 */
class NativeBackend : public SimulationBackend {
public:
    NativeBackend();

    std::string_view GetName() const override;
    const BackendCapabilities &GetCapabilities() const override;

    OperatingPointRun RunOperatingPoint(const Circuit &circuit) override;
    TransientRun RunTransient(const Circuit &circuit, const TransientSettings &settings,
                              const std::optional<double> &temperature_celsius, const RunMonitor &monitor) override;
    DCSweepRun RunDCSweep(const Circuit &circuit, const DCSweepSettings &settings,
                          const std::optional<double> &temperature_celsius) override;
    ACSweepRun RunACSweep(const Circuit &circuit, const ACSweepSettings &settings,
                          const std::optional<double> &temperature_celsius) override;
    NoiseRun RunNoise(const Circuit &circuit, const NoiseSettings &settings) override;
    TransferFunctionRun RunTransferFunction(const Circuit &circuit, const TransferFunctionSettings &settings) override;

private:
    BackendCapabilities m_Capabilities;
};

} // namespace Core

#endif // IMCSIM_NATIVE_BACKEND_H
