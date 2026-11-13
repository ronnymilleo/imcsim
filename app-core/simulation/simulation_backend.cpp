/**
 * @file    simulation_backend.cpp
 * @brief   The seam between the simulator and the program that solves a circuit, and what each backend can run.
 */

#include "simulation/simulation_backend.h"

#include <format>
#include <mutex>
#include <utility>

namespace Core {

namespace {

/// What every run of the stand-in backend reports.
constexpr std::string_view NO_BACKEND_REASON = "No simulation engine was installed";

/// The backend in use; made by the default factory on first use, by whichever thread asks first.
std::unique_ptr<SimulationBackend> g_backend;
/// Makes the backend that g_backend becomes when none was set, or null before the program registers one.
BackendFactory g_default_factory = nullptr;
/// Guards g_backend and g_default_factory, which any thread may ask for.
std::mutex g_backend_mutex;

/**
 * @brief   Stands in when no default backend was registered, so the mistake shows up as a reason on every analysis
 *          instead of a crash.
 */
class NoBackend : public SimulationBackend {
public:
    std::string_view GetName() const override { return "The missing simulation engine"; }
    const BackendCapabilities &GetCapabilities() const override { return m_Capabilities; }

    OperatingPointRun RunOperatingPoint(const Circuit &, const std::optional<double> &) override {
        return Fail<OperatingPointRun>();
    }
    TransientRun RunTransient(const Circuit &, const TransientSettings &, const std::optional<double> &,
                              const RunMonitor &) override {
        return Fail<TransientRun>();
    }
    ACSweepRun RunACSweep(const Circuit &, const ACSweepSettings &, const std::optional<double> &) override {
        return Fail<ACSweepRun>();
    }
    DCSweepRun RunDCSweep(const Circuit &, const DCSweepSettings &, const std::optional<double> &) override {
        return Fail<DCSweepRun>();
    }
    NoiseRun RunNoise(const Circuit &, const NoiseSettings &) override { return Fail<NoiseRun>(); }
    TransferFunctionRun RunTransferFunction(const Circuit &, const TransferFunctionSettings &) override {
        return Fail<TransferFunctionRun>();
    }
    SensitivityRun RunSensitivity(const Circuit &, const SensitivitySettings &) override {
        return Fail<SensitivityRun>();
    }
    PoleZeroRun RunPoleZero(const Circuit &, const PoleZeroSettings &) override { return Fail<PoleZeroRun>(); }
    SParametersRun RunSParameters(const Circuit &, const SParameterSettings &) override {
        return Fail<SParametersRun>();
    }

private:
    template <typename Run> static Run Fail() {
        Run run;
        run.Result = std::unexpected(std::string(NO_BACKEND_REASON));
        return run;
    }

    /// No analysis is listed, so FindUnsupported() refuses each one with this backend's name.
    BackendCapabilities m_Capabilities;
};

/**
 * @brief   The truth table asks the backend only for operating points.
 */
AnalysisKind GetRequiredAnalysis(const AnalysisKind kind) {
    return kind == AnalysisKind::TRUTH_TABLE ? AnalysisKind::OPERATING_POINT : kind;
}

} // namespace

/**
 * @brief   Returns the backend that runs the simulations.
 * @return  The backend set with SetBackend(), or the one the default factory makes when none was set. When no
 *          factory was registered either, a backend that runs no analysis and says why.
 * @note    A program registers the default factory once at startup, with SetDefaultBackendFactory().
 */
SimulationBackend &GetBackend() {
    const std::lock_guard lock(g_backend_mutex);
    if (!g_backend && g_default_factory != nullptr) {
        g_backend = g_default_factory();
    }
    if (!g_backend) {
        // Not kept in g_backend, so a factory registered later still takes effect
        static NoBackend no_backend;
        return no_backend;
    }
    return *g_backend;
}

/**
 * @brief   Replaces the backend that runs the simulations, such as a fake one in a test.
 * @param[in] backend  The new backend, which the simulator owns from now on; null goes back to the default backend.
 * @note    Call it while no simulation runs, since the one in progress uses the old backend.
 */
void SetBackend(std::unique_ptr<SimulationBackend> backend) {
    const std::lock_guard lock(g_backend_mutex);
    g_backend = std::move(backend);
}

/**
 * @brief   Registers the function that makes the default backend, the one in use until SetBackend() sets another.
 * @param[in] factory  Makes a new backend each time it is called, or null to register none.
 * @note    Call it before anything asks for the backend, and while no simulation runs.
 */
void SetDefaultBackendFactory(const BackendFactory factory) {
    const std::lock_guard lock(g_backend_mutex);
    g_default_factory = factory;
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
    case AnalysisKind::SENSITIVITY:
        return "sensitivity";
    case AnalysisKind::POLE_ZERO:
        return "pole-zero";
    case AnalysisKind::S_PARAMETERS:
        return "S-parameter";
    case AnalysisKind::TRUTH_TABLE:
        return "truth table";
    case AnalysisKind::HARMONIC_BALANCE:
        return "harmonic balance";
    default:
        return "analysis";
    }
}

/**
 * @brief   Finds periodic steady-state node voltages and currents at the fundamental and its harmonics.
 * @param[in] circuit   Circuit to simulate, already checked.
 * @param[in] settings  Fundamental frequency, harmonic count, and optional sample count, already checked.
 * @param[in] control   Where to look for a stop request between iterations, or null.
 * @return  The harmonic spectra and one-cycle waveforms or an error, plus the messages of the backend.
 * @note    A backend that runs no harmonic balance keeps this default, which says so.
 */
HarmonicBalanceRun SimulationBackend::RunHarmonicBalance(const Circuit & /*circuit*/,
                                                         const HarmonicBalanceSettings & /*settings*/,
                                                         RunControl * /*control*/) {
    HarmonicBalanceRun run;
    run.Result = std::unexpected(std::format("{} does not run the harmonic balance analysis", GetName()));
    return run;
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
    const auto in_analysis = capabilities.UnsupportedPartsByAnalysis.find(kind);
    if (in_analysis == capabilities.UnsupportedPartsByAnalysis.end()) {
        return std::nullopt;
    }
    for (const ComponentType part : parts) {
        const auto unsupported = in_analysis->second.find(part);
        if (unsupported != in_analysis->second.end()) {
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
    const std::vector<const Component *> all_parts = CollectParts(circuit);
    for (const Component *part : all_parts) {
        parts.insert(part->GetType());
    }
    if (const std::optional<std::string> reason = FindUnsupported(backend, parts, kind)) {
        return reason;
    }
    const auto &find_problem = backend.GetCapabilities().FindPartProblem;
    if (!find_problem) {
        return std::nullopt;
    }
    for (const Component *part : all_parts) {
        if (const std::optional<std::string> reason = find_problem(*part, kind)) {
            return reason;
        }
    }
    return std::nullopt;
}

} // namespace Core
