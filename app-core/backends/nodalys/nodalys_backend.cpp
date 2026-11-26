/**
 * @file    nodalys_backend.cpp
 * @brief   The Nodalys simulation backend, which solves circuits with the autonomous Nodalys engine.
 */

#include "backends/nodalys/nodalys_backend.h"

#include "backends/nodalys/compiler.h"
#include "backends/nodalys/nodalys_results.h"
#include "circuit/components/behavioral_source.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/source.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/transmission_line.h"
#include "simulation/sensitivity.h"
#include <chrono>
#include <format>
#include <nodalys/nodalys.h>
#include <utility>

namespace Core {

namespace {

constexpr auto NODALYS_ANALYSES = {AnalysisKind::OPERATING_POINT,
                                   AnalysisKind::TRANSIENT,
                                   AnalysisKind::DC_SWEEP,
                                   AnalysisKind::AC_SWEEP,
                                   AnalysisKind::NOISE,
                                   AnalysisKind::TRANSFER_FUNCTION,
                                   AnalysisKind::SENSITIVITY,
                                   AnalysisKind::AC_SENSITIVITY,
                                   AnalysisKind::POLE_ZERO,
                                   AnalysisKind::S_PARAMETERS,
                                   AnalysisKind::HARMONIC_BALANCE};

/// The temperature a run simulates at unless it is stepped, which the models are given at unless tnom says otherwise.
constexpr double SIMULATION_TEMPERATURE_CELSIUS = 27.0;

/**
 * @brief   Tells why Nodalys cannot run the expression of a behavioral source: a function or a variable it has not, or
 *          a time-dependent expression in the harmonic balance, which evaluates the circuit at samples without the
 *          instant.
 * @details The engine reads the same text as ngspice, so this is only what ngspice reads and Nodalys does not.
 */
std::optional<std::string> FindBehavioralProblem(const Component &part, const AnalysisKind kind) {
    if (!IsBehavioralSource(part.GetType())) {
        return std::nullopt;
    }
    const auto expression = Nodalys::Expression::Parse(static_cast<const BehavioralSource &>(part).GetExpression());
    if (!expression) {
        return std::format("Nodalys cannot read the expression of {}: {}", part.GetName(), expression.error());
    }
    if (kind == AnalysisKind::HARMONIC_BALANCE && expression->IsTimeDependent()) {
        return std::format("Nodalys harmonic balance cannot run {}: its expression reads time", part.GetName());
    }
    return std::nullopt;
}

/**
 * @brief   Tells why Nodalys cannot run a part whose limit depends on what it holds: the expression of a behavioral
 *          source, or the text of a block kept as SPICE text.
 */
std::optional<std::string> FindPartProblem(const Circuit &circuit, const Component &part, const AnalysisKind kind) {
    if (part.GetType() == ComponentType::SUBCIRCUIT) {
        return FindSpiceBlockProblem(circuit, static_cast<const SubcircuitInstance &>(part));
    }
    if (part.GetType() == ComponentType::TRANSMISSION_LINE && kind == AnalysisKind::TRANSIENT
        && static_cast<const TransmissionLine &>(part).GetParameters().IsLossy()) {
        return std::format("Nodalys transient analysis supports lossless transmission lines only; {} has conductor or "
                           "dielectric loss, which is simulated in frequency-domain and Harmonic Balance analyses",
                           part.GetName());
    }
    return FindBehavioralProblem(part, kind);
}

/**
 * @brief   The tolerances of the circuit as Nodalys reads them.
 */
Nodalys::ConvergenceOptions ToConvergenceOptions(const Circuit &circuit) {
    const SolverOptions &solver = circuit.GetSolverOptions();
    Nodalys::ConvergenceOptions options;
    options.RelTol = solver.RelativeTolerance;
    options.CurrentAbsTol = solver.CurrentToleranceAmperes;
    options.VoltageAbsTol = solver.VoltageToleranceVolts;
    return options;
}

/**
 * @brief   The integration method of a transient of the circuit.
 * @details A fuse that cuts an inductor makes the trapezoidal rule ring for ever, so the netlist for ngspice asks for
 *          Gear when the user leaves the method to the program, and Nodalys follows it.
 */
Nodalys::IntegrationMethod ToNodalysMethod(const Circuit &circuit) {
    switch (circuit.GetSolverOptions().Method) {
    case IntegrationMethod::TRAPEZOIDAL:
        return Nodalys::IntegrationMethod::TRAPEZOIDAL;
    case IntegrationMethod::GEAR:
        return Nodalys::IntegrationMethod::GEAR2;
    case IntegrationMethod::DEFAULT:
    default:
        return (circuit.HasFuse() || circuit.HasThyristor()) ? Nodalys::IntegrationMethod::GEAR2
                                                             : Nodalys::IntegrationMethod::TRAPEZOIDAL;
    }
}

/**
 * @brief   Compiles a circuit, runs one Nodalys analysis on it and turns the result into the one of imcsim.
 * @details Every analysis goes the same way, so a compile failure, the messages of the engine and its failures read
 *          alike whatever the analysis.
 * @param[in] circuit              Circuit to simulate.
 * @param[in] compute              Runs the analysis on the compiled circuit, filling the messages it is handed.
 * @param[in] convert              Turns the result of Nodalys into the result of imcsim, or into the reason it has
 *                                 none.
 * @param[in] temperature_celsius  Simulation temperature in degrees Celsius, or none for the nominal 27 C.
 */
template <typename Run, typename Compute, typename Convert>
Run RunOnNodalys(const Circuit &circuit,
                 Compute compute,
                 Convert convert,
                 const std::optional<double> &temperature_celsius = std::nullopt) {
    Run run;
    std::vector<std::string> compile_warnings;
    std::expected<Nodalys::Circuit, std::string> compiled = CompileCircuit(circuit, &compile_warnings);
    if (!compiled) {
        run.Result = std::unexpected(std::format("Nodalys cannot simulate this circuit: {}", compiled.error()));
        return run;
    }
    for (std::string &warning : compile_warnings) {
        run.Messages.push_back({std::move(warning), false});
    }
    const double nominal_celsius = circuit.GetSolverOptions().NominalTemperatureCelsius;
    if (temperature_celsius || nominal_celsius != DEFAULT_NOMINAL_TEMPERATURE_CELSIUS) {
        compiled->SetTemperature(temperature_celsius.value_or(SIMULATION_TEMPERATURE_CELSIUS), nominal_celsius);
    }
    std::vector<Nodalys::DiagnosticMessage> messages;
    auto result = compute(*compiled, &messages);
    for (Nodalys::DiagnosticMessage &message : messages) {
        run.Messages.push_back({std::move(message.Text), message.IsError});
    }
    if (!result) {
        run.Result = std::unexpected(std::move(result.error()));
        return run;
    }
    run.Result = convert(std::move(*result));
    return run;
}

} // namespace

/**
 * @brief   Sets up what Nodalys runs and the parts it cannot simulate, each with the reason shown to the user.
 */
NodalysBackend::NodalysBackend() {
    m_Capabilities.Analyses = NODALYS_ANALYSES;
    m_Capabilities.SupportsConcurrentRuns = true;
    m_Capabilities.FindPartProblem = FindPartProblem;
    for (int index = 0; index <= static_cast<int>(LAST_COMPONENT_TYPE); ++index) {
        const auto type = static_cast<ComponentType>(index);
        if (IsLogicPart(type)) {
            m_Capabilities.UnsupportedPartsByAnalysis[AnalysisKind::SENSITIVITY][type] =
                "Nodalys cannot run a sensitivity analysis on logic gates and flip-flops";
            m_Capabilities.UnsupportedPartsByAnalysis[AnalysisKind::AC_SENSITIVITY][type] =
                "Nodalys cannot run an AC sensitivity analysis on logic gates and flip-flops";
            m_Capabilities.UnsupportedPartsByAnalysis[AnalysisKind::POLE_ZERO][type] =
                "Pole-zero analysis is small-signal, and digital parts have no small-signal model";
            m_Capabilities.UnsupportedPartsByAnalysis[AnalysisKind::S_PARAMETERS][type] =
                "S-parameters are small-signal, and digital parts have no small-signal RF model";
        }
    }
    m_Capabilities.UnsupportedPartsByAnalysis[AnalysisKind::HARMONIC_BALANCE][ComponentType::V_SWITCH] =
        "Nodalys harmonic balance does not support hysteretic switches";
    m_Capabilities.UnsupportedPartsByAnalysis[AnalysisKind::HARMONIC_BALANCE][ComponentType::RELAY] =
        "Nodalys harmonic balance does not support hysteretic relays";
    m_Capabilities.UnsupportedPartsByAnalysis[AnalysisKind::HARMONIC_BALANCE][ComponentType::THYRISTOR] =
        "Nodalys harmonic balance does not support the latching thyristor";
    m_Capabilities.UnsupportedPartsByAnalysis[AnalysisKind::HARMONIC_BALANCE][ComponentType::TRIAC] =
        "Nodalys harmonic balance does not support the latching TRIAC";
    m_Capabilities.UnsupportedPartsByAnalysis[AnalysisKind::HARMONIC_BALANCE][ComponentType::DIAC] =
        "Nodalys harmonic balance does not support the latching DIAC";
    m_Capabilities.UnsupportedPartsByAnalysis[AnalysisKind::POLE_ZERO][ComponentType::THYRISTOR] =
        "Pole-zero analysis is small-signal, and a thyristor blocks nearly open or conducts as a switch, whichever "
        "the circuit last drove, so its frozen state has no roots the solver can place";
    m_Capabilities.UnsupportedPartsByAnalysis[AnalysisKind::POLE_ZERO][ComponentType::TRIAC] =
        "Pole-zero analysis is small-signal, and a TRIAC blocks nearly open or conducts as a switch, whichever "
        "the circuit last drove, so its frozen state has no roots the solver can place";
    m_Capabilities.UnsupportedPartsByAnalysis[AnalysisKind::POLE_ZERO][ComponentType::DIAC] =
        "Pole-zero analysis is small-signal, and a DIAC blocks nearly open or conducts as a switch, whichever "
        "the circuit last drove, so its frozen state has no roots the solver can place";
    m_Capabilities.UnsupportedPartsByAnalysis[AnalysisKind::POLE_ZERO][ComponentType::TRANSMISSION_LINE] =
        "Pole-zero analysis does not support transmission lines, which have infinitely many poles and zeros";
}

/**
 * @brief   Names the backend in the messages that say what it cannot run.
 * @return  "Nodalys".
 */
std::string_view NodalysBackend::GetName() const {
    return "Nodalys";
}

/**
 * @brief   Tells which analyses Nodalys runs and which parts it cannot simulate; set once, so safe from any thread.
 * @return  The capabilities.
 */
const BackendCapabilities &NodalysBackend::GetCapabilities() const {
    return m_Capabilities;
}

/**
 * @brief   Computes the DC operating point of a circuit.
 * @param[in] circuit              Circuit to simulate, already checked.
 * @param[in] temperature_celsius  Simulation temperature in degrees Celsius, or none for 27 C.
 * @return  The node voltages and currents or an error, plus the messages of Nodalys.
 */
OperatingPointRun NodalysBackend::RunOperatingPoint(const Circuit &circuit,
                                                    const std::optional<double> &temperature_celsius) {
    return RunOnNodalys<OperatingPointRun>(
        circuit,
        [options = ToConvergenceOptions(circuit)](const Nodalys::Circuit &compiled,
                                                  std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeOperatingPoint(compiled, options, messages);
        },
        [&circuit](Nodalys::OperatingPointResult result) { return ToOperatingPoint(circuit, std::move(result)); },
        temperature_celsius);
}

/**
 * @brief   Simulates how the node voltages and currents of a circuit change over time.
 * @param[in] circuit              Circuit to simulate, already checked.
 * @param[in] settings             Stop time, time step and optional random seed.
 * @param[in] temperature_celsius  Simulation temperature in degrees Celsius, or none for 27 C.
 * @param[in] monitor              Stop request, progress, chunks, live changes and pace of the run.
 * @return  The voltages and currents at every time point or an error, plus the messages of Nodalys.
 */
TransientRun NodalysBackend::RunTransient(const Circuit &circuit,
                                          const TransientSettings &settings,
                                          const std::optional<double> &temperature_celsius,
                                          const RunMonitor &monitor) {
    const Nodalys::TransientConfig config{.StopTime = settings.StopTime,
                                          .TimeStep = settings.TimeStep,
                                          .MaxStep = settings.TimeStep,
                                          .Method = ToNodalysMethod(circuit),
                                          .Seed = settings.Seed};
    const bool has_initial_conditions = circuit.HasInitialConditions();
    auto live_start = std::chrono::steady_clock::now();
    const Nodalys::RunMonitor nodalys_monitor = ToNodalysMonitor(monitor, live_start, circuit);
    return RunOnNodalys<TransientRun>(
        circuit,
        [&, options = ToConvergenceOptions(circuit)](const Nodalys::Circuit &compiled,
                                                     std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeTransient(compiled,
                                             config,
                                             has_initial_conditions,
                                             nodalys_monitor,
                                             options,
                                             messages);
        },
        [&circuit, has_initial_conditions](Nodalys::TransientResult result) {
            return ToFinishedTransient(circuit, std::move(result), has_initial_conditions);
        },
        temperature_celsius);
}

/**
 * @brief   Sweeps a source through a range of DC values, once per value of a second stepped source if there is one.
 * @param[in] circuit              Circuit to simulate, already checked.
 * @param[in] settings             Swept source and range, and the stepped one.
 * @param[in] temperature_celsius  Simulation temperature in degrees Celsius, or none for 27 C.
 * @return  One curve per stepped value or an error, plus the messages of Nodalys.
 */
DCSweepRun NodalysBackend::RunDCSweep(const Circuit &circuit,
                                      const DCSweepSettings &settings,
                                      const std::optional<double> &temperature_celsius) {
    Nodalys::DcSweepConfig config{.SweptSource = settings.Swept.Source,
                                  .SweptStart = settings.Swept.Start,
                                  .SweptStop = settings.Swept.Stop,
                                  .SweptStep = settings.Swept.Step};
    if (settings.Stepped) {
        config.SteppedSource = settings.Stepped->Source;
        config.SteppedStart = settings.Stepped->Start;
        config.SteppedStop = settings.Stepped->Stop;
        config.SteppedStep = settings.Stepped->Step;
    }
    return RunOnNodalys<DCSweepRun>(
        circuit,
        [&config, options = ToConvergenceOptions(circuit)](const Nodalys::Circuit &compiled,
                                                           std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeDCSweep(compiled, config, options, messages);
        },
        [node_count = circuit.GetNodeCount()](Nodalys::DcSweepResult result) {
            return ToDCSweep(std::move(result), node_count);
        },
        temperature_celsius);
}

/**
 * @brief   Sweeps the small-signal response of a circuit across frequency.
 * @param[in] circuit              Circuit to simulate, already checked.
 * @param[in] settings             Frequency range and points per decade.
 * @param[in] temperature_celsius  Simulation temperature in degrees Celsius, or none for 27 C.
 * @return  The magnitude and phase of every node and current or an error, plus the messages of Nodalys.
 */
ACSweepRun NodalysBackend::RunACSweep(const Circuit &circuit,
                                      const ACSweepSettings &settings,
                                      const std::optional<double> &temperature_celsius) {
    const Nodalys::AcSweepConfig config{.StartFrequency = settings.StartFrequency,
                                        .StopFrequency = settings.StopFrequency,
                                        .PointsPerDecade = settings.PointsPerDecade};
    return RunOnNodalys<ACSweepRun>(
        circuit,
        [&config, options = ToConvergenceOptions(circuit)](const Nodalys::Circuit &compiled,
                                                           std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeACSweep(compiled, config, options, messages);
        },
        [node_count = circuit.GetNodeCount()](Nodalys::AcSweepResult result) {
            return ToACSweep(std::move(result), node_count);
        },
        temperature_celsius);
}

/**
 * @brief   Computes the noise at a node across frequency and refers it to an input source.
 * @param[in] circuit   Circuit to simulate, already checked.
 * @param[in] settings  Output node, input source and frequency range.
 * @return  The densities, totals and the share of each part or an error, plus the messages of Nodalys.
 */
NoiseRun NodalysBackend::RunNoise(const Circuit &circuit, const NoiseSettings &settings) {
    const Nodalys::NoiseConfig config{.OutputPositiveNode = settings.OutputNode,
                                      .OutputNegativeNode = 0,
                                      .InputSource = settings.InputSource,
                                      .StartFrequency = settings.StartFrequency,
                                      .StopFrequency = settings.StopFrequency,
                                      .PointsPerDecade = settings.PointsPerDecade};
    return RunOnNodalys<NoiseRun>(
        circuit,
        [&config, options = ToConvergenceOptions(circuit)](const Nodalys::Circuit &compiled,
                                                           std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeNoise(compiled, config, options, messages);
        },
        [](Nodalys::NoiseResult result) { return ToNoise(std::move(result)); });
}

/**
 * @brief   Computes the small-signal DC gain from a source to a node and the input and output resistances.
 * @param[in] circuit   Circuit to simulate, already checked.
 * @param[in] settings  Input source and output node.
 * @return  The gain and resistances or an error, plus the messages of Nodalys.
 */
TransferFunctionRun NodalysBackend::RunTransferFunction(const Circuit &circuit,
                                                        const TransferFunctionSettings &settings) {
    const Nodalys::TransferFunctionConfig config{.InputSource = settings.InputSource,
                                                 .OutputPositiveNode = settings.OutputNode,
                                                 .OutputNegativeNode = 0};
    return RunOnNodalys<TransferFunctionRun>(
        circuit,
        [&config, options = ToConvergenceOptions(circuit)](const Nodalys::Circuit &compiled,
                                                           std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeTransferFunction(compiled, config, options, messages);
        },
        [](Nodalys::TransferFunctionResult result) { return ToTransferFunction(std::move(result)); });
}

/**
 * @brief   Finds how much the DC voltage of a node changes with the value of each part.
 * @param[in] circuit   Circuit to simulate.
 * @param[in] settings  Output node.
 * @return  The sensitivity of each part, or an error.
 */
SensitivityRun NodalysBackend::RunSensitivity(const Circuit &circuit, const SensitivitySettings &settings) {
    const Nodalys::SensitivityConfig config{.OutputPositiveNode = settings.OutputNode, .OutputNegativeNode = 0};
    return RunOnNodalys<SensitivityRun>(
        circuit,
        [&config, options = ToConvergenceOptions(circuit)](const Nodalys::Circuit &compiled,
                                                           std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeSensitivity(compiled, config, options, messages);
        },
        [&circuit, &settings](Nodalys::SensitivityResult result) {
            return BuildSensitivity(circuit, settings.OutputNode, result.Vectors);
        });
}

/**
 * @brief   Finds how much the response of a node to an AC source changes with the value of each part.
 * @param[in] circuit   Circuit to simulate.
 * @param[in] settings  Output node, AC input source and frequency range.
 * @return  The figures of each part at every frequency, or an error.
 */
ACSensitivityRun NodalysBackend::RunACSensitivity(const Circuit &circuit, const ACSensitivitySettings &settings) {
    const Nodalys::AcSensitivityConfig config{.OutputPositiveNode = settings.OutputNode,
                                              .OutputNegativeNode = 0,
                                              .InputSource = settings.InputSource,
                                              .StartFrequency = settings.StartFrequency,
                                              .StopFrequency = settings.StopFrequency,
                                              .PointsPerDecade = settings.PointsPerDecade};
    return RunOnNodalys<ACSensitivityRun>(
        circuit,
        [&config, options = ToConvergenceOptions(circuit)](const Nodalys::Circuit &compiled,
                                                           std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeAcSensitivity(compiled, config, options, messages);
        },
        [&circuit, &settings](Nodalys::AcSensitivityResult result) {
            return ToACSensitivity(circuit, settings, std::move(result));
        });
}

/**
 * @brief   Finds the poles and zeros of the transfer function from an input source to a node voltage.
 * @param[in] circuit   Circuit to simulate.
 * @param[in] settings  Output node and input source.
 * @return  The poles and zeros, or an error, also when the solver found none at all.
 */
PoleZeroRun NodalysBackend::RunPoleZero(const Circuit &circuit, const PoleZeroSettings &settings) {
    const Nodalys::PoleZeroConfig config{.OutputNode = settings.OutputNode,
                                         .InputSource = settings.InputSource,
                                         .FindPoles = true,
                                         .FindZeros = true};
    return RunOnNodalys<PoleZeroRun>(
        circuit,
        [&config, options = ToConvergenceOptions(circuit)](const Nodalys::Circuit &compiled,
                                                           std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputePoleZero(compiled, config, options, messages);
        },
        [&circuit, &settings](const Nodalys::PoleZeroResult &result) { return ToPoleZero(circuit, settings, result); });
}

/**
 * @brief   Measures the scattering matrix of the ports of a circuit across frequency.
 * @param[in] circuit   Circuit to simulate.
 * @param[in] settings  Frequency range and points.
 * @return  The S-parameter matrix at each frequency, or an error.
 */
SParametersRun NodalysBackend::RunSParameters(const Circuit &circuit, const SParameterSettings &settings) {
    Nodalys::SParameterConfig config{
        .SweepType = settings.IsLogarithmic ? Nodalys::FrequencySweepType::DECADE : Nodalys::FrequencySweepType::LINEAR,
        .StartFrequency = settings.StartFrequency,
        .StopFrequency = settings.StopFrequency,
        .Points = settings.IsLogarithmic ? settings.PointsPerDecade : settings.LinearPoints,
        .Ports = {}};
    for (const CircuitPort &port : circuit.GetPorts()) {
        config.Ports.push_back({.Number = port.Number, .Node = port.Node, .ReferenceOhms = port.ReferenceOhms});
    }
    return RunOnNodalys<SParametersRun>(
        circuit,
        [&config, options = ToConvergenceOptions(circuit)](const Nodalys::Circuit &compiled,
                                                           std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeSParameters(compiled, config, options, messages);
        },
        [](Nodalys::SParameterResult result) { return ToSParameters(std::move(result)); });
}

/**
 * @brief   Finds the periodic steady state of a circuit at a fundamental and its harmonics.
 * @param[in]     circuit   Circuit to simulate, already checked.
 * @param[in]     settings  Fundamental frequency, harmonic count and sample count.
 * @param[in,out] control   Where the run looks for a stop request between its Newton iterations, or null.
 * @return  The spectra and one-cycle waveforms or an error, plus the messages of Nodalys.
 */
HarmonicBalanceRun NodalysBackend::RunHarmonicBalance(const Circuit &circuit,
                                                      const HarmonicBalanceSettings &settings,
                                                      RunControl *control) {
    Nodalys::HarmonicBalanceConfig config{.FundamentalFrequency = settings.FundamentalFrequency,
                                          .Harmonics = settings.Harmonics,
                                          .Samples = settings.Samples};
    if (control != nullptr) {
        config.ShouldStop = [control]() { return control->IsCancelled(); };
    }
    return RunOnNodalys<HarmonicBalanceRun>(
        circuit,
        [&config, options = ToConvergenceOptions(circuit)](const Nodalys::Circuit &compiled,
                                                           std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeHarmonicBalance(compiled, config, options, messages);
        },
        [&settings, node_count = circuit.GetNodeCount()](Nodalys::HarmonicBalanceResult result) {
            return ToHarmonicBalance(settings, std::move(result), node_count);
        });
}

} // namespace Core
