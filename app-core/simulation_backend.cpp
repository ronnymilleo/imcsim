/**
 * @file    simulation_backend.cpp
 * @brief   The seam between the simulator and the program that solves a circuit, and what each backend can run.
 */

#include "simulation_backend.h"

#include "ngspice_backend.h"
#include <format>
#include <utility>

namespace Core {

namespace {

// The backend in use; created as ngspice on first use
std::unique_ptr<SimulationBackend> g_backend;

// The truth table asks the backend only for operating points
AnalysisKind GetRequiredAnalysis(const AnalysisKind kind) {
    return kind == AnalysisKind::TRUTH_TABLE ? AnalysisKind::OPERATING_POINT : kind;
}

} // namespace

/**
 * @brief   Returns the backend that runs the simulations.
 * @return  The backend set with SetBackend(), or ngspice when none was set.
 */
SimulationBackend &GetBackend() {
    if (!g_backend) {
        g_backend = std::make_unique<NgspiceBackend>();
    }
    return *g_backend;
}

/**
 * @brief   Replaces the backend that runs the simulations, such as a fake one in a test.
 * @param[in] backend  The new backend, which the simulator owns from now on; null goes back to ngspice.
 */
void SetBackend(std::unique_ptr<SimulationBackend> backend) {
    g_backend = std::move(backend);
}

/**
 * @brief   Names an analysis for the messages that say it cannot run.
 * @param[in] kind  Analysis to name.
 * @return  Such as "operating point" or "AC sweep".
 */
std::string_view GetAnalysisKindName(const AnalysisKind kind) {
    switch (kind) {
    case AnalysisKind::OPERATING_POINT:
        return "operating point";
    case AnalysisKind::TRANSIENT:
        return "transient";
    case AnalysisKind::AC_SWEEP:
        return "AC sweep";
    case AnalysisKind::DC_SWEEP:
        return "DC sweep";
    case AnalysisKind::NOISE:
        return "noise";
    case AnalysisKind::TRANSFER_FUNCTION:
        return "transfer function";
    case AnalysisKind::TRUTH_TABLE:
        return "truth table";
    default:
        return "analysis";
    }
}

/**
 * @brief   Finds why a backend cannot run an analysis on a circuit made of some kinds of parts.
 * @param[in] backend  Backend that would run it.
 * @param[in] parts    Types of the parts in the circuit.
 * @param[in] kind     Analysis to run.
 * @return  The reason, written for the user, or no value when the backend can run it.
 */
std::optional<std::string> FindUnsupported(const SimulationBackend &backend, const std::set<ComponentType> &parts,
                                           const AnalysisKind kind) {
    const BackendCapabilities &capabilities = backend.GetCapabilities();
    if (!capabilities.Analyses.contains(GetRequiredAnalysis(kind))) {
        return std::format("{} does not run the {} analysis", backend.GetName(), GetAnalysisKindName(kind));
    }
    for (const ComponentType part : parts) {
        const auto unsupported = capabilities.UnsupportedParts.find(part);
        if (unsupported != capabilities.UnsupportedParts.end()) {
            return unsupported->second;
        }
    }
    return std::nullopt;
}

/**
 * @brief   Finds why a backend cannot run an analysis on a circuit.
 * @param[in] backend  Backend that would run it.
 * @param[in] circuit  Circuit to simulate.
 * @param[in] kind     Analysis to run.
 * @return  The reason, written for the user, or no value when the backend can run it.
 */
std::optional<std::string> FindUnsupported(const SimulationBackend &backend, const Circuit &circuit,
                                           const AnalysisKind kind) {
    std::set<ComponentType> parts;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        parts.insert(entry.Part->GetType());
    }
    return FindUnsupported(backend, parts, kind);
}

} // namespace Core
