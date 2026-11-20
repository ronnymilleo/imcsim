/**
 * @file    logic_lane_plot.cpp
 * @brief   The plot of logic lanes under the analog traces of the Transient tab: which lanes it has, how tall it is,
 * and the drawing of its buses, signals and bit values.
 */

#include "windows/output_tabs/logic_lane_plot.h"

#include "base/node_colors.h"
#include "implot.h"
#include "schematic/probing.h"
#include <algorithm>
#include <array>
#include <string_view>

namespace GUI {

namespace {

/// Height of the plot of logic lanes for its axis, in font sizes.
constexpr float LANE_PLOT_MARGIN = 3.5f;
/// Height of each lane of the plot, in font sizes.
constexpr float LANE_ROW_HEIGHT = 2.2f;
/// Most of the room the plot of logic lanes may take.
constexpr float MAX_LANE_SHARE = 0.6f;
/// Room around the digit of a bit label, in font sizes.
constexpr float BIT_BOX_PADDING = 0.25f;
/// Corner rounding of the box of a bit label.
constexpr float BIT_BOX_ROUNDING = 3.0f;
/// How far the ends of a value of a bus slant in, in font sizes, as a logic analyzer draws a change of a bus.
constexpr float BUS_SLANT = 0.3f;
/// How much of the lane color fills a value of a bus that is unknown.
constexpr float UNKNOWN_BUS_FILL_ALPHA = 0.25f;

/**
 * @brief   The measured nodes of a result that logic parts are connected to, which the transient plots as lanes.
 */
std::vector<LogicNode> ListMeasuredLogicNodes(const std::vector<LogicNode> &logic_nodes,
                                              const std::size_t node_count,
                                              const Schematic &schematic) {
    std::vector<LogicNode> measured;
    for (const LogicNode &logic_node : logic_nodes) {
        if (static_cast<std::size_t>(logic_node.Node) < node_count
            && schematic.GetMeasurements().IsVoltageMeasured(logic_node.Node)) {
            measured.push_back(logic_node);
        }
    }
    return measured;
}

/**
 * @brief   The height a lane sits at, counted from the bottom, as the first lane is drawn on top.
 */
std::size_t GetLanePosition(const std::size_t lane, const std::size_t lane_count) {
    return lane_count - 1 - lane;
}

/**
 * @brief   Writes 0, 1 or X in a box at the middle of every cell of a lane that has room for it, within what the plot
 *          shows, so zooming out drops the labels that no longer fit.
 */
void DrawBitValues(const std::vector<LogicSegment> &cells, const std::size_t lane_position, const ImU32 color) {
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    const double middle = static_cast<double>(lane_position) * LANE_STRIDE + LANE_HIGH_OFFSET / 2.0;
    const float pixel_y = ImPlot::PlotToPixels(view.Min, middle).y;
    const float padding = ImGui::GetFontSize() * BIT_BOX_PADDING;
    ImDrawList *draw_list = ImPlot::GetPlotDrawList();
    ImPlot::PushPlotClipRect();
    for (const LogicSegment &cell : cells) {
        const double from = std::max(cell.From, view.Min);
        const double to = std::min(cell.To, view.Max);
        if (to <= from) {
            continue;
        }
        const char *text = GetLogicStateName(cell.State);
        const ImVec2 text_size = ImGui::CalcTextSize(text);
        const float left = ImPlot::PlotToPixels(from, middle).x;
        const float right = ImPlot::PlotToPixels(to, middle).x;
        const ImVec2 box_size = text_size + ImVec2(padding, padding) * 2.0f;
        if (right - left < box_size.x + padding) {
            continue;
        }
        const ImVec2 box_min((left + right - box_size.x) / 2.0f, pixel_y - box_size.y / 2.0f);
        draw_list->AddRectFilled(box_min, box_min + box_size, ImGui::GetColorU32(ImGuiCol_FrameBg), BIT_BOX_ROUNDING);
        draw_list->AddRect(box_min, box_min + box_size, color, BIT_BOX_ROUNDING);
        draw_list->AddText(box_min + ImVec2(padding, padding), ImGui::GetColorU32(ImGuiCol_Text), text);
    }
    ImPlot::PopPlotClipRect();
}

/**
 * @brief   Draws one value of a bus between two pixel columns of its lane: a band whose ends slant in to its middle,
 *          shaded while the value is unknown, with the value written in it when it fits.
 */
void DrawBusValue(ImDrawList *draw_list,
                  const float left,
                  const float right,
                  const float top,
                  const float bottom,
                  const std::string &text,
                  const bool is_unknown,
                  const ImU32 color) {
    const float middle = (top + bottom) / 2.0f;
    const float slant = std::min(ImGui::GetFontSize() * BUS_SLANT, (right - left) / 2.0f);
    const std::array<ImVec2, 6> band = {ImVec2(left, middle),
                                        ImVec2(left + slant, top),
                                        ImVec2(right - slant, top),
                                        ImVec2(right, middle),
                                        ImVec2(right - slant, bottom),
                                        ImVec2(left + slant, bottom)};
    if (is_unknown) {
        ImVec4 fill = ImGui::ColorConvertU32ToFloat4(color);
        fill.w *= UNKNOWN_BUS_FILL_ALPHA;
        draw_list->AddConvexPolyFilled(band.data(),
                                       static_cast<int>(band.size()),
                                       ImGui::ColorConvertFloat4ToU32(fill));
    }
    draw_list->AddPolyline(band.data(), static_cast<int>(band.size()), color, ImDrawFlags_Closed, 1.5f);
    const ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
    // The text sits across the middle, where the band is widest, so it may reach into the slanted ends
    if (right - left >= text_size.x + slant) {
        draw_list->AddText({(left + right - text_size.x) / 2.0f, middle - text_size.y / 2.0f},
                           ImGui::GetColorU32(ImGuiCol_Text),
                           text.c_str());
    }
}

/**
 * @brief   Draws the lane of a bus: one band per value within what the plot shows, so a value too short to see at the
 *          zoom is a sliver and its text is left out.
 */
void DrawBusLane(const Core::Transient &transient,
                 const BusLane &lane,
                 const std::size_t lane_position,
                 const ImU32 color) {
    const std::vector<LogicSignal> bits = GetBusSignals(lane, transient.NodeVoltages);
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    const double base = static_cast<double>(lane_position) * LANE_STRIDE;
    const float top = ImPlot::PlotToPixels(view.Min, base + LANE_HIGH_OFFSET).y;
    const float bottom = ImPlot::PlotToPixels(view.Min, base + LANE_LOW_OFFSET).y;
    ImDrawList *draw_list = ImPlot::GetPlotDrawList();
    ImPlot::PushPlotClipRect();
    for (const BusSegment &segment : FindBusSegments(transient.Times, bits)) {
        const double from = std::max(segment.From, view.Min);
        const double to = std::min(segment.To, view.Max);
        if (to <= from) {
            continue;
        }
        DrawBusValue(draw_list,
                     ImPlot::PlotToPixels(from, base).x,
                     ImPlot::PlotToPixels(to, base).x,
                     top,
                     bottom,
                     FormatBusValue(segment.Value, lane.Bits.size(), lane.Format),
                     !segment.Value,
                     color);
    }
    ImPlot::PopPlotClipRect();
}

} // namespace

/**
 * @brief   The number of lanes, buses and signals together.
 */
std::size_t LaneLayout::GetCount() const {
    return Buses.size() + Signals.size();
}

/**
 * @brief   Finds the lanes the plot shows, from one build of the circuit.
 * @param[in] schematic        Schematic with the measurements, the buses and the nets, which builds its circuit here.
 * @param[in] node_count       Nodes of the result, which the lanes must name.
 * @param[in] are_lanes_shown  Whether the user wants the lanes.
 * @return  The buses shown whose bits all name a node of the result, and the measured nodes of logic parts; none of
 *          them while lanes are turned off, though whether the circuit has logic parts is still told.
 */
LaneLayout FindLaneLayout(Schematic &schematic, const std::size_t node_count, const bool are_lanes_shown) {
    const std::vector<LogicNode> logic_nodes = FindLogicNodes(schematic.BuildCircuit());
    LaneLayout layout{.Buses = {}, .Signals = {}, .HasLogicParts = !logic_nodes.empty()};
    if (!are_lanes_shown) {
        return layout;
    }
    const NetResolver find_node = [&schematic](const std::string_view net) {
        return schematic.GetConnectivity().FindNodeOfNet(net);
    };
    layout.Buses = ResolveBusLanes(schematic.GetMeasurements().GetBuses(), logic_nodes, find_node, node_count);
    layout.Signals = ListMeasuredLogicNodes(logic_nodes, node_count, schematic);
    return layout;
}

/**
 * @brief   Splits the room left in the window between the analog plot and the plot of logic lanes.
 * @details The lanes take a row each, up to a share of the room, and the analog plot takes the rest; a plot alone takes
 *          all of it.
 */
PlotHeights GetPlotHeights(const std::size_t lane_count, const bool has_analog_plot) {
    if (lane_count == 0 || !has_analog_plot) {
        const float height = PlotHeight(1);
        return {.Analog = height, .Lanes = height};
    }
    const float font_size = ImGui::GetFontSize();
    const float available = ImGui::GetContentRegionAvail().y;
    const float lane_height =
        std::clamp(font_size * (LANE_PLOT_MARGIN + static_cast<float>(lane_count) * LANE_ROW_HEIGHT),
                   font_size * MIN_PLOT_HEIGHT,
                   available * MAX_LANE_SHARE);
    const float analog_height =
        std::max(font_size * MIN_PLOT_HEIGHT, available - lane_height - ImGui::GetStyle().ItemSpacing.y * 2.0f);
    return {.Analog = analog_height, .Lanes = lane_height};
}

/**
 * @brief   The names of the lanes, in order from the top: each bus by its name, each signal by its net.
 */
std::vector<std::string> ListLaneNames(const LaneLayout &layout, const Connectivity &connectivity) {
    std::vector<std::string> names;
    for (const BusLane &bus : layout.Buses) {
        names.push_back(bus.Name);
    }
    for (const LogicNode &signal : layout.Signals) {
        names.push_back(GetVoltageLabel(connectivity, signal.Node));
    }
    return names;
}

/**
 * @brief   Sets the axes of the plot of lanes: time, linked to the analog plot, and a locked axis with each lane named
 *          at its middle.
 * @param[in]     names  Names of the lanes, from the top.
 * @param[in,out] view   Time range the analog plot shares.
 */
void SetupLaneAxes(const std::vector<std::string> &names, PlotSpan &view) {
    const std::size_t lane_count = names.size();
    const auto top = static_cast<double>(lane_count - 1) * LANE_STRIDE + LANE_HIGH_OFFSET + LANE_MARGIN;
    ImPlot::SetupAxes("Time",
                      "",
                      ImPlotAxisFlags_None,
                      ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickMarks);
    ImPlot::SetupAxisLimits(ImAxis_Y1, LANE_LOW_OFFSET - LANE_MARGIN, top, ImPlotCond_Always);
    ImPlot::SetupAxisLinks(ImAxis_X1, &view.From, &view.To);
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("s"));
    std::vector<double> positions;
    std::vector<const char *> labels;
    for (std::size_t lane = 0; lane < lane_count; ++lane) {
        positions.push_back(static_cast<double>(GetLanePosition(lane, lane_count)) * LANE_STRIDE
                            + LANE_HIGH_OFFSET / 2.0);
        labels.push_back(names[lane].c_str());
    }
    ImPlot::SetupAxisTicks(ImAxis_Y1, positions.data(), static_cast<int>(positions.size()), labels.data());
}

