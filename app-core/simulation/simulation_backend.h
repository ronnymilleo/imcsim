/**
 * @file    simulation_backend.h
 * @brief   The seam between the simulator and the program that solves a circuit, and what each backend can run.
 * @details A backend runs a circuit that has already been checked and returns the results of one analysis. What does
 *          not depend on the solver, such as the checks of the settings, the stepped parameter and the truth table,
 *          stays in simulator.h and calls the backend in use.
 */

#ifndef IMCSIM_SIMULATION_BACKEND_H
#define IMCSIM_SIMULATION_BACKEND_H

#include "model/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/run_control.h"
#include "simulation/simulation_results.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace Core {

/**
 * @enum    AnalysisKind
 * @brief   The analyses imcsim runs.
 */
enum class AnalysisKind {
    OPERATING_POINT,
    TRANSIENT,
    AC_SWEEP,
    DC_SWEEP,
    NOISE,
    TRANSFER_FUNCTION,
    SENSITIVITY,
    POLE_ZERO,
    S_PARAMETERS,
    TRUTH_TABLE,
    HARMONIC_BALANCE
};

/**
 * @struct  BackendCapabilities
 * @brief   What a backend can simulate.
 * @details The truth table is not listed: it runs an operating point for each combination, so it needs only that.
 */
struct BackendCapabilities {
    std::set<AnalysisKind> Analyses;
    /// Parts the backend cannot simulate, each with the reason shown to the user.
    std::map<ComponentType, std::string> UnsupportedParts;
    /// Parts the backend cannot simulate in one analysis only, each with the reason shown to the user.
    std::map<AnalysisKind, std::map<ComponentType, std::string>> UnsupportedPartsByAnalysis;
    /// Why the backend cannot simulate at a temperature other than the default one, or none when it can.
    std::optional<std::string> TemperatureUnsupported;
};

/**
 * @struct  RunMonitor
 * @brief   What the caller of a transient watches while it goes on, and how it can stop it.
 * @details A backend that runs the transient in stretches of simulated time reads the stop request and reports the
 *          progress between stretches, and hands over the points of each stretch as it goes. With neither set, the run
 *          goes from start to end in one piece. A live run is paced against the clock, LivePace simulated seconds to
 * the second, holds while the RunControl pauses it, and applies the changes the caller sends through the RunControl
 * between stretches. A backend that cannot split the run ignores the monitor.
 */
struct RunMonitor {
    /// Where to look for a stop request and to report the share of the stop time reached, or null.
    RunControl *Control = nullptr;
    /// Receives the points made since the last call, from the worker thread, or empty.
    std::function<void(Transient)> OnChunk;
    /// Simulated seconds a live run covers per second of the clock, or 0 to run as fast as the backend goes.
    double LivePace = 0.0;
};

/**
 * @class   SimulationBackend
 * @brief   A program that solves circuits, such as ngspice.
 * @details Every Run method takes a circuit and settings the simulator has checked, and a circuit whose parts the
 *          backend supports (see FindUnsupported()). A run reports its failures in its result and never throws.
 */
class SimulationBackend {
public:
    virtual ~SimulationBackend() = default;

    /**
     * @brief   Names the backend in the messages that say what it cannot run.
     * @return  A short name, such as "ngspice".
     */
    virtual std::string_view GetName() const = 0;
    /**
     * @brief   Tells which analyses the backend runs and which parts it cannot simulate.
     * @return  The capabilities, each unsupported part with the reason shown to the user.
     * @note    The editor asks every frame while a run goes on in the worker, so it must be safe to call from any
     *          thread.
     */
    virtual const BackendCapabilities &GetCapabilities() const = 0;

