/**
 * @file    nodalys_backend.cpp
 * @brief   The Nodalys simulation backend, which solves circuits with the autonomous Nodalys engine.
 */

#include "backends/nodalys/nodalys_backend.h"

#include "backends/nodalys/compiler.h"
#include "circuit/components/behavioral_source.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/source.h"
#include "simulation/live_pace.h"
#include "simulation/pole_zero.h"
#include "simulation/sensitivity.h"
#include <chrono>
#include <format>
#include <nodalys/nodalys.h>
#include <utility>

namespace Core {

namespace {

constexpr auto NODALYS_ANALYSES = {
    AnalysisKind::OPERATING_POINT, AnalysisKind::TRANSIENT,         AnalysisKind::DC_SWEEP,    AnalysisKind::AC_SWEEP,
    AnalysisKind::NOISE,           AnalysisKind::TRANSFER_FUNCTION, AnalysisKind::SENSITIVITY, AnalysisKind::POLE_ZERO,
    AnalysisKind::S_PARAMETERS,    AnalysisKind::HARMONIC_BALANCE};

/**
 * @brief   Keeps the rows of the nodes of the circuit and drops the rest.
 * @details Nodalys also reports the nodes it adds, one for each probe that carries a current a source reads and one
 *          for each node inside a block. ngspice gives the nodes of the circuit only, and what reads a result by node
 *          number, such as the table of the operating point, must not meet nodes that are not in the schematic.
 */
template <typename Row> void KeepCircuitNodes(std::vector<Row> &rows, const int node_count) {
    if (rows.size() > static_cast<std::size_t>(node_count)) {
        rows.resize(static_cast<std::size_t>(node_count));
    }
}

std::vector<ComponentTrace> ToTraces(std::vector<Nodalys::ComponentTrace> traces) {
    std::vector<ComponentTrace> converted;
    converted.reserve(traces.size());
    for (Nodalys::ComponentTrace &trace : traces) {
        converted.push_back({std::move(trace.Name), std::move(trace.Values)});
    }
    return converted;
}

Transient ToTransient(Nodalys::TransientResult result, const int node_count) {
    KeepCircuitNodes(result.NodeVoltages, node_count);
    return {.Times = std::move(result.Times),
            .NodeVoltages = std::move(result.NodeVoltages),
            .Currents = ToTraces(std::move(result.Currents)),
            .Warnings = std::move(result.Warnings),
            .IsComplete = result.IsComplete};
}

/**
 * @brief   The monitor Nodalys reads, wired to the stop request, the progress, the live changes, the chunks and the
 *          pace of the caller's monitor.
 * @param[in]     monitor     What the caller watches; it outlives the run.
 * @param[in,out] live_start  When the clock of a live run started, moved later by the time spent paused.
 * @param[in]     node_count  Nodes of the circuit, which the chunks keep.
 */
Nodalys::RunMonitor ToNodalysMonitor(const RunMonitor &monitor, std::chrono::steady_clock::time_point &live_start,
                                     const int node_count) {
    Nodalys::RunMonitor nodalys_monitor;
    if (monitor.Control != nullptr) {
        nodalys_monitor.ShouldStop = [&monitor]() { return monitor.Control->IsCancelled(); };
        nodalys_monitor.OnProgress = [&monitor](const double progress) { monitor.Control->SetProgress(progress); };
        nodalys_monitor.TakeLiveChanges = [&monitor]() {
            std::vector<Nodalys::LiveChange> changes;
            for (const LiveChange &change : monitor.Control->TakeChanges()) {
                const Nodalys::LiveQuantity quantity = change.Quantity == LiveQuantity::RESISTANCE
                                                           ? Nodalys::LiveQuantity::RESISTANCE
                                                           : Nodalys::LiveQuantity::DC_VOLTAGE;
                changes.push_back({.Element = change.Element, .Quantity = quantity, .Value = change.Value});
            }
            return changes;
        };
    }
    if (monitor.LivePace > 0.0) {
        nodalys_monitor.ChunkSpan = monitor.LivePace * LIVE_CHUNK_SECONDS;
        nodalys_monitor.AfterChunk = [&monitor, &live_start](const double time) {
            return KeepLivePace(live_start, time, monitor.LivePace, monitor.Control);
        };
    }
    if (monitor.OnChunk) {
        nodalys_monitor.PostTransientChunk = [&monitor, node_count](const Nodalys::TransientResult &chunk) {
            monitor.OnChunk(ToTransient(chunk, node_count));
        };
    }
    return nodalys_monitor;
}

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
 * @brief   Compiles a circuit, runs one Nodalys analysis on it and turns the result into the one of imcsim.
 * @details Every analysis goes the same way, so a compile failure, the messages of the engine and its failures read
 *          alike whatever the analysis.
 * @param[in] circuit              Circuit to simulate.
 * @param[in] compute              Runs the analysis on the compiled circuit, filling the messages it is handed.
 * @param[in] convert              Turns the result of Nodalys into the result of imcsim, or into the reason it has
 * none.
 * @param[in] temperature_celsius  Simulation temperature in degrees Celsius, or none for the nominal 27 C.
 */
template <typename Run, typename Compute, typename Convert>
Run RunOnNodalys(const Circuit &circuit, Compute compute, Convert convert,
                 const std::optional<double> &temperature_celsius = std::nullopt) {
    Run run;
    std::expected<Nodalys::Circuit, std::string> compiled = CompileCircuit(circuit);
    if (!compiled) {
        run.Result = std::unexpected(std::format("Nodalys cannot simulate this circuit: {}", compiled.error()));
        return run;
    }
    if (temperature_celsius) {
        compiled->SetTemperature(*temperature_celsius);
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
    m_Capabilities.FindPartProblem = FindBehavioralProblem;
    for (int index = 0; index <= static_cast<int>(ComponentType::SEVEN_SEGMENT); ++index) {
        const auto type = static_cast<ComponentType>(index);
        if (IsLogicPart(type)) {
            m_Capabilities.UnsupportedPartsByAnalysis[AnalysisKind::SENSITIVITY][type] =
                "Nodalys cannot run a sensitivity analysis on logic gates and flip-flops";
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
        [](const Nodalys::Circuit &compiled, std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeOperatingPoint(compiled, {}, messages);
        },
        [node_count = circuit.GetNodeCount()](Nodalys::OperatingPointResult result) {
            KeepCircuitNodes(result.NodeVoltages, node_count);
            OperatingPoint point{
                .NodeVoltages = std::move(result.NodeVoltages), .Currents = {}, .Warnings = std::move(result.Warnings)};
            for (Nodalys::ComponentCurrent &current : result.Currents) {
                point.Currents.push_back({std::move(current.Name), current.Current});
            }
            return point;
        },
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
TransientRun NodalysBackend::RunTransient(const Circuit &circuit, const TransientSettings &settings,
                                          const std::optional<double> &temperature_celsius, const RunMonitor &monitor) {
    // A fuse that cuts an inductor makes the trapezoidal rule ring for ever, so the netlist for ngspice asks for Gear
    // and Nodalys follows it
    const Nodalys::TransientConfig config{.StopTime = settings.StopTime,
                                          .TimeStep = settings.TimeStep,
                                          .MaxStep = settings.TimeStep,
                                          .Method = circuit.HasFuse() ? Nodalys::IntegrationMethod::GEAR2
                                                                      : Nodalys::IntegrationMethod::TRAPEZOIDAL,
                                          .Seed = settings.Seed};
    const bool has_initial_conditions = circuit.HasInitialConditions();
    auto live_start = std::chrono::steady_clock::now();
    const Nodalys::RunMonitor nodalys_monitor = ToNodalysMonitor(monitor, live_start, circuit.GetNodeCount());
    return RunOnNodalys<TransientRun>(
        circuit,
        [&](const Nodalys::Circuit &compiled, std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeTransient(compiled, config, has_initial_conditions, nodalys_monitor, {}, messages);
        },
        [has_initial_conditions, node_count = circuit.GetNodeCount()](Nodalys::TransientResult result) {
            Transient transient = ToTransient(std::move(result), node_count);
            if (has_initial_conditions) {
                transient.Warnings.emplace_back(
                    "The transient starts from the initial conditions (uic): the parts that have one start from it "
                    "and the other capacitors and inductors from zero, since the operating point is not computed "
                    "first");
            }
            return transient;
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
DCSweepRun NodalysBackend::RunDCSweep(const Circuit &circuit, const DCSweepSettings &settings,
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
        [&config](const Nodalys::Circuit &compiled, std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeDCSweep(compiled, config, {}, messages);
        },
        [node_count = circuit.GetNodeCount()](Nodalys::DcSweepResult result) {
            DCSweep sweep{.SweptSource = std::move(result.SweptSource),
                          .SweptUnit = std::move(result.SweptUnit),
                          .SweptValues = std::move(result.SweptValues),
                          .SteppedSource = std::move(result.SteppedSource),
                          .SteppedUnit = std::move(result.SteppedUnit),
                          .Curves = {}};
            for (Nodalys::DcSweepCurve &curve : result.Curves) {
                KeepCircuitNodes(curve.NodeVoltages, node_count);
                sweep.Curves.push_back({.StepValue = curve.StepValue,
                                        .NodeVoltages = std::move(curve.NodeVoltages),
                                        .Currents = ToTraces(std::move(curve.Currents))});
            }
            return sweep;
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
ACSweepRun NodalysBackend::RunACSweep(const Circuit &circuit, const ACSweepSettings &settings,
                                      const std::optional<double> &temperature_celsius) {
    const Nodalys::AcSweepConfig config{.StartFrequency = settings.StartFrequency,
                                        .StopFrequency = settings.StopFrequency,
                                        .PointsPerDecade = settings.PointsPerDecade};
    return RunOnNodalys<ACSweepRun>(
        circuit,
        [&config](const Nodalys::Circuit &compiled, std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeACSweep(compiled, config, {}, messages);
        },
        [node_count = circuit.GetNodeCount()](Nodalys::AcSweepResult result) {
            KeepCircuitNodes(result.NodeMagnitudesDecibels, node_count);
            KeepCircuitNodes(result.NodePhasesDegrees, node_count);
            return ACSweep{.Frequencies = std::move(result.Frequencies),
                           .NodeMagnitudesDecibels = std::move(result.NodeMagnitudesDecibels),
                           .NodePhasesDegrees = std::move(result.NodePhasesDegrees),
                           .CurrentMagnitudesDecibels = ToTraces(std::move(result.CurrentMagnitudesDecibels)),
                           .CurrentPhasesDegrees = ToTraces(std::move(result.CurrentPhasesDegrees))};
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
        [&config](const Nodalys::Circuit &compiled, std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeNoise(compiled, config, {}, messages);
        },
        [](Nodalys::NoiseResult result) {
            Noise noise{.OutputNode = result.OutputNode,
                        .InputSource = std::move(result.InputSource),
                        .InputUnit = std::move(result.InputUnit),
                        .Frequencies = std::move(result.Frequencies),
                        .OutputDensities = std::move(result.OutputDensities),
                        .InputDensities = std::move(result.InputDensities),
                        .OutputTotal = result.OutputTotal,
                        .InputTotal = result.InputTotal,
                        .Contributions = {}};
            for (Nodalys::NoiseContribution &contribution : result.Contributions) {
                noise.Contributions.push_back(
                    {std::move(contribution.Name), std::move(contribution.OutputDensities), contribution.OutputTotal});
            }
            return noise;
        });
}

/**
 * @brief   Computes the small-signal DC gain from a source to a node and the input and output resistances.
 * @param[in] circuit   Circuit to simulate, already checked.
 * @param[in] settings  Input source and output node.
 * @return  The gain and resistances or an error, plus the messages of Nodalys.
 */
TransferFunctionRun NodalysBackend::RunTransferFunction(const Circuit &circuit,
                                                        const TransferFunctionSettings &settings) {
    const Nodalys::TransferFunctionConfig config{
        .InputSource = settings.InputSource, .OutputPositiveNode = settings.OutputNode, .OutputNegativeNode = 0};
    return RunOnNodalys<TransferFunctionRun>(
        circuit,
        [&config](const Nodalys::Circuit &compiled, std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeTransferFunction(compiled, config, {}, messages);
        },
        [](Nodalys::TransferFunctionResult result) {
            // A gain in V/A comes from a current source
            return TransferFunction{.OutputNode = result.OutputPositiveNode,
                                    .InputSource = std::move(result.InputSource),
                                    .InputUnit = result.GainUnit == "V/A" ? "A" : "V",
                                    .Gain = result.Gain,
                                    .InputImpedance = result.InputImpedance,
                                    .OutputImpedance = result.OutputImpedance};
        });
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
        [&config](const Nodalys::Circuit &compiled, std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeSensitivity(compiled, config, {}, messages);
        },
        [&circuit, &settings](Nodalys::SensitivityResult result) {
            return BuildSensitivity(circuit, settings.OutputNode, result.Vectors);
        });
}

/**
 * @brief   Finds the poles and zeros of the transfer function from an input source to a node voltage.
 * @param[in] circuit   Circuit to simulate.
 * @param[in] settings  Output node and input source.
 * @return  The poles and zeros, or an error, also when the solver found none at all.
 */
PoleZeroRun NodalysBackend::RunPoleZero(const Circuit &circuit, const PoleZeroSettings &settings) {
    const Nodalys::PoleZeroConfig config{
        .OutputNode = settings.OutputNode, .InputSource = settings.InputSource, .FindPoles = true, .FindZeros = true};
    return RunOnNodalys<PoleZeroRun>(
        circuit,
        [&config](const Nodalys::Circuit &compiled, std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputePoleZero(compiled, config, {}, messages);
        },
        [&circuit, &settings](const Nodalys::PoleZeroResult &result) -> std::expected<PoleZero, std::string> {
            if (result.Poles.empty() && result.Zeros.empty()) {
                return std::unexpected(std::string(NO_POLE_ZERO_RESULT));
            }
            return BuildPoleZero(circuit, settings, result.Poles, result.Zeros);
        });
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
        [&config](const Nodalys::Circuit &compiled, std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeSParameters(compiled, config, {}, messages);
        },
        [](Nodalys::SParameterResult result) {
            return SParameters{.Frequencies = std::move(result.Frequencies),
                               .ReferenceOhms = std::move(result.ReferenceOhms),
                               .Values = std::move(result.Values),
                               .Warnings = std::move(result.Warnings)};
        });
}

/**
 * @brief   Finds the periodic steady state of a circuit at a fundamental and its harmonics.
 * @param[in]     circuit   Circuit to simulate, already checked.
 * @param[in]     settings  Fundamental frequency, harmonic count and sample count.
 * @param[in,out] control   Where the run looks for a stop request between its Newton iterations, or null.
 * @return  The spectra and one-cycle waveforms or an error, plus the messages of Nodalys.
 */
HarmonicBalanceRun NodalysBackend::RunHarmonicBalance(const Circuit &circuit, const HarmonicBalanceSettings &settings,
                                                      RunControl *control) {
    Nodalys::HarmonicBalanceConfig config{.FundamentalFrequency = settings.FundamentalFrequency,
                                          .Harmonics = settings.Harmonics,
                                          .Samples = settings.Samples};
    if (control != nullptr) {
        config.ShouldStop = [control]() { return control->IsCancelled(); };
    }
    return RunOnNodalys<HarmonicBalanceRun>(
        circuit,
        [&config](const Nodalys::Circuit &compiled, std::vector<Nodalys::DiagnosticMessage> *messages) {
            return Nodalys::ComputeHarmonicBalance(compiled, config, {}, messages);
        },
        [&settings, node_count = circuit.GetNodeCount()](Nodalys::HarmonicBalanceResult result) {
            KeepCircuitNodes(result.NodePhasors, node_count);
            KeepCircuitNodes(result.NodeWaveforms, node_count);
            HarmonicBalance balance{.FundamentalFrequency = settings.FundamentalFrequency,
                                    .Frequencies = std::move(result.Frequencies),
                                    .NodePhasors = std::move(result.NodePhasors),
                                    .CurrentPhasors = {},
                                    .Times = std::move(result.Times),
                                    .NodeWaveforms = std::move(result.NodeWaveforms),
                                    .CurrentWaveforms = ToTraces(std::move(result.CurrentWaveforms)),
                                    .Warnings = std::move(result.Warnings),
                                    .Iterations = result.Iterations,
                                    .Strategy = std::move(result.Strategy)};
            balance.CurrentPhasors.reserve(result.CurrentPhasors.size());
            for (Nodalys::ComponentPhasors &current : result.CurrentPhasors) {
                balance.CurrentPhasors.push_back({std::move(current.Name), std::move(current.Phasors)});
            }
            return balance;
        });
}

} // namespace Core
