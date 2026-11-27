/**
 * @file    two_port_metrics.h
 * @brief   The stability factors and the gain bounds of a two-port, worked out from its S matrix.
 * @details Pure functions with no ImGui, over the S-parameters of any backend. The gains are ratios of power, so in
 *          decibels they are 10 log10 of the ratio. The formulas read power waves, so they
 *          hold for ports of different real reference impedances too: each port is normalized to its own, and the
 *          reflection planes of the source and load circles are the ones of the port they belong to.
 *
 *          Sources: the Rollett factor K and Delta from Rollett, "Stability and Power-Gain Invariants of Linear
 *          Twoports", IRE Transactions on Circuit Theory, 1962; the geometric factors mu and mu' from Edwards and
 *          Sinsky, "A New Criterion for Linear 2-Port Stability Using a Single Geometrically Derived Parameter", IEEE
 *          Transactions on Microwave Theory and Techniques, 1992; the gains from Gonzalez, "Microwave Transistor
 *          Amplifiers", chapter 3.
 */

#ifndef IMCSIM_TWO_PORT_METRICS_H
#define IMCSIM_TWO_PORT_METRICS_H

#include "simulation/simulation_results.h"
#include <complex>
#include <optional>
#include <vector>

namespace GUI {

/// A product |S12 S21| at or below this makes the two-port unilateral: K is then infinite and the circles shrink to
/// points, so the formulas that divide by it give way to their limits.
constexpr double UNILATERAL_PRODUCT_FLOOR = 1e-12;

/// A mu this far below 1 still counts as stable. A lossless network, such as an LC filter, has mu = 1 exactly, which
/// rounding would otherwise put on either side.
constexpr double STABILITY_TOLERANCE = 1e-6;

/// A magnitude at or below this is zero, so a gain of it is no gain at all.
constexpr double TRANSMISSION_FLOOR = 1e-15;

/**
 * @struct  TwoPortMatrix
 * @brief   The four S-parameters of a two-port at one frequency.
 */
struct TwoPortMatrix {
    std::complex<double> S11;
    std::complex<double> S12;
    std::complex<double> S21;
    std::complex<double> S22;
};

/**
 * @enum    GainKind
 * @brief   Which bound the gain of a two-port is.
 * @details MAG, the maximum available gain, exists only when the two-port is unconditionally stable; MSG, the maximum
 *          stable gain, is the bound of a potentially unstable one. A unilateral two-port (S12 = 0) has neither, but
 *          its unilateral power gain, the limit of MAG, when it is stable. NONE: no finite bound, or nothing is
 *          transmitted.
 */
enum class GainKind {
    MAG,
    MSG,
    UNILATERAL,
    NONE
};

/**
 * @struct  TwoPortMetrics
 * @brief   Stability and gain of a two-port at one frequency.
 */
struct TwoPortMetrics {
    /// False when an entry of the matrix is not a finite number; every other field is then meaningless.
    bool IsValid = true;
    double K = 0.0;              ///< Rollett's factor; infinite when the two-port is unilateral.
    double DeltaMagnitude = 0.0; ///< |S11 S22 - S12 S21|.
    /// Edwards-Sinsky factor of the load side, the distance from the load circle to the edge of the chart; above 1 is
    /// unconditionally stable. Infinite when nothing in the denominator is left.
    double Mu = 0.0;
    double MuPrime = 0.0;      ///< The same measured from the source side; it is above 1 exactly when Mu is.
    bool IsUnilateral = false; ///< Whether |S12 S21| is at or below UNILATERAL_PRODUCT_FLOOR.
    /// Whether S21 is more than zero; without it nothing is transmitted and there is no gain to bound.
    bool HasForwardTransmission = false;
    /// Whether no passive source and load can make it unstable: mu above 1, within STABILITY_TOLERANCE.
    bool IsUnconditionallyStable = false;
    GainKind Gain = GainKind::NONE;
    std::optional<double> GainDecibels; ///< The gain bound named by Gain in decibels of power; none for GainKind::NONE.
    /// |S21 / S12| as a power ratio in decibels, whatever the stability; none when it is unilateral or nothing is
    /// transmitted.
    std::optional<double> MaximumStableGainDecibels;
};

/**
 * @struct  TwoPortSweep
 * @brief   The metrics of a two-port at every frequency of a sweep.
 */
struct TwoPortSweep {
    std::vector<double> Frequencies;
    std::vector<TwoPortMetrics> Metrics;
};

// Matrix
bool IsTwoPort(const Core::SParameters &result);
TwoPortMatrix ReadTwoPortMatrix(const Core::SParameters &result, std::size_t frequency);

// Metrics
TwoPortMetrics ComputeTwoPortMetrics(const TwoPortMatrix &matrix);
TwoPortSweep ComputeTwoPortSweep(const Core::SParameters &result);

} // namespace GUI

#endif // IMCSIM_TWO_PORT_METRICS_H
