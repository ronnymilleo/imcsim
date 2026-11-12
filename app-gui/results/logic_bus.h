/**
 * @file    logic_bus.h
 * @brief   Logic buses: several logic signals read together as one number, as a logic analyzer shows them.
 * @details Pure functions, so the plot and the export only draw what they give.
 */

#ifndef IMCSIM_LOGIC_BUS_H
#define IMCSIM_LOGIC_BUS_H

#include "results/logic_trace.h"
#include "results/trace_math.h"
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/**
 * @enum    BusFormat
 * @brief   How the value of a bus is written: in binary, one digit per bit, in hexadecimal or in decimal.
 */
enum class BusFormat {
    BINARY,
    HEXADECIMAL,
    DECIMAL
};

/// Most bits a bus may have, so its value fits a number and its label a lane.
inline constexpr std::size_t MAX_BUS_BITS = 32;

/**
 * @struct  LogicBus
 * @brief   Logic signals read as the bits of one number, the most significant first.
 * @details A bit names a voltage as the trace list does, V(Q0) for a net or V(3) for a node, so a bus saved with the
 *          file finds its nets again after edits that number the nodes anew.
 */
struct LogicBus {
    std::string Name;
    std::vector<std::string> Bits;
    BusFormat Format = BusFormat::HEXADECIMAL;
    bool Shown = true;

    bool operator==(const LogicBus &) const = default;
};

/**
 * @struct  BusLane
 * @brief   A bus as a plot draws it: its name, the node and logic level of each bit, the most significant first, and
 *          the format of its value.
 */
struct BusLane {
    std::string Name;
    std::vector<LogicNode> Bits;
    BusFormat Format = BusFormat::HEXADECIMAL;
};

/**
 * @struct  BusOutline
 * @brief   The lane of a bus as two traces, its top and bottom edges, which meet at its middle where its value changes,
 *          and the value at every point.
 * @details Lets an export draw the lane with the samples of the result alone; a value is not a number while a bit is
 *          unknown.
 */
struct BusOutline {
    std::vector<double> Top;
    std::vector<double> Bottom;
    std::vector<double> Values;
};

/**
 * @struct  BusSegment
 * @brief   A stretch of time over which a bus keeps one value; none while any of its bits is unknown.
 */
struct BusSegment {
    double From = 0.0;
    double To = 0.0;
    std::optional<std::uint64_t> Value;
};

// Naming
std::string GetNextBusName(const std::vector<LogicBus> &buses);
const char *GetBusFormatName(BusFormat format);
std::optional<BusFormat> ParseBusFormat(std::string_view name);

// Resolving
std::expected<std::vector<int>, std::string> ResolveBusBits(const LogicBus &bus, const NetResolver &find_node,
                                                            std::size_t node_count);
std::vector<BusLane> ResolveBusLanes(const std::vector<LogicBus> &buses, const std::vector<LogicNode> &logic_nodes,
                                     const NetResolver &find_node, std::size_t node_count);
std::vector<LogicSignal> GetBusSignals(const BusLane &lane, const std::vector<std::vector<double>> &node_voltages);

// Reading
std::optional<std::uint64_t> ReadBusValue(std::span<const LogicSignal> bits, std::size_t sample);
std::vector<BusSegment> FindBusSegments(std::span<const double> times, std::span<const LogicSignal> bits);
std::string FormatBusValue(std::optional<std::uint64_t> value, std::size_t bit_count, BusFormat format);
BusOutline BuildBusOutline(std::span<const LogicSignal> bits, std::size_t sample_count, std::size_t lane_position);

} // namespace GUI

#endif // IMCSIM_LOGIC_BUS_H
