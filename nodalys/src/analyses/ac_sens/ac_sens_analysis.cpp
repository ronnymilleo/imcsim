/**
 * @file    ac_sens_analysis.cpp
 * @brief   Small-signal AC sensitivity analysis: how the complex output voltage changes with every element value.
 * @details At each frequency the small-signal system is \f$Y(j\omega) x = b\f$ and the output is \f$V = d^T x\f$. One
 *          transposed solve \f$Y^T \lambda = d\f$ gives the derivative with respect to every element, \f$-\lambda^T
 *          (\partial Y / \partial p) x\f$. A nonlinear device also moves with the operating point, which an element
 *          value shifts: its weights \f$w_k = -\lambda^T (\partial Y / \partial x_k) x\f$ go through the DC adjoint
 *          \f$J^T \mu = w\f$ and add \f$-\mu^T g_p\f$, where \f$g_p\f$ is the derivative of the DC equations with
 * respect to \f$p\f$.
 */

#include "analyses/analyses.h"

#include "analyses/ac_sens/ac_sens_bias.h"
#include "analyses/ac_sens/ac_sens_elements.h"
#include "analyses/common/analysis_preamble.h"
#include "analyses/common/frequency_sweep.h"
#include "devices/device.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <optional>

namespace Nodalys {

namespace {

/// Decibels of a magnitude ratio per neper.
constexpr double DECIBELS_PER_NEPER = 20.0 / std::numbers::ln10;
constexpr double DEGREES_PER_RADIAN = 180.0 / std::numbers::pi;
/// Output smaller than this share of the AC magnitude of the input counts as zero: a relative change of it means
/// nothing, and it is far below what the solver resolves (about -180 dB).
constexpr double MIN_RELATIVE_OUTPUT = 1e-9;
constexpr double PERCENT = 100.0;

using Complex = std::complex<double>;

/**
 * @struct  AcInput
 * @brief   The input source and its AC magnitude.
 */
struct AcInput {
    const Device &Source;
    double Magnitude = 0.0;
};

/// Solves the small-signal system and both adjoints at one frequency.
struct FrequencySolution {
    std::vector<Complex> AcSolution;
    std::vector<Complex> Adjoint;
    std::vector<Complex> BiasAdjoint;
};

/// The output node of the analysis and what reads its voltage.
struct Output {
    int Positive = -1;
    int Negative = -1;
};

/// Everything the loop over the frequencies reads and does not change.
struct SweepContext {
    const CompiledCircuit &Compiled;
    const MnaSystem &Reactive;
    const std::vector<double> &DcSolution;
    const std::vector<Complex> &Selector;
    const std::vector<JacobianDerivative> &Derivatives;
    const std::vector<AcSensitivityElement> &Elements;
};

std::optional<std::string> CheckConfig(const CompiledCircuit &compiled, const AcSensitivityConfig &settings) {
    if (const std::optional<std::string> refusal = CheckOutputNode(compiled, settings.OutputPositiveNode)) {
        return refusal;
    }
    if (settings.OutputNegativeNode < 0 || settings.OutputNegativeNode >= compiled.CircuitNodeCount) {
        return std::format("The circuit has no negative output node {}", settings.OutputNegativeNode);
    }
    if (settings.OutputPositiveNode == settings.OutputNegativeNode) {
        return "Pick an output node; ground has no sensitivity";
    }
    if (!IsValidSweepRange(settings.StartFrequency, settings.StopFrequency, settings.PointsPerDecade)) {
        return "The AC sensitivity analysis has invalid frequency settings; check start, stop and points per decade";
    }
    return std::nullopt;
}

/**
 * @brief   Finds the input source and checks that it has an AC magnitude to drive the analysis with.
 */
std::expected<AcInput, std::string> ResolveAcInput(const CompiledCircuit &compiled,
                                                   const AcSensitivityConfig &settings) {
    const std::expected<InputSource, std::string> source =
        ResolveInputSource(compiled, settings.InputSource, "an AC sensitivity");
    if (!source) {
        return std::unexpected(source.error());
    }
    const double magnitude = source->GetSource().GetAcMagnitude();
    if (magnitude == 0.0) {
        return std::unexpected(std::format("The source {} has no AC magnitude; set it to AC", settings.InputSource));
    }
    return AcInput{.Source = source->GetDevice(), .Magnitude = magnitude};
}

/**
 * @brief   The small-signal system with the input source as its only excitation.
 */
MnaSystem
BuildInputReactiveSystem(const CompiledCircuit &compiled, const std::vector<double> &x_dc, const Device &input) {
    MnaSystem reactive = BuildReactiveSystem(compiled, x_dc);
    // Every AC source stamped its excitation above; only the input one stays, so the response is the one to it
    reactive.ClearRhs();
    input.StampAc(reactive, x_dc);
    return reactive;
}

/**
 * @brief   The vector that picks the output voltage out of the solution.
 */
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

/**
 * @brief   Appends the derivative of one frequency and its two figures, which are none where the output is too small to
 * take a ratio to.
 */
void AppendPoint(AcElementSensitivity &element,
                 const Complex &derivative,
                 const Complex &output,
                 const double minimum_output) {
    element.Derivatives.push_back(derivative);
    if (std::abs(output) <= minimum_output) {
        element.MagnitudeDecibelsPerPercent.push_back(std::nullopt);
        element.PhaseDegreesPerPercent.push_back(std::nullopt);
        return;
    }
    const Complex relative = derivative / output * (element.Value / PERCENT);
    element.MagnitudeDecibelsPerPercent.push_back(DECIBELS_PER_NEPER * relative.real());
    element.PhaseDegreesPerPercent.push_back(DEGREES_PER_RADIAN * relative.imag());
}

/**
 * @brief   Says once where the output was too small to give figures, or nothing when it was never.
 */
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
                       frequencies.size(),
                       result.Frequencies.size(),
                       frequencies.front(),
                       frequencies.back());
}

