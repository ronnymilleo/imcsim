/**
 * @file    subcircuit_pins.cpp
 * @brief   Checks the pins of a block definition, tells the edge each one goes on and hands them to its instances.
 */

#include "document/subcircuit_pins.h"

#include "circuit/components/subcircuit_pin.h"
#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace GUI {

namespace {

/// Ground is node 0 when the block has a ground, through a Ground part or a label named 0 or GND.
constexpr int GROUND_NODE = 0;

} // namespace

/**
 * @brief   Checks that every pin of a block reaches a node of its own that the outside can connect to.
 * @param[in] connectivity  Nodes of the block's contents.
 * @param[in] pins          Pins of the block, each at the point of its marker.
 * @return  Why the pins cannot be written, in words for the user: a pin on no node, a pin on ground, which ngspice
 *          keeps global so the pin would connect nothing, or two pins on one node, which would short the nets of every
 *          instance; no value when they are fine.
 */
std::optional<std::string> CheckPinNodes(const Connectivity &connectivity,
                                         const std::vector<SavedSubcircuitPin> &pins) {
    std::set<int> nodes;
    for (const SavedSubcircuitPin &pin : pins) {
        const std::optional<int> node = connectivity.GetNode(pin.Point);
        if (!node) {
            return "Pin " + pin.Name + " is on a point that connects to nothing in the block";
        }
        if (*node == GROUND_NODE) {
            return "Pin " + pin.Name +
                   " is on the ground of the block, which every part shares already; remove the pin or the ground";
        }
        if (!nodes.insert(*node).second) {
            return "Pin " + pin.Name + " shares a node with another pin; place only one pin marker on each node";
        }
    }
    return std::nullopt;
}

/**
 * @brief   Tells the edge of the block a pin marker puts its pin on.
 * @param[in] marker  A pin marker, as placed in the block.
 * @return  The edge its arrow comes from, since the arrow points from the block's edge into the circuit: the left for
 * an arrow pointing right, the top for one pointing down.
 */
Core::PinSide GetMarkerSide(const UIElement &marker) {
    // The terminal is at the tip of the arrow, so inward from it runs back toward the edge
    const GridPoint inward = marker.GetTerminalInward(0);
    if (inward.X < 0) {
        return Core::PinSide::LEFT;
    }
    if (inward.X > 0) {
        return Core::PinSide::RIGHT;
    }
    return inward.Y < 0 ? Core::PinSide::TOP : Core::PinSide::BOTTOM;
}

/**
 * @brief   Finds the edge the pin marker on a point puts its pin on.
 * @param[in] elements  Contents of a block.
 * @param[in] point     Point of a pin.
 * @return  The side of the marker whose terminal is on the point, or no value when none is.
 */
std::optional<Core::PinSide> FindMarkerSide(const std::vector<std::unique_ptr<UIElement>> &elements,
                                            const GridPoint point) {
    for (const auto &element : elements) {
        if (Core::IsSubcircuitPin(element->GetComponent().GetType()) && element->GetTerminals().front() == point) {
            return GetMarkerSide(*element);
        }
    }
    return std::nullopt;
}

/**
 * @brief   Returns the rotation that makes a pin marker put its pin on an edge, unmirrored.
 * @param[in] side  Edge of the block.
 * @return  The rotation whose arrow comes from that edge: none for the left, a half turn for the right, a quarter turn
 *          for the top and three for the bottom; GetMarkerSide() gives the side back.
 */
Rotation GetSideRotation(const Core::PinSide side) {
    switch (side) {
    case Core::PinSide::RIGHT:
        return Rotation::R180;
    case Core::PinSide::TOP:
        return Rotation::R90;
    case Core::PinSide::BOTTOM:
        return Rotation::R270;
    case Core::PinSide::LEFT:
    default:
        return Rotation::R0;
    }
}

/**
 * @brief   Returns a readable name for an edge of a block.
 * @param[in] side  Edge of the box.
 * @return  "left", "right", "top" or "bottom".
 */
const char *GetPinSideName(const Core::PinSide side) {
    switch (side) {
    case Core::PinSide::RIGHT:
        return "right";
    case Core::PinSide::TOP:
        return "top";
    case Core::PinSide::BOTTOM:
        return "bottom";
    case Core::PinSide::LEFT:
    default:
        return "left";
    }
}

/**
 * @brief   Reads the name of an edge of a block, as GetPinSideName() writes it.
 * @param[in] name  "left", "right", "top" or "bottom".
 * @return  The edge, or no value for any other text.
 */
std::optional<Core::PinSide> ParsePinSide(const std::string_view name) {
    for (const Core::PinSide side : PIN_SIDES) {
        if (name == GetPinSideName(side)) {
            return side;
        }
    }
    return std::nullopt;
}

/**
 * @brief   Tells whether another pin marker of the schematic is an S-parameter port with this number.
 * @param[in] elements  Elements of the schematic.
 * @param[in] number    Port number to look for.
 * @param[in] except    Marker to leave out, the one being edited, or null.
 * @return  True when a marker other than except is a port with that number.
 */
bool IsPortNumberTaken(const std::vector<std::unique_ptr<UIElement>> &elements, const int number,
                       const Core::Component *except) {
    return std::ranges::any_of(elements, [number, except](const std::unique_ptr<UIElement> &element) {
        const Core::Component &component = element->GetComponent();
        if (&component == except || !Core::IsSubcircuitPin(component.GetType())) {
            return false;
        }
        const Core::PortSettings &port = static_cast<const Core::SubcircuitPinMarker &>(component).GetPort();
        return port.IsPort && port.Number == number;
    });
}

/**
 * @brief   Finds the number a new port should take: the lowest one no other port has, so the numbers stay 1 to N.
 * @param[in] elements  Elements of the schematic.
 * @param[in] except    Marker to leave out, the one being edited, or null.
 * @return  The lowest number from 1 that no other marker uses.
 */
int FindFreePortNumber(const std::vector<std::unique_ptr<UIElement>> &elements, const Core::Component *except) {
    int number = 1;
    while (IsPortNumberTaken(elements, number, except)) {
        ++number;
    }
    return number;
}

/**
 * @brief   Gives an instance the pins of its definition: their names in order and the edge each one goes on.
 * @param[in,out] instance    Instance to set, which takes the name of the definition too.
 * @param[in]     definition  Definition it uses; each pin takes the side of the marker on its point, or the left, or
 *                            for a block kept as SPICE text the side kept with the pin.
 */
void ApplyDefinitionPins(Core::SubcircuitInstance &instance, const SavedSubcircuit &definition) {
    std::vector<std::string> names;
    std::map<std::string, Core::PinSide, std::less<>> sides;
    for (const SavedSubcircuitPin &pin : definition.Pins) {
        names.push_back(pin.Name);
        // A block kept as SPICE text has no markers, so it keeps the edge its pins were given
        sides[pin.Name] =
            definition.Spice ? pin.Side : FindMarkerSide(definition.Elements, pin.Point).value_or(Core::PinSide::LEFT);
    }
    instance.SetDefinitionName(definition.Name);
    instance.SetPins(std::move(names));
    instance.SetPinSides(std::move(sides));
}

} // namespace GUI
