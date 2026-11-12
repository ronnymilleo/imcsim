/**
 * @file    logic_bus_definition.cpp
 * @brief   What a logic bus is, as the schematic keeps and saves it.
 */

#include "schematic/logic_bus_definition.h"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace GUI {

namespace {

constexpr auto BUS_FORMAT_NAMES = std::to_array<std::pair<BusFormat, const char *>>({
    {BusFormat::BINARY, "bin"},
    {BusFormat::HEXADECIMAL, "hex"},
    {BusFormat::DECIMAL, "dec"},
});

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

} // namespace GUI
