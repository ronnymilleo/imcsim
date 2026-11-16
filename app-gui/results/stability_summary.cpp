/**
 * @file    stability_summary.cpp
 * @brief   Tells in words how stable a two-port is over its sweep, where its worst point is and what its circles show.
 */

#include "results/stability_summary.h"

#include "spice/spice_value.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace GUI {

namespace {

/// Digits of a factor on screen, enough to tell 0.98 from 1.02.
constexpr int FACTOR_DIGITS = 3;

/// The two kinds of termination, named in the sentences.
constexpr const char *SOURCE_NAME = "Source";
constexpr const char *LOAD_NAME = "Load";

std::string FormatFactor(const double value) {
    return std::isfinite(value) ? std::format("{:.{}g}", value, FACTOR_DIGITS) : "infinite";
}

std::string FormatFrequency(const double frequency_hertz) {
    return Core::FormatValue(frequency_hertz) + "Hz";
}

bool IsUnstable(const TwoPortMetrics &metrics) {
    return metrics.IsValid && !metrics.IsUnconditionallyStable;
}

/**
 * @brief   Tells whether the unstable points are all unstable because K is below 1, which is the form most readers
 *          know; mu says it for the rest.
 */
bool AreAllKBelowOne(const TwoPortSweep &sweep) {
    return std::ranges::all_of(sweep.Metrics, [](const TwoPortMetrics &metrics) {
        return !IsUnstable(metrics) || (std::isfinite(metrics.K) && metrics.K < 1.0);
    });
}

/**
 * @brief   Finds the unstable point with the lowest value of K, or of mu where K does not say it.
 */
std::size_t FindWorstPoint(const TwoPortSweep &sweep, const bool is_k) {
    std::size_t worst = 0;
    double lowest = std::numeric_limits<double>::infinity();
    bool is_found = false;
    for (std::size_t index = 0; index < sweep.Metrics.size(); ++index) {
        if (!IsUnstable(sweep.Metrics[index])) {
            continue;
        }
        const double value = is_k ? sweep.Metrics[index].K : sweep.Metrics[index].Mu;
        if (!is_found || value < lowest) {
            is_found = true;
            lowest = value;
            worst = index;
        }
    }
    return worst;
}

std::string DescribeBand(const TwoPortSweep &sweep, const UnstableRegion &region) {
    const std::string first = FormatFrequency(sweep.Frequencies[region.First]);
    const std::string last = FormatFrequency(sweep.Frequencies[region.Last]);
    if (region.First == region.Last) {
        return "at " + first;
    }
    if (region.First == 0) {
        return "up to " + last;
    }
    if (region.Last == sweep.Frequencies.size() - 1) {
        return "from " + first + " upward";
    }
    return std::format("from {} to {}", first, last);
}

std::string DescribeInstability(const TwoPortSweep &sweep, const std::vector<UnstableRegion> &regions) {
    const bool is_k = AreAllKBelowOne(sweep);
    const std::string reason = is_k ? "K < 1" : "mu < 1";
    const std::size_t worst = FindWorstPoint(sweep, is_k);
    const std::string lowest = std::format("; lowest {} is {} at {}", is_k ? "K" : "mu",
                                           FormatFactor(is_k ? sweep.Metrics[worst].K : sweep.Metrics[worst].Mu),
                                           FormatFrequency(sweep.Frequencies[worst]));
    const bool is_whole =
        regions.size() == 1 && regions.front().First == 0 && regions.front().Last == sweep.Frequencies.size() - 1;
    if (is_whole) {
        return std::format("Potentially unstable over the whole sweep ({}){}.", reason, lowest);
    }
    std::string where;
    if (regions.size() == 1) {
        where = DescribeBand(sweep, regions.front());
    } else if (regions.size() == 2) {
        where = DescribeBand(sweep, regions.front()) + " and " + DescribeBand(sweep, regions.back());
    } else {
        where = std::format("in {} separate bands between {} and {}", regions.size(),
                            FormatFrequency(sweep.Frequencies[regions.front().First]),
                            FormatFrequency(sweep.Frequencies[regions.back().Last]));
    }
    return std::format("Potentially unstable {} ({}){}. Unconditionally stable elsewhere in the sweep.", where, reason,
                       lowest);
}

std::string DescribeStabilityMargin(const TwoPortSweep &sweep) {
    const std::optional<std::size_t> lowest = FindLowestMu(sweep);
    if (!lowest || !std::isfinite(sweep.Metrics[*lowest].Mu)) {
        return "";
    }
    return std::format("; the smallest margin is mu = {} at {}", FormatFactor(sweep.Metrics[*lowest].Mu),
                       FormatFrequency(sweep.Frequencies[*lowest]));
}

/**
 * @brief   What is special about the points of a sweep that lack a reverse or a forward transmission.
 */
std::string DescribeDegenerateTransmission(const TwoPortSweep &sweep, const std::size_t valid_count) {
    const auto unilateral = static_cast<std::size_t>(std::ranges::count_if(
        sweep.Metrics, [](const TwoPortMetrics &metrics) { return metrics.IsValid && metrics.IsUnilateral; }));
    const auto silent =
        static_cast<std::size_t>(std::ranges::count_if(sweep.Metrics, [](const TwoPortMetrics &metrics) {
            return metrics.IsValid && !metrics.HasForwardTransmission;
        }));
    std::string text;
    if (silent == valid_count) {
        return " S21 is 0, so nothing is transmitted and there is no gain to bound.";
    }
    if (unilateral == valid_count) {
        text = " S12 is 0, so the two-port is unilateral: K is infinite and the gain shown is the unilateral power "
               "gain.";
    } else if (unilateral > 0) {
        text = std::format(" S12 is 0 at {} of {} points, where the two-port is unilateral.", unilateral, valid_count);
    }
    if (silent > 0) {
        text += std::format(" S21 is 0 at {} of {} points, which have no gain bound.", silent, valid_count);
    }
    return text;
}

std::string DescribeCoverage(const char *name, const StabilityBoundary &boundary, const std::string &frequency) {
    if (boundary.Coverage == ChartCoverage::PARTLY) {
        return "";
    }
    return std::format("{} circle: the whole chart is {} at {}, so no circle shows.", name,
                       boundary.Coverage == ChartCoverage::ENTIRELY_STABLE ? "stable" : "unstable", frequency);
}

} // namespace

