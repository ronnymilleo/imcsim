/**
 * @file    ac_sens_analysis.cpp
 * @brief   Small-signal AC sensitivity analysis: how the complex output voltage changes with every element value.
 * @details At each frequency the small-signal system is $Y(j\omega) x = b$ and the output is $V = d^T x$. One
 *          transposed solve $Y^T \lambda = d$ gives the derivative with respect to every element, $-\lambda^T
 *          (\partial Y / \partial p) x$. A nonlinear device also moves with the operating point, which an element
 *          value shifts: its weights $w_k = -\lambda^T (\partial Y / \partial x_k) x$ go through the DC adjoint
 *          $J^T \mu = w$ and add $-\mu^T g_p$, where $g_p$ is the derivative of the DC equations with respect to $p$.
 */

#include "analyses/analyses.h"

#include "analyses/ac_sens_bias.h"
#include "analyses/ac_sens_elements.h"
#include "devices/devices.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>

namespace Nodalys {

namespace {

using Complex = std::complex<double>;

/// Output smaller than this share of the AC magnitude of the input counts as zero: a relative change of it means
/// nothing, and it is far below what the solver resolves (about -180 dB).
constexpr double MIN_RELATIVE_OUTPUT = 1e-9;

/// Decibels of a magnitude ratio per neper.
constexpr double DECIBELS_PER_NEPER = 20.0 / std::numbers::ln10;
constexpr double DEGREES_PER_RADIAN = 180.0 / std::numbers::pi;
constexpr double PERCENT = 100.0;

/// The output node of the analysis and what reads its voltage.
struct Output {
    int Positive = -1;
    int Negative = -1;
};

std::optional<std::string> CheckConfig(const CompiledCircuit &compiled, const AcSensitivityConfig &settings) {
    if (settings.OutputPositiveNode < 1 || settings.OutputPositiveNode >= compiled.CircuitNodeCount) {
        return std::format("The circuit has no output node {}", settings.OutputPositiveNode);
    }
    if (settings.OutputNegativeNode < 0 || settings.OutputNegativeNode >= compiled.CircuitNodeCount) {
        return std::format("The circuit has no negative output node {}", settings.OutputNegativeNode);
    }
    if (settings.OutputPositiveNode == settings.OutputNegativeNode) {
        return "Pick an output node; ground has no sensitivity";
    }
    if (settings.StartFrequency <= 0.0 || settings.StopFrequency < settings.StartFrequency ||
        settings.PointsPerDecade <= 0) {
        return "The AC sensitivity analysis has invalid frequency settings; check start, stop and points per decade";
    }
    return std::nullopt;
}

/// The frequencies of a sweep, from the start to the stop with the stop included.
std::vector<double> MakeFrequencies(const AcSensitivityConfig &settings) {
    std::vector<double> frequencies;
    if (settings.SweepType == FrequencySweepType::LINEAR) {
        if (settings.PointsPerDecade == 1 || settings.StartFrequency == settings.StopFrequency) {
            frequencies.push_back(settings.StartFrequency);
            return frequencies;
        }
        const double step =
            (settings.StopFrequency - settings.StartFrequency) / static_cast<double>(settings.PointsPerDecade - 1);
        for (int index = 0; index < settings.PointsPerDecade; ++index) {
            frequencies.push_back(
                std::min(settings.StartFrequency + static_cast<double>(index) * step, settings.StopFrequency));
        }
        return frequencies;
    }
    const double base = settings.SweepType == FrequencySweepType::OCTAVE ? 2.0 : 10.0;
    for (int index = 0;; ++index) {
        const double frequency =
            settings.StartFrequency *
            std::pow(base, static_cast<double>(index) / static_cast<double>(settings.PointsPerDecade));
        if (frequency > settings.StopFrequency * (1.0 + 1e-9)) {
            break;
        }
        frequencies.push_back(std::min(frequency, settings.StopFrequency));
        if (frequency >= settings.StopFrequency * (1.0 - 1e-9)) {
            break;
        }
    }
    return frequencies;
}

/// The AC magnitude of the input source, or none when it is not an independent source that can drive the analysis.
std::optional<double> FindInputMagnitude(const Device &source) {
    if (const auto *voltage = dynamic_cast<const VoltageSourceDevice *>(&source)) {
        return voltage->GetAcMagnitude();
    }
    if (const auto *current = dynamic_cast<const CurrentSourceDevice *>(&source)) {
        return current->GetAcMagnitude();
    }
    return std::nullopt;
}

/// The small-signal system with the input source as its only excitation.
MnaSystem BuildReactiveSystem(const CompiledCircuit &compiled, const std::vector<double> &x_dc, const Device &input) {
    const int size = compiled.System->GetSize();
    std::vector<bool> node_flags(static_cast<std::size_t>(size), false);
    for (int index = 0; index < size; ++index) {
        node_flags[static_cast<std::size_t>(index)] = compiled.System->IsNodeUnknown(index);
    }
    MnaSystem reactive(std::move(node_flags));
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        device->StampAc(reactive, x_dc);
    }
    // Every AC source stamped its excitation above; only the input one stays, so the response is the one to it
    reactive.ClearRhs();
    input.StampAc(reactive, x_dc);
    return reactive;
}

/// The vector that picks the output voltage out of the solution.
std::vector<Complex> MakeOutputSelector(const MnaSystem &system, const AcSensitivityConfig &settings) {
    std::vector<Complex> selector(static_cast<std::size_t>(system.GetSize()), Complex{});
    const int positive = system.NodeIndex(settings.OutputPositiveNode);
    const int negative = system.NodeIndex(settings.OutputNegativeNode);
    if (positive >= 0) {
        selector[static_cast<std::size_t>(positive)] = 1.0;
    }
    if (negative >= 0) {
        selector[static_cast<std::size_t>(negative)] = -1.0;
    }
    return selector;
}

Complex ReadOutput(const std::vector<Complex> &selector, const std::vector<Complex> &x_ac) {
    Complex output{};
    for (std::size_t index = 0; index < selector.size(); ++index) {
        output += selector[index] * x_ac[index];
    }
    return output;
}

/// Writes the two figures of one derivative, or none where the output is too small to take a ratio to.
void StoreFigures(AcElementSensitivity &element, const Complex &output, const double minimum_output) {
    if (std::abs(output) <= minimum_output) {
        element.MagnitudeDecibelsPerPercent.push_back(std::nullopt);
        element.PhaseDegreesPerPercent.push_back(std::nullopt);
        return;
    }
    const Complex relative = element.Derivatives.back() / output * (element.Value / PERCENT);
    element.MagnitudeDecibelsPerPercent.push_back(DECIBELS_PER_NEPER * relative.real());
    element.PhaseDegreesPerPercent.push_back(DEGREES_PER_RADIAN * relative.imag());
}

/// Says once where the output was too small to give figures, or nothing when it was never.
std::optional<std::string> DescribeSmallOutput(const AcSensitivityResult &result, const double minimum_output) {
    std::vector<double> frequencies;
    for (std::size_t index = 0; index < result.Output.size(); ++index) {
        if (std::abs(result.Output[index]) <= minimum_output) {
            frequencies.push_back(result.Frequencies[index]);
        }
    }
    if (frequencies.empty()) {
        return std::nullopt;
    }
    return std::format("The output is almost zero at {} of {} frequencies, between {:.4g} Hz and {:.4g} Hz, so a "
                       "relative change of it means nothing there and those points have no value",
                       frequencies.size(), result.Frequencies.size(), frequencies.front(), frequencies.back());
}

/// Everything the loop over the frequencies reads and does not change.
struct SweepContext {
    const CompiledCircuit &Compiled;
    const MnaSystem &Reactive;
    const std::vector<double> &DcSolution;
    const std::vector<Complex> &Selector;
    const std::vector<JacobianDerivative> &Derivatives;
    const std::vector<AcSensitivityElement> &Elements;
};

/// Solves the small-signal system and both adjoints at one frequency.
struct FrequencySolution {
    std::vector<Complex> AcSolution;
    std::vector<Complex> Adjoint;
    std::vector<Complex> BiasAdjoint;
};

std::expected<FrequencySolution, std::string> SolveFrequency(const SweepContext &context, const double frequency) {
    const double omega = 2.0 * std::numbers::pi * frequency;
    const MnaSystem &system = *context.Compiled.System;
    FrequencySolution solution;
    if (!system.SolveAc(context.Reactive, omega, solution.AcSolution) ||
        !system.SolveAcTransposeFor(context.Reactive, omega, context.Selector, solution.Adjoint)) {
        return std::unexpected(
            std::format("The Nodalys AC sensitivity solver encountered a singular matrix at f = {} Hz", frequency));
    }
    const auto size = static_cast<std::size_t>(system.GetSize());
    if (context.Derivatives.empty()) {
        solution.BiasAdjoint.assign(size, Complex{});
        return solution;
    }
    const std::vector<Complex> weights =
        FindBiasWeights(context.Derivatives, size, solution.Adjoint, solution.AcSolution, omega);
    if (!system.SolveTransposeComplex(weights, solution.BiasAdjoint)) {
        return std::unexpected(std::format(
            "The Nodalys AC sensitivity solver found no operating point derivative at f = {} Hz", frequency));
    }
    return solution;
}

} // namespace