/**
 * @brief   Draws the buses in the top lanes.
 */
void DrawBusLanes(const Core::Transient &transient, const LaneLayout &layout) {
    for (std::size_t bus = 0; bus < layout.Buses.size(); ++bus) {
        DrawBusLane(transient,
                    layout.Buses[bus],
                    GetLanePosition(bus, layout.GetCount()),
                    GetNodeColor(layout.Buses[bus].Bits.front().Node));
    }
}

/**
 * @brief   Plots each signal as a trace between its low and high levels, in the lanes under the buses.
 * @details A lane the circuit drives to a voltage between the thresholds is drawn halfway between low and high.
 * @return  The traces drawn, for the readout, which reads a logic state.
 */
std::vector<PlottedTrace>
PlotSignalLanes(const Core::Transient &transient, const LaneLayout &layout, const std::vector<std::string> &names) {
    std::vector<PlottedTrace> plotted;
    for (std::size_t signal = 0; signal < layout.Signals.size(); ++signal) {
        const std::size_t lane = layout.Buses.size() + signal;
        const LogicNode &node = layout.Signals[signal];
        const std::vector<double> &voltages = transient.NodeVoltages[static_cast<std::size_t>(node.Node)];
        const std::vector<double> heights =
            BuildLane(voltages, node.LogicLevel, GetLanePosition(lane, layout.GetCount()));
        const ImU32 color = GetNodeColor(node.Node);
        ImPlotSpec spec;
        spec.LineColor = ImGui::ColorConvertU32ToFloat4(color);
        ImPlot::PlotLine(names[lane].c_str(),
                         transient.Times.data(),
                         heights.data(),
                         static_cast<int>(heights.size()),
                         spec);
        plotted.push_back({names[lane], &voltages, "V", color, node.LogicLevel});
    }
    return plotted;
}

/**
 * @brief   Labels every cell of the signal lanes with 0, 1 or X, within what the plot shows.
 */
void DrawBitValueLabels(const Core::Transient &transient, const LaneLayout &layout) {
    std::vector<LogicSignal> signals;
    for (const LogicNode &node : layout.Signals) {
        signals.push_back({transient.NodeVoltages[static_cast<std::size_t>(node.Node)], node.LogicLevel});
    }
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    const auto cells = FindBitCells(transient.Times, signals, view.Min, view.Max);
    for (std::size_t signal = 0; signal < layout.Signals.size(); ++signal) {
        DrawBitValues(cells[signal],
                      GetLanePosition(layout.Buses.size() + signal, layout.GetCount()),
                      GetNodeColor(layout.Signals[signal].Node));
    }
}

} // namespace GUI
