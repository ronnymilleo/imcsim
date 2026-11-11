/**
 * @file    nodalys_backend.cpp
 * @brief   The Nodalys simulation backend, which solves circuits with the autonomous Nodalys engine.
 */

#include "nodalys/nodalys_backend.h"

#include "components/logic_part.h"
#include "nodalys/compiler.h"
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

std::expected<Nodalys::Circuit, std::string> CompileOrFail(const Circuit &circuit) {
    auto compiled = CompileCircuit(circuit);
    if (!compiled) {
        return std::unexpected(std::format("Nodalys cannot simulate this circuit: {}", compiled.error()));
    }
    return std::move(*compiled);
}

void ForwardMessages(const std::vector<Nodalys::DiagnosticMessage> &source, std::vector<SimulatorMessage> &target) {
    for (const auto &msg : source) {
        target.push_back({msg.Text, msg.IsError});
    }
}

} // namespace

NodalysBackend::NodalysBackend() {
    m_Capabilities.Analyses = NODALYS_ANALYSES;
    for (const ComponentType type : {ComponentType::BEHAVIORAL_VOLTAGE, ComponentType::BEHAVIORAL_CURRENT}) {
        m_Capabilities.UnsupportedParts[type] = "Nodalys has no device for behavioral sources yet";
    }
    m_Capabilities.UnsupportedParts[ComponentType::FUSE] = "Nodalys has no device for fuses yet";
    m_Capabilities.TemperatureUnsupported =
        "Nodalys simulates at 27 C only: its device models do not follow the temperature yet";
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

std::string_view NodalysBackend::GetName() const {
    return "Nodalys";
}

const BackendCapabilities &NodalysBackend::GetCapabilities() const {
    return m_Capabilities;
}

OperatingPointRun NodalysBackend::RunOperatingPoint(const Circuit &circuit,
                                                    const std::optional<double> &temperature_celsius) {
    (void)temperature_celsius;
    OperatingPointRun run;
    auto compiled = CompileOrFail(circuit);
    if (!compiled) {
        run.Result = std::unexpected(compiled.error());
        return run;
    }
    std::vector<Nodalys::DiagnosticMessage> messages;
    auto result = Nodalys::ComputeOperatingPoint(*compiled, {}, &messages);
    ForwardMessages(messages, run.Messages);
    if (!result) {
        run.Result = std::unexpected(result.error());
        return run;
    }
    OperatingPoint op;
    op.NodeVoltages = std::move(result->NodeVoltages);
    for (auto &curr : result->Currents) {
        op.Currents.push_back({std::move(curr.Name), curr.Current});
    }
    op.Warnings = std::move(result->Warnings);
    run.Result = std::move(op);
    return run;
}

TransientRun NodalysBackend::RunTransient(const Circuit &circuit, const TransientSettings &settings,
                                          const std::optional<double> &temperature_celsius, const RunMonitor &monitor) {
    (void)temperature_celsius;
    TransientRun run;
    auto compiled = CompileOrFail(circuit);
    if (!compiled) {
        run.Result = std::unexpected(compiled.error());
        return run;
    }

    Nodalys::TransientConfig config{
        .StopTime = settings.StopTime, .TimeStep = settings.TimeStep, .MaxStep = settings.TimeStep};

    Nodalys::RunMonitor nodalys_monitor;
    // When the clock a live run keeps pace with started, moved later by the time spent paused
    auto live_start = std::chrono::steady_clock::now();
    if (monitor.Control != nullptr) {
        nodalys_monitor.ShouldStop = [&monitor]() { return monitor.Control->IsCancelled(); };
        nodalys_monitor.OnProgress = [&monitor](const double progress) { monitor.Control->SetProgress(progress); };
        nodalys_monitor.TakeLiveChanges = [&monitor]() {
            std::vector<Nodalys::LiveChange> changes;
            for (const auto &c : monitor.Control->TakeChanges()) {
                changes.push_back({c.Element,
                                   c.Quantity == LiveQuantity::RESISTANCE ? Nodalys::LiveQuantity::RESISTANCE
                                                                          : Nodalys::LiveQuantity::DC_VOLTAGE,
                                   c.Value});
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
        nodalys_monitor.PostTransientChunk = [&monitor](const Nodalys::TransientResult &chunk) {
            Transient c;
            c.Times = chunk.Times;
            c.NodeVoltages = chunk.NodeVoltages;
            for (const auto &curr : chunk.Currents) {
                c.Currents.push_back({curr.Name, curr.Values});
            }
            c.Warnings = chunk.Warnings;
            c.IsComplete = chunk.IsComplete;
            monitor.OnChunk(std::move(c));
        };
    }

    const bool has_initial_conditions = circuit.HasInitialConditions();
    std::vector<Nodalys::DiagnosticMessage> messages;
    auto result = Nodalys::ComputeTransient(*compiled, config, has_initial_conditions, nodalys_monitor, {}, &messages);
    ForwardMessages(messages, run.Messages);
    if (!result) {
        run.Result = std::unexpected(result.error());
        return run;
    }

    Transient tr;
    tr.Times = std::move(result->Times);
    tr.NodeVoltages = std::move(result->NodeVoltages);
    for (auto &curr : result->Currents) {
        tr.Currents.push_back({std::move(curr.Name), std::move(curr.Values)});
    }
    tr.Warnings = std::move(result->Warnings);
    tr.IsComplete = result->IsComplete;
    if (has_initial_conditions) {
        tr.Warnings.emplace_back(
            "The transient starts from the initial conditions (uic): the parts that have one start from it and the "
            "other capacitors and inductors from zero, since the operating point is not computed first");
    }
    run.Result = std::move(tr);
    return run;
}

DCSweepRun NodalysBackend::RunDCSweep(const Circuit &circuit, const DCSweepSettings &settings,
                                      const std::optional<double> &temperature_celsius) {
    (void)temperature_celsius;
    DCSweepRun run;
    auto compiled = CompileOrFail(circuit);
    if (!compiled) {
        run.Result = std::unexpected(compiled.error());
        return run;
    }

    Nodalys::DcSweepConfig config;
    config.SweptSource = settings.Swept.Source;
    config.SweptStart = settings.Swept.Start;
    config.SweptStop = settings.Swept.Stop;
    config.SweptStep = settings.Swept.Step;
    if (settings.Stepped) {
        config.SteppedSource = settings.Stepped->Source;
        config.SteppedStart = settings.Stepped->Start;
        config.SteppedStop = settings.Stepped->Stop;
        config.SteppedStep = settings.Stepped->Step;
    }

    std::vector<Nodalys::DiagnosticMessage> messages;
    auto result = Nodalys::ComputeDCSweep(*compiled, config, {}, &messages);
    ForwardMessages(messages, run.Messages);
    if (!result) {
        run.Result = std::unexpected(result.error());
        return run;
    }

    DCSweep sweep;
    sweep.SweptSource = std::move(result->SweptSource);
    sweep.SweptUnit = std::move(result->SweptUnit);
    sweep.SweptValues = std::move(result->SweptValues);
    sweep.SteppedSource = std::move(result->SteppedSource);
    sweep.SteppedUnit = std::move(result->SteppedUnit);
    for (auto &curve : result->Curves) {
        DCSweepCurve c;
        c.StepValue = curve.StepValue;
        c.NodeVoltages = std::move(curve.NodeVoltages);
        for (auto &curr : curve.Currents) {
            c.Currents.push_back({std::move(curr.Name), std::move(curr.Values)});
        }
        sweep.Curves.push_back(std::move(c));
    }
    run.Result = std::move(sweep);
    return run;
}

ACSweepRun NodalysBackend::RunACSweep(const Circuit &circuit, const ACSweepSettings &settings,
                                      const std::optional<double> &temperature_celsius) {
    (void)temperature_celsius;
    ACSweepRun run;
    auto compiled = CompileOrFail(circuit);
    if (!compiled) {
        run.Result = std::unexpected(compiled.error());
        return run;
    }

    Nodalys::AcSweepConfig config{.StartFrequency = settings.StartFrequency,
                                  .StopFrequency = settings.StopFrequency,
                                  .PointsPerDecade = settings.PointsPerDecade};

    std::vector<Nodalys::DiagnosticMessage> messages;
    auto result = Nodalys::ComputeACSweep(*compiled, config, {}, &messages);
    ForwardMessages(messages, run.Messages);
    if (!result) {
        run.Result = std::unexpected(result.error());
        return run;
    }

    ACSweep ac;
    ac.Frequencies = std::move(result->Frequencies);
    ac.NodeMagnitudesDecibels = std::move(result->NodeMagnitudesDecibels);
    ac.NodePhasesDegrees = std::move(result->NodePhasesDegrees);
    for (auto &curr : result->CurrentMagnitudesDecibels) {
        ac.CurrentMagnitudesDecibels.push_back({std::move(curr.Name), std::move(curr.Values)});
    }
    for (auto &curr : result->CurrentPhasesDegrees) {
        ac.CurrentPhasesDegrees.push_back({std::move(curr.Name), std::move(curr.Values)});
    }
    run.Result = std::move(ac);
    return run;
}

NoiseRun NodalysBackend::RunNoise(const Circuit &circuit, const NoiseSettings &settings) {
    NoiseRun run;
    auto compiled = CompileOrFail(circuit);
    if (!compiled) {
        run.Result = std::unexpected(compiled.error());
        return run;
    }

    Nodalys::NoiseConfig config{.OutputPositiveNode = settings.OutputNode,
                                .OutputNegativeNode = 0,
                                .InputSource = settings.InputSource,
                                .StartFrequency = settings.StartFrequency,
                                .StopFrequency = settings.StopFrequency,
                                .PointsPerDecade = settings.PointsPerDecade};

    std::vector<Nodalys::DiagnosticMessage> messages;
    auto result = Nodalys::ComputeNoise(*compiled, config, {}, &messages);
    ForwardMessages(messages, run.Messages);
    if (!result) {
        run.Result = std::unexpected(result.error());
        return run;
    }

    Noise noise;
    noise.OutputNode = result->OutputNode;
    noise.InputSource = std::move(result->InputSource);
    noise.InputUnit = std::move(result->InputUnit);
    noise.Frequencies = std::move(result->Frequencies);
    noise.OutputDensities = std::move(result->OutputDensities);
    noise.InputDensities = std::move(result->InputDensities);
    noise.OutputTotal = result->OutputTotal;
    noise.InputTotal = result->InputTotal;
    for (auto &c : result->Contributions) {
        noise.Contributions.push_back({std::move(c.Name), std::move(c.OutputDensities), c.OutputTotal});
    }
    run.Result = std::move(noise);
    return run;
}

TransferFunctionRun NodalysBackend::RunTransferFunction(const Circuit &circuit,
                                                        const TransferFunctionSettings &settings) {
    TransferFunctionRun run;
    auto compiled = CompileOrFail(circuit);
    if (!compiled) {
        run.Result = std::unexpected(compiled.error());
        return run;
    }

    Nodalys::TransferFunctionConfig config{
        .InputSource = settings.InputSource, .OutputPositiveNode = settings.OutputNode, .OutputNegativeNode = 0};

    std::vector<Nodalys::DiagnosticMessage> messages;
    auto result = Nodalys::ComputeTransferFunction(*compiled, config, {}, &messages);
    ForwardMessages(messages, run.Messages);
    if (!result) {
        run.Result = std::unexpected(result.error());
        return run;
    }

    TransferFunction tf;
    tf.OutputNode = result->OutputPositiveNode;
    tf.InputSource = std::move(result->InputSource);
    tf.InputUnit = result->GainUnit == "V/A" ? "A" : "V";
    tf.Gain = result->Gain;
    tf.InputImpedance = result->InputImpedance;
    tf.OutputImpedance = result->OutputImpedance;
    run.Result = std::move(tf);
    return run;
}

/**
 * @brief   Finds how much the DC voltage of a node changes with the value of each part.
 * @param[in] circuit   Circuit to simulate.
 * @param[in] settings  Output node.
 * @return  The sensitivity of each part, or an error.
 */
SensitivityRun NodalysBackend::RunSensitivity(const Circuit &circuit, const SensitivitySettings &settings) {
    SensitivityRun run;
    auto compiled = CompileOrFail(circuit);
    if (!compiled) {
        run.Result = std::unexpected(compiled.error());
        return run;
    }

    Nodalys::SensitivityConfig config{
        .OutputPositiveNode = settings.OutputNode,
        .OutputNegativeNode = 0,
    };

    std::vector<Nodalys::DiagnosticMessage> messages;
    auto result = Nodalys::ComputeSensitivity(*compiled, config, {}, &messages);
    ForwardMessages(messages, run.Messages);
    if (!result) {
        run.Result = std::unexpected(result.error());
        return run;
    }

    run.Result = BuildSensitivity(circuit, settings.OutputNode, result->Vectors);
    return run;
}

/**
 * @brief   Finds the poles and zeros of the transfer function from an input source to a node voltage.
 * @param[in] circuit   Circuit to simulate.
 * @param[in] settings  Output node and input source.
 * @return  The poles and zeros, or an error.
 */
PoleZeroRun NodalysBackend::RunPoleZero(const Circuit &circuit, const PoleZeroSettings &settings) {
    PoleZeroRun run;
    auto compiled = CompileOrFail(circuit);
    if (!compiled) {
        run.Result = std::unexpected(compiled.error());
        return run;
    }

    Nodalys::PoleZeroConfig config{
        .OutputNode = settings.OutputNode,
        .InputSource = settings.InputSource,
        .FindPoles = true,
        .FindZeros = true,
    };

    std::vector<Nodalys::DiagnosticMessage> messages;
    auto result = Nodalys::ComputePoleZero(*compiled, config, {}, &messages);
    ForwardMessages(messages, run.Messages);
    if (!result) {
        run.Result = std::unexpected(result.error());
        return run;
    }

    if (result->Poles.empty() && result->Zeros.empty()) {
        run.Result = std::unexpected(std::string(NO_POLE_ZERO_RESULT));
        return run;
    }

    run.Result = Core::BuildPoleZero(circuit, settings, result->Poles, result->Zeros);
    return run;
}

/**
 * @brief   Measures the scattering matrix of the ports of a circuit across frequency.
 * @param[in] circuit   Circuit to simulate.
 * @param[in] settings  Frequency range and points.
 * @return  The S-parameter matrix at each frequency, or an error.
 */
SParametersRun NodalysBackend::RunSParameters(const Circuit &circuit, const SParameterSettings &settings) {
    SParametersRun run;
    auto compiled = CompileOrFail(circuit);
    if (!compiled) {
        run.Result = std::unexpected(compiled.error());
        return run;
    }

    Nodalys::SParameterConfig config{
        .SweepType = settings.IsLogarithmic ? Nodalys::FrequencySweepType::DECADE : Nodalys::FrequencySweepType::LINEAR,
        .StartFrequency = settings.StartFrequency,
        .StopFrequency = settings.StopFrequency,
        .Points = settings.IsLogarithmic ? settings.PointsPerDecade : settings.LinearPoints};
    for (const auto &port : circuit.GetPorts()) {
        config.Ports.push_back({.Number = port.Number, .Node = port.Node, .ReferenceOhms = port.ReferenceOhms});
    }

    std::vector<Nodalys::DiagnosticMessage> messages;
    auto result = Nodalys::ComputeSParameters(*compiled, config, {}, &messages);
    ForwardMessages(messages, run.Messages);
    if (!result) {
        run.Result = std::unexpected(result.error());
        return run;
    }

    SParameters sp;
    sp.Frequencies = std::move(result->Frequencies);
    sp.ReferenceOhms = std::move(result->ReferenceOhms);
    sp.Values = std::move(result->Values);
    sp.Warnings = std::move(result->Warnings);
    run.Result = std::move(sp);
    return run;
}

HarmonicBalanceRun NodalysBackend::RunHarmonicBalance(const Circuit &circuit, const HarmonicBalanceSettings &settings,
                                                      RunControl *control) {
    HarmonicBalanceRun run;
    auto compiled = CompileOrFail(circuit);
    if (!compiled) {
        run.Result = std::unexpected(compiled.error());
        return run;
    }

    Nodalys::HarmonicBalanceConfig config{.FundamentalFrequency = settings.FundamentalFrequency,
                                          .Harmonics = settings.Harmonics,
                                          .Samples = settings.Samples};
    if (control != nullptr) {
        config.ShouldStop = [control]() { return control->IsCancelled(); };
    }

    std::vector<Nodalys::DiagnosticMessage> messages;
    auto result = Nodalys::ComputeHarmonicBalance(*compiled, config, {}, &messages);
    ForwardMessages(messages, run.Messages);
    if (!result) {
        run.Result = std::unexpected(result.error());
        return run;
    }

    HarmonicBalance hb;
    hb.FundamentalFrequency = settings.FundamentalFrequency;
    hb.Frequencies = std::move(result->Frequencies);
    hb.NodePhasors = std::move(result->NodePhasors);
    hb.CurrentPhasors.reserve(result->CurrentPhasors.size());
    for (auto &cur : result->CurrentPhasors) {
        hb.CurrentPhasors.push_back({std::move(cur.Name), std::move(cur.Phasors)});
    }
    hb.Times = std::move(result->Times);
    hb.NodeWaveforms = std::move(result->NodeWaveforms);
    hb.CurrentWaveforms.reserve(result->CurrentWaveforms.size());
    for (auto &cur : result->CurrentWaveforms) {
        hb.CurrentWaveforms.push_back({std::move(cur.Name), std::move(cur.Values)});
    }
    hb.Warnings = std::move(result->Warnings);
    hb.Iterations = result->Iterations;
    hb.Strategy = std::move(result->Strategy);
    run.Result = std::move(hb);
    return run;
}

} // namespace Core
