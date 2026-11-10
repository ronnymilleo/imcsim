/**
 * @file    tf_analysis.cpp
 * @brief   DC small-signal transfer function (gain, input and output impedance) solver.
 */

#include "analyses/analyses.h"

#include "devices/devices.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace nodalys {

namespace {

constexpr double OPEN_CIRCUIT_IMPEDANCE = 1e20;
constexpr double SHORT_CIRCUIT_THRESHOLD = 1e-15;

} // namespace

std::expected<TransferFunctionResult, std::string> ComputeTransferFunction(
    CompiledCircuit &compiled,
    const TransferFunctionConfig &settings,
    const ConvergenceOptions &options,
    std::vector<DiagnosticMessage> *messages) {

    const int out_node = settings.OutputPositiveNode;
    if (out_node <= 0 || out_node >= compiled.CircuitNodeCount) {
        return std::unexpected(std::format("The circuit has no output node {}", out_node));
    }
    const auto source_it = compiled.SourcesByName.find(settings.InputSource);
    if (source_it == compiled.SourcesByName.end()) {
        return std::unexpected(std::format("The circuit has no source named {}", settings.InputSource));
    }

    auto *voltage_source = dynamic_cast<VoltageSourceDevice *>(source_it->second);
    auto *current_source = dynamic_cast<CurrentSourceDevice *>(source_it->second);
    if (voltage_source == nullptr && current_source == nullptr) {
        return std::unexpected(std::format("The source {} cannot drive a transfer function", settings.InputSource));
    }

    const int size = compiled.System->Size();
    std::vector<double> x_dc(static_cast<std::size_t>(size), 0.0);
    const NewtonOutcome outcome = SolveNewton(*compiled.System, compiled.Devices, x_dc, TimeContext{}, options);
    if (!outcome.Converged) {
        return std::unexpected("The native solver found no operating point for the transfer function");
    }
    DescribeOutcome(outcome, "operating point", messages);

    const int out_idx = compiled.System->NodeIndex(out_node);

    compiled.System->ClearRhs();
    if (voltage_source != nullptr) {
        compiled.System->AddRhs(voltage_source->Branch(), 1.0);
    } else {
        compiled.System->AddRhs(current_source->NodeA(), -1.0);
        compiled.System->AddRhs(current_source->NodeB(), 1.0);
    }

    std::vector<double> dx_in(static_cast<std::size_t>(size), 0.0);
    if (!compiled.System->Solve(dx_in)) {
        return std::unexpected("The native solver encountered a singular matrix while computing the transfer function");
    }

    TransferFunctionResult result;
    result.OutputPositiveNode = out_node;
    result.OutputNegativeNode = settings.OutputNegativeNode;
    result.InputSource = settings.InputSource;
    result.GainUnit = voltage_source != nullptr ? "V/V" : "V/A";
    result.Gain = dx_in[static_cast<std::size_t>(out_idx)];

    if (voltage_source != nullptr) {
        const double current_into_circuit = -dx_in[static_cast<std::size_t>(voltage_source->Branch())];
        if (std::fabs(current_into_circuit) <= 1.01 * options.Gmin) {
            result.InputImpedance = OPEN_CIRCUIT_IMPEDANCE;
        } else {
            result.InputImpedance = 1.0 / current_into_circuit;
        }
    } else {
        const double va =
            current_source->NodeA() < 0 ? 0.0 : dx_in[static_cast<std::size_t>(current_source->NodeA())];
        const double vb =
            current_source->NodeB() < 0 ? 0.0 : dx_in[static_cast<std::size_t>(current_source->NodeB())];
        result.InputImpedance = std::min(vb - va, OPEN_CIRCUIT_IMPEDANCE);
    }

    compiled.System->ClearRhs();
    compiled.System->AddRhs(out_idx, 1.0);
    std::vector<double> dx_out(static_cast<std::size_t>(size), 0.0);
    if (!compiled.System->Solve(dx_out)) {
        return std::unexpected("The native solver encountered a singular matrix while computing the output impedance");
    }
    const double z_out = dx_out[static_cast<std::size_t>(out_idx)];
    result.OutputImpedance =
        std::fabs(z_out) < SHORT_CIRCUIT_THRESHOLD ? 0.0 : std::min(z_out, OPEN_CIRCUIT_IMPEDANCE);

    if (messages) {
        messages->push_back({"nodalys: computed small-signal transfer function and impedances", false});
    }
    return result;
}

} // namespace nodalys