/**
 * @brief   Calculates how the complex voltage of a node responds, at each frequency, to the value of every element.
 * @param[in,out] compiled  Compiled circuit with its MNA matrix and devices.
 * @param[in]     settings  Output node, input source and logarithmic frequency range.
 * @param[in]     options   Numerical limits of the operating point.
 * @param[out]    messages  Diagnostic messages collected during the operating point solve.
 * @return  One sensitivity per element, with the figures a user reads, or an error description.
 */
std::expected<AcSensitivityResult, std::string> ComputeAcSensitivity(CompiledCircuit &compiled,
                                                                     const AcSensitivityConfig &settings,
                                                                     const ConvergenceOptions &options,
                                                                     std::vector<DiagnosticMessage> *messages) {
    if (const std::optional<std::string> error = CheckConfig(compiled, settings)) {
        return std::unexpected(*error);
    }
    const auto source_found = compiled.SourcesByName.find(settings.InputSource);
    if (source_found == compiled.SourcesByName.end()) {
        return std::unexpected(std::format("The circuit has no source named {}", settings.InputSource));
    }
    const Device &input = *source_found->second;
    const std::optional<double> input_magnitude = FindInputMagnitude(input);
    if (!input_magnitude) {
        return std::unexpected(std::format("The source {} cannot drive an AC sensitivity", settings.InputSource));
    }
    if (*input_magnitude == 0.0) {
        return std::unexpected(std::format("The source {} has no AC magnitude; set it to AC", settings.InputSource));
    }

    std::vector<double> x_dc(static_cast<std::size_t>(compiled.System->GetSize()), 0.0);
    const NewtonOutcome outcome = SolveNewton(*compiled.System, compiled.Devices, x_dc, TimeContext{}, options);
    if (!outcome.Converged) {
        return std::unexpected(
            DescribeFailure("Nodalys found no operating point to linearize the AC sensitivity around", outcome));
    }
    DescribeOutcome(outcome, "operating point", messages);

    const MnaSystem reactive = BuildReactiveSystem(compiled, x_dc, input);
    const std::vector<Complex> selector = MakeOutputSelector(*compiled.System, settings);
    const std::vector<JacobianDerivative> derivatives = FindJacobianDerivatives(compiled, x_dc);
    const std::vector<AcSensitivityElement> elements = FindSensitivityElements(compiled);
    const SweepContext context{compiled, reactive, x_dc, selector, derivatives, elements};

    AcSensitivityResult result;
    result.OutputPositiveNode = settings.OutputPositiveNode;
    result.OutputNegativeNode = settings.OutputNegativeNode;
    result.InputSource = settings.InputSource;
    result.Frequencies = MakeFrequencies(settings);
    for (const AcSensitivityElement &element : elements) {
        result.Elements.push_back({.Name = element.Name, .Parameter = element.Parameter, .Value = element.Value});
    }
    const double minimum_output = MIN_RELATIVE_OUTPUT * std::abs(*input_magnitude);
    for (const double frequency : result.Frequencies) {
        const std::expected<FrequencySolution, std::string> solution = SolveFrequency(context, frequency);
        if (!solution) {
            return std::unexpected(solution.error());
        }
        const Complex output = ReadOutput(selector, solution->AcSolution);
        result.Output.push_back(output);
        const AcSensitivityPoint point{.Omega = 2.0 * std::numbers::pi * frequency,
                                       .DcSolution = x_dc,
                                       .AcSolution = solution->AcSolution,
                                       .Adjoint = solution->Adjoint,
                                       .BiasAdjoint = solution->BiasAdjoint};
        for (std::size_t index = 0; index < elements.size(); ++index) {
            result.Elements[index].Derivatives.push_back(FindOutputDerivative(elements[index], point));
            StoreFigures(result.Elements[index], output, minimum_output);
        }
    }
    if (const std::optional<std::string> warning = DescribeSmallOutput(result, minimum_output)) {
        const bool is_everywhere = std::ranges::all_of(
            result.Output, [minimum_output](const Complex &output) { return std::abs(output) <= minimum_output; });
        if (is_everywhere) {
            return std::unexpected(
                std::format("The output is zero at every frequency: {} does not drive it", settings.InputSource));
        }
        result.Warnings.push_back(*warning);
    }
    return result;
}

} // namespace Nodalys
