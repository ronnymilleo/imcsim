/**
 * @file    node_index.cpp
 * @brief   Checks that a node number has a trace in a result, before the trace is read.
 */

#include "results/node_index.h"

namespace GUI {

/**
 * @brief   Turns the node of a point or a net into the index of its trace, when the result has one.
 * @param[in] node        Node number, or no value for a point that is not on a node.
 * @param[in] node_count  How many nodes the result has traces for, ground included.
 * @return  The node as an index, or no value when there is no node or it is negative or beyond the result.
 */
std::optional<std::size_t> FindNodeIndex(const std::optional<int> node, const std::size_t node_count) {
    if (!node || *node < 0 || static_cast<std::size_t>(*node) >= node_count) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(*node);
}

} // namespace GUI