std::expected<FrequencySolution, std::string> SolveFrequency(const SweepContext &context, const double frequency) {
    const double omega = 2.0 * std::numbers::pi * frequency;
    const MnaSystem &system = *context.Compiled.System;
    FrequencySolution solution;
    if (!system.SolveAc(context.Reactive, omega, solution.AcSolution)
        || !system.SolveAcTransposeFor(context.Reactive, omega, context.Selector, solution.Adjoint)) {
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
        return std::unexpected(
            std::format("The Nodalys AC sensitivity solver found no operating point derivative at f = {} Hz",
                        frequency));
    }
    return solution;
}

AcSensitivityResult MakeEmptyResult(const AcSensitivityConfig &settings,
                                    const std::vector<AcSensitivityElement> &elements) {
    AcSensitivityResult result;
    result.OutputPositiveNode = settings.OutputPositiveNode;
    result.OutputNegativeNode = settings.OutputNegativeNode;
    result.InputSource = settings.InputSource;
    result.Frequencies = MakeFrequencySweep(settings.SweepType,
                                            settings.StartFrequency,
                                            settings.StopFrequency,
                                            settings.PointsPerDecade);
    for (const AcSensitivityElement &element : elements) {
        result.Elements.push_back({.Name = element.Name, .Parameter = element.Parameter, .Value = element.Value});
    }
    return result;
}

/**
 * @brief   Solves each frequency and appends its output and the derivative of every element.
 */
std::optional<std::string>
AddFrequencyPoints(AcSensitivityResult &result, const SweepContext &context, const double minimum_output) {
    for (const double frequency : result.Frequencies) {
        const std::expected<FrequencySolution, std::string> solution = SolveFrequency(context, frequency);
        if (!solution) {
            return solution.error();
        }
        const Complex output = ReadOutput(context.Selector, solution->AcSolution);
        result.Output.push_back(output);
        const AcSensitivityPoint point{.Omega = 2.0 * std::numbers::pi * frequency,
                                       .DcSolution = context.DcSolution,
                                       .AcSolution = solution->AcSolution,
                                       .Adjoint = solution->Adjoint,
                                       .BiasAdjoint = solution->BiasAdjoint};
        for (std::size_t index = 0; index < context.Elements.size(); ++index) {
            AppendPoint(result.Elements[index],
                        FindOutputDerivative(context.Elements[index], point),
                        output,
                        minimum_output);
        }
    }
    return std::nullopt;
}

/**
 * @brief   Warns of the frequencies where the output was too small, or refuses the analysis when it was so everywhere.
 */
std::optional<std::string>
AddOutputWarning(AcSensitivityResult &result, const double minimum_output, const std::string &input_name) {
    const std::optional<std::string> warning = DescribeSmallOutput(result, minimum_output);
    if (!warning) {
        return std::nullopt;
    }
    const bool is_everywhere = std::ranges::all_of(result.Output, [minimum_output](const Complex &output) {
        return std::abs(output) <= minimum_output;
    });
    if (is_everywhere) {
        return std::format("The output is zero at every frequency: {} does not drive it", input_name);
    }
    result.Warnings.push_back(*warning);
    return std::nullopt;
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
    const std::expected<AcInput, std::string> input = ResolveAcInput(compiled, settings);
    if (!input) {
        return std::unexpected(input.error());
    }
    const std::expected<std::vector<double>, std::string> x_dc =
        SolveOperatingPoint(compiled,
                            options,
                            "Nodalys found no operating point to linearize the AC sensitivity around",
                            messages);
    if (!x_dc) {
        return std::unexpected(x_dc.error());
    }

    const MnaSystem reactive = BuildInputReactiveSystem(compiled, *x_dc, input->Source);
    const std::vector<Complex> selector = MakeOutputSelector(*compiled.System, settings);
    const std::vector<JacobianDerivative> derivatives = FindJacobianDerivatives(compiled, *x_dc);
    const std::vector<AcSensitivityElement> elements = FindSensitivityElements(compiled);
    const SweepContext context{compiled, reactive, *x_dc, selector, derivatives, elements};

    AcSensitivityResult result = MakeEmptyResult(settings, elements);
    const double minimum_output = MIN_RELATIVE_OUTPUT * std::abs(input->Magnitude);
    if (const std::optional<std::string> error = AddFrequencyPoints(result, context, minimum_output)) {
        return std::unexpected(*error);
    }
    if (const std::optional<std::string> error = AddOutputWarning(result, minimum_output, settings.InputSource)) {
        return std::unexpected(*error);
    }
    return result;
}

} // namespace Nodalys
