/**
 * @file    pz_analysis.cpp
 * @brief   Small-signal pole-zero analysis solver for Nodalys.
 */

#include "analyses/analyses.h"

#include "analyses/pz_checks.h"
#include "devices/devices.h"
#include "linear/qz.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <limits>
#include <utility>
#include <vector>

namespace Nodalys {

namespace {

/**
 * @brief   Builds the system G x + C dx/dt = B u, y = L x of the circuit linearized at an operating point.
 * @param[in] compiled  Compiled circuit, whose matrix holds the Jacobian of the operating point just solved.
 * @param[in] x_dc      The operating point.
 * @param[in] output_node  Node whose voltage is the output.
 * @param[in] voltage_source  The input when it is a voltage source, or null.
 * @param[in] current_source  The input when it is a current source, or null.
 */
SmallSignalSystem BuildSmallSignalSystem(CompiledCircuit &compiled, const std::vector<double> &x_dc,
                                         const int output_node, const VoltageSourceDevice *voltage_source,
                                         const CurrentSourceDevice *current_source) {
    const int size = compiled.System->GetSize();
    std::vector<bool> node_flags(static_cast<std::size_t>(size), false);
    for (int i = 0; i < size; ++i) {
        node_flags[static_cast<std::size_t>(i)] = compiled.System->IsNodeUnknown(i);
    }
    MnaSystem reactive(std::move(node_flags));
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        device->StampAc(reactive, x_dc);
    }

    SmallSignalSystem system{.Conductance = compiled.System->Matrix(),
                             .Reactive = reactive.Matrix(),
                             .Input = std::vector<double>(static_cast<std::size_t>(size), 0.0),
                             .Output = std::vector<double>(static_cast<std::size_t>(size), 0.0)};
    if (voltage_source != nullptr) {
        system.Input[static_cast<std::size_t>(voltage_source->GetBranch())] = 1.0;
    } else {
        system.IsCurrentInput = true;
        system.PortPositive = current_source->GetNodeA();
        system.PortNegative = current_source->GetNodeB();
        if (system.PortPositive >= 0) {
            system.Input[static_cast<std::size_t>(system.PortPositive)] = -1.0;
        }
        if (system.PortNegative >= 0) {
            system.Input[static_cast<std::size_t>(system.PortNegative)] = 1.0;
        }
    }
    const int output_index = compiled.System->NodeIndex(output_node);
    if (output_index >= 0) {
        system.Output[static_cast<std::size_t>(output_index)] = 1.0;
    }
    return system;
}

/// The poles: the finite generalized eigenvalues of (G, -C), where det(G + sC) = 0.
std::vector<std::complex<double>> FindPoles(const SmallSignalSystem &system) {
    const std::size_t size = system.Input.size();
    DenseMatrix negative_reactive(size, size);
    for (std::size_t r = 0; r < size; ++r) {
        for (std::size_t c = 0; c < size; ++c) {
            negative_reactive.At(r, c) = -system.Reactive.At(r, c);
        }
    }
    return ComputeGeneralizedEigenvalues(system.Conductance, negative_reactive);
}

/**
 * @brief   The zeros: the finite generalized eigenvalues of the Rosenbrock system matrix
 *          [G + sC, -B; L^T, 0], where the output is zero for an input that is not.
 */
std::vector<std::complex<double>> FindZeros(const SmallSignalSystem &system) {
    const std::size_t size = system.Input.size();
    DenseMatrix constant_part(size + 1, size + 1);
    DenseMatrix dynamic_part(size + 1, size + 1);
    for (std::size_t r = 0; r < size; ++r) {
        for (std::size_t c = 0; c < size; ++c) {
            constant_part.At(r, c) = system.Conductance.At(r, c);
            dynamic_part.At(r, c) = -system.Reactive.At(r, c);
        }
        constant_part.At(r, size) = -system.Input[r];
    }
    for (std::size_t c = 0; c < size; ++c) {
        constant_part.At(size, c) = system.Output[c];
    }
    return ComputeGeneralizedEigenvalues(constant_part, dynamic_part);
}

/// Roots closer to the origin than this many roundings of the largest root cannot be told from 0.
constexpr double ORIGIN_NOISE_FACTOR = 10.0;

/**
 * @brief   Sets to exactly 0 the roots that are only rounding noise around the origin.
 * @details QZ finds each eigenvalue to about a rounding of the largest one, so beside a junction capacitance with a
 *          pole at 3e12 rad/s, a zero that is really at the origin comes out at 1e-6, and in the right half-plane half
 *          of the time, which would read as an instability that the circuit does not have.
 */
void SnapNoiseToOrigin(PoleZeroResult &roots) {
    double largest = 0.0;
    for (const auto &list : {roots.Poles, roots.Zeros}) {
        for (const std::complex<double> &root : list) {
            largest = std::max(largest, std::abs(root));
        }
    }
    const double noise = ORIGIN_NOISE_FACTOR * std::numeric_limits<double>::epsilon() * largest;
    for (auto *list : {&roots.Poles, &roots.Zeros}) {
        for (std::complex<double> &root : *list) {
            if (std::abs(root) <= noise) {
                root = {0.0, 0.0};
            }
        }
    }
}

} // namespace

/**
 * @brief   Finds the small-signal poles and zeros of the transfer function from an input source to an output node.
 * @details The roots are the finite generalized eigenvalues of two pencils, found by QZ, so no starting guess is
 *          needed and no root can be skipped for being far from one. Two checks then back the result: an output that
 *          the input does not drive is refused, and roots that do not rebuild the response of the circuit at the
 *          frequencies around them are reported as a warning, so a short or wrong list is never silent.
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

    const auto *voltage_source = dynamic_cast<VoltageSourceDevice *>(source_it->second);
    const auto *current_source = dynamic_cast<CurrentSourceDevice *>(source_it->second);
    if (voltage_source == nullptr && current_source == nullptr) {
        return std::unexpected(std::format("The source {} cannot drive a pole-zero analysis", settings.InputSource));
    }

    std::vector<double> x_dc(static_cast<std::size_t>(compiled.System->GetSize()), 0.0);
    const NewtonOutcome outcome = SolveNewton(*compiled.System, compiled.Devices, x_dc, TimeContext{}, options);
    if (!outcome.Converged) {
        return std::unexpected(
            DescribeFailure("Nodalys found no operating point to linearize the pole-zero analysis around", outcome));
    }
    DescribeOutcome(outcome, "operating point", messages);

    const SmallSignalSystem system = BuildSmallSignalSystem(compiled, x_dc, out_node, voltage_source, current_source);

    PoleZeroResult result;
    if (settings.FindPoles) {
        result.Poles = FindPoles(system);
    }
    if (settings.FindZeros) {
        result.Zeros = FindZeros(system);
    }

    SnapNoiseToOrigin(result);

    if (const std::optional<std::string> reason = FindUndrivenOutput(system, result)) {
        return std::unexpected(*reason);
    }
    // A partial list cannot rebuild the response, so the check only applies when both lists were asked for
    if (settings.FindPoles && settings.FindZeros) {
        if (const std::optional<std::string> warning = FindRootMismatch(system, result)) {
            result.Warnings.push_back(*warning);
        }
    }

    if (messages != nullptr) {
        messages->push_back(
            {std::format("nodalys: computed {} poles and {} zeros", result.Poles.size(), result.Zeros.size()), false});
    }
    return result;
}

} // namespace Nodalys
