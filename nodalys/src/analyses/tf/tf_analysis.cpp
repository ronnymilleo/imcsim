/**
 * @file    tf_analysis.cpp
 * @brief   DC small-signal transfer function (gain, input and output impedance) solver.
 */

#include "analyses/analyses.h"

#include "analyses/common/analysis_preamble.h"
#include "devices/sources/independent_sources.h"
#include <algorithm>
#include <cmath>

namespace Nodalys {

namespace {

constexpr double OPEN_CIRCUIT_IMPEDANCE = 1e20;
constexpr double SHORT_CIRCUIT_THRESHOLD = 1e-15;

bool SolveInputExcitation(MnaSystem &system, const InputSource &input, std::vector<double> &dx_in) {
    system.ClearRhs();
    if (const VoltageSourceDevice *voltage_source = input.AsVoltage()) {
        system.AddRhs(voltage_source->GetBranch(), 1.0);
    } else {
        const IndependentSourceDevice &source = input.GetSource();
        system.AddRhs(source.GetNodeA(), -1.0);
        system.AddRhs(source.GetNodeB(), 1.0);
    }
    return system.Solve(dx_in);
}

double ComputeInputImpedance(const InputSource &input, const std::vector<double> &dx_in, const double gmin) {
    if (const VoltageSourceDevice *voltage_source = input.AsVoltage()) {
        const double current_into_circuit = -dx_in[static_cast<std::size_t>(voltage_source->GetBranch())];
        return std::fabs(current_into_circuit) <= 1.01 * gmin ? OPEN_CIRCUIT_IMPEDANCE : 1.0 / current_into_circuit;
    }
    const IndependentSourceDevice &source = input.GetSource();
    const double va = source.GetNodeA() < 0 ? 0.0 : dx_in[static_cast<std::size_t>(source.GetNodeA())];
    const double vb = source.GetNodeB() < 0 ? 0.0 : dx_in[static_cast<std::size_t>(source.GetNodeB())];
    return std::min(vb - va, OPEN_CIRCUIT_IMPEDANCE);
}

/**
 * @brief   Injects a unit test current at the output node and returns the clamped small-signal output impedance.
 */
std::expected<double, std::string> SolveOutputImpedance(MnaSystem &system, const int out_idx) {
    system.ClearRhs();
    system.AddRhs(out_idx, 1.0);
    std::vector<double> dx_out(static_cast<std::size_t>(system.GetSize()), 0.0);
    if (!system.Solve(dx_out)) {
        return std::unexpected("Nodalys encountered a singular matrix while computing the output impedance");
    }
    const double z_out = dx_out[static_cast<std::size_t>(out_idx)];
    return std::fabs(z_out) < SHORT_CIRCUIT_THRESHOLD ? 0.0 : std::min(z_out, OPEN_CIRCUIT_IMPEDANCE);
}

} // namespace

/**
 * @brief   Computes the DC small-signal gain, input impedance and output impedance from a source to an output node.
 * @param[in,out] compiled  Compiled circuit with its matrix and devices.
 * @param[in]     settings  Output node and input source.
 * @param[in]     options   Numerical limits of the operating point.
 * @param[out]    messages  Diagnostic messages collected during the analysis; may be null.
 * @return  The gain and the impedances, or an error description.
 */
std::expected<TransferFunctionResult, std::string> ComputeTransferFunction(CompiledCircuit &compiled,
                                                                           const TransferFunctionConfig &settings,
                                                                           const ConvergenceOptions &options,
                                                                           std::vector<DiagnosticMessage> *messages) {
    const int out_node = settings.OutputPositiveNode;
    if (const std::optional<std::string> refusal = CheckOutputNode(compiled, out_node)) {
        return std::unexpected(*refusal);
    }
    const auto input = ResolveInputSource(compiled, settings.InputSource, "a transfer function");
    if (!input) {
        return std::unexpected(input.error());
    }
    if (const auto x_dc = SolveOperatingPoint(compiled,
                                              options,
                                              "Nodalys found no operating point for the transfer function",
                                              messages);
        !x_dc) {
        return std::unexpected(x_dc.error());
    }

    const int out_idx = compiled.System->NodeIndex(out_node);
    std::vector<double> dx_in(static_cast<std::size_t>(compiled.System->GetSize()), 0.0);
    if (!SolveInputExcitation(*compiled.System, *input, dx_in)) {
        return std::unexpected("Nodalys encountered a singular matrix while computing the transfer function");
    }
    const std::expected<double, std::string> z_out = SolveOutputImpedance(*compiled.System, out_idx);
    if (!z_out) {
        return std::unexpected(z_out.error());
    }
    if (messages) {
        messages->push_back({"nodalys: computed small-signal transfer function and impedances", false});
    }
    return TransferFunctionResult{.InputSource = settings.InputSource,
                                  .OutputPositiveNode = out_node,
                                  .OutputNegativeNode = settings.OutputNegativeNode,
                                  .Gain = dx_in[static_cast<std::size_t>(out_idx)],
                                  .GainUnit = input->IsVoltage() ? "V/V" : "V/A",
                                  .InputImpedance = ComputeInputImpedance(*input, dx_in, options.Gmin),
                                  .OutputImpedance = *z_out};
}

} // namespace Nodalys
