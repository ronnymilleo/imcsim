/**
 * @file    node_readouts.h
 * @brief   Which nodes get a voltage readout, and the wire or terminal each one sits beside.
 */

#ifndef IMCSIM_NODE_READOUTS_H
#define IMCSIM_NODE_READOUTS_H

#include "base/helpers.h"
#include "imgui.h"
#include "schematic/connectivity.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <cstddef>
#include <memory>
#include <vector>

namespace GUI {

/**
 * @struct  ReadoutAnchor
 * @brief   A place a readout can sit beside: a wire, from one end to the other, or a terminal, whose two ends are the
 *          same point.
 */
struct ReadoutAnchor {
    GridPoint Point; ///< The point whose node the readout shows.
    ImVec2 Start;    ///< One end of what the readout sits beside, in world units.
    ImVec2 End;      ///< The other end, in world units.
};

/**
 * @struct  NodeReadout
 * @brief   The readout of one node and where it sits.
 */
struct NodeReadout {
    std::size_t Node = 0; ///< The node, above ground and with a voltage in the result.
    ReadoutAnchor Anchor;
};

// Choosing the readouts
std::vector<ReadoutAnchor> ListReadoutAnchors(const std::vector<UIWire> &wires,
                                              const std::vector<std::unique_ptr<UIElement>> &elements);
std::vector<NodeReadout>
ChooseNodeReadouts(const std::vector<ReadoutAnchor> &anchors, const Connectivity &connectivity, std::size_t node_count);

} // namespace GUI

#endif // IMCSIM_NODE_READOUTS_H