    /**
     * @brief   Computes the DC operating point of a circuit.
     * @param[in] circuit              Circuit to simulate, already checked.
     * @param[in] temperature_celsius  Temperature of the run in degrees Celsius, or none for the default one.
     * @return  The node voltages and currents or an error, plus the messages of the backend.
     */
    virtual OperatingPointRun RunOperatingPoint(const Circuit &circuit,
                                                const std::optional<double> &temperature_celsius = std::nullopt) = 0;
    /**
     * @brief   Simulates how the node voltages and currents of a circuit change over time.
     * @param[in] circuit              Circuit to simulate, already checked.
     * @param[in] settings             Stop time and time step, already checked.
     * @param[in] temperature_celsius  Temperature of the run in degrees Celsius, or none for the default one.
     * @param[in] monitor              Where to look for a stop request and report progress, and who receives the points
     *                                 as they are made; a backend that cannot split the run ignores it.
     * @return  The values at every time point or an error, plus the messages of the backend. A run that was stopped
     *          returns the points made until then, with IsComplete false.
     */
    virtual TransientRun RunTransient(const Circuit &circuit, const TransientSettings &settings,
                                      const std::optional<double> &temperature_celsius, const RunMonitor &monitor) = 0;
    /**
     * @brief   Sweeps the small-signal response of a circuit across frequency.
     * @param[in] circuit              Circuit to simulate, already checked.
     * @param[in] settings             Frequency range and points per decade, already checked.
     * @param[in] temperature_celsius  Temperature of the run in degrees Celsius, or none for the default one.
     * @return  The magnitude and phase of every node and current at every frequency or an error, plus the messages of
     *          the backend.
     */
    virtual ACSweepRun RunACSweep(const Circuit &circuit, const ACSweepSettings &settings,
                                  const std::optional<double> &temperature_celsius) = 0;
    /**
     * @brief   Sweeps a source through a range of DC values and finds the operating point at each one.
     * @param[in] circuit              Circuit to simulate, already checked.
     * @param[in] settings             Swept source and range, and optionally a second source stepped once per curve.
     * @param[in] temperature_celsius  Temperature of the run in degrees Celsius, or none for the default one.
     * @return  The node voltages and currents along the sweep, one curve per stepped value, or an error, plus the
     *          messages of the backend.
     */
    virtual DCSweepRun RunDCSweep(const Circuit &circuit, const DCSweepSettings &settings,
                                  const std::optional<double> &temperature_celsius) = 0;
    /**
     * @brief   Computes the noise of a circuit at a node across frequency, and refers it to an input source.
     * @param[in] circuit   Circuit to simulate, already checked.
     * @param[in] settings  Output node, input source and frequency range, already checked.
     * @return  The output and input-referred noise densities, their totals and the share of every part, or an error,
     *          plus the messages of the backend.
     */
    virtual NoiseRun RunNoise(const Circuit &circuit, const NoiseSettings &settings) = 0;
    /**
     * @brief   Finds the small-signal DC transfer function from an input source to an output, with both impedances.
     * @param[in] circuit   Circuit to simulate, already checked.
     * @param[in] settings  Output node and input source, already checked.
     * @return  The DC gain, the input impedance and the output impedance, or an error, plus the messages of the
     *          backend.
     */
    virtual TransferFunctionRun RunTransferFunction(const Circuit &circuit,
                                                    const TransferFunctionSettings &settings) = 0;
    /**
     * @brief   Finds how much the DC voltage of a node changes with the value of each part of the circuit.
     * @param[in] circuit   Circuit to simulate, already checked.
     * @param[in] settings  Output node, already checked.
     * @return  The sensitivity of every part with a value, or an error, plus the messages of the backend.
     */
    virtual SensitivityRun RunSensitivity(const Circuit &circuit, const SensitivitySettings &settings) = 0;
    /**
     * @brief   Finds the poles and zeros of the transfer function from an input source to the voltage of a node.
     * @param[in] circuit   Circuit to simulate, already checked.
     * @param[in] settings  Output node and input source, already checked.
     * @return  The poles and zeros, or an error, plus the messages of the backend.
     */
    virtual PoleZeroRun RunPoleZero(const Circuit &circuit, const PoleZeroSettings &settings) = 0;
    /**
     * @brief   Measures the scattering matrix of the ports of a circuit across frequency.
     * @param[in] circuit   Circuit to simulate, already checked: it holds at least two ports, numbered 1 to N.
     * @param[in] settings  Frequency range and points, already checked.
     * @return  The matrix at every frequency or an error, plus the messages of the backend.
     */
    virtual SParametersRun RunSParameters(const Circuit &circuit, const SParameterSettings &settings) = 0;

    virtual HarmonicBalanceRun RunHarmonicBalance(const Circuit &circuit, const HarmonicBalanceSettings &settings,
                                                  RunControl *control);
};

// Backend in use
SimulationBackend &GetBackend();
void SetBackend(std::unique_ptr<SimulationBackend> backend);

// Capabilities
std::string_view GetAnalysisKindName(AnalysisKind kind);
std::optional<std::string> FindUnsupported(const SimulationBackend &backend, const std::set<ComponentType> &parts,
                                           AnalysisKind kind);
std::optional<std::string> FindUnsupported(const SimulationBackend &backend, const Circuit &circuit, AnalysisKind kind);

} // namespace Core

#endif // IMCSIM_SIMULATION_BACKEND_H