/**
 * @brief   Finds the runs of frequencies at which the two-port can be made to oscillate.
 * @param[in] sweep  Metrics of a sweep.
 * @return  Runs of consecutive points that are not unconditionally stable, in order; a point with no valid matrix is
 *          not unstable and ends a run.
 */
std::vector<UnstableRegion> FindUnstableRegions(const TwoPortSweep &sweep) {
    std::vector<UnstableRegion> regions;
    for (std::size_t index = 0; index < sweep.Metrics.size(); ++index) {
        if (!IsUnstable(sweep.Metrics[index])) {
            continue;
        }
        if (!regions.empty() && regions.back().Last + 1 == index) {
            regions.back().Last = index;
        } else {
            regions.push_back({.First = index, .Last = index});
        }
    }
    return regions;
}

/**
 * @brief   Finds the frequency where the two-port is the least stable.
 * @param[in] sweep  Metrics of a sweep.
 * @return  Index of the lowest finite mu, the first of equal ones; the first valid point when every mu is infinite;
 *          none when no point has a valid matrix.
 */
std::optional<std::size_t> FindLowestMu(const TwoPortSweep &sweep) {
    std::optional<std::size_t> lowest;
    for (std::size_t index = 0; index < sweep.Metrics.size(); ++index) {
        const TwoPortMetrics &metrics = sweep.Metrics[index];
        if (!metrics.IsValid) {
            continue;
        }
        if (!lowest || metrics.Mu < sweep.Metrics[*lowest].Mu) {
            lowest = index;
        }
    }
    return lowest;
}

/**
 * @brief   Words the stability of a two-port over its sweep.
 * @param[in] sweep  Metrics of a sweep.
 * @return  One or two sentences: unconditionally stable over the whole sweep, or where it can oscillate (the first and
 *          last frequency of each band, with the lowest K or mu) and that it is stable elsewhere; then what its
 *          transmissions say, when S12 or S21 is 0. Empty for an empty sweep.
 */
