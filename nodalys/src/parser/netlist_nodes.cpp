/**
 * @file    netlist_nodes.cpp
 * @brief   The node names of a netlist and the numbers the engine gives them.
 */

#include "parser/netlist_nodes.h"

#include "parser/spice_text.h"
#include "text/text_case.h"

#include <algorithm>
#include <charconv>
#include <optional>

namespace Nodalys {

namespace {

/// The highest node number a netlist may use as a name; a larger one is read as a name, not a number.
constexpr int MAX_NUMERIC_NODE = 10'000;

/**
 * @brief   The number a node name is, such as 12 for "12".
 * @return  The number, or none for a name that is not a whole number above 0.
 */
std::optional<int> ParseNodeNumber(const std::string &upper_name) {
    int number = 0;
    const char *end = upper_name.data() + upper_name.size();
    const auto [stop, error] = std::from_chars(upper_name.data(), end, number);
    if (error != std::errc() || stop != end || number <= 0) {
        return std::nullopt;
    }
    return number;
}

} // namespace

/**
 * @brief   Notes a node that a line of the netlist names, before the numbers are given.
 * @param[in] name  Node name as written; ground and names already seen are skipped.
 */
void NetlistNodes::Register(const std::string_view name) {
    const std::string upper = ToUpper(TrimSpaces(name));
    if (IsGroundName(upper) || m_Registered.contains(upper)) {
        return;
    }
    m_Registered.insert(upper);
    if (const std::optional<int> number = ParseNodeNumber(upper); number && *number <= MAX_NUMERIC_NODE) {
        m_NumericNodes.push_back({upper, *number});
        m_MaxNumeric = std::max(m_MaxNumeric, *number);
    } else {
        m_SymbolicNodes.push_back(upper);
    }
}

/**
 * @brief   Gives every registered node its number: its own for a numbered one, the next free above them for a name.
 * @return  The number of nodes of the circuit, ground included, for the builder.
 */
int NetlistNodes::AssignIds() {
    for (const auto &[name, number] : m_NumericNodes) {
        m_NodeMap[name] = number;
    }
    int next_id = m_MaxNumeric + 1;
    for (const std::string &name : m_SymbolicNodes) {
        if (!m_NodeMap.contains(name)) {
            m_NodeMap[name] = next_id++;
        }
    }
    return std::max(next_id, 1);
}

/**
 * @brief   Gives a name to a node that something else made, such as an internal node of a subcircuit instance.
 * @param[in] upper_name  Name in upper case, such as "X1.MID".
 * @param[in] node        The node it names.
 */
void NetlistNodes::Bind(const std::string &upper_name, const NodeId node) {
    m_NodeMap[upper_name] = node;
}

/**
 * @brief   The number of a node, after AssignIds().
 * @param[in] name  Node name as written.
 * @return  Its number; a number that was never registered is taken as it is, and an unknown name is ground.
 */
NodeId NetlistNodes::Resolve(const std::string_view name) const {
    const std::string upper = ToUpper(TrimSpaces(name));
    if (IsGroundName(upper)) {
        return GROUND_NODE;
    }
    if (const auto found = m_NodeMap.find(upper); found != m_NodeMap.end()) {
        return found->second;
    }
    return ParseNodeNumber(upper).value_or(GROUND_NODE);
}

} // namespace Nodalys
