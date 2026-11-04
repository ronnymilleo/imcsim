/**
 * @file    spice_import.cpp
 * @brief   Turns the subcircuits of a SPICE library into blocks of the schematic, with pins the user can name and
 * place.
 */

#include "document/spice_import.h"

#include "components/subcircuit.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <set>
#include <utility>

namespace GUI {

namespace {

/// Port names of supplies, whose pins go on the top edge of the box.
constexpr auto TOP_PORTS = std::to_array<std::string_view>({"V+", "VCC", "VDD", "VPOS", "VS+", "VCC+", "VP"});
/// Port names of negative supplies and ground, whose pins go on the bottom edge.
constexpr auto BOTTOM_PORTS = std::to_array<std::string_view>({"V-", "VEE", "VSS", "VNEG", "VS-", "VCC-", "GND", "0"});

std::string ToUpper(const std::string_view text) {
    std::string upper(text);
    std::ranges::transform(upper, upper.begin(),
                           [](const unsigned char character) { return static_cast<char>(std::toupper(character)); });
    return upper;
}

/**
 * @brief   The edge a port name suggests; most libraries number their ports, which suggests nothing and stays on the
 *          left.
 */
Core::PinSide SuggestSide(const std::string_view port) {
    const std::string upper = ToUpper(port);
    if (std::ranges::contains(TOP_PORTS, upper)) {
        return Core::PinSide::TOP;
    }
    if (std::ranges::contains(BOTTOM_PORTS, upper)) {
        return Core::PinSide::BOTTOM;
    }
    if (upper.contains("OUT")) {
        return Core::PinSide::RIGHT;
    }
    return Core::PinSide::LEFT;
}

} // namespace

/**
 * @brief   Turns a SPICE name into one the schematic accepts for blocks and pins.
 * @param[in] text      Name as the library writes it, such as "OPA-2134", "IN+" or "3".
 * @param[in] fallback  Prefix for a name that starts with a digit, such as "P" for ports or "X" for blocks.
 * @return  The name with every character other than a letter, digit or "_" turned into "_", and the fallback before
 *          a leading digit: "OPA_2134", "IN_", "P3"; just the fallback for an empty name.
 */
std::string MakeIdentifier(const std::string_view text, const std::string_view fallback) {
    std::string name;
    for (const char character : text) {
        name += std::isalnum(static_cast<unsigned char>(character)) != 0 ? character : '_';
    }
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front())) != 0) {
        name = std::string(fallback) + name;
    }
    return name;
}

/**
 * @brief   Suggests a pin for each port of a library subcircuit.
 * @param[in] subcircuit  Subcircuit read from a library.
 * @return  One pin per port, in port order: named after the port, made a valid and unique name, and on the edge its
 *          name suggests, such as the top for "VCC" and the right for "OUT"; numbered ports go on the left.
 */
std::vector<SavedSubcircuitPin> SuggestSpicePins(const Core::LibrarySubcircuit &subcircuit) {
    std::vector<SavedSubcircuitPin> pins;
    std::set<std::string> names;
    for (const std::string &port : subcircuit.Ports) {
        const std::string base = MakeIdentifier(port, "P");
        std::string name = base;
        for (int suffix = 2; !names.insert(name).second; ++suffix) {
            name = std::format("{}_{}", base, suffix);
        }
        pins.push_back({.Name = std::move(name), .Point = {}, .Side = SuggestSide(port)});
    }
    return pins;
}

/**
 * @brief   Checks the names the user gave the pins of a SPICE block.
 * @param[in] pins  Pins as the import or settings dialog holds them.
 * @return  Why they cannot be kept, in words for the user, or no value when every name is a unique identifier.
 */
std::optional<std::string> CheckSpicePins(const std::vector<SavedSubcircuitPin> &pins) {
    std::set<std::string> names;
    for (const SavedSubcircuitPin &pin : pins) {
        if (!Core::SubcircuitInstance::IsValidIdentifier(pin.Name)) {
            return std::format("\"{}\" is not a pin name: use a letter or _, then letters, digits or _", pin.Name);
        }
        if (!names.insert(pin.Name).second) {
            return std::format("Two pins are named {}", pin.Name);
        }
    }
    return std::nullopt;
}

/**
 * @brief   Makes a block of the schematic from a library subcircuit and the pins chosen for it.
 * @param[in] subcircuit  Subcircuit read from a library.
 * @param[in] pins        One pin per port, in port order, as SuggestSpicePins() gives them or the user changed them.
 * @return  A block kept as SPICE text, named after the subcircuit, with its parameters and its text as they came.
 */
SavedSubcircuit MakeSpiceBlock(const Core::LibrarySubcircuit &subcircuit, std::vector<SavedSubcircuitPin> pins) {
    SavedSubcircuit block;
    block.Name = MakeIdentifier(subcircuit.Name, "X");
    block.Pins = std::move(pins);
    block.Parameters = subcircuit.Parameters;
    block.Spice = Core::SubcircuitText{.Ports = subcircuit.Ports, .Text = subcircuit.Text};
    return block;
}

} // namespace GUI
