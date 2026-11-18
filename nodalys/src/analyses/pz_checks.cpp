/**
 * @file    pz_checks.cpp
 * @brief   Checks that a pole-zero analysis is worth showing: the input reaches the output and the roots found rebuild
 *          the response of the circuit.
 */

#include "analyses/pz_checks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <format>
#include <limits>
#include <numbers>

namespace Nodalys {

namespace {

/// Below this, the output is the input times a number lost in rounding: Gmin leaks about 1e-12 through an open diode.
constexpr double NEGLIGIBLE_TRANSFER = 1e-9;
/// Samples per decade of the frequencies the response is solved at.
constexpr int SAMPLES_PER_DECADE = 3;
/// Decades the samples go beyond the lowest and the highest root: a root missing at w shows once the samples reach
/// a thousandth of it.
constexpr double MARGIN_DECADES = 1.0;
/// Frequencies, in rad/s, of the samples of a circuit with no root to center them on.
constexpr std::array<double, 4> DEFAULT_FREQUENCIES{1.0, 1e3, 1e6, 1e9};
/// The lowest frequency sampled, in rad/s: a root this near the origin is a leak of Gmin or rounding, and its error
/// would read as a mismatch at frequencies where nothing happens.
constexpr double LOWEST_SAMPLE_OMEGA = 1e-3;
/// The rebuilt response may differ from the solved one by this share before the roots are called wrong.
constexpr double MISMATCH_TOLERANCE = 1e-3;
/// Samples this far below the strongest are skipped: close to a zero a small absolute error is a large relative one.
constexpr double WEAK_SAMPLE_FRACTION = 1e-6;
constexpr double DEGREES_PER_RADIAN = 180.0 / std::numbers::pi;
constexpr double DECIBELS_PER_NEPER = 20.0 / std::numbers::ln10;

/**
 * @struct  Mismatch
 * @brief   The sample where the gain read from the roots drifts the most.
 */
struct Mismatch {
    /// Distance in the log plane, in nepers, which counts the magnitude and the angle together.
    double Size = 0.0;
    double Omega = 0.0;
    double Decibels = 0.0;
    double Degrees = 0.0;
};

/**
 * @struct  Sample
 * @brief   The small-signal response at one frequency.
 */
struct Sample {
    double Omega = 0.0;
    std::complex<double> Output;
    /// The voltage across the input: 1 for a voltage source, the driving-point voltage for a current source.
    std::complex<double> Port;
};

std::complex<double> Dot(const std::vector<double> &weights, const std::vector<std::complex<double>> &values) {
    std::complex<double> sum{0.0, 0.0};
    for (std::size_t index = 0; index < weights.size(); ++index) {
        sum += weights[index] * values[index];
    }
    return sum;
}

/**
 * @brief   Solves the system at s = j omega.
 * @return  The response, or no value when G + jwC is singular there, such as at a pole on the imaginary axis.
 */
std::optional<Sample> SolveAt(const SmallSignalSystem &system, const double omega) {
    const std::size_t size = system.Input.size();
    ComplexDenseMatrix matrix(size, size);
    for (std::size_t r = 0; r < size; ++r) {
        for (std::size_t c = 0; c < size; ++c) {
            matrix.At(r, c) = {system.Conductance.At(r, c), omega * system.Reactive.At(r, c)};
        }
    }
    std::vector<std::complex<double>> rhs(size);
    for (std::size_t index = 0; index < size; ++index) {
        rhs[index] = system.Input[index];
    }
    std::vector<std::complex<double>> solution;
    if (!matrix.FactorizeLU() || !matrix.Solve(rhs, solution)) {
        return std::nullopt;
    }
    Sample sample{.Omega = omega, .Output = Dot(system.Output, solution), .Port = {1.0, 0.0}};
    if (system.IsCurrentInput) {
        const auto voltage = [&solution](const int unknown) {
            return unknown >= 0 ? solution[static_cast<std::size_t>(unknown)] : std::complex<double>{0.0, 0.0};
        };
        sample.Port = voltage(system.PortPositive) - voltage(system.PortNegative);
    }
    return sample;
}

/// The frequencies, in rad/s, to solve at: a log grid that reaches past the lowest and the highest root.
std::vector<double> ChooseFrequencies(const PoleZeroResult &roots) {
    double lowest = std::numeric_limits<double>::infinity();
    double highest = 0.0;
    for (const auto &list : {roots.Poles, roots.Zeros}) {
        for (const std::complex<double> &root : list) {
            if (std::abs(root) >= LOWEST_SAMPLE_OMEGA) {
                lowest = std::min(lowest, std::abs(root));
                highest = std::max(highest, std::abs(root));
            }
        }
    }
    if (highest == 0.0) {
        return {DEFAULT_FREQUENCIES.begin(), DEFAULT_FREQUENCIES.end()};
    }
    const double first_decade = std::log10(std::max(lowest * std::pow(10.0, -MARGIN_DECADES), LOWEST_SAMPLE_OMEGA));
    const double last_decade = std::log10(highest) + MARGIN_DECADES;
    std::vector<double> frequencies;
    const int count = static_cast<int>(std::ceil((last_decade - first_decade) * SAMPLES_PER_DECADE));
    for (int step = 0; step <= count; ++step) {
        frequencies.push_back(std::pow(10.0, first_decade + static_cast<double>(step) / SAMPLES_PER_DECADE));
    }
    return frequencies;
}

std::vector<Sample> SolveAtFrequencies(const SmallSignalSystem &system, const PoleZeroResult &roots) {
    std::vector<Sample> samples;
    for (const double omega : ChooseFrequencies(roots)) {
        if (const std::optional<Sample> sample = SolveAt(system, omega)) {
            samples.push_back(*sample);
        }
    }
    return samples;
}

/**
 * @brief   The log of the magnitude and the angle of prod(s - zero) / prod(s - pole) at s = j omega.
 * @details Logs, since 60 poles of 1e7 rad/s would underflow the product itself.
 */
std::complex<double> LogShape(const PoleZeroResult &roots, const double omega) {
    const std::complex<double> s{0.0, omega};
    std::complex<double> total{0.0, 0.0};
    for (const std::complex<double> &zero : roots.Zeros) {
        total += std::log(s - zero);
    }
    for (const std::complex<double> &pole : roots.Poles) {
        total -= std::log(s - pole);
    }
    return total;
}

/// The log of the ratio of the response to the shape of the roots, which is the same at every frequency when the roots
/// are right: the log of a real gain.
std::complex<double> LogGain(const PoleZeroResult &roots, const Sample &sample) {
    return std::log(sample.Output) - LogShape(roots, sample.Omega);
}

} // namespace

/**
 * @brief   Tells whether the output is a response to the input at all.
 * @param[in] system  The linearized circuit.
 * @param[in] roots   Its poles and zeros, which say where to look.
 * @return  The explanation when the output is under a billionth of the input voltage at every frequency sampled, which
 *          is what a diode that is off, or a node the source does not reach, gives; no value otherwise.
 */
std::optional<std::string> FindUndrivenOutput(const SmallSignalSystem &system, const PoleZeroResult &roots) {
    double strongest = 0.0;
    for (const Sample &sample : SolveAtFrequencies(system, roots)) {
        if (std::abs(sample.Port) > 0.0) {
            strongest = std::max(strongest, std::abs(sample.Output / sample.Port));
        } else {
            return std::nullopt;
        }
    }
    if (strongest >= NEGLIGIBLE_TRANSFER) {
        return std::nullopt;
    }
    return std::format("The output does not respond to the input: it is under {:.0e} of the input voltage at every "
                       "frequency, so it has no poles or zeros. The source may not reach that node, or a part between "
                       "them may be off at the operating point, such as a diode below its threshold",
                       NEGLIGIBLE_TRANSFER);
}

/**
 * @brief   Compares the response the roots describe with the one the system gives.
 * @details The transfer function is a real gain times prod(s - zero) / prod(s - pole), so the gain read from each
 *          sample must be the same one. A pole or zero that is missing, doubled or misplaced makes it drift.
 * @param[in] system  The linearized circuit.
 * @param[in] roots   Its poles and zeros in rad/s.
 * @return  The warning to show when the gain drifts by more than a thousandth between frequencies or is not real; no
 *          value when the roots rebuild the response.
 */
std::optional<std::string> FindRootMismatch(const SmallSignalSystem &system, const PoleZeroResult &roots) {
    const std::vector<Sample> samples = SolveAtFrequencies(system, roots);
    const auto strongest =
        std::ranges::max_element(samples, {}, [](const Sample &sample) { return std::abs(sample.Output); });
    if (strongest == samples.end() || std::abs(strongest->Output) == 0.0) {
        return std::nullopt;
    }
    const std::complex<double> reference = LogGain(roots, *strongest);
    // A real gain has an angle of 0 or pi, so what is left of the reference is a mismatch of its own
    const double reference_angle_error = std::abs(std::remainder(reference.imag(), std::numbers::pi));
    Mismatch worst{.Size = reference_angle_error,
                   .Omega = strongest->Omega,
                   .Decibels = 0.0,
                   .Degrees = reference_angle_error * DEGREES_PER_RADIAN};
    for (const Sample &sample : samples) {
        if (std::abs(sample.Output) < WEAK_SAMPLE_FRACTION * std::abs(strongest->Output)) {
            continue;
        }
        const std::complex<double> difference = LogGain(roots, sample) - reference;
        const double angle = std::remainder(difference.imag(), 2.0 * std::numbers::pi);
        const double size = std::hypot(difference.real(), angle);
        if (size > worst.Size) {
            worst = {.Size = size,
                     .Omega = sample.Omega,
                     .Decibels = std::abs(difference.real()) * DECIBELS_PER_NEPER,
                     .Degrees = std::abs(angle) * DEGREES_PER_RADIAN};
        }
    }
    if (worst.Size <= MISMATCH_TOLERANCE) {
        return std::nullopt;
    }
    return std::format("The poles and zeros found do not rebuild the response of the circuit (they are off by {:.3g} "
                       "dB and {:.3g} degrees around {:.3g} Hz), so one may be missing or misplaced",
                       worst.Decibels,
                       worst.Degrees,
                       worst.Omega / (2.0 * std::numbers::pi));
}

} // namespace Nodalys
