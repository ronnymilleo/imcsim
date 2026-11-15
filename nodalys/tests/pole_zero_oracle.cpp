/**
 * @file    pole_zero_oracle.cpp
 * @brief   Checks the poles and zeros Nodalys finds against its own AC sweep of the same circuit.
 */

#include "pole_zero_oracle.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace Testing {

namespace {

constexpr double TWO_PI = 2.0 * std::numbers::pi;
/// Points this far below the peak of the response are skipped: near a notch a tiny absolute error is a big one in dB.
constexpr double NOTCH_FLOOR_DECIBELS = 60.0;
constexpr double DEGREES_PER_RADIAN = 180.0 / std::numbers::pi;
constexpr double DECIBELS_PER_NEPER = 20.0 / std::numbers::ln10;

/// The log of prod(s - zero) / prod(s - pole), which a product of 60 poles would overflow.
std::complex<double> LogShape(const std::complex<double> s, const std::vector<std::complex<double>> &poles,
                              const std::vector<std::complex<double>> &zeros) {
    std::complex<double> total{0.0, 0.0};
    for (const std::complex<double> &zero : zeros) {
        total += std::log(s - zero);
    }
    for (const std::complex<double> &pole : poles) {
        total -= std::log(s - pole);
    }
    return total;
}

/// One complex response of the AC sweep at a node, rebuilt from its decibels and degrees.
std::complex<double> SweepValue(const Nodalys::AcSweepResult &sweep, const int node, const std::size_t index) {
    const double magnitude = std::pow(10.0, sweep.NodeMagnitudesDecibels[static_cast<std::size_t>(node)][index] / 20.0);
    const double phase = sweep.NodePhasesDegrees[static_cast<std::size_t>(node)][index] / DEGREES_PER_RADIAN;
    return std::polar(magnitude, phase);
}

/// How far the AC response is from the roots at one frequency, relative to the gain: log magnitude and angle.
std::complex<double> LogGainAt(const Nodalys::AcSweepResult &sweep, const PoleZeroReport &report, const int node,
                               const std::size_t index) {
    const std::complex<double> s{0.0, TWO_PI * sweep.Frequencies[index]};
    return std::log(SweepValue(sweep, node, index)) - LogShape(s, report.Poles, report.Zeros);
}

} // namespace

/**
 * @brief   Finds the roots of a circuit and compares the response they describe with the AC sweep.
 * @param[in] circuit  Circuit whose input source has an AC magnitude of 1 and the other sources none, so the sweep is
 *                     the transfer function.
 * @param[in] config   Output node and input source of the pole-zero analysis.
 * @param[in] band     Frequencies to compare at.
 * @return  The roots, the gain and the largest differences; Error is set when a run failed.
 */
PoleZeroReport CheckPoleZero(const Nodalys::Circuit &circuit, const Nodalys::PoleZeroConfig &config,
                             const OracleBand &band) {
    PoleZeroReport report;
    const auto roots = Nodalys::ComputePoleZero(circuit, config);
    if (!roots) {
        report.Error = roots.error();
        return report;
    }
    report.Poles = roots->Poles;
    report.Zeros = roots->Zeros;
    report.Warnings = roots->Warnings;

    const auto sweep = Nodalys::ComputeACSweep(circuit, {.StartFrequency = band.StartFrequencyHertz,
                                                         .StopFrequency = band.StopFrequencyHertz,
                                                         .PointsPerDecade = band.PointsPerDecade});
    if (!sweep) {
        report.Error = sweep.error();
        return report;
    }

    const std::size_t count = sweep->Frequencies.size();
    double peak = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        peak = std::max(peak, std::abs(SweepValue(*sweep, config.OutputNode, index)));
    }

    // The gain is read in the middle of the band; a wrong root list shows as a ratio that drifts from it elsewhere
    const std::complex<double> reference = LogGainAt(*sweep, report, config.OutputNode, count / 2);
    report.Gain = std::exp(reference.real()) * (std::cos(reference.imag()) < 0.0 ? -1.0 : 1.0);
    const double floor = peak * std::pow(10.0, -NOTCH_FLOOR_DECIBELS / 20.0);
    for (std::size_t index = 0; index < count; ++index) {
        if (std::abs(SweepValue(*sweep, config.OutputNode, index)) < floor) {
            continue;
        }
        const std::complex<double> difference = LogGainAt(*sweep, report, config.OutputNode, index) - reference;
        const double magnitude_error = std::abs(difference.real()) * DECIBELS_PER_NEPER;
        if (magnitude_error > report.MaxMagnitudeErrorDecibels) {
            report.MaxMagnitudeErrorDecibels = magnitude_error;
            report.WorstFrequencyHertz = sweep->Frequencies[index];
        }
        report.MaxPhaseErrorDegrees = std::max(
            report.MaxPhaseErrorDegrees, std::abs(std::remainder(difference.imag(), TWO_PI)) * DEGREES_PER_RADIAN);
    }
    // The gain of a transfer function is real, so its angle is 0 or 180 degrees
    report.MaxPhaseErrorDegrees = std::max(
        report.MaxPhaseErrorDegrees, std::abs(std::remainder(reference.imag(), std::numbers::pi)) * DEGREES_PER_RADIAN);
    return report;
}

