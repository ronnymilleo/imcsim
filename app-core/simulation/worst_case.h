/**
 * @file    worst_case.h
 * @brief   Analytical worst-case output bounds from sensitivities and part tolerances, without Monte Carlo sampling.
 */

#ifndef IMCSIM_WORST_CASE_H
#define IMCSIM_WORST_CASE_H

#include "simulation/simulation_results.h"
#include <cstddef>
#include <optional>
#include <vector>

namespace Core {

/**
 * @struct  DCWorstCase
 * @brief   Worst-case DC output voltage deviation bounds from sensitivities and tolerances.
 * @details Extreme Value Analysis adds the magnitude of every toleranced part's contribution, assuming every part
 *          lands at the corner that pushes the output the same way. Root Sum of Squares combines them in quadrature
 *          for independent tolerances.
 */
struct DCWorstCase {
    std::size_t TolerancedPartCount = 0;
    /// Extreme Value Analysis deviation bound in volts.
    double ExtremeVolts = 0.0;
    /// Root Sum of Squares deviation bound in volts.
    double RssVolts = 0.0;
};

/**
 * @struct  ACWorstCasePoint
 * @brief   Worst-case gain and phase deviation bounds at one frequency from AC sensitivities and tolerances.
 */
struct ACWorstCasePoint {
    std::size_t TolerancedPartCount = 0;
    /// Extreme Value Analysis gain bound in decibels, or none when no toleranced part has a magnitude figure.
    std::optional<double> ExtremeGainDecibels;
    /// Root Sum of Squares gain bound in decibels, or none when no toleranced part has a magnitude figure.
    std::optional<double> RssGainDecibels;
    /// Extreme Value Analysis phase bound in degrees, or none when no toleranced part has a phase figure.
    std::optional<double> ExtremePhaseDegrees;
    /// Root Sum of Squares phase bound in degrees, or none when no toleranced part has a phase figure.
    std::optional<double> RssPhaseDegrees;
};

/**
 * @struct  ACWorstCaseSweep
 * @brief   Worst-case gain and phase deviation bounds across every frequency of an AC sensitivity sweep.
 */
struct ACWorstCaseSweep {
    std::size_t TolerancedPartCount = 0;
    std::vector<double> Frequencies;
    std::vector<std::optional<double>> ExtremeGainDecibels;
    std::vector<std::optional<double>> RssGainDecibels;
    std::vector<std::optional<double>> ExtremePhaseDegrees;
    std::vector<std::optional<double>> RssPhaseDegrees;
};

// DC worst case
[[nodiscard]] std::optional<double> GetPartWorstCaseChange(const PartSensitivity &part);
[[nodiscard]] DCWorstCase ComputeWorstCase(const Sensitivity &sensitivity);

// AC worst case
[[nodiscard]] std::optional<double> GetACPartGainWorstCase(const ACPartSensitivity &part, std::size_t frequency_index);
[[nodiscard]] std::optional<double> GetACPartPhaseWorstCase(const ACPartSensitivity &part, std::size_t frequency_index);
[[nodiscard]] ACWorstCasePoint ComputeACWorstCaseAt(const ACSensitivity &sensitivity, std::size_t frequency_index);
[[nodiscard]] ACWorstCaseSweep ComputeACWorstCaseSweep(const ACSensitivity &sensitivity);

} // namespace Core

#endif // IMCSIM_WORST_CASE_H
