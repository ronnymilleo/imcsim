/**
 * @file    ngspice_backend.h
 * @brief   Simulation backend that runs circuits through the ngspice shared library.
 */

#ifndef IMCSIM_NGSPICE_BACKEND_H
#define IMCSIM_NGSPICE_BACKEND_H

#include "simulation_backend.h"
#include <optional>

namespace Core {

/**
 * @class   NgspiceBackend
 * @brief   Runs circuits through the ngspice shared library, one global instance per process.
 * @details Runs are synchronous and single-threaded, and the library is cleaned after each one.
 */
class NgspiceBackend : public SimulationBackend {
public:
    std::string_view GetName() const override;
    const BackendCapabilities &GetCapabilities() const override;

    OperatingPointRun RunOperatingPoint(const Circuit &circuit) override;
    TransientRun RunTransient(const Circuit &circuit, const TransientSettings &settings,
                              const std::optional<double> &temperature_celsius) override;
    ACSweepRun RunACSweep(const Circuit &circuit, const ACSweepSettings &settings,
                          const std::optional<double> &temperature_celsius) override;
    DCSweepRun RunDCSweep(const Circuit &circuit, const DCSweepSettings &settings,
                          const std::optional<double> &temperature_celsius) override;
    NoiseRun RunNoise(const Circuit &circuit, const NoiseSettings &settings) override;
    TransferFunctionRun RunTransferFunction(const Circuit &circuit, const TransferFunctionSettings &settings) override;

private:
    // Known after ngspice starts, so it is worked out on first use
    mutable std::optional<BackendCapabilities> m_Capabilities;
};

} // namespace Core

#endif // IMCSIM_NGSPICE_BACKEND_H
