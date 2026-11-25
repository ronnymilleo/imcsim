/**
 * @file    sens_analysis.cpp
 * @brief   Small-signal DC sensitivity analysis (.sens) solver.
 * @details Computes the first-order sensitivity of a specified output node voltage with respect to circuit element
 *          values and parameters around the DC operating point using the adjoint network method:
 *          J^T * lambda = e_out
 *          where J is the converged DC Jacobian matrix and e_out is the unit selector vector for the output port.
 *          For each circuit device, the sensitivity dV_out/dp = -lambda^T * (df/dp) is evaluated analytically.
 */

#include "analyses/analyses.h"

#include "analyses/common/analysis_preamble.h"
#include "devices/sensitivity.h"
#include "text/text_case.h"
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Nodalys {

namespace {

std::optional<std::string> CheckOutputNodes(const CompiledCircuit &compiled, const SensitivityConfig &settings) {
    if (settings.OutputPositiveNode < 0 || settings.OutputPositiveNode >= compiled.CircuitNodeCount) {
        return std::format("The circuit has no output node {}", settings.OutputPositiveNode);
    }
    if (settings.OutputNegativeNode < 0 || settings.OutputNegativeNode >= compiled.CircuitNodeCount) {
        return std::format("The circuit has no negative output node {}", settings.OutputNegativeNode);
    }
    if (settings.OutputPositiveNode == settings.OutputNegativeNode) {
        return "Pick an output node; ground has no sensitivity";
    }
    return std::nullopt;
}

/**
 * @brief   The excitation of the adjoint system: +1 at the positive output node, -1 at the negative one.
 */
std::vector<double> MakeAdjointExcitation(const MnaSystem &system, const SensitivityConfig &settings) {
    const int pos_unknown = system.NodeIndex(settings.OutputPositiveNode);
    const int neg_unknown = system.NodeIndex(settings.OutputNegativeNode);

    std::vector<double> excitation(static_cast<std::size_t>(system.GetSize()), 0.0);
    if (pos_unknown >= 0 && static_cast<std::size_t>(pos_unknown) < excitation.size()) {
        excitation[static_cast<std::size_t>(pos_unknown)] = 1.0;
    }
    if (neg_unknown >= 0 && static_cast<std::size_t>(neg_unknown) < excitation.size()) {
        excitation[static_cast<std::size_t>(neg_unknown)] = -1.0;
    }
    return excitation;
}

/**
 * @brief   A subcircuit element is told apart from a top-level one of the same name by the letter of its kind before
 * it.
 */
std::string MakeVectorKey(const std::string_view kind_prefix, const std::string &lower_name, const bool is_subcircuit) {
    return is_subcircuit ? std::string(kind_prefix) + lower_name : lower_name;
}

void StoreSensitivity(SensitivityResult &result,
                      const std::string &derivative_key,
                      const std::string &scale_key,
                      const double derivative,
                      const double scale) {
    result.Vectors[derivative_key] = derivative;
    result.Vectors[scale_key] = scale;
}

void StoreDeviceSensitivity(SensitivityResult &result,
                            Device &device,
                            const std::string &lower_name,
                            const DcAdjointView &point) {
    const SensitiveDevice *sensitive = device.AsSensitive();
    if (sensitive == nullptr) {
        return;
    }
    const std::optional<SensitivityDescriptor> descriptor = sensitive->DescribeSensitivity();
    if (!descriptor) {
        return;
    }
    const double derivative = sensitive->DcSensitivityDerivative(point);
    const double scale = descriptor->Value * derivative;
    if (descriptor->IsGainParameter) {
        StoreSensitivity(result, lower_name + "_gain", lower_name + "_scale", derivative, scale);
        return;
    }
    const std::string vec_key = MakeVectorKey(descriptor->SubcircuitPrefix, lower_name, lower_name.contains('.'));
    StoreSensitivity(result, vec_key, vec_key + "_scale", derivative, scale);
}

} // namespace

/**
 * @brief   Calculates small-signal DC sensitivity vectors for all circuit elements using the adjoint method.
 * @param[in,out] compiled  Compiled circuit representation with MNA matrix and stamped devices.
 * @param[in]     settings  Output node pair (positive and negative terminal).
 * @param[in]     options   Numerical convergence limits and tolerances for the operating point.
 * @param[out]    messages  Diagnostic messages collected during the operating point solve.
 * @return  Sensitivity result holding the map of vector names to sensitivity values, or an error description.
 */
std::expected<SensitivityResult, std::string> ComputeSensitivity(CompiledCircuit &compiled,
                                                                 const SensitivityConfig &settings,
                                                                 const ConvergenceOptions &options,
                                                                 std::vector<DiagnosticMessage> *messages) {
    if (const std::optional<std::string> refusal = CheckOutputNodes(compiled, settings)) {
        return std::unexpected(*refusal);
    }
    const std::expected<std::vector<double>, std::string> x_dc =
        SolveOperatingPoint(compiled,
                            options,
                            "Nodalys found no operating point for the sensitivity analysis",
                            messages);
    if (!x_dc) {
        return std::unexpected(x_dc.error());
    }

    std::vector<double> adjoint;
    if (!compiled.System->SolveTranspose(MakeAdjointExcitation(*compiled.System, settings), adjoint)) {
        return std::unexpected("Nodalys encountered a singular matrix while solving the adjoint sensitivity system");
    }

    SensitivityResult result;
    result.OutputPositiveNode = settings.OutputPositiveNode;
    result.OutputNegativeNode = settings.OutputNegativeNode;

    const DcAdjointView point{.DcSolution = *x_dc, .Adjoint = adjoint};
    for (const auto &[name, device] : compiled.ElementsByName) {
        if (device == nullptr) {
            continue;
        }
        StoreDeviceSensitivity(result, *device, ToLower(name), point);
    }
    return result;
}

} // namespace Nodalys
