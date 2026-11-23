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
    const std::expected<InputSource, std::string> input =
        ResolveInputSource(compiled, settings.InputSource, "a transfer function");
    if (!input) {
        return std::unexpected(input.error());
    }
    const VoltageSourceDevice *voltage_source = input->AsVoltage();
    const IndependentSourceDevice &input_source = input->GetSource();

    const int size = compiled.System->GetSize();
    const std::expected<std::vector<double>, std::string> x_dc =
        SolveOperatingPoint(compiled, options, "Nodalys found no operating point for the transfer function", messages);
    if (!x_dc) {
        return std::unexpected(x_dc.error());
    }

    const int out_idx = compiled.System->NodeIndex(out_node);

    compiled.System->ClearRhs();
    if (voltage_source != nullptr) {
        compiled.System->AddRhs(voltage_source->GetBranch(), 1.0);
    } else {
        compiled.System->AddRhs(input_source.GetNodeA(), -1.0);
        compiled.System->AddRhs(input_source.GetNodeB(), 1.0);
    }

    std::vector<double> dx_in(static_cast<std::size_t>(size), 0.0);
    if (!compiled.System->Solve(dx_in)) {
        return std::unexpected("Nodalys encountered a singular matrix while computing the transfer function");
    }

    TransferFunctionResult result;
    result.OutputPositiveNode = out_node;
    result.OutputNegativeNode = settings.OutputNegativeNode;
    result.InputSource = settings.InputSource;
    result.GainUnit = voltage_source != nullptr ? "V/V" : "V/A";
    result.Gain = dx_in[static_cast<std::size_t>(out_idx)];

    if (voltage_source != nullptr) {
        const double current_into_circuit = -dx_in[static_cast<std::size_t>(voltage_source->GetBranch())];
        if (std::fabs(current_into_circuit) <= 1.01 * options.Gmin) {
            result.InputImpedance = OPEN_CIRCUIT_IMPEDANCE;
        } else {
            result.InputImpedance = 1.0 / current_into_circuit;
        }
    } else {
        const double va = input_source.GetNodeA() < 0 ? 0.0 : dx_in[static_cast<std::size_t>(input_source.GetNodeA())];
        const double vb = input_source.GetNodeB() < 0 ? 0.0 : dx_in[static_cast<std::size_t>(input_source.GetNodeB())];
        result.InputImpedance = std::min(vb - va, OPEN_CIRCUIT_IMPEDANCE);
    }

    compiled.System->ClearRhs();
    compiled.System->AddRhs(out_idx, 1.0);
    std::vector<double> dx_out(static_cast<std::size_t>(size), 0.0);
    if (!compiled.System->Solve(dx_out)) {
        return std::unexpected("Nodalys encountered a singular matrix while computing the output impedance");
    }
    const double z_out = dx_out[static_cast<std::size_t>(out_idx)];
    result.OutputImpedance = std::fabs(z_out) < SHORT_CIRCUIT_THRESHOLD ? 0.0 : std::min(z_out, OPEN_CIRCUIT_IMPEDANCE);

    if (messages) {
        messages->push_back({"nodalys: computed small-signal transfer function and impedances", false});
    }
    return result;
}

} // namespace Nodalys
