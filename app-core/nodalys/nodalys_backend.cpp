/**
 * @file    nodalys_backend.cpp
 * @brief   The Nodalys simulation backend, which solves circuits with the autonomous Nodalys engine.
 */

#include "nodalys/nodalys_backend.h"

#include "components/logic_part.h"
#include "nodalys/compiler.h"
#include <format>
#include <nodalys/nodalys.h>
#include <utility>

namespace Core {

namespace {

constexpr auto NODALYS_ANALYSES = {
    AnalysisKind::OPERATING_POINT, AnalysisKind::TRANSIENT,         AnalysisKind::DC_SWEEP,    AnalysisKind::AC_SWEEP,
    AnalysisKind::NOISE,           AnalysisKind::TRANSFER_FUNCTION, AnalysisKind::S_PARAMETERS};

std::expected<nodalys::Circuit, std::string> CompileOrFail(const Circuit &circuit) {
    auto compiled = CompileCircuit(circuit);
    if (!compiled) {
        return std::unexpected(std::format("Nodalys cannot simulate this circuit: {}", compiled.error()));
    }
    return std::move(*compiled);
}

void ForwardMessages(const std::vector<nodalys::DiagnosticMessage> &source, std::vector<SimulatorMessage> &target) {
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
    for (int index = 0; index <= static_cast<int>(ComponentType::SEVEN_SEGMENT); ++index) {
        const auto type = static_cast<ComponentType>(index);
        if (IsLogicPart(type)) {
            m_Capabilities.UnsupportedPartsByAnalysis[AnalysisKind::S_PARAMETERS][type] =
                "S-parameters are small-signal, and digital parts have no small-signal RF model";
        }
    }
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
    std::vector<nodalys::DiagnosticMessage> messages;
    auto result = nodalys::ComputeOperatingPoint(*compiled, {}, &messages);
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

    nodalys::TransientConfig config{
        .StopTime = settings.StopTime, .TimeStep = settings.TimeStep, .MaxStep = settings.TimeStep};

    nodalys::RunMonitor nodalys_monitor;
    if (monitor.Control != nullptr) {
        nodalys_monitor.ShouldStop = [&monitor]() { return monitor.Control->IsCancelled(); };
        nodalys_monitor.TakeLiveChanges = [&monitor]() {
            std::vector<nodalys::LiveChange> changes;
            for (const auto &c : monitor.Control->TakeChanges()) {
                changes.push_back({c.Element,
                                   c.Quantity == LiveQuantity::RESISTANCE ? nodalys::LiveQuantity::Resistance
                                                                          : nodalys::LiveQuantity::DcVoltage,
                                   c.Value});
            }
            return changes;
        };
    }
    if (monitor.OnChunk) {
        nodalys_monitor.PostTransientChunk = [&monitor](const nodalys::TransientResult &chunk) {
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
    std::vector<nodalys::DiagnosticMessage> messages;
    auto result = nodalys::ComputeTransient(*compiled, config, has_initial_conditions, nodalys_monitor, {}, &messages);
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

    nodalys::DcSweepConfig config;
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

    std::vector<nodalys::DiagnosticMessage> messages;
    auto result = nodalys::ComputeDCSweep(*compiled, config, {}, &messages);
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

    nodalys::AcSweepConfig config{.StartFrequency = settings.StartFrequency,
                                  .StopFrequency = settings.StopFrequency,
                                  .PointsPerDecade = settings.PointsPerDecade};

    std::vector<nodalys::DiagnosticMessage> messages;
    auto result = nodalys::ComputeACSweep(*compiled, config, {}, &messages);
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

    nodalys::NoiseConfig config{.OutputPositiveNode = settings.OutputNode,
                                .OutputNegativeNode = 0,
                                .InputSource = settings.InputSource,
                                .StartFrequency = settings.StartFrequency,
                                .StopFrequency = settings.StopFrequency,
                                .PointsPerDecade = settings.PointsPerDecade};

    std::vector<nodalys::DiagnosticMessage> messages;
    auto result = nodalys::ComputeNoise(*compiled, config, {}, &messages);
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

    nodalys::TransferFunctionConfig config{
        .InputSource = settings.InputSource, .OutputPositiveNode = settings.OutputNode, .OutputNegativeNode = 0};

    std::vector<nodalys::DiagnosticMessage> messages;
    auto result = nodalys::ComputeTransferFunction(*compiled, config, {}, &messages);
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
SensitivityRun NodalysBackend::RunSensitivity(const Circuit & /*circuit*/, const SensitivitySettings & /*settings*/) {
    return {std::unexpected("Nodalys has no sensitivity analysis yet; it runs with the ngspice backend"), {}};
}

/**
 * @brief   Finds the poles and zeros of the transfer function from an input source to a node voltage.
 * @param[in] circuit   Circuit to simulate.
 * @param[in] settings  Output node and input source.
 * @return  The poles and zeros, or an error.
 */
PoleZeroRun NodalysBackend::RunPoleZero(const Circuit & /*circuit*/, const PoleZeroSettings & /*settings*/) {
    return {std::unexpected("Nodalys has no pole-zero analysis yet; it runs with the ngspice backend"), {}};
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

    nodalys::SParameterConfig config{
        .SweepType = settings.IsLogarithmic ? nodalys::FrequencySweepType::Decade : nodalys::FrequencySweepType::Linear,
        .StartFrequency = settings.StartFrequency,
        .StopFrequency = settings.StopFrequency,
        .Points = settings.IsLogarithmic ? settings.PointsPerDecade : settings.LinearPoints};
    for (const auto &port : circuit.GetPorts()) {
        config.Ports.push_back({.Number = port.Number, .Node = port.Node, .ReferenceOhms = port.ReferenceOhms});
    }

    std::vector<nodalys::DiagnosticMessage> messages;
    auto result = nodalys::ComputeSParameters(*compiled, config, {}, &messages);
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

} // namespace Core
