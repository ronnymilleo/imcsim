/**
 * @file    logic_bus_definition.h
 * @brief   What a logic bus is, as the schematic keeps and saves it.
 */

#ifndef IMCSIM_LOGIC_BUS_DEFINITION_H
#define IMCSIM_LOGIC_BUS_DEFINITION_H

#include <cstddef>
#include <optional>
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

// Naming
std::string GetNextBusName(const std::vector<LogicBus> &buses);
const char *GetBusFormatName(BusFormat format);
std::optional<BusFormat> ParseBusFormat(std::string_view name);

} // namespace GUI

#endif // IMCSIM_LOGIC_BUS_DEFINITION_H