std::string DescribeStability(const TwoPortSweep &sweep) {
    if (sweep.Metrics.empty()) {
        return "";
    }
    const auto valid_count = static_cast<std::size_t>(
        std::ranges::count_if(sweep.Metrics, [](const TwoPortMetrics &metrics) { return metrics.IsValid; }));
    if (valid_count == 0) {
        return "The S matrix has no finite values, so its stability cannot be told.";
    }
    const std::vector<UnstableRegion> regions = FindUnstableRegions(sweep);
    std::string text;
    if (valid_count == 1) {
        const std::size_t index = FindLowestMu(sweep).value_or(0);
        const TwoPortMetrics &metrics = sweep.Metrics[index];
        const std::string frequency = FormatFrequency(sweep.Frequencies[index]);
        text = IsUnstable(metrics)
                   ? std::format("Potentially unstable at {} ({} = {}).", frequency,
                                 std::isfinite(metrics.K) && metrics.K < 1.0 ? "K" : "mu",
                                 FormatFactor(std::isfinite(metrics.K) && metrics.K < 1.0 ? metrics.K : metrics.Mu))
                   : std::format("Unconditionally stable at {} (K = {}, mu = {}).", frequency, FormatFactor(metrics.K),
                                 FormatFactor(metrics.Mu));
    } else if (regions.empty()) {
        text = "Unconditionally stable over the whole sweep" + DescribeStabilityMargin(sweep) + ".";
    } else {
        text = DescribeInstability(sweep, regions);
    }
    return text + DescribeDegenerateTransmission(sweep, valid_count);
}

/**
 * @brief   Words what the circles show at one frequency when one of them, or both, leaves the chart wholly stable or
 *          wholly unstable.
 * @param[in] circles          The two boundaries.
 * @param[in] frequency_hertz  Frequency they were worked out at.
 * @return  A sentence for each boundary that does not cross the chart, joined by a space; empty when both do.
 */
std::string DescribeCircles(const StabilityCircles &circles, const double frequency_hertz) {
    const std::string frequency = FormatFrequency(frequency_hertz);
    const std::string source = DescribeCoverage(SOURCE_NAME, circles.Source, frequency);
    const std::string load = DescribeCoverage(LOAD_NAME, circles.Load, frequency);
    return source.empty() || load.empty() ? source + load : source + " " + load;
}

/**
 * @brief   Labels the Smith chart with the frequency its stability circles are drawn at and the mu found there.
 * @param[in] metrics          Metrics at that frequency.
 * @param[in] frequency_hertz  The frequency.
 * @return  Text such as "Circles at 1.2GHz, mu = 0.83", or one that says the S matrix has no finite value there.
 */
std::string DescribeCircleFrequency(const TwoPortMetrics &metrics, const double frequency_hertz) {
    const std::string frequency = FormatFrequency(frequency_hertz);
    if (!metrics.IsValid) {
        return std::format("Circles at {}, no finite S matrix", frequency);
    }
    return std::format("Circles at {}, mu = {}", frequency, FormatFactor(metrics.Mu));
}

/**
 * @brief   Words the metrics at one frequency for a tooltip, one line each.
 * @param[in] metrics          Metrics at the frequency.
 * @param[in] frequency_hertz  The frequency.
 * @return  The frequency, K and |Delta|, mu and mu', and the gain bound with its kind.
 */
std::vector<std::string> DescribeMetrics(const TwoPortMetrics &metrics, const double frequency_hertz) {
    std::vector<std::string> lines;
    lines.push_back(std::format("Stability at {}", FormatFrequency(frequency_hertz)));
    if (!metrics.IsValid) {
        lines.emplace_back("The S matrix has no finite value here");
        return lines;
    }
    lines.push_back(std::format("K = {}, |Delta| = {}", FormatFactor(metrics.K), FormatFactor(metrics.DeltaMagnitude)));
    lines.push_back(std::format("mu = {}, mu' = {}", FormatFactor(metrics.Mu), FormatFactor(metrics.MuPrime)));
    lines.emplace_back(metrics.IsUnconditionallyStable ? "Unconditionally stable" : "Potentially unstable");
    const std::string gain = metrics.GainDecibels ? FormatFactor(*metrics.GainDecibels) + " dB" : "";
    switch (metrics.Gain) {
    case GainKind::MAG:
        lines.push_back("MAG " + gain);
        break;
    case GainKind::MSG:
        lines.push_back("MSG " + gain);
        break;
    case GainKind::UNILATERAL:
        lines.push_back("Unilateral gain " + gain);
        break;
    case GainKind::NONE:
    default:
        lines.emplace_back("No gain bound");
        break;
    }
    return lines;
}

} // namespace GUI
