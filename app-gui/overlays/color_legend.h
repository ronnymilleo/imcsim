/**
 * @file    color_legend.h
 * @brief   What the legend of the heat colors reads: the values at the ends of its bar, and what they measure.
 */

#ifndef IMCSIM_COLOR_LEGEND_H
#define IMCSIM_COLOR_LEGEND_H

#include "results/circuit_snapshot.h"
#include "results/result_store.h"
#include "schematic/connectivity.h"
#include "schematic/preferences.h"
#include <optional>
#include <string>

namespace GUI {

/**
 * @struct  LegendLabels
 * @brief   The values at the two ends of the legend.
 */
struct LegendLabels {
    std::string Low;
    std::string High;
    /// What the colors measure, drawn above the bar, or empty.
    std::string Title;

    bool operator==(const LegendLabels &) const = default;
};

// Labels
bool HasLegend(WireColoring coloring);
LegendLabels GetHeatLegend(WireColoring coloring, const HeatScale &scale);
std::optional<LegendLabels> GetSensitivityLegend(const ResultStore &results, const Connectivity &connectivity);

} // namespace GUI

#endif // IMCSIM_COLOR_LEGEND_H
