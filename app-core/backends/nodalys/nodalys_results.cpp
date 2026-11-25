/**
 * @file    nodalys_results.cpp
 * @brief   Conversions from Nodalys engine analysis outputs to imcsim Core simulation results.
 */

#include "backends/nodalys/nodalys_results.h"

#include "backends/nodalys/compiler.h"
#include "circuit/components/switch.h"
#include "simulation/ac_sensitivity.h"
#include "simulation/live_pace.h"
#include "simulation/pole_zero.h"
#include "simulation/ratings.h"
#include <format>
#include <set>
#include <utility>

namespace Core {

namespace {

/// What the netlist of ngspice puts before the name of a switch to name its resistor, as a live change does.
constexpr std::string_view SWITCH_RESISTOR_PREFIX = "R-";

std::set<std::string> FindSwitchElements(const Circuit &circuit) {
    std::set<std::string> elements;
    for (const Component *part : CollectParts(circuit)) {
        if (IsSwitch(part->GetType())) {
            elements.insert(std::format("{}{}", SWITCH_RESISTOR_PREFIX, part->GetName()));
        }
    }
    return elements;
}

/**
 * @brief   Keeps the rows of the nodes of the circuit and drops the rest.
 * @details Nodalys also reports the nodes it adds, one for each probe that carries a current a source reads and one
 *          for each node inside a block. ngspice gives the nodes of the circuit only, and what reads a result by node
 *          number, such as the table of the operating point, must not meet nodes that are not in the schematic.
 */
template <typename Row>
void KeepCircuitNodes(std::vector<Row> &rows, const int node_count) {
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

/**
 * @brief   Says why a transfer function has no pole or zero to show.
 * @param[in] settings         Output node and input source of the run.
 * @param[in] cancelled_pairs  Poles that a zero at the same place left out.
 * @return  The text of the error, which tells the user what to look at in the circuit.
 */
std::string DescribeConstantTransfer(const PoleZeroSettings &settings, const int cancelled_pairs) {
    std::string text = std::format("The transfer from {} to node {} has no poles or zeros: it is a constant gain at "
                                   "every frequency. Either no capacitor or inductor lies between them, or the output "
                                   "is the node the source holds",
                                   settings.InputSource,
                                   settings.OutputNode);
    if (cancelled_pairs > 0) {
        text += std::format(", or what does lie between them cancels out ({} pole-zero {} at the same place)",
                            cancelled_pairs,
                            cancelled_pairs == 1 ? "pair" : "pairs");
    }
    return text;
}

} // namespace

/**
 * @brief   Bridges an imcsim RunMonitor to a Nodalys::RunMonitor with live pacing, chunk streaming, and switch aliases.
 * @param[in]     monitor     Caller monitor callbacks and control handle.
 * @param[in,out] live_start  Wall-clock start time adjusted by pauses.
 * @param[in]     circuit     Simulated circuit for top-level node count and switch element names.
 * @return  Configured Nodalys::RunMonitor.
 */
Nodalys::RunMonitor
ToNodalysMonitor(const RunMonitor &monitor, std::chrono::steady_clock::time_point &live_start, const Circuit &circuit) {
    const int node_count = circuit.GetNodeCount();
    Nodalys::RunMonitor nodalys_monitor;
    if (monitor.Control != nullptr) {
        nodalys_monitor.ShouldStop = [&monitor]() { return monitor.Control->IsCancelled(); };
        nodalys_monitor.OnProgress = [&monitor](const double progress) { monitor.Control->SetProgress(progress); };
        nodalys_monitor.TakeLiveChanges = [&monitor, switch_elements = FindSwitchElements(circuit)]() {
            std::vector<Nodalys::LiveChange> changes;
            for (const LiveChange &change : monitor.Control->TakeChanges()) {
                const Nodalys::LiveQuantity quantity = change.Quantity == LiveQuantity::RESISTANCE
                                                         ? Nodalys::LiveQuantity::RESISTANCE
                                                         : Nodalys::LiveQuantity::DC_VOLTAGE;
                std::string element = change.Element;
                if (switch_elements.contains(element)) {
                    element.erase(0, SWITCH_RESISTOR_PREFIX.size());
                }
                changes.push_back({.Element = std::move(element), .Quantity = quantity, .Value = change.Value});
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
 * @brief   Converts a Nodalys operating point result to an imcsim OperatingPoint and attaches rating warnings.
 * @param[in] circuit  Circuit that was simulated.
 * @param[in] result   Raw Nodalys operating point output.
 * @return  Converted OperatingPoint result.
 */
OperatingPoint ToOperatingPoint(const Circuit &circuit, Nodalys::OperatingPointResult result) {
    KeepCircuitNodes(result.NodeVoltages, circuit.GetNodeCount());
    OperatingPoint point{.NodeVoltages = std::move(result.NodeVoltages),
                         .Currents = {},
                         .Warnings = std::move(result.Warnings)};
    for (Nodalys::ComponentCurrent &current : result.Currents) {
        point.Currents.push_back({std::move(current.Name), current.Current});
    }
    AddRatingWarnings(circuit, point);
    return point;
}

/**
 * @brief   Converts a Nodalys transient chunk or result to an imcsim Transient trimmed to top-level circuit nodes.
 * @param[in] result      Raw Nodalys transient output.
 * @param[in] node_count  Number of top-level circuit nodes.
 * @return  Converted Transient result.
 */
Transient ToTransient(Nodalys::TransientResult result, const int node_count) {
    KeepCircuitNodes(result.NodeVoltages, node_count);
    return {.Times = std::move(result.Times),
            .NodeVoltages = std::move(result.NodeVoltages),
            .Currents = ToTraces(std::move(result.Currents)),
            .Warnings = std::move(result.Warnings),
            .IsComplete = result.IsComplete};
}

/**
 * @brief   Converts a completed Nodalys transient result and attaches initial-condition and rating warnings.
 * @param[in] circuit                 Circuit that was simulated.
 * @param[in] result                  Raw Nodalys transient output.
 * @param[in] has_initial_conditions  Whether the run started from `.ic` (`uic`).
 * @return  Converted Transient result with warnings.
 */
Transient
ToFinishedTransient(const Circuit &circuit, Nodalys::TransientResult result, const bool has_initial_conditions) {
    Transient transient = ToTransient(std::move(result), circuit.GetNodeCount());
    if (has_initial_conditions) {
        transient.Warnings.emplace_back(
            "The transient starts from the initial conditions (uic): the parts that have one start from it "
            "and the other capacitors and inductors from zero, since the operating point is not computed "
            "first");
    }
    AddRatingWarnings(circuit, transient);
    return transient;
}

/**
 * @brief   Converts a Nodalys DC sweep result to an imcsim DCSweep trimmed to top-level circuit nodes.
 * @param[in] result      Raw Nodalys DC sweep output.
 * @param[in] node_count  Number of top-level circuit nodes.
 * @return  Converted DCSweep result.
 */
DCSweep ToDCSweep(Nodalys::DcSweepResult result, const int node_count) {
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
}

/**
 * @brief   Converts a Nodalys AC sweep result to an imcsim ACSweep trimmed to top-level circuit nodes.
 * @param[in] result      Raw Nodalys AC sweep output.
 * @param[in] node_count  Number of top-level circuit nodes.
 * @return  Converted ACSweep result.
 */
ACSweep ToACSweep(Nodalys::AcSweepResult result, const int node_count) {
    KeepCircuitNodes(result.NodeMagnitudesDecibels, node_count);
    KeepCircuitNodes(result.NodePhasesDegrees, node_count);
    return ACSweep{.Frequencies = std::move(result.Frequencies),
                   .NodeMagnitudesDecibels = std::move(result.NodeMagnitudesDecibels),
                   .NodePhasesDegrees = std::move(result.NodePhasesDegrees),
                   .CurrentMagnitudesDecibels = ToTraces(std::move(result.CurrentMagnitudesDecibels)),
                   .CurrentPhasesDegrees = ToTraces(std::move(result.CurrentPhasesDegrees))};
}

/**
 * @brief   Converts a Nodalys noise analysis result to an imcsim Noise result.
 * @param[in] result  Raw Nodalys noise output.
 * @return  Converted Noise result.
 */
Noise ToNoise(Nodalys::NoiseResult result) {
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
}

/**
 * @brief   Converts a Nodalys DC transfer function result to an imcsim TransferFunction result.
 * @param[in] result  Raw Nodalys transfer function output.
 * @return  Converted TransferFunction result.
 */
TransferFunction ToTransferFunction(Nodalys::TransferFunctionResult result) {
    // A gain in V/A comes from a current source
    return TransferFunction{.OutputNode = result.OutputPositiveNode,
                            .InputSource = std::move(result.InputSource),
                            .InputUnit = result.GainUnit == "V/A" ? "A" : "V",
                            .Gain = result.Gain,
                            .InputImpedance = result.InputImpedance,
                            .OutputImpedance = result.OutputImpedance};
}

/**
 * @brief   Converts a Nodalys AC sensitivity result to an imcsim ACSensitivity result grouped by schematic part.
 * @param[in] circuit   Circuit that was simulated.
 * @param[in] settings  AC sensitivity settings.
 * @param[in] result    Raw Nodalys AC sensitivity output.
 * @return  Converted ACSensitivity result.
 */
ACSensitivity
ToACSensitivity(const Circuit &circuit, const ACSensitivitySettings &settings, Nodalys::AcSensitivityResult result) {
    ACElementFiguresByName elements;
    for (Nodalys::AcElementSensitivity &element : result.Elements) {
        elements[element.Name] = {.Value = element.Value,
                                  .MagnitudeDecibelsPerPercent = std::move(element.MagnitudeDecibelsPerPercent),
                                  .PhaseDegreesPerPercent = std::move(element.PhaseDegreesPerPercent)};
    }
    ACSensitivity header{.OutputNode = settings.OutputNode,
                         .InputSource = settings.InputSource,
                         .Frequencies = std::move(result.Frequencies),
                         .Output = std::move(result.Output),
                         .Parts = {},
                         .Warnings = std::move(result.Warnings)};
    return BuildACSensitivity(circuit, std::move(header), elements);
}

/**
 * @brief   Converts a Nodalys pole-zero result to an imcsim PoleZero result, or an error if the transfer is constant.
 * @param[in] circuit   Circuit that was simulated.
 * @param[in] settings  Pole-zero settings.
 * @param[in] result    Raw Nodalys pole-zero output.
 * @return  Converted PoleZero result, or a diagnostic error message when no poles or zeros remain.
 */
std::expected<PoleZero, std::string>
ToPoleZero(const Circuit &circuit, const PoleZeroSettings &settings, const Nodalys::PoleZeroResult &result) {
    PoleZero poles_and_zeros = BuildPoleZero(circuit, settings, result.Poles, result.Zeros);
    if (poles_and_zeros.Roots.empty()) {
        return std::unexpected(DescribeConstantTransfer(settings, poles_and_zeros.CancelledPairs));
    }
    poles_and_zeros.Warnings = result.Warnings;
    return poles_and_zeros;
}

/**
 * @brief   Converts a Nodalys S-parameter result to an imcsim SParameters result.
 * @param[in] result  Raw Nodalys S-parameter output.
 * @return  Converted SParameters result.
 */
SParameters ToSParameters(Nodalys::SParameterResult result) {
    return SParameters{.Frequencies = std::move(result.Frequencies),
                       .ReferenceOhms = std::move(result.ReferenceOhms),
                       .Values = std::move(result.Values),
                       .ImpedanceMatrices = std::move(result.ImpedanceMatrices),
                       .AdmittanceMatrices = std::move(result.AdmittanceMatrices),
                       .Warnings = std::move(result.Warnings)};
}

/**
 * @brief   Converts a Nodalys Harmonic Balance result to an imcsim HarmonicBalance trimmed to top-level circuit nodes.
 * @param[in] settings    Harmonic Balance settings.
 * @param[in] result      Raw Nodalys Harmonic Balance output.
 * @param[in] node_count  Number of top-level circuit nodes.
 * @return  Converted HarmonicBalance result.
 */
HarmonicBalance ToHarmonicBalance(const HarmonicBalanceSettings &settings,
                                  Nodalys::HarmonicBalanceResult result,
                                  const int node_count) {
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
}

} // namespace Core
