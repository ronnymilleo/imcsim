/**
 * @file    node_index.h
 * @brief   Checks that a node number has a trace in a result, before the trace is read.
 */

#ifndef IMCSIM_NODE_INDEX_H
#define IMCSIM_NODE_INDEX_H

#include <cstddef>
#include <optional>

namespace GUI {

// Range
[[nodiscard]] std::optional<std::size_t> FindNodeIndex(std::optional<int> node, std::size_t node_count);

} // namespace GUI

#endif // IMCSIM_NODE_INDEX_H
