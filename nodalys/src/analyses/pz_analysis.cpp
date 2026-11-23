/**
 * @file    pz_analysis.cpp
 * @brief   Small-signal pole-zero analysis solver for Nodalys.
 */

#include "analyses/analyses.h"

#include "analyses/analysis_preamble.h"
#include "analyses/pz_checks.h"
#include "devices/device.h"
#include "linear/qz.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <utility>
#include <vector>

namespace Nodalys {

namespace {

/**
 * @brief   Builds the system G x + C dx/dt = B u, y = L x of the circuit linearized at an operating point.
 * @param[in] compiled     Compiled circuit, whose matrix holds the Jacobian of the operating point just solved.
 * @param[in] x_dc         The operating point.
 * @param[in] output_node  Node whose voltage is the output.
 * @param[in] input        The source that is the input.
 * @return  The linearized system.
 */
SmallSignalSystem BuildSmallSignalSystem(CompiledCircuit &compiled,
                                         const std::vector<double> &x_dc,
                                         const int output_node,
                                         const InputSource &input) {
    const int size = compiled.System->GetSize();
    const MnaSystem reactive = BuildReactiveSystem(compiled, x_dc);

    SmallSignalSystem system{.Conductance = compiled.System->Matrix(),
                             .Reactive = reactive.Matrix(),
                             .Input = std::vector<double>(static_cast<std::size_t>(size), 0.0),
                             .Output = std::vector<double>(static_cast<std::size_t>(size), 0.0)};
    if (const VoltageSourceDevice *voltage = input.AsVoltage()) {
        system.Input[static_cast<std::size_t>(voltage->GetBranch())] = 1.0;
    } else {
        const IndependentSourceDevice &source = input.GetSource();
        system.IsCurrentInput = true;
        system.PortPositive = source.GetNodeA();
        system.PortNegative = source.GetNodeB();
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

/**
 * @brief   The poles: the finite generalized eigenvalues of (G, -C), where det(G + sC) = 0.
 */
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
std::expected<PoleZeroResult, std::string> ComputePoleZero(CompiledCircuit &compiled,
                                                           const PoleZeroConfig &settings,
                                                           const ConvergenceOptions &options,
                                                           std::vector<DiagnosticMessage> *messages) {

    const int out_node = settings.OutputNode;
    if (const std::optional<std::string> refusal = CheckOutputNode(compiled, out_node)) {
        return std::unexpected(*refusal);
    }
    const std::expected<InputSource, std::string> input =
        ResolveInputSource(compiled, settings.InputSource, "a pole-zero analysis");
    if (!input) {
        return std::unexpected(input.error());
    }

    const std::expected<std::vector<double>, std::string> x_dc =
        SolveOperatingPoint(compiled,
                            options,
                            "Nodalys found no operating point to linearize the pole-zero analysis around",
                            messages);
    if (!x_dc) {
        return std::unexpected(x_dc.error());
    }

    const SmallSignalSystem system = BuildSmallSignalSystem(compiled, *x_dc, out_node, *input);

    PoleZeroResult result;
    if (settings.FindPoles) {
        result.Poles = FindPoles(system);
    }
    if (settings.FindZeros) {
        result.Zeros = FindZeros(system);
    }

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
