/**
 * @file    connectivity.cpp
 * @brief   Finds which terminals and wires of the schematic are electrically connected.
 */

#include "connectivity.h"

#include "components/component.h"
#include "components/net_label.h"
#include <algorithm>
#include <array>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>

namespace GUI {

namespace {

// A dot is drawn where three or more connections meet, like in most schematic editors
constexpr int MinJunctionConnections = 3;
// The nets a label can name to mean ground, in the form nets are compared in
constexpr std::array<std::string_view, 2> GroundNets = {"0", "gnd"};
// The net that joins every VCC supply, in the form nets are compared in
constexpr std::string_view SupplyNet = "vcc";

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
    // Named labels by the name they are compared with, and as typed by grid point, for the name of the node
    std::map<std::string, std::vector<GridPoint>> labeled_points;
    std::map<GridPoint, std::string> label_text;
    std::vector<GridPoint> supply_points;
    for (const auto &element : elements) {
        const Core::Component &component = element->GetComponent();
        bool is_ground = component.GetType() == Core::ComponentType::Ground;
        const bool is_label = component.GetType() == Core::ComponentType::NetLabel;
        const std::string net = is_label ? static_cast<const Core::NetLabel &>(component).GetNet() : std::string();
        const std::string compared = Core::NetLabel::NormalizeNet(net);
        // A label named 0, as in SPICE, or GND is ground
        is_ground = is_ground || (is_label && std::ranges::contains(GroundNets, compared));
        const bool is_supply = component.GetType() == Core::ComponentType::VCC;
        for (const GridPoint terminal : element->GetTerminals()) {
            ++connection_count[terminal];
            if (is_supply) {
                supply_points.push_back(terminal);
            }
            if (is_ground) {
                ground_points.push_back(terminal);
            } else if (is_label && Core::NetLabel::IsValidNet(net)) {
                labeled_points[compared].push_back(terminal);
                label_text.emplace(terminal, net);
            }
        }
    }
    // A label named VCC joins every VCC supply, as one named GND joins every ground
    if (const auto supply = labeled_points.find(std::string(SupplyNet)); supply != labeled_points.end()) {
        supply->second.insert(supply->second.end(), supply_points.begin(), supply_points.end());
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

    // Labels with one name are one node, with or without a wire between them
    for (const auto &[name, points] : labeled_points) {
        for (const GridPoint point : points) {
            nodes.Unite(index_of_point.at(point), index_of_point.at(points.front()));
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
    for (const auto &[point, text] : label_text) {
        m_NetOfNode.emplace(m_NodeOfPoint.at(point), text);
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
 * @brief   Lists every connection point of a node, such as to find the node again after the numbering changes.
 * @param[in] node  Node number.
 * @return  The points in order, or an empty list when no point has that number.
 */
std::vector<GridPoint> Connectivity::GetPointsOfNode(const int node) const {
    std::vector<GridPoint> points;
    for (const auto &[point, point_node] : m_NodeOfPoint) {
        if (point_node == node) {
            points.push_back(point);
        }
    }
    return points;
}

/**
 * @brief   Finds a connection point of a node, such as to save which node is measured independently of numbering.
 * @param[in] node  Node number.
 * @return  The top-left point of the node, so the same schematic always gives the same point, or no value when
 *          no point has that number.
 */
std::optional<GridPoint> Connectivity::FindPointOfNode(const int node) const {
    const auto entry = std::ranges::find(m_NodeOfPoint, node, &std::pair<const GridPoint, int>::second);
    if (entry == m_NodeOfPoint.end()) {
        return std::nullopt;
    }
    return entry->first;
}

/**
 * @brief   Returns the name a node was given by a net label.
 * @param[in] node  Node number.
 * @return  The name as typed on the first label of the node in grid order, or no value for ground, for a node
 *          without a label and for a number that is not a node.
 */
std::optional<std::string> Connectivity::GetNetName(const int node) const {
    const auto entry = m_NetOfNode.find(node);
    if (node == 0 || entry == m_NetOfNode.end()) {
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
        // A label only joins points; the simulator never sees it
        if (element->GetComponent().GetType() == Core::ComponentType::NetLabel) {
            continue;
        }
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
