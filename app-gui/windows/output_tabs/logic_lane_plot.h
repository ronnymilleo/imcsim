/**
 * @file    logic_lane_plot.h
 * @brief   The plot of logic lanes under the analog traces of the Transient tab: which lanes it has, how tall it is,
 * and the drawing of its buses, signals and bit values.
 */

#ifndef IMCSIM_LOGIC_LANE_PLOT_H
#define IMCSIM_LOGIC_LANE_PLOT_H

#include "plots/plot_types.h"
#include "results/logic_bus.h"
#include "results/logic_trace.h"
#include "schematic/connectivity.h"
#include "schematic/schematic.h"
#include "simulation/simulation_results.h"
#include "windows/output_tabs/plot_widgets.h"
#include <cstddef>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  LaneLayout
 * @brief   The lanes of the plot, buses on top and signals under them, worked out from one build of the circuit.
 */
struct LaneLayout {
    std::vector<BusLane> Buses;
    std::vector<LogicNode> Signals;
    /// Whether the circuit has logic parts at all, so the lanes can be offered even while they are turned off.
    bool HasLogicParts = false;

    std::size_t GetCount() const;
};

/**
 * @struct  PlotHeights
 * @brief   The heights of the analog plot and of the plot of logic lanes under it, in pixels.
 */
struct PlotHeights {
    float Analog = 0.0f;
    float Lanes = 0.0f;
};

// Layout
LaneLayout FindLaneLayout(Schematic &schematic, std::size_t node_count, bool are_lanes_shown);
PlotHeights GetPlotHeights(std::size_t lane_count, bool has_analog_plot);
std::vector<std::string> ListLaneNames(const LaneLayout &layout, const Connectivity &connectivity);

// Drawing, between BeginPlot and EndPlot
void SetupLaneAxes(const std::vector<std::string> &names, PlotSpan &view);
void DrawBusLanes(const Core::Transient &transient, const LaneLayout &layout);
std::vector<PlottedTrace>
PlotSignalLanes(const Core::Transient &transient, const LaneLayout &layout, const std::vector<std::string> &names);
void DrawBitValueLabels(const Core::Transient &transient, const LaneLayout &layout);

} // namespace GUI

#endif // IMCSIM_LOGIC_LANE_PLOT_H
