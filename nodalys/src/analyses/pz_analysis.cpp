/**
 * @file    pz_analysis.cpp
 * @brief   Small-signal pole-zero analysis solver for Nodalys.
 */

#include "analyses/analyses.h"

#include "devices/devices.h"
#include "linear/qz.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <utility>
#include <vector>

namespace nodalys {

/**
 * @brief   Finds the small-signal poles and zeros of the transfer function from an input source to an output node.
 * @param[in,out] compiled  Compiled circuit with equation system and devices.
 * @param[in]     settings  Output node, input source, and pole/zero selection.
 * @param[in]     options   Newton-Raphson convergence tolerances for operating point linearization.
 * @param[in,out] messages  Optional list receiving diagnostic lines.
 * @return  Result containing the complex poles and zeros in rad/s, or an error description.
 */
std::expected<PoleZeroResult, std::string> ComputePoleZero(CompiledCircuit &compiled, const PoleZeroConfig &settings,
                                                           const ConvergenceOptions &options,
                                                           std::vector<DiagnosticMessage> *messages) {

    const int out_node = settings.OutputNode;
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
        return std::unexpected(std::format("The source {} cannot drive a pole-zero analysis", settings.InputSource));
    }

    const int size = compiled.System->Size();
    std::vector<double> x_dc(static_cast<std::size_t>(size), 0.0);
    const NewtonOutcome outcome = SolveNewton(*compiled.System, compiled.Devices, x_dc, TimeContext{}, options);
    if (!outcome.Converged) {
        return std::unexpected("Nodalys found no operating point to linearize the pole-zero analysis around");
    }
    DescribeOutcome(outcome, "operating point", messages);

    std::vector<bool> node_flags(static_cast<std::size_t>(size), false);
    for (int i = 0; i < size; ++i) {
        node_flags[static_cast<std::size_t>(i)] = compiled.System->IsNodeUnknown(i);
    }
    MnaSystem reactive(std::move(node_flags));
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        device->StampAc(reactive, x_dc);
    }

    const DenseMatrix &g_mat = compiled.System->Matrix();
    const DenseMatrix &c_mat = reactive.Matrix();

    std::vector<double> b_vec(static_cast<std::size_t>(size), 0.0);
    if (voltage_source != nullptr) {
        b_vec[static_cast<std::size_t>(voltage_source->Branch())] = 1.0;
    } else {
        if (current_source->NodeA() >= 0) {
            b_vec[static_cast<std::size_t>(current_source->NodeA())] = -1.0;
        }
        if (current_source->NodeB() >= 0) {
            b_vec[static_cast<std::size_t>(current_source->NodeB())] = 1.0;
        }
    }

    const int out_idx = compiled.System->NodeIndex(out_node);
    std::vector<double> l_vec(static_cast<std::size_t>(size), 0.0);
    if (out_idx >= 0) {
        l_vec[static_cast<std::size_t>(out_idx)] = 1.0;
    }

    PoleZeroResult result;

    if (settings.FindPoles) {
        DenseMatrix b_p(static_cast<std::size_t>(size), static_cast<std::size_t>(size));
        for (std::size_t r = 0; r < static_cast<std::size_t>(size); ++r) {
            for (std::size_t c = 0; c < static_cast<std::size_t>(size); ++c) {
                b_p.At(r, c) = -c_mat.At(r, c);
            }
        }
        result.Poles = ComputeGeneralizedEigenvalues(g_mat, b_p);
    }

    if (settings.FindZeros) {
        const std::size_t m = static_cast<std::size_t>(size + 1);
        DenseMatrix a_z(m, m);
        DenseMatrix b_z(m, m);

        for (std::size_t r = 0; r < static_cast<std::size_t>(size); ++r) {
            for (std::size_t c = 0; c < static_cast<std::size_t>(size); ++c) {
                a_z.At(r, c) = g_mat.At(r, c);
                b_z.At(r, c) = -c_mat.At(r, c);
            }
            a_z.At(r, static_cast<std::size_t>(size)) = -b_vec[r];
        }

        for (std::size_t c = 0; c < static_cast<std::size_t>(size); ++c) {
            a_z.At(static_cast<std::size_t>(size), c) = l_vec[c];
        }

        result.Zeros = ComputeGeneralizedEigenvalues(a_z, b_z);
    }

    if (messages != nullptr) {
        messages->push_back(
            {std::format("nodalys: computed {} poles and {} zeros", result.Poles.size(), result.Zeros.size()), false});
    }

    return result;
}

} // namespace nodalys
