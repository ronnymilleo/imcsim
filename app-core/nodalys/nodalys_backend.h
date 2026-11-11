/**
 * @file    nodalys_backend.h
 * @brief   The Nodalys simulation backend, which solves circuits with the clean-room Nodalys solver of imcsim.
 * @details The library is named **Nodalys** — a contraction of *nodal analysis* (`nodal` + the `-lys` of `analysis`),
 *          after the Modified Nodal Analysis (MNA) at its core. It compiles the Circuit directly into its MNA equation
 *          system (compiler.h) and runs the requested analysis on it.
 */

#ifndef IMCSIM_NODALYS_BACKEND_H
#define IMCSIM_NODALYS_BACKEND_H

#include "simulation/simulation_backend.h"

namespace Core {

/**
 * @class   NodalysBackend
 * @brief   The Nodalys circuit simulator as a SimulationBackend.
 */
class NodalysBackend : public SimulationBackend {
public:
    NodalysBackend();

    std::string_view GetName() const override;
    const BackendCapabilities &GetCapabilities() const override;

    OperatingPointRun RunOperatingPoint(const Circuit &circuit,
                                        const std::optional<double> &temperature_celsius = std::nullopt) override;
    TransientRun RunTransient(const Circuit &circuit, const TransientSettings &settings,
                              const std::optional<double> &temperature_celsius, const RunMonitor &monitor) override;
    DCSweepRun RunDCSweep(const Circuit &circuit, const DCSweepSettings &settings,
                          const std::optional<double> &temperature_celsius) override;
    ACSweepRun RunACSweep(const Circuit &circuit, const ACSweepSettings &settings,
                          const std::optional<double> &temperature_celsius) override;
    NoiseRun RunNoise(const Circuit &circuit, const NoiseSettings &settings) override;
    TransferFunctionRun RunTransferFunction(const Circuit &circuit, const TransferFunctionSettings &settings) override;
    SensitivityRun RunSensitivity(const Circuit &circuit, const SensitivitySettings &settings) override;
    PoleZeroRun RunPoleZero(const Circuit &circuit, const PoleZeroSettings &settings) override;
    SParametersRun RunSParameters(const Circuit &circuit, const SParameterSettings &settings) override;
    HarmonicBalanceRun RunHarmonicBalance(const Circuit &circuit, const HarmonicBalanceSettings &settings) override;

private:
    BackendCapabilities m_Capabilities;
};

} // namespace Core

#endif // IMCSIM_NODALYS_BACKEND_H
