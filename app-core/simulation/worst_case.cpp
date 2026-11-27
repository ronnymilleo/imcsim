/**
 * @file    worst_case.cpp
 * @brief   Analytical worst-case output bounds from sensitivities and part tolerances, without Monte Carlo sampling.
 */

#include "simulation/worst_case.h"

#include <cmath>

namespace Core {

namespace {

/// Normalized DC sensitivity is given per 100 % change of a part's value.
constexpr double NORMALIZED_PERCENT = 100.0;

struct BoundAccumulator {
    double Extreme = 0.0;
    double SumSquares = 0.0;
    bool HasContribution = false;

    void Add(const std::optional<double> contribution) {
        if (!contribution) {
            return;
        }
        HasContribution = true;
        Extreme += *contribution;
        SumSquares += (*contribution) * (*contribution);
    }

    std::optional<double> GetExtreme() const { return HasContribution ? std::optional<double>(Extreme) : std::nullopt; }

    std::optional<double> GetRss() const {
        return HasContribution ? std::optional<double>(std::sqrt(SumSquares)) : std::nullopt;
    }
};

bool HasPositiveTolerance(const std::optional<double> tolerance_percent) {
    return tolerance_percent.has_value() && *tolerance_percent > 0.0;
}

std::optional<double> ComputeACPartMetricWorstCase(const std::optional<double> tolerance_percent,
                                                   const std::vector<std::optional<double>> &sensitivities,
                                                   const std::size_t frequency_index) {
    if (!tolerance_percent || *tolerance_percent <= 0.0 || frequency_index >= sensitivities.size()) {
        return std::nullopt;
    }
    const std::optional<double> sensitivity = sensitivities[frequency_index];
    if (!sensitivity) {
        return std::nullopt;
    }
    return std::abs(*sensitivity) * (*tolerance_percent);
}

} // namespace

/**
 * @brief   Computes the maximum DC output voltage change a single part can cause within its tolerance.
 * @param[in] part  DC sensitivity entry for one part.
 * @return  Non-negative deviation in volts, or none when the part carries no positive tolerance.
 */
std::optional<double> GetPartWorstCaseChange(const PartSensitivity &part) {
    if (!part.TolerancePercent || *part.TolerancePercent <= 0.0) {
        return std::nullopt;
    }
    return std::abs(part.Normalized / NORMALIZED_PERCENT) * (*part.TolerancePercent);
}

/**
 * @brief   Combines DC sensitivities with part tolerances to bound the output voltage deviation without sampling.
 * @param[in] sensitivity  DC sensitivity result carrying per-part normalized sensitivities and tolerances.
 * @return  Extreme Value Analysis and Root Sum of Squares deviation bounds, and the count of toleranced parts.
 */
DCWorstCase ComputeWorstCase(const Sensitivity &sensitivity) {
    DCWorstCase result;
    BoundAccumulator bounds;
    for (const PartSensitivity &part : sensitivity.Parts) {
        const std::optional<double> delta = GetPartWorstCaseChange(part);
        if (!delta) {
            continue;
        }
        ++result.TolerancedPartCount;
        bounds.Add(delta);
    }
    result.ExtremeVolts = bounds.GetExtreme().value_or(0.0);
    result.RssVolts = bounds.GetRss().value_or(0.0);
    return result;
}

/**
 * @brief   Computes the maximum AC gain change in decibels a single part can cause at one frequency.
 * @param[in] part             AC sensitivity entry for one part.
 * @param[in] frequency_index  Index into the frequency sweep.
 * @return  Non-negative gain deviation in decibels, or none without a positive tolerance or valid figure.
 */
std::optional<double> GetACPartGainWorstCase(const ACPartSensitivity &part, const std::size_t frequency_index) {
    return ComputeACPartMetricWorstCase(part.TolerancePercent, part.MagnitudeDecibelsPerPercent, frequency_index);
}

/**
 * @brief   Computes the maximum AC phase change in degrees a single part can cause at one frequency.
 * @param[in] part             AC sensitivity entry for one part.
 * @param[in] frequency_index  Index into the frequency sweep.
 * @return  Non-negative phase deviation in degrees, or none without a positive tolerance or valid figure.
 */
std::optional<double> GetACPartPhaseWorstCase(const ACPartSensitivity &part, const std::size_t frequency_index) {
    return ComputeACPartMetricWorstCase(part.TolerancePercent, part.PhaseDegreesPerPercent, frequency_index);
}

/**
 * @brief   Computes worst-case gain and phase deviation bounds at one frequency of an AC sensitivity result.
 * @param[in] sensitivity      AC sensitivity result carrying per-part figures and tolerances.
 * @param[in] frequency_index  Index into Frequencies.
 * @return  Extreme and RSS bounds for gain and phase, with none where no toleranced part has a valid figure.
 */
ACWorstCasePoint ComputeACWorstCaseAt(const ACSensitivity &sensitivity, const std::size_t frequency_index) {
    ACWorstCasePoint result;
    BoundAccumulator gain;
    BoundAccumulator phase;
    for (const ACPartSensitivity &part : sensitivity.Parts) {
        if (!HasPositiveTolerance(part.TolerancePercent)) {
            continue;
        }
        ++result.TolerancedPartCount;
        gain.Add(GetACPartGainWorstCase(part, frequency_index));
        phase.Add(GetACPartPhaseWorstCase(part, frequency_index));
    }
    result.ExtremeGainDecibels = gain.GetExtreme();
    result.RssGainDecibels = gain.GetRss();
    result.ExtremePhaseDegrees = phase.GetExtreme();
    result.RssPhaseDegrees = phase.GetRss();
    return result;
}

/**
 * @brief   Computes worst-case gain and phase deviation bounds across every frequency of an AC sensitivity sweep.
 * @param[in] sensitivity  AC sensitivity result carrying per-part figures and tolerances.
 * @return  Bounds aligned with sensitivity.Frequencies.
 */
ACWorstCaseSweep ComputeACWorstCaseSweep(const ACSensitivity &sensitivity) {
    ACWorstCaseSweep sweep;
    sweep.Frequencies = sensitivity.Frequencies;
    for (const ACPartSensitivity &part : sensitivity.Parts) {
        if (HasPositiveTolerance(part.TolerancePercent)) {
            ++sweep.TolerancedPartCount;
        }
    }
    const std::size_t count = sensitivity.Frequencies.size();
    sweep.ExtremeGainDecibels.reserve(count);
    sweep.RssGainDecibels.reserve(count);
    sweep.ExtremePhaseDegrees.reserve(count);
    sweep.RssPhaseDegrees.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const ACWorstCasePoint point = ComputeACWorstCaseAt(sensitivity, index);
        sweep.ExtremeGainDecibels.push_back(point.ExtremeGainDecibels);
        sweep.RssGainDecibels.push_back(point.RssGainDecibels);
        sweep.ExtremePhaseDegrees.push_back(point.ExtremePhaseDegrees);
        sweep.RssPhaseDegrees.push_back(point.RssPhaseDegrees);
    }
    return sweep;
}

} // namespace Core
