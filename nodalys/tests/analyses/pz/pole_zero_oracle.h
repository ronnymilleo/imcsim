/**
 * @file    pole_zero_oracle.h
 * @brief   Checks the poles and zeros Nodalys finds against its own AC sweep of the same circuit.
 * @details The transfer function of a circuit is H(s) = K * prod(s - z) / prod(s - p), with one real gain K. Given the
 *          roots, H(jw) is rebuilt and compared with the AC sweep across the band: a missing, extra or misplaced root
 *          shows as a magnitude or phase error, so no list of expected roots is needed to know the list is complete.
 */

#ifndef NODALYS_POLE_ZERO_ORACLE_H
#define NODALYS_POLE_ZERO_ORACLE_H

#include <complex>
#include <nodalys/nodalys.h>
#include <string>
#include <vector>

namespace Testing {

/**
 * @struct  OracleBand
 * @brief   The frequencies the rebuilt response is compared at, a decade sweep, in hertz.
 */
struct OracleBand {
    double StartFrequencyHertz = 1.0;
    double StopFrequencyHertz = 1e6;
    int PointsPerDecade = 20;
};

/**
 * @struct  PoleZeroReport
 * @brief   The roots of a run and how far the response rebuilt from them is from the AC sweep.
 */
struct PoleZeroReport {
    std::vector<std::complex<double>> Poles;
    std::vector<std::complex<double>> Zeros;
    /// What Nodalys warned about.
    std::vector<std::string> Warnings;
    /// The gain that scales the rebuilt response, read in the middle of the band.
    double Gain = 0.0;
    /// Largest magnitude difference over the band, in decibels, ignoring points far below the peak of the response.
    double MaxMagnitudeErrorDecibels = 0.0;
    /// Frequency of that largest magnitude difference, in hertz.
    double WorstFrequencyHertz = 0.0;
    /// Largest phase difference over the band, in degrees, on the same points.
    double MaxPhaseErrorDegrees = 0.0;
    /// Describes the failure when the run or the sweep gave no result; empty otherwise.
    std::string Error;
};

/// The largest magnitude difference, in decibels, that counts as a match.
inline constexpr double MATCH_DECIBELS = 1e-3;
/// The largest phase difference, in degrees, that counts as a match.
inline constexpr double MATCH_DEGREES = 1e-2;

// Running
PoleZeroReport
CheckPoleZero(const Nodalys::Circuit &circuit, const Nodalys::PoleZeroConfig &config, const OracleBand &band);

// Reading
bool IsMatch(const PoleZeroReport &report);
std::string Describe(const PoleZeroReport &report);
bool HasRightHalfPlaneRoot(const std::vector<std::complex<double>> &roots, double tolerance_radians_per_second);
bool HasRootNear(const std::vector<std::complex<double>> &roots,
                 std::complex<double> expected,
                 double relative_tolerance);

} // namespace Testing

#endif // NODALYS_POLE_ZERO_ORACLE_H
