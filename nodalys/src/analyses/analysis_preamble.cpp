/**
 * @file    analysis_preamble.cpp
 * @brief   The steps that the small-signal analyses take before their own: the checks of the output node and the
 *          input source, the operating point and the reactive system linearized around it.
 */

#include "analyses/analysis_preamble.h"

#include "devices/devices.h"
#include <format>
#include <memory>
#include <utility>

namespace Nodalys {

/**
 * @brief   Tells which of the two kinds of source this is.
 * @return  True for a voltage source, false for a current source.
 */
bool InputSource::IsVoltage() const {
    return Voltage != nullptr;
}

/**
 * @brief   Gives the source as a device, whichever kind it is.
 * @return  The voltage source or the current source.
 */
const Device &InputSource::GetDevice() const {
    if (Voltage != nullptr) {
        return *Voltage;
    }
    return *Current;
}

/**
 * @brief   Checks that a node can be the output of an analysis.
 * @param[in] compiled  Compiled circuit whose nodes are counted.
 * @param[in] node      Node number of the output; ground is not an output.
 * @return  The refusal, or nothing when the node is in the circuit.
 */
std::optional<std::string> CheckOutputNode(const CompiledCircuit &compiled, const int node) {
    if (node <= 0 || node >= compiled.CircuitNodeCount) {
        return std::format("The circuit has no output node {}", node);
    }
    return std::nullopt;
}

/**
 * @brief   Finds a source of the circuit by its name.
 * @param[in] compiled  Compiled circuit with the sources by name.
 * @param[in] name      Name of the source.
 * @return  The source, which belongs to the circuit, or the refusal when there is none of that name.
 */
std::expected<Device *, std::string> FindSourceByName(const CompiledCircuit &compiled, const std::string_view name) {
    const auto found = compiled.SourcesByName.find(std::string(name));
    if (found == compiled.SourcesByName.end()) {
        return std::unexpected(std::format("The circuit has no source named {}", name));
    }
    return found->second;
}

/**
 * @brief   Finds the source that drives an analysis by its name.
 * @param[in] compiled  Compiled circuit with the sources by name.
 * @param[in] name      Name of the source.
 * @param[in] purpose   What the source would drive, such as "a noise analysis", for the refusal of any other device.
 * @return  The source, or the reason the name cannot drive the analysis.
 */
std::expected<InputSource, std::string>
ResolveInputSource(const CompiledCircuit &compiled, const std::string_view name, const std::string_view purpose) {
    const std::expected<Device *, std::string> found = FindSourceByName(compiled, name);
    if (!found) {
        return std::unexpected(found.error());
    }
    const InputSource source{.Voltage = dynamic_cast<const VoltageSourceDevice *>(*found),
                             .Current = dynamic_cast<const CurrentSourceDevice *>(*found)};
    if (source.Voltage == nullptr && source.Current == nullptr) {
        return std::unexpected(std::format("The source {} cannot drive {}", name, purpose));
    }
    return source;
}

/**
 * @brief   Solves the operating point that an analysis linearizes around, and says how it converged.
 * @param[in,out] compiled         Compiled circuit with its matrix and devices.
 * @param[in]     options          Numerical limits of the solve.
 * @param[in]     failure_message  What the analysis says when no operating point is found.
 * @param[out]    messages         Receives the line about the convergence; may be null.
 * @return  The solution vector, or the failure message with the fault of a device when one was seen.
 */
std::expected<std::vector<double>, std::string> SolveOperatingPoint(CompiledCircuit &compiled,
                                                                    const ConvergenceOptions &options,
                                                                    const std::string_view failure_message,
                                                                    std::vector<DiagnosticMessage> *messages) {
    std::vector<double> x_dc(static_cast<std::size_t>(compiled.System->GetSize()), 0.0);
    const NewtonOutcome outcome = SolveNewton(*compiled.System, compiled.Devices, x_dc, TimeContext{}, options);
    if (!outcome.Converged) {
        return std::unexpected(DescribeFailure(std::string(failure_message), outcome));
    }
    DescribeOutcome(outcome, "operating point", messages);
    return x_dc;
}

/**
 * @brief   Adds to the messages the line that says how a solve converged.
 * @param[in]  outcome   Outcome of the solve.
 * @param[in]  analysis  What was solved, such as "operating point".
 * @param[out] messages  Receives the line; may be null.
 */
void DescribeOutcome(const NewtonOutcome &outcome, const char *analysis, std::vector<DiagnosticMessage> *messages) {
    if (!messages) {
        return;
    }
    std::string text = std::format("nodalys: {} converged in {} iterations", analysis, outcome.Iterations);
    if (!outcome.Strategy.empty()) {
        text += std::format(" (with {})", outcome.Strategy);
    }
    messages->push_back({std::move(text), false});
}

/**
 * @brief   Makes an empty system with the same unknowns as another, nodes and branches in the same places.
 * @param[in] system  System whose layout is copied.
 * @return  A system with no stamps.
 */
MnaSystem MakeEmptySystemLike(const MnaSystem &system) {
    const int size = system.GetSize();
    std::vector<bool> node_flags(static_cast<std::size_t>(size), false);
    for (int index = 0; index < size; ++index) {
        node_flags[static_cast<std::size_t>(index)] = system.IsNodeUnknown(index);
    }
    return MnaSystem(std::move(node_flags));
}

/**
 * @brief   Stamps the small-signal reactive parts of every device, and the excitation of the AC sources, at an
 *          operating point.
 * @param[in] compiled  Compiled circuit with its devices.
 * @param[in] x_dc      The operating point.
 * @return  The system that the AC solves of the analysis read.
 */
MnaSystem BuildReactiveSystem(const CompiledCircuit &compiled, const std::vector<double> &x_dc) {
    MnaSystem reactive = MakeEmptySystemLike(*compiled.System);
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        device->StampAc(reactive, x_dc);
    }
    return reactive;
}

} // namespace Nodalys
