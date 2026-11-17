/**
 * @file    stability_summary.h
 * @brief   Tells in words how stable a two-port is over its sweep, where its worst point is and what its circles show.
 * @details Pure functions with no ImGui, so the tests run every sentence the S-parameters tab can show about it.
 */

#ifndef IMCSIM_STABILITY_SUMMARY_H
#define IMCSIM_STABILITY_SUMMARY_H

#include "results/stability_circles.h"
#include "results/two_port_metrics.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  UnstableRegion
 * @brief   A run of consecutive frequencies of the sweep at which the two-port is not unconditionally stable.
 */
struct UnstableRegion {
    std::size_t First = 0;
    std::size_t Last = 0;

    bool operator==(const UnstableRegion &) const = default;
};

// Regions
std::vector<UnstableRegion> FindUnstableRegions(const TwoPortSweep &sweep);
std::optional<std::size_t> FindLowestMu(const TwoPortSweep &sweep);

// Sentences
std::string DescribeStability(const TwoPortSweep &sweep);
std::string DescribeCircles(const StabilityCircles &circles, double frequency_hertz);
std::string DescribeCircleFrequency(const TwoPortMetrics &metrics, double frequency_hertz);
std::vector<std::string> DescribeMetrics(const TwoPortMetrics &metrics, double frequency_hertz);

} // namespace GUI

#endif // IMCSIM_STABILITY_SUMMARY_H