/**
 * @brief   Tells whether a report matches the AC sweep within MATCH_DECIBELS and MATCH_DEGREES.
 * @param[in] report  Report from CheckPoleZero().
 * @return  True when the run gave a result, with no warning, and the rebuilt response is as close as required.
 */
bool IsMatch(const PoleZeroReport &report) {
    return report.Error.empty() && report.Warnings.empty() && report.MaxMagnitudeErrorDecibels <= MATCH_DECIBELS &&
           report.MaxPhaseErrorDegrees <= MATCH_DEGREES;
}

/**
 * @brief   Lists the roots and errors of a report, for the message of a failed check.
 * @param[in] report  Report to describe.
 * @return  Text with one line for the errors and one for each root.
 */
std::string Describe(const PoleZeroReport &report) {
    if (!report.Error.empty()) {
        return report.Error;
    }
    std::string text =
        std::format("gain {:.6g}, magnitude error {:.4g} dB at {:.4g} Hz, phase error {:.4g} deg", report.Gain,
                    report.MaxMagnitudeErrorDecibels, report.WorstFrequencyHertz, report.MaxPhaseErrorDegrees);
    for (const std::string &warning : report.Warnings) {
        text += "\n  warning: " + warning;
    }
    for (const std::complex<double> &pole : report.Poles) {
        text += std::format("\n  pole ({:.6g}, {:.6g})", pole.real(), pole.imag());
    }
    for (const std::complex<double> &zero : report.Zeros) {
        text += std::format("\n  zero ({:.6g}, {:.6g})", zero.real(), zero.imag());
    }
    return text;
}

/**
 * @brief   Tells whether a root lies in the right half plane.
 * @param[in] roots                         Roots in rad/s.
 * @param[in] tolerance_radians_per_second  How far right of the axis a root must be to count.
 * @return  True when some root has a real part above the tolerance.
 */
bool HasRightHalfPlaneRoot(const std::vector<std::complex<double>> &roots, const double tolerance_radians_per_second) {
    return std::ranges::any_of(
        roots, [tolerance_radians_per_second](const auto &root) { return root.real() > tolerance_radians_per_second; });
}

/**
 * @brief   Tells whether a list holds a root close to an expected one.
 * @param[in] roots               Roots in rad/s.
 * @param[in] expected            The root looked for.
 * @param[in] relative_tolerance  Share of the expected root's size the found one may be off by.
 * @return  True when some root is within the tolerance.
 */
bool HasRootNear(const std::vector<std::complex<double>> &roots, const std::complex<double> expected,
                 const double relative_tolerance) {
    return std::ranges::any_of(roots, [&](const std::complex<double> &root) {
        return std::abs(root - expected) <= relative_tolerance * std::abs(expected);
    });
}

} // namespace Testing
