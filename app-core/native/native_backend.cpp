/**
 * @file    native_backend.cpp
 * @brief   The Nodalys simulation backend, which solves circuits with the native solver of imcsim.
 */

#include "native/native_backend.h"

#include "native/analyses.h"
#include "native/compiler.h"
#include <format>
#include <utility>

namespace Core {

namespace {

/// The analyses Nodalys runs so far; the rest of the seam returns a clear "not yet".
constexpr auto NATIVE_ANALYSES = {AnalysisKind::OPERATING_POINT, AnalysisKind::TRANSIENT, AnalysisKind::DC_SWEEP};

/// Why the logic parts are not runnable yet, one reason for every gate and flip-flop type.
constexpr const char *DIGITAL_REASON = "Nodalys has no event-driven digital engine yet; the logic parts "
                                       "run with the ngspice backend";

/**
 * @brief   Compiles the circuit or turns the reason it cannot be compiled into the failure of the run.
 */
std::expected<Native::CompiledCircuit, std::string> CompileOrFail(const Circuit &circuit) {
    std::expected<Native::CompiledCircuit, std::string> compiled = Native::CompileCircuit(circuit);
    if (!compiled) {
        return std::unexpected(std::format("Nodalys cannot simulate this circuit: {}", compiled.error()));
    }
    return std::move(*compiled);
}

} // namespace

/**
 * @brief   Creates the backend with its capabilities.
 */
NativeBackend::NativeBackend() {
    m_Capabilities.Analyses = NATIVE_ANALYSES;
    for (const ComponentType type :
         {ComponentType::AND, ComponentType::OR, ComponentType::NOT, ComponentType::NAND, ComponentType::NOR,
          ComponentType::XOR, ComponentType::XNOR, ComponentType::BUFFER, ComponentType::D_FLIP_FLOP,
          ComponentType::JK_FLIP_FLOP, ComponentType::T_FLIP_FLOP, ComponentType::SR_FLIP_FLOP}) {
        m_Capabilities.UnsupportedParts[type] = DIGITAL_REASON;
    }
}

/**
 * @brief   Names the backend in the messages that say what it cannot run.
 * @return  "Nodalys".
 */
std::string_view NativeBackend::GetName() const {
    return "Nodalys";
}

/**
 * @brief   Tells what Nodalys runs.
 * @return  The operating point, the transient and the DC sweep, with the logic parts listed as unsupported.
 * @note    Fixed at construction, so any thread may ask any time.
 */
const BackendCapabilities &NativeBackend::GetCapabilities() const {
    return m_Capabilities;
}

OperatingPointRun NativeBackend::RunOperatingPoint(const Circuit &circuit) {
    OperatingPointRun run;
    auto compiled = CompileOrFail(circuit);
    if (!compiled) {
        run.Result = std::unexpected(compiled.error());
        return run;
    }
    run.Result = Native::ComputeOperatingPoint(*compiled, {}, run.Messages);
    return run;
}

TransientRun NativeBackend::RunTransient(const Circuit &circuit, const TransientSettings &settings,
                                         const std::optional<double> &temperature_celsius, const RunMonitor &monitor) {
    (void)temperature_celsius; // The device models are evaluated at 27 °C, the nominal temperature, so far
    TransientRun run;
    auto compiled = CompileOrFail(circuit);
    if (!compiled) {
        run.Result = std::unexpected(compiled.error());
        return run;
    }
    const bool has_initial_conditions = circuit.HasInitialConditions();
    run.Result = Native::ComputeTransient(*compiled, settings, has_initial_conditions, monitor, {}, run.Messages);
    if (run.Result && has_initial_conditions) {
        run.Result->Warnings.emplace_back(
            "The transient starts from the initial conditions (uic): the parts that have one start from it and the "
            "other capacitors and inductors from zero, since the operating point is not computed first");
    }
    return run;
}

DCSweepRun NativeBackend::RunDCSweep(const Circuit &circuit, const DCSweepSettings &settings,
                                     const std::optional<double> &temperature_celsius) {
    (void)temperature_celsius;
    DCSweepRun run;
    auto compiled = CompileOrFail(circuit);
    if (!compiled) {
        run.Result = std::unexpected(compiled.error());
        return run;
    }
    run.Result = Native::ComputeDCSweep(circuit, *compiled, settings, {}, run.Messages);
    return run;
}

ACSweepRun NativeBackend::RunACSweep(const Circuit &circuit, const ACSweepSettings &settings,
                                     const std::optional<double> &temperature_celsius) {
    (void)circuit;
    (void)settings;
    (void)temperature_celsius;
    return {std::unexpected("Nodalys does not run the AC sweep yet"), {}};
}

NoiseRun NativeBackend::RunNoise(const Circuit &circuit, const NoiseSettings &settings) {
    (void)circuit;
    (void)settings;
    return {std::unexpected("Nodalys does not run the noise analysis yet"), {}};
}

TransferFunctionRun NativeBackend::RunTransferFunction(const Circuit &circuit,
                                                       const TransferFunctionSettings &settings) {
    (void)circuit;
    (void)settings;
    return {std::unexpected("Nodalys does not run the transfer function analysis yet"), {}};
}

} // namespace Core
