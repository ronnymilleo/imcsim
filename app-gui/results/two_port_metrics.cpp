/**
 * @file    two_port_metrics.cpp
 * @brief   The stability factors and the gain bounds of a two-port, worked out from its S matrix.
 */

#include "results/two_port_metrics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace GUI {

namespace {

constexpr double DECIBELS_PER_DECADE_OF_POWER = 10.0;

double PowerToDecibels(const double power) {
    return DECIBELS_PER_DECADE_OF_POWER * std::log10(std::max(power, TRANSMISSION_FLOOR * TRANSMISSION_FLOOR));
}

bool IsFinite(const std::complex<double> value) {
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

/**
 * @brief   The Edwards-Sinsky factor of one side.
 * @details (1 - |near|^2) over |far - delta conj(near)| + |S12 S21|. A denominator of nothing leaves a ratio that is
 *          infinite when the numerator is positive, and with no numerator either, the side sits on the edge of the
 *          chart: that reads as 0, the least stable value.
 */
double ComputeMu(const std::complex<double> near_side,
                 const std::complex<double> far_side,
                 const std::complex<double> delta,
                 const double product) {
    const double numerator = 1.0 - std::norm(near_side);
    const double denominator = std::abs(far_side - delta * std::conj(near_side)) + product;
    if (denominator > 0.0) {
        return numerator / denominator;
    }
    return numerator > 0.0 ? std::numeric_limits<double>::infinity() : 0.0;
}

/**
 * @brief   The gain bound that goes with the stability.
 */
void FillGain(TwoPortMetrics &metrics, const TwoPortMatrix &matrix) {
    const double forward = std::abs(matrix.S21);
    const double reverse = std::abs(matrix.S12);
    metrics.HasForwardTransmission = forward > TRANSMISSION_FLOOR;
    if (!metrics.HasForwardTransmission) {
        return;
    }
    if (metrics.IsUnilateral) {
        // MAG as S12 goes to 0: |S21|^2 over (1 - |S11|^2)(1 - |S22|^2), which has a finite value only when both
        // reflections are below 1, that is, when it is stable
        if (metrics.IsUnconditionallyStable) {
            metrics.Gain = GainKind::UNILATERAL;
            metrics.GainDecibels = PowerToDecibels(std::norm(matrix.S21)
                                                   / ((1.0 - std::norm(matrix.S11)) * (1.0 - std::norm(matrix.S22))));
        }
        return;
    }
    metrics.MaximumStableGainDecibels = PowerToDecibels(forward / reverse);
    if (metrics.IsUnconditionallyStable) {
        // 1 / (K + sqrt(K^2 - 1)) is K - sqrt(K^2 - 1) without the cancellation of a large K; a K a hair below 1,
        // from rounding on a lossless network, is 1
        const double root = std::sqrt(std::max(metrics.K * metrics.K - 1.0, 0.0));
        metrics.Gain = GainKind::MAG;
        metrics.GainDecibels = PowerToDecibels(forward / reverse / (std::max(metrics.K, 1.0) + root));
    } else {
        metrics.Gain = GainKind::MSG;
        metrics.GainDecibels = metrics.MaximumStableGainDecibels;
    }
}

} // namespace

/**
 * @brief   Tells whether a result has the two ports that the metrics need.
 * @param[in] result  S matrix of a sweep.
 * @return  True for exactly two ports and at least one frequency.
 */
bool IsTwoPort(const Core::SParameters &result) {
    return result.GetPortCount() == 2 && !result.Frequencies.empty();
}

/**
 * @brief   Reads the four entries of a two-port at one frequency.
 * @param[in] result     S matrix of a sweep of two ports.
 * @param[in] frequency  Index of the frequency.
 * @return  S11, S12, S21 and S22.
 */
TwoPortMatrix ReadTwoPortMatrix(const Core::SParameters &result, const std::size_t frequency) {
    return {.S11 = result.Get(frequency, 1, 1),
            .S12 = result.Get(frequency, 1, 2),
            .S21 = result.Get(frequency, 2, 1),
            .S22 = result.Get(frequency, 2, 2)};
}

/**
 * @brief   Works out the stability factors and the gain bound of a two-port at one frequency.
 * @param[in] matrix  Its S-parameters, which may be those of an active network.
 * @return  K, |Delta|, mu and mu', the gain bound and whether it is unconditionally stable; a matrix with a value that
 *          is not finite is invalid.
 * @details K is (1 - |S11|^2 - |S22|^2 + |Delta|^2) over 2 |S12 S21|, mu is (1 - |S11|^2) over |S22 - Delta conj(S11)|
 *          + |S12 S21|, and mu' swaps the ports. A product |S12 S21| at the floor makes K infinite and mu the limit
 *          of its formula, 1 / |S22| and 1 / |S11| with a stable side: the two-port is unilateral.
 */
TwoPortMetrics ComputeTwoPortMetrics(const TwoPortMatrix &matrix) {
    TwoPortMetrics metrics;
    if (!IsFinite(matrix.S11) || !IsFinite(matrix.S12) || !IsFinite(matrix.S21) || !IsFinite(matrix.S22)) {
        metrics.IsValid = false;
        return metrics;
    }
    const std::complex<double> delta = matrix.S11 * matrix.S22 - matrix.S12 * matrix.S21;
    const double product = std::abs(matrix.S12 * matrix.S21);
    metrics.DeltaMagnitude = std::abs(delta);
    metrics.IsUnilateral = product <= UNILATERAL_PRODUCT_FLOOR;
    metrics.K = metrics.IsUnilateral
                  ? std::numeric_limits<double>::infinity()
                  : (1.0 - std::norm(matrix.S11) - std::norm(matrix.S22) + std::norm(delta)) / (2.0 * product);
    metrics.Mu = ComputeMu(matrix.S11, matrix.S22, delta, product);
    metrics.MuPrime = ComputeMu(matrix.S22, matrix.S11, delta, product);
    metrics.IsUnconditionallyStable = metrics.Mu > 1.0 - STABILITY_TOLERANCE;
    FillGain(metrics, matrix);
    return metrics;
}

/**
 * @brief   Works out the metrics at every frequency of a sweep.
 * @param[in] result  S matrix of a sweep of two ports.
 * @return  One entry per frequency; empty when the result is not of exactly two ports.
 */
TwoPortSweep ComputeTwoPortSweep(const Core::SParameters &result) {
    TwoPortSweep sweep;
    if (!IsTwoPort(result)) {
        return sweep;
    }
    sweep.Frequencies = result.Frequencies;
    sweep.Metrics.reserve(result.Frequencies.size());
    for (std::size_t frequency = 0; frequency < result.Frequencies.size(); ++frequency) {
        sweep.Metrics.push_back(ComputeTwoPortMetrics(ReadTwoPortMatrix(result, frequency)));
    }
    return sweep;
}

} // namespace GUI
