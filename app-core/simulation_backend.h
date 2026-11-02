/**
 * @file    simulation_backend.h
 * @brief   The seam between the simulator and the program that solves a circuit, and what each backend can run.
 * @details A backend runs a circuit that has already been checked and returns the results of one analysis. What does
 *          not depend on the solver, such as the checks of the settings, the stepped parameter and the truth table,
 *          stays in simulator.h and calls the backend in use.
 */

#ifndef IMCSIM_SIMULATION_BACKEND_H
#define IMCSIM_SIMULATION_BACKEND_H

#include "analysis_settings.h"
#include "circuit.h"
#include "run_control.h"
#include "simulation_results.h"
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
    TRUTH_TABLE
};

/**
 * @struct  BackendCapabilities
 * @brief   What a backend can simulate.
 * @details The truth table is not listed: it runs an operating point for each combination, so it needs only that.
 */
struct BackendCapabilities {
    std::set<AnalysisKind> Analyses;
    // Parts the backend cannot simulate, each with the reason shown to the user
    std::map<ComponentType, std::string> UnsupportedParts;
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
    // Where to look for a stop request and to report the share of the stop time reached, or null
    RunControl *Control = nullptr;
    // Receives the points made since the last call, from the worker thread, or empty
    std::function<void(Transient)> OnChunk;
    // Simulated seconds a live run covers per second of the clock, or 0 to run as fast as the backend goes
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

    virtual std::string_view GetName() const = 0;
    virtual const BackendCapabilities &GetCapabilities() const = 0;

    virtual OperatingPointRun RunOperatingPoint(const Circuit &circuit) = 0;
    virtual TransientRun RunTransient(const Circuit &circuit, const TransientSettings &settings,
                                      const std::optional<double> &temperature_celsius, const RunMonitor &monitor) = 0;
    virtual ACSweepRun RunACSweep(const Circuit &circuit, const ACSweepSettings &settings,
                                  const std::optional<double> &temperature_celsius) = 0;
    virtual DCSweepRun RunDCSweep(const Circuit &circuit, const DCSweepSettings &settings,
                                  const std::optional<double> &temperature_celsius) = 0;
    virtual NoiseRun RunNoise(const Circuit &circuit, const NoiseSettings &settings) = 0;
    virtual TransferFunctionRun RunTransferFunction(const Circuit &circuit,
                                                    const TransferFunctionSettings &settings) = 0;
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
