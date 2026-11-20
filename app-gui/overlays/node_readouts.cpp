/**
 * @file    node_readouts.cpp
 * @brief   Which nodes get a voltage readout, and the wire or terminal each one sits beside.
 */

#include "overlays/node_readouts.h"

#include <optional>

namespace GUI {

/**
 * @brief   Every place a readout can sit beside, in the order they are tried: the wires first, then the terminals,
 *          for nodes whose terminals are joined without wires.
 * @param[in] wires     The wires of the schematic.
 * @param[in] elements  The parts of the schematic.
 * @return  One anchor per wire, from its start, then one per terminal of each part.
 */
std::vector<ReadoutAnchor> ListReadoutAnchors(const std::vector<UIWire> &wires,
                                              const std::vector<std::unique_ptr<UIElement>> &elements) {
    std::vector<ReadoutAnchor> anchors;
    for (const UIWire &wire : wires) {
        anchors.push_back({.Point = wire.GetStart(), .Start = ToVec2(wire.GetStart()), .End = ToVec2(wire.GetEnd())});
    }
    for (const auto &element : elements) {
        for (const GridPoint terminal : element->GetTerminals()) {
            anchors.push_back({.Point = terminal, .Start = ToVec2(terminal), .End = ToVec2(terminal)});
        }
    }
    return anchors;
}

/**
 * @brief   One readout per node, at the first anchor of the node.
 * @param[in] anchors       Where readouts can sit, in the order they are tried.
 * @param[in] connectivity  The nodes of the schematic.
 * @param[in] node_count    How many nodes the result has a voltage for, ground included.
 * @return  The readouts, in the order of their anchors. Ground is always 0 V, so it is left out, and so are the points
 *          on no node or on a node the result has no voltage for.
 */
std::vector<NodeReadout> ChooseNodeReadouts(const std::vector<ReadoutAnchor> &anchors,
                                            const Connectivity &connectivity,
                                            const std::size_t node_count) {
    std::vector<bool> is_labeled(node_count, false);
    std::vector<NodeReadout> readouts;
    for (const ReadoutAnchor &anchor : anchors) {
        const std::optional<int> node = connectivity.GetNode(anchor.Point);
        if (!node || *node <= 0 || static_cast<std::size_t>(*node) >= node_count) {
            continue;
        }
        const auto index = static_cast<std::size_t>(*node);
        if (is_labeled[index]) {
            continue;
        }
        is_labeled[index] = true;
        readouts.push_back({.Node = index, .Anchor = anchor});
    }
    return readouts;
}

} // namespace GUI
