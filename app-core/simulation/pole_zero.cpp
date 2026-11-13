/**
 * @file    pole_zero.cpp
 * @brief   Turns the poles and zeros a solver reports into the rows a pole-zero analysis shows, and tells in words what
 *          went wrong when it finds none.
 */

#include "simulation/pole_zero.h"

#include "simulation/analysis_sources.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>

namespace Core {

namespace {

/// A pole and a zero closer than this share of their size are one place, so they cancel.
constexpr double CANCELLATION_TOLERANCE = 1e-3;
/// An imaginary part under this share of the root's size is rounding noise of a real root.
constexpr double REAL_ROOT_TOLERANCE = 1e-9;

/// The lines the solver prints when it cannot start, each with what to tell the user.
struct KnownFailure {
    std::string_view Phrase;
    std::string_view Explanation;
};

constexpr std::array<KnownFailure, 4> KNOWN_FAILURES = {{
    {"input signal is shorted on the way to the output",
     "No signal reaches the output node from the input source through a capacitor or an inductor; a circuit of only "
     "resistors has no poles or zeros, and the output must be linked to the source"},
    {"Transfer function is unity", "The output node is the node of the input source, so the transfer is 1 and has no "
                                   "poles or zeros; pick another output node"},
    {"Output is shorted", "The output node is held to ground, so there is nothing to measure there; pick another node"},
    {"Input is shorted", "The terminals of the input source are the same node; pick a source across two nodes"},
}};

/**
 * @brief   The share of the larger root by which two roots differ.
 */
double GetRelativeDistance(const std::complex<double> first, const std::complex<double> second) {
    const double scale = std::max(std::abs(first), std::abs(second));
    return scale > 0.0 ? std::abs(first - second) / scale : 0.0;
}

/**
 * @brief   Keeps the real roots and the member of each conjugate pair with a positive imaginary part, with rounding
 * noise taken out of the imaginary part of the real ones.
 */
ComplexRoots KeepUpperHalfPlane(const ComplexRoots &roots) {
    ComplexRoots kept;
    for (const std::complex<double> root : roots) {
        if (std::abs(root.imag()) <= REAL_ROOT_TOLERANCE * std::abs(root)) {
            kept.emplace_back(root.real(), 0.0);
        } else if (root.imag() > 0.0) {
            kept.push_back(root);
        }
    }
    return kept;
}

/**
 * @brief   Removes every pole that has a zero at the same place, and that zero, and counts them.
 */
int CancelPairs(ComplexRoots &poles, ComplexRoots &zeros) {
    int cancelled = 0;
    for (auto pole = poles.begin(); pole != poles.end();) {
        const auto zero = std::ranges::find_if(zeros, [&pole](const std::complex<double> candidate) {
            return GetRelativeDistance(*pole, candidate) <= CANCELLATION_TOLERANCE;
        });
        if (zero == zeros.end()) {
            ++pole;
            continue;
        }
        zeros.erase(zero);
        pole = poles.erase(pole);
        ++cancelled;
    }
    return cancelled;
}

PoleZeroRoot MakeRoot(const std::complex<double> root, const bool is_pole) {
    PoleZeroRoot result;
    result.IsPole = is_pole;
    result.Real = root.real();
    result.Imaginary = root.imag();
    const double size = std::abs(root);
    result.NaturalFrequencyHertz = size / (2.0 * std::numbers::pi);
    result.IsRightHalfPlane = root.real() > 0.0;
    if (root.imag() != 0.0) {
        if (root.real() != 0.0) {
            result.QualityFactor = size / (2.0 * std::abs(root.real()));
        }
        result.DampingRatio = -root.real() / size;
    }
    return result;
}

void AppendSorted(std::vector<PoleZeroRoot> &roots, ComplexRoots found, const bool is_pole) {
    std::ranges::sort(found, {}, [](const std::complex<double> root) { return std::abs(root); });
    for (const std::complex<double> root : found) {
        roots.push_back(MakeRoot(root, is_pole));
    }
}

bool HasMessageContaining(const std::vector<SimulatorMessage> &messages, const std::string_view phrase) {
    return std::ranges::any_of(messages,
                               [phrase](const SimulatorMessage &message) { return message.Text.contains(phrase); });
}

} // namespace

/**
 * @brief   Finds the terminals of an input source, which are the port the poles and zeros are measured from.
 * @param[in] circuit      Circuit to look in.
 * @param[in] source_name  Name of a voltage source, current source, supply rail, clock or logic input.
 * @return  The nodes of its positive and negative terminal, and whether it is a current source; no value when the
 *          circuit has no such source. A supply rail, clock or logic input has its negative terminal at ground.
 */
std::optional<PoleZeroInput> FindPoleZeroInput(const Circuit &circuit, const std::string_view source_name) {
    const Component *source = FindTransferFunctionInput(circuit, source_name);
    if (source == nullptr) {
        return std::nullopt;
    }
    const auto entry = std::ranges::find(circuit.GetEntries(), source, &CircuitEntry::Part);
    const bool has_two_terminals =
        source->GetType() == ComponentType::VOLTAGE_SOURCE || source->GetType() == ComponentType::CURRENT_SOURCE;
    return PoleZeroInput{.PositiveNode = entry->Nodes[0],
                         .NegativeNode = has_two_terminals ? entry->Nodes[1] : 0,
                         .IsCurrent = source->GetType() == ComponentType::CURRENT_SOURCE};
}

/**
 * @brief   Turns the roots a solver found into the rows of a pole-zero result.
 * @param[in] circuit   Circuit that was analyzed, which gives the unit of the input source.
 * @param[in] settings  Output node and input source of the run.
 * @param[in] poles     Every pole in rad/s, both members of each conjugate pair.
 * @param[in] zeros     Every zero in rad/s, both members of each conjugate pair.
 * @return  A row per real root and per conjugate pair, poles first and zeros after, each by growing natural frequency,
 *          without the poles and zeros that sit at the same place, which are only counted.
 * @note    Two roots are at the same place when they differ by less than a thousandth of the larger.
 */
PoleZero BuildPoleZero(const Circuit &circuit, const PoleZeroSettings &settings, const ComplexRoots &poles,
                       const ComplexRoots &zeros) {
    PoleZero result;
    result.OutputNode = settings.OutputNode;
    result.InputSource = settings.InputSource;
    if (const Component *source = FindTransferFunctionInput(circuit, settings.InputSource)) {
        result.InputUnit = SourceUnit(*source);
    }
    ComplexRoots kept_poles = KeepUpperHalfPlane(poles);
    ComplexRoots kept_zeros = KeepUpperHalfPlane(zeros);
    result.CancelledPairs = CancelPairs(kept_poles, kept_zeros);
    AppendSorted(result.Roots, std::move(kept_poles), true);
    AppendSorted(result.Roots, std::move(kept_zeros), false);
    return result;
}

/**
 * @brief   Tells in plain words why the solver refused a pole-zero analysis, from what it printed.
 * @param[in] messages  Every line ngspice printed during the run.
 * @return  The explanation of the first line it knows; no value when none of them is known.
 */
std::optional<std::string> DescribePoleZeroFailure(const std::vector<SimulatorMessage> &messages) {
    for (const KnownFailure &failure : KNOWN_FAILURES) {
        if (HasMessageContaining(messages, failure.Phrase)) {
            return std::string(failure.Explanation);
        }
    }
    return std::nullopt;
}

/**
 * @brief   Finds the warning the solver gives when it stops searching before it has found every root.
 * @param[in] messages  Every line ngspice printed during the run.
 * @return  The warning to show with the result; no value when the solver did not complain.
 */
std::optional<std::string> FindPoleZeroWarning(const std::vector<SimulatorMessage> &messages) {
    if (HasMessageContaining(messages, "Pole-zero iteration limit reached")) {
        return "ngspice gave up searching for some roots, so some poles or zeros may be missing";
    }
    return std::nullopt;
}

} // namespace Core
