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

#include "devices/devices.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <string>

namespace Nodalys {

namespace {

std::string ToLower(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
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
    const int pos_node = settings.OutputPositiveNode;
    const int neg_node = settings.OutputNegativeNode;

    if (pos_node < 0 || pos_node >= compiled.CircuitNodeCount) {
        return std::unexpected(std::format("The circuit has no output node {}", pos_node));
    }
    if (neg_node < 0 || neg_node >= compiled.CircuitNodeCount) {
        return std::unexpected(std::format("The circuit has no negative output node {}", neg_node));
    }
    if (pos_node == neg_node) {
        return std::unexpected("Pick an output node; ground has no sensitivity");
    }

    const int size = compiled.System->GetSize();
    std::vector<double> x_dc(static_cast<std::size_t>(size), 0.0);
    const NewtonOutcome outcome = SolveNewton(*compiled.System, compiled.Devices, x_dc, TimeContext{}, options);
    if (!outcome.Converged) {
        return std::unexpected(
            DescribeFailure("Nodalys found no operating point for the sensitivity analysis", outcome));
    }
    DescribeOutcome(outcome, "operating point", messages);

    // Build adjoint excitation vector d: +1 at positive output node, -1 at negative output node
    const int pos_unknown = compiled.System->NodeIndex(pos_node);
    const int neg_unknown = compiled.System->NodeIndex(neg_node);

    std::vector<double> d(static_cast<std::size_t>(size), 0.0);
    if (pos_unknown >= 0 && static_cast<std::size_t>(pos_unknown) < d.size()) {
        d[static_cast<std::size_t>(pos_unknown)] = 1.0;
    }
    if (neg_unknown >= 0 && static_cast<std::size_t>(neg_unknown) < d.size()) {
        d[static_cast<std::size_t>(neg_unknown)] = -1.0;
    }

    std::vector<double> adjoint;
    if (!compiled.System->SolveTranspose(d, adjoint)) {
        return std::unexpected("Nodalys encountered a singular matrix while solving the adjoint sensitivity system");
    }

    auto voltage_at = [&](const int unknown) -> double {
        return (unknown >= 0 && static_cast<std::size_t>(unknown) < x_dc.size())
                   ? x_dc[static_cast<std::size_t>(unknown)]
                   : 0.0;
    };

    auto adjoint_at = [&](const int unknown) -> double {
        return (unknown >= 0 && static_cast<std::size_t>(unknown) < adjoint.size())
                   ? adjoint[static_cast<std::size_t>(unknown)]
                   : 0.0;
    };

    SensitivityResult result;
    result.OutputPositiveNode = pos_node;
    result.OutputNegativeNode = neg_node;

    for (const auto &[name, device] : compiled.ElementsByName) {
        if (device == nullptr) {
            continue;
        }

        const std::string lower_name = ToLower(name);
        const bool is_subcircuit = lower_name.contains('.');

        if (const auto *resistor = dynamic_cast<const ResistorDevice *>(device)) {
            const double resistance = resistor->GetResistance();
            if (resistance <= 0.0) {
                continue;
            }
            const double vr = voltage_at(resistor->GetNodeA()) - voltage_at(resistor->GetNodeB());
            const double lr = adjoint_at(resistor->GetNodeA()) - adjoint_at(resistor->GetNodeB());
            const double d_v_dr = (vr * lr) / (resistance * resistance);
            const double scale = resistance * d_v_dr;

            const std::string vec_key = is_subcircuit ? "r." + lower_name : lower_name;
            result.Vectors[vec_key] = d_v_dr;
            result.Vectors[vec_key + "_scale"] = scale;
        } else if (const auto *vsource = dynamic_cast<const VoltageSourceDevice *>(device)) {
            const double vs = vsource->GetDcValue();
            const double d_v_dvs = adjoint_at(vsource->GetBranch());
            const double scale = vs * d_v_dvs;

            const std::string vec_key = is_subcircuit ? "v." + lower_name : lower_name;
            result.Vectors[vec_key] = d_v_dvs;
            result.Vectors[vec_key + "_scale"] = scale;
        } else if (const auto *isource = dynamic_cast<const CurrentSourceDevice *>(device)) {
            const double is = isource->GetDcValue();
            const double d_v_dis = adjoint_at(isource->GetNodeB()) - adjoint_at(isource->GetNodeA());
            const double scale = is * d_v_dis;

            const std::string vec_key = is_subcircuit ? "i." + lower_name : lower_name;
            result.Vectors[vec_key] = d_v_dis;
            result.Vectors[vec_key + "_scale"] = scale;
        } else if (dynamic_cast<const CapacitorDevice *>(device) != nullptr) {
            const std::string vec_key = is_subcircuit ? "c." + lower_name : lower_name;
            result.Vectors[vec_key] = 0.0;
            result.Vectors[vec_key + "_scale"] = 0.0;
        } else if (dynamic_cast<const InductorDevice *>(device) != nullptr) {
            const std::string vec_key = is_subcircuit ? "l." + lower_name : lower_name;
            result.Vectors[vec_key] = 0.0;
            result.Vectors[vec_key + "_scale"] = 0.0;
        } else if (const auto *vcvs = dynamic_cast<const VcvsDevice *>(device)) {
            const double v_ctrl = voltage_at(vcvs->GetControlPositive()) - voltage_at(vcvs->GetControlNegative());
            const double d_v_dgain = adjoint_at(vcvs->GetBranch()) * v_ctrl;
            const double scale = vcvs->GetGain() * d_v_dgain;

            result.Vectors[lower_name + "_gain"] = d_v_dgain;
            result.Vectors[lower_name + "_scale"] = scale;
        } else if (const auto *vccs = dynamic_cast<const VccsDevice *>(device)) {
            const double v_ctrl = voltage_at(vccs->GetControlPositive()) - voltage_at(vccs->GetControlNegative());
            const double d_v_dgain = (adjoint_at(vccs->GetNodeB()) - adjoint_at(vccs->GetNodeA())) * v_ctrl;
            const double scale = vccs->GetGain() * d_v_dgain;

            result.Vectors[lower_name + "_gain"] = d_v_dgain;
            result.Vectors[lower_name + "_scale"] = scale;
        } else if (const auto *cccs = dynamic_cast<const CccsDevice *>(device)) {
            const int cb = cccs->ControlBranch();
            const double i_ctrl =
                (cb >= 0 && static_cast<std::size_t>(cb) < x_dc.size()) ? x_dc[static_cast<std::size_t>(cb)] : 0.0;
            const double d_v_dgain = (adjoint_at(cccs->GetNodeB()) - adjoint_at(cccs->GetNodeA())) * i_ctrl;
            const double scale = cccs->GetGain() * d_v_dgain;

            result.Vectors[lower_name + "_gain"] = d_v_dgain;
            result.Vectors[lower_name + "_scale"] = scale;
        } else if (const auto *ccvs = dynamic_cast<const CcvsDevice *>(device)) {
            const int cb = ccvs->ControlBranch();
            const double i_ctrl =
                (cb >= 0 && static_cast<std::size_t>(cb) < x_dc.size()) ? x_dc[static_cast<std::size_t>(cb)] : 0.0;
            const double d_v_dgain = adjoint_at(ccvs->GetBranch()) * i_ctrl;
            const double scale = ccvs->GetGain() * d_v_dgain;

            result.Vectors[lower_name + "_gain"] = d_v_dgain;
            result.Vectors[lower_name + "_scale"] = scale;
        }
    }

    return result;
}

} // namespace Nodalys
