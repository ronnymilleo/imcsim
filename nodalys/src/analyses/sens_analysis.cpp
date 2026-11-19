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

#include "analyses/analysis_preamble.h"
#include "devices/devices.h"
#include "text/text_case.h"
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Nodalys {

namespace {

/**
 * @struct  DcAdjointPoint
 * @brief   The operating point and the adjoint of the output, which every element reads its sensitivity from.
 * @details An unknown outside the vectors, such as ground, reads as zero.
 */
struct DcAdjointPoint {
    const std::vector<double> &DcSolution;
    const std::vector<double> &Adjoint;

    double VoltageAt(const int unknown) const {
        return (unknown >= 0 && static_cast<std::size_t>(unknown) < DcSolution.size())
                 ? DcSolution[static_cast<std::size_t>(unknown)]
                 : 0.0;
    }

    double AdjointAt(const int unknown) const {
        return (unknown >= 0 && static_cast<std::size_t>(unknown) < Adjoint.size())
                 ? Adjoint[static_cast<std::size_t>(unknown)]
                 : 0.0;
    }
};

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

/**
 * @brief   Stores the sensitivity of an element whose value is the parameter: a resistor, a source, a capacitor or an
 *          inductor.
 * @return  True when the device is one of these, even if it stores nothing; false for any other device.
 */
bool StoreValueSensitivity(SensitivityResult &result,
                           const Device *device,
                           const std::string &lower_name,
                           const bool is_subcircuit,
                           const DcAdjointPoint &point) {
    if (const auto *resistor = dynamic_cast<const ResistorDevice *>(device)) {
        const double resistance = resistor->GetResistance();
        if (resistance <= 0.0) {
            return true;
        }
        const double vr = point.VoltageAt(resistor->GetNodeA()) - point.VoltageAt(resistor->GetNodeB());
        const double lr = point.AdjointAt(resistor->GetNodeA()) - point.AdjointAt(resistor->GetNodeB());
        const double d_v_dr = (vr * lr) / (resistance * resistance);
        const double scale = resistance * d_v_dr;

        const std::string vec_key = MakeVectorKey("r.", lower_name, is_subcircuit);
        StoreSensitivity(result, vec_key, vec_key + "_scale", d_v_dr, scale);
        return true;
    }
    if (const auto *vsource = dynamic_cast<const VoltageSourceDevice *>(device)) {
        const double vs = vsource->GetDcValue();
        const double d_v_dvs = point.AdjointAt(vsource->GetBranch());
        const double scale = vs * d_v_dvs;

        const std::string vec_key = MakeVectorKey("v.", lower_name, is_subcircuit);
        StoreSensitivity(result, vec_key, vec_key + "_scale", d_v_dvs, scale);
        return true;
    }
    if (const auto *isource = dynamic_cast<const CurrentSourceDevice *>(device)) {
        const double is = isource->GetDcValue();
        const double d_v_dis = point.AdjointAt(isource->GetNodeB()) - point.AdjointAt(isource->GetNodeA());
        const double scale = is * d_v_dis;

        const std::string vec_key = MakeVectorKey("i.", lower_name, is_subcircuit);
        StoreSensitivity(result, vec_key, vec_key + "_scale", d_v_dis, scale);
        return true;
    }
    // A capacitor or an inductor does not change the DC solution, so its sensitivity is zero
    if (dynamic_cast<const CapacitorDevice *>(device) != nullptr) {
        const std::string vec_key = MakeVectorKey("c.", lower_name, is_subcircuit);
        StoreSensitivity(result, vec_key, vec_key + "_scale", 0.0, 0.0);
        return true;
    }
    if (dynamic_cast<const InductorDevice *>(device) != nullptr) {
        const std::string vec_key = MakeVectorKey("l.", lower_name, is_subcircuit);
        StoreSensitivity(result, vec_key, vec_key + "_scale", 0.0, 0.0);
        return true;
    }
    return false;
}

/**
 * @brief   Stores the sensitivity to the gain of a controlled source; any other device stores nothing.
 */
void StoreGainSensitivity(SensitivityResult &result,
                          const Device *device,
                          const std::string &lower_name,
                          const DcAdjointPoint &point) {
    double d_v_dgain = 0.0;
    double gain = 0.0;
    if (const auto *vcvs = dynamic_cast<const VcvsDevice *>(device)) {
        const double v_ctrl = point.VoltageAt(vcvs->GetControlPositive()) - point.VoltageAt(vcvs->GetControlNegative());
        d_v_dgain = point.AdjointAt(vcvs->GetBranch()) * v_ctrl;
        gain = vcvs->GetGain();
    } else if (const auto *vccs = dynamic_cast<const VccsDevice *>(device)) {
        const double v_ctrl = point.VoltageAt(vccs->GetControlPositive()) - point.VoltageAt(vccs->GetControlNegative());
        d_v_dgain = (point.AdjointAt(vccs->GetNodeB()) - point.AdjointAt(vccs->GetNodeA())) * v_ctrl;
        gain = vccs->GetGain();
    } else if (const auto *cccs = dynamic_cast<const CccsDevice *>(device)) {
        const double i_ctrl = point.VoltageAt(cccs->ControlBranch());
        d_v_dgain = (point.AdjointAt(cccs->GetNodeB()) - point.AdjointAt(cccs->GetNodeA())) * i_ctrl;
        gain = cccs->GetGain();
    } else if (const auto *ccvs = dynamic_cast<const CcvsDevice *>(device)) {
        const double i_ctrl = point.VoltageAt(ccvs->ControlBranch());
        d_v_dgain = point.AdjointAt(ccvs->GetBranch()) * i_ctrl;
        gain = ccvs->GetGain();
    } else {
        return;
    }
    StoreSensitivity(result, lower_name + "_gain", lower_name + "_scale", d_v_dgain, gain * d_v_dgain);
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

    const DcAdjointPoint point{.DcSolution = *x_dc, .Adjoint = adjoint};
    for (const auto &[name, device] : compiled.ElementsByName) {
        if (device == nullptr) {
            continue;
        }
        const std::string lower_name = ToLower(name);
        const bool is_subcircuit = lower_name.contains('.');
        if (!StoreValueSensitivity(result, device, lower_name, is_subcircuit, point)) {
            StoreGainSensitivity(result, device, lower_name, point);
        }
    }
    return result;
}

} // namespace Nodalys
