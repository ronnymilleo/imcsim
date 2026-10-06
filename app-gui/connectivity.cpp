/**
 * @file    connectivity.cpp
 * @brief   Finds which terminals and wires of the schematic are electrically connected.
 */

#include "connectivity.h"

#include "components/component.h"
#include <numeric>
#include <utility>

namespace GUI {

namespace {

// A dot is drawn where three or more connections meet, like in most schematic editors
constexpr int MinJunctionConnections = 3;

/**
 * @class   DisjointSet
 * @brief   Union-find over indices 0..size-1, used to merge connection points into nodes.
 */
class DisjointSet {
public:
    explicit DisjointSet(const std::size_t size) : m_Parents(size) {
        std::iota(m_Parents.begin(), m_Parents.end(), std::size_t{0});
    }

    std::size_t Find(std::size_t index) {
        while (m_Parents[index] != index) {
            // Path halving keeps the trees shallow
            m_Parents[index] = m_Parents[m_Parents[index]];
            index = m_Parents[index];
        }
        return index;
    }

    void Unite(const std::size_t first, const std::size_t second) { m_Parents[Find(first)] = Find(second); }

private:
    std::vector<std::size_t> m_Parents;
};

} // namespace

/**
 * @brief   Computes the nodes and junctions of a schematic.
 * @param[in] elements  Components placed on the grid.
 * @param[in] wires     Wire segments.
 * @note    Cost grows with points times wires, which is fine for hand-drawn schematics.
 */
Connectivity::Connectivity(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires) {
    // Count how many connections reach each point; a wire passing through a point counts twice
    std::map<GridPoint, int> connection_count;
    std::vector<GridPoint> ground_points;
    for (const auto &element : elements) {
        const bool is_ground = element->GetComponent().GetType() == Core::ComponentType::Ground;
        for (const GridPoint terminal : element->GetTerminals()) {
            ++connection_count[terminal];
            if (is_ground) {
                ground_points.push_back(terminal);
            }
        }
    }
    for (const UIWire &wire : wires) {
        ++connection_count[wire.GetStart()];
        ++connection_count[wire.GetEnd()];
    }

    std::map<GridPoint, std::size_t> index_of_point;
    for (const auto &[point, count] : connection_count) {
        index_of_point.emplace(point, index_of_point.size());
    }

    DisjointSet nodes(index_of_point.size());
    for (const UIWire &wire : wires) {
        nodes.Unite(index_of_point.at(wire.GetStart()), index_of_point.at(wire.GetEnd()));
    }
    for (auto &[point, count] : connection_count) {
        for (const UIWire &wire : wires) {
            if (wire.PassesThrough(point)) {
                nodes.Unite(index_of_point.at(point), index_of_point.at(wire.GetStart()));
                count += 2;
            }
        }
        if (count >= MinJunctionConnections) {
            m_Junctions.push_back(point);
        }
    }

    // Number the groups: ground first as node 0, the rest in grid order from 1
    std::map<std::size_t, int> node_of_root;
    for (const GridPoint point : ground_points) {
        node_of_root.emplace(nodes.Find(index_of_point.at(point)), 0);
    }
    int next_node = 1;
    for (const auto &[point, index] : index_of_point) {
        const auto [entry, inserted] = node_of_root.emplace(nodes.Find(index), next_node);
        if (inserted) {
            ++next_node;
        }
        m_NodeOfPoint.emplace(point, entry->second);
    }
}

/**
 * @brief   Returns the node a connection point belongs to.
 * @param[in] point  Terminal or wire end on the grid.
 * @return  The node number, or no value when nothing connects at that point.
 */
std::optional<int> Connectivity::GetNode(const GridPoint point) const {
    const auto entry = m_NodeOfPoint.find(point);
    if (entry == m_NodeOfPoint.end()) {
        return std::nullopt;
    }
    return entry->second;
}

/**
 * @brief   Returns the points where three or more connections meet.
 * @return  Grid points that get a junction dot.
 */
const std::vector<GridPoint> &Connectivity::GetJunctions() const {
    return m_Junctions;
}

/**
 * @brief   Builds the circuit topology the simulator works on.
 * @param[in] elements      Components placed on the grid; they must outlive the returned circuit.
 * @param[in] connectivity  Nodes computed from the same elements and the wires.
 * @return  Each component with the node of every terminal.
 */
Core::Circuit BuildCircuit(const std::vector<std::unique_ptr<UIElement>> &elements, const Connectivity &connectivity) {
    Core::Circuit circuit;
    for (const auto &element : elements) {
        std::vector<int> nodes;
        for (const GridPoint terminal : element->GetTerminals()) {
            // Terminals are always connection points, so they always have a node
            nodes.push_back(connectivity.GetNode(terminal).value_or(0));
        }
        circuit.Add(element->GetComponent(), std::move(nodes));
    }
    return circuit;
}

} // namespace GUI
