/**
 * @file    subcircuit_pins.cpp
 * @brief   Checks the pins of a block definition and hands them to its instances.
 */

#include "subcircuit_pins.h"

#include <set>
#include <utility>

namespace GUI {

namespace {

// Ground is node 0 when the block has a ground, through a Ground part or a label named 0 or GND
constexpr int GROUND_NODE = 0;

bool IsOutputMarkerAt(const SavedSubcircuit &definition, const GridPoint point) {
    for (const auto &element : definition.Elements) {
        if (element->GetComponent().GetType() == Core::ComponentType::POUT &&
            element->GetTerminals().front() == point) {
            return true;
        }
    }
    return false;
}

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
            return "Pin " + pin.Name + " shares a node with another pin; place only one PIN or POUT on each node";
        }
    }
    return std::nullopt;
}

/**
 * @brief   Gives an instance the pins of its definition: their names in order and which of them are outputs.
 * @param[in,out] instance    Instance to set, which takes the name of the definition too.
 * @param[in]     definition  Definition it uses; a pin is an output when a POUT marker sits on its point.
 */
void ApplyDefinitionPins(Core::SubcircuitInstance &instance, const SavedSubcircuit &definition) {
    std::vector<std::string> names;
    std::set<std::string, std::less<>> outputs;
    for (const SavedSubcircuitPin &pin : definition.Pins) {
        names.push_back(pin.Name);
        if (IsOutputMarkerAt(definition, pin.Point)) {
            outputs.insert(pin.Name);
        }
    }
    instance.SetDefinitionName(definition.Name);
    instance.SetPins(std::move(names));
    instance.SetOutputPins(std::move(outputs));
}

} // namespace GUI
