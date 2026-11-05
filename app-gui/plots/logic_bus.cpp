/**
 * @file    logic_bus.cpp
 * @brief   Logic buses: several logic signals read together as one number, as a logic analyzer shows them.
 */

#include "plots/logic_bus.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <limits>
#include <utility>

namespace GUI {

namespace {

constexpr auto BUS_FORMAT_NAMES = std::to_array<std::pair<BusFormat, const char *>>({
    {BusFormat::BINARY, "bin"},
    {BusFormat::HEXADECIMAL, "hex"},
    {BusFormat::DECIMAL, "dec"},
});

/// Bits written by one hexadecimal digit.
constexpr std::size_t BITS_PER_HEX_DIGIT = 4;

/**
 * @brief   The node a bit names, V(3) by its number or V(Q0) by the net, or none for anything else.
 */
std::optional<int> FindBitNode(const std::string_view bit, const NetResolver &find_node) {
    if (!bit.starts_with("V(") || !bit.ends_with(')') || bit.size() <= 3) {
        return std::nullopt;
    }
    const std::string_view inside = bit.substr(2, bit.size() - 3);
    int node = 0;
    const auto [end, error] = std::from_chars(inside.data(), inside.data() + inside.size(), node);
    if (error == std::errc() && end == inside.data() + inside.size()) {
        return node;
    }
    return find_node(inside);
}

} // namespace

/**
 * @brief   A name for a new bus that no other bus has.
 * @param[in] buses  The buses there are.
 * @return  BUS1, or BUS2 when BUS1 is taken, and so on.
 */
std::string GetNextBusName(const std::vector<LogicBus> &buses) {
    for (std::size_t number = 1;; ++number) {
        std::string name = std::format("BUS{}", number);
        if (!std::ranges::contains(buses, name, &LogicBus::Name)) {
            return name;
        }
    }
}

/**
 * @brief   The short name of a format, as the editor offers it and the file keeps it.
 * @param[in] format  A format.
 * @return  "bin", "hex" or "dec".
 */
const char *GetBusFormatName(const BusFormat format) {
    for (const auto &[candidate, name] : BUS_FORMAT_NAMES) {
        if (candidate == format) {
            return name;
        }
    }
    return "hex";
}

/**
 * @brief   Reads a format back from its short name.
 * @param[in] name  "bin", "hex" or "dec".
 * @return  The format; none for another name.
 */
std::optional<BusFormat> ParseBusFormat(const std::string_view name) {
    for (const auto &[format, candidate] : BUS_FORMAT_NAMES) {
        if (name == candidate) {
            return format;
        }
    }
    return std::nullopt;
}

/**
 * @brief   Finds the node of every bit of a bus in a result.
 * @param[in] bus         The bus.
 * @param[in] find_node   Looks a net name up in the circuit of the result.
 * @param[in] node_count  Nodes in the result, ground included.
 * @return  The node of each bit, the most significant first; the reason otherwise: a bus with no bits or more than
 *          MAX_BUS_BITS, or a bit that names no node of the result.
 */
std::expected<std::vector<int>, std::string> ResolveBusBits(const LogicBus &bus, const NetResolver &find_node,
                                                            const std::size_t node_count) {
    if (bus.Bits.empty()) {
        return std::unexpected("Add the signals of its bits");
    }
    if (bus.Bits.size() > MAX_BUS_BITS) {
        return std::unexpected(std::format("A bus has at most {} bits", MAX_BUS_BITS));
    }
    std::vector<int> nodes;
    for (const std::string &bit : bus.Bits) {
        const std::optional<int> node = FindBitNode(bit, find_node);
        if (!node || *node < 0 || static_cast<std::size_t>(*node) >= node_count) {
            return std::unexpected(std::format("The circuit has no {}", bit));
        }
        nodes.push_back(*node);
    }
    return nodes;
}

/**
 * @brief   Finds the buses a plot draws as lanes.
 * @param[in] buses        Every bus of the schematic.
 * @param[in] logic_nodes  The nodes of logic parts, whose logic level a bit on them is read against.
 * @param[in] find_node    Looks a net name up in the circuit of the result.
 * @param[in] node_count   Nodes in the result, ground included.
 * @return  The shown buses whose bits all name a node of the result, in order; a bit on a node no logic part is
 *          connected to is read against the usual 5 V.
 */
std::vector<BusLane> ResolveBusLanes(const std::vector<LogicBus> &buses, const std::vector<LogicNode> &logic_nodes,
                                     const NetResolver &find_node, const std::size_t node_count) {
    std::vector<BusLane> lanes;
    for (const LogicBus &bus : buses) {
        if (!bus.Shown) {
            continue;
        }
        const auto nodes = ResolveBusBits(bus, find_node, node_count);
        if (!nodes) {
            continue;
        }
        BusLane lane{.Name = bus.Name, .Bits = {}, .Format = bus.Format};
        for (const int node : *nodes) {
            const auto logic_node = std::ranges::find(logic_nodes, node, &LogicNode::Node);
            lane.Bits.push_back(
                {.Node = node,
                 .LogicLevel = logic_node != logic_nodes.end() ? logic_node->LogicLevel : LogicNode{}.LogicLevel});
        }
        lanes.push_back(std::move(lane));
    }
    return lanes;
}

/**
 * @brief   The signals of the bits of a bus lane in a result.
 * @param[in] lane           A lane ResolveBusLanes() gave for the result.
 * @param[in] node_voltages  Voltage of every node of the result at every point.
 * @return  One signal per bit, the most significant first.
 */
std::vector<LogicSignal> GetBusSignals(const BusLane &lane, const std::vector<std::vector<double>> &node_voltages) {
    std::vector<LogicSignal> signals;
    for (const LogicNode &bit : lane.Bits) {
        signals.push_back({node_voltages[static_cast<std::size_t>(bit.Node)], bit.LogicLevel});
    }
    return signals;
}

/**
 * @brief   The value of a bus at one point of a result.
 * @param[in] bits    The signals of its bits, the most significant first, each with a voltage at every point.
 * @param[in] sample  The point.
 * @return  The bits read as a binary number; none when any of them is unknown there.
 */
std::optional<std::uint64_t> ReadBusValue(const std::span<const LogicSignal> bits, const std::size_t sample) {
    std::uint64_t value = 0;
    for (const LogicSignal &bit : bits) {
        const LogicState state = ReadLogicState(bit.Voltages[sample], bit.LogicLevel);
        if (state == LogicState::UNKNOWN) {
            return std::nullopt;
        }
        value = (value << 1U) | (state == LogicState::HIGH ? 1U : 0U);
    }
    return value;
}

/**
 * @brief   Splits a result into the stretches over which a bus keeps one value.
 * @param[in] times  The time of every point.
 * @param[in] bits   The signals of its bits, the most significant first, each with a voltage at every point.
 * @return  The stretches in order, each from the point where its value starts to the point where the next one does,
 *          the last one to the last point; none for a result without points.
 */
std::vector<BusSegment> FindBusSegments(const std::span<const double> times, const std::span<const LogicSignal> bits) {
    std::vector<BusSegment> segments;
    for (std::size_t sample = 0; sample < times.size(); ++sample) {
        const std::optional<std::uint64_t> value = ReadBusValue(bits, sample);
        if (!segments.empty() && segments.back().Value == value) {
            segments.back().To = times[sample];
            continue;
        }
        if (!segments.empty()) {
            segments.back().To = times[sample];
        }
        segments.push_back({.From = times[sample], .To = times[sample], .Value = value});
    }
    return segments;
}

/**
 * @brief   Writes the value of a bus as its lane shows it.
 * @param[in] value      The value, or none while a bit is unknown.
 * @param[in] bit_count  Bits of the bus, which set how many digits binary and hexadecimal take.
 * @param[in] format     How to write it.
 * @return  Such as "1010", "A" or "10", with leading zeros in binary and hexadecimal so every value of a bus is as
 *          wide; "X" while a bit is unknown.
 */
std::string FormatBusValue(const std::optional<std::uint64_t> value, const std::size_t bit_count,
                           const BusFormat format) {
    if (!value) {
        return "X";
    }
    switch (format) {
    case BusFormat::BINARY:
        return std::format("{:0{}b}", *value, std::max<std::size_t>(bit_count, 1));
    case BusFormat::HEXADECIMAL:
        return std::format("{:0{}X}", *value,
                           std::max<std::size_t>((bit_count + BITS_PER_HEX_DIGIT - 1) / BITS_PER_HEX_DIGIT, 1));
    case BusFormat::DECIMAL:
    default:
        return std::format("{}", *value);
    }
}

/**
 * @brief   Lays the lane of a bus out as two traces at the heights of its lane, for an export.
 * @param[in] bits           The signals of its bits, the most significant first.
 * @param[in] sample_count   Points of the result.
 * @param[in] lane_position  Lane it takes, counted from the bottom.
 * @return  Its top and bottom edges, which both go to the middle of the lane at each point where the value changes,
 *          so the lane reads as a band pinched at every change; and the value at each point, not a number while a bit
 *          is unknown.
 */
BusOutline BuildBusOutline(const std::span<const LogicSignal> bits, const std::size_t sample_count,
                           const std::size_t lane_position) {
    const double base = static_cast<double>(lane_position) * LANE_STRIDE;
    const double middle = base + (LANE_LOW_OFFSET + LANE_HIGH_OFFSET) / 2.0;
    BusOutline outline;
    std::optional<std::uint64_t> previous;
    for (std::size_t sample = 0; sample < sample_count; ++sample) {
        const std::optional<std::uint64_t> value = ReadBusValue(bits, sample);
        const bool changes = sample > 0 && value != previous;
        outline.Top.push_back(changes ? middle : base + LANE_HIGH_OFFSET);
        outline.Bottom.push_back(changes ? middle : base + LANE_LOW_OFFSET);
        outline.Values.push_back(value ? static_cast<double>(*value) : std::numeric_limits<double>::quiet_NaN());
        previous = value;
    }
    return outline;
}

} // namespace GUI
