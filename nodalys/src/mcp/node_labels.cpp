/**
 * @file    node_labels.cpp
 * @brief   Names the nodes of a parsed netlist for the AI, and finds a node from the name it wrote.
 */

#include "mcp/node_labels.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace Nodalys {

namespace {

/// Name of the ground node in the answers.
constexpr const char *GROUND_NAME = "0";

std::string ToLowerCase(const std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(),
                           [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return lower;
}

/// Whether a name belongs to a node inside a subcircuit instance, such as "x1.mid".
bool IsInternalName(const std::string_view name) {
    return name.find('.') != std::string_view::npos;
}

/// Reads "v(out)" as "out", since an AI often writes a node as SPICE prints it.
std::string StripVoltageWrapper(const std::string_view name) {
    constexpr std::string_view PREFIX = "v(";
    if (name.size() > PREFIX.size() && name.starts_with(PREFIX) && name.ends_with(')')) {
        return std::string(name.substr(PREFIX.size(), name.size() - PREFIX.size() - 1));
    }
    return std::string(name);
}

} // namespace

/**
 * @param[in] parsed  Netlist whose node names are read.
 */
NodeLabels::NodeLabels(const ParsedNetlist &parsed) {
    for (const auto &[upper_name, node] : parsed.NodeNames) {
        const std::string name = ToLowerCase(upper_name);
        m_NodesByName[name] = node;
        // Several names can stand for one node (a port of an instance); the shortest top-level one is the best label
        const auto existing = m_NamesByNode.find(node);
        const bool is_better =
            existing == m_NamesByNode.end() || (IsInternalName(existing->second) && !IsInternalName(name)) ||
            (IsInternalName(existing->second) == IsInternalName(name) && name.size() < existing->second.size());
        if (is_better) {
            m_NamesByNode[node] = name;
        }
    }
}

/**
 * @param[in] node  Node number of a result.
 * @return  The name of the node, "0" for ground, or "n<number>" for a node the netlist does not name, such as the
 *          probes of a device.
 */
std::string NodeLabels::GetName(const NodeId node) const {
    if (node == GROUND_NODE) {
        return GROUND_NAME;
    }
    const auto found = m_NamesByNode.find(node);
    return found != m_NamesByNode.end() ? found->second : std::format("n{}", node);
}

/**
 * @param[in] name  Node name as the AI wrote it: any case, ground as 0, gnd or ground, optionally as "v(name)".
 * @return  The node number, or none when the netlist has no such node.
 */
std::optional<NodeId> NodeLabels::FindNode(const std::string_view name) const {
    const std::string lower = StripVoltageWrapper(ToLowerCase(name));
    if (lower == GROUND_NAME || lower == "gnd" || lower == "ground") {
        return GROUND_NODE;
    }
    const auto found = m_NodesByName.find(lower);
    if (found == m_NodesByName.end()) {
        return std::nullopt;
    }
    return found->second;
}

/**
 * @return  The nodes of the circuit itself, not those inside subcircuit instances, ordered by number.
 */
std::vector<NodeId> NodeLabels::GetNamedNodes() const {
    std::vector<NodeId> nodes;
    for (const auto &[node, name] : m_NamesByNode) {
        if (!IsInternalName(name)) {
            nodes.push_back(node);
        }
    }
    return nodes;
}

} // namespace Nodalys
