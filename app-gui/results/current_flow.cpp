/**
 * @file    current_flow.cpp
 * @brief   Works out the current along every piece of wire from the currents of the parts at its ends.
 */

#include "results/current_flow.h"

#include "circuit/circuit_currents.h"
#include "circuit/components/logic_part.h"
#include "results/logarithmic_level.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <utility>

namespace GUI {

namespace {

std::optional<double> FindCurrent(const std::vector<Core::ComponentCurrent> &currents, const std::string &name) {
    const auto current = std::ranges::find(currents, name, &Core::ComponentCurrent::Name);
    return current != currents.end() ? std::optional(current->Current) : std::nullopt;
}

/**
 * @brief   Splits every wire at the connection points on it, so each piece joins two points with nothing in between.
 */
std::vector<WireCurrent> SplitWires(const std::vector<UIWire> &wires, const std::vector<GridPoint> &points) {
    std::vector<WireCurrent> pieces;
    for (const UIWire &wire : wires) {
        std::vector<GridPoint> stops = {wire.GetStart(), wire.GetEnd()};
        for (const GridPoint point : points) {
            if (wire.PassesThrough(point)) {
                stops.push_back(point);
            }
        }
        // Wires are horizontal or vertical, so the sum of the offsets orders the stops along the wire
        const auto distance = [start = wire.GetStart()](const GridPoint point) {
            return std::abs(point.X - start.X) + std::abs(point.Y - start.Y);
        };
        std::ranges::sort(stops, {}, distance);
        const auto last = std::ranges::unique(stops).begin();
        for (auto stop = stops.begin(); stop + 1 < last; ++stop) {
            pieces.push_back({*stop, *(stop + 1), 0.0});
        }
    }
    return pieces;
}

/**
 * @brief   A part with several currents, each at the terminal it was probed on; the other end of a winding or of a
 *          contact takes what the probed end gives out.
 */
std::optional<double> GetProbedTerminalCurrent(const Core::Component &component,
                                               const std::vector<std::string> &names,
                                               const std::size_t terminal,
                                               const std::vector<Core::ComponentCurrent> &currents) {
    const std::vector<std::size_t> probed = Core::GetCurrentTerminals(component);
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (probed[index] == terminal) {
            return FindCurrent(currents, names[index]);
        }
    }
    const Core::ComponentType type = component.GetType();
    if ((type != Core::ComponentType::RELAY && type != Core::ComponentType::TRANSFORMER
         && type != Core::ComponentType::TRANSMISSION_LINE)
        || terminal % 2 == 0) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (probed[index] + 1 == terminal) {
            const std::optional<double> current = FindCurrent(currents, names[index]);
            return current ? std::optional(-*current) : std::nullopt;
        }
    }
    return std::nullopt;
}

/**
 * @brief   The output current of an op-amp or a gate returns through ground or the supply, and their other pins carry
 *          none.
 * @details Any other single current enters terminal 1 and leaves terminal 2, and the control pins of a controlled
 *          source carry none.
 */
double GetSingleCurrentAt(const Core::Component &component, const double current, const std::size_t terminal) {
    if (component.GetType() == Core::ComponentType::OP_AMP || Core::IsLogicGate(component.GetType())) {
        return terminal == Core::GetCurrentTerminals(component).front() ? current : 0.0;
    }
    if (terminal == 0) {
        return current;
    }
    return terminal == 1 ? -current : 0.0;
}

/**
 * @struct  TerminalLoads
 * @brief   What the terminals of the parts ask of the wires.
 */
struct TerminalLoads {
    std::map<GridPoint, double> DrawnAt; ///< Current each point hands to the parts there.
    std::set<GridPoint> Grounded;        ///< The points ground holds.
    std::vector<GridPoint> Points;       ///< Every terminal, where the wires are split.
};

/**
 * @struct  WireGraph
 * @brief   The pieces of wire, and the pieces that meet at each point.
 */
struct WireGraph {
    std::vector<WireCurrent> Pieces;
    std::map<GridPoint, std::vector<std::size_t>> PiecesAt;

    /**
     * @brief   Gives the end of a piece that is not the given point.
     * @param[in] piece_index  Index into Pieces.
     * @param[in] point        One end of the piece.
     * @return  The other end.
     */
    GridPoint GetOtherEnd(const std::size_t piece_index, const GridPoint point) const {
        const WireCurrent &piece = Pieces[piece_index];
        return piece.Start == point ? piece.End : piece.Start;
    }
};

/**
 * @struct  SpanningTree
 * @brief   The points of a group of joined wires, each reached through one piece from the root.
 */
struct SpanningTree {
    std::vector<GridPoint> Order;                 ///< Points by distance from the root, which comes first.
    std::map<GridPoint, std::size_t> ParentPiece; ///< For every point but the root, the piece that reached it.
};

/**
 * @brief   Collects the current each terminal draws and the terminals that are ground.
 */
TerminalLoads CollectTerminalLoads(const std::vector<std::unique_ptr<UIElement>> &elements,
                                   const std::vector<Core::ComponentCurrent> &currents) {
    TerminalLoads loads;
    for (const auto &element : elements) {
        const std::vector<GridPoint> terminals = element->GetTerminals();
        for (std::size_t index = 0; index < terminals.size(); ++index) {
            loads.Points.push_back(terminals[index]);
            if (const std::optional<double> current = GetTerminalCurrent(element->GetComponent(), index, currents)) {
                loads.DrawnAt[terminals[index]] += *current;
            } else if (element->GetComponent().GetType() == Core::ComponentType::GROUND) {
                loads.Grounded.insert(terminals[index]);
            }
        }
    }
    return loads;
}

/**
 * @brief   Splits the wires at the terminals and at each other's ends, and indexes the pieces by their points.
 */
WireGraph BuildWireGraph(const std::vector<UIWire> &wires, std::vector<GridPoint> points) {
    for (const UIWire &wire : wires) {
        points.push_back(wire.GetStart());
        points.push_back(wire.GetEnd());
    }
    WireGraph graph{.Pieces = SplitWires(wires, points), .PiecesAt = {}};
    for (std::size_t index = 0; index < graph.Pieces.size(); ++index) {
        graph.PiecesAt[graph.Pieces[index].Start].push_back(index);
        graph.PiecesAt[graph.Pieces[index].End].push_back(index);
    }
    return graph;
}

/**
 * @brief   Visits the points joined to a root by wire, nearest first, each once.
 * @param[in]     graph     The wires.
 * @param[in]     root      Where the walk starts.
 * @param[in,out] reached   Points already visited, which the walk skips and adds to; the root is added too.
 * @param[in]     on_reach  Called with each new point and the piece that reached it.
 * @return  The points reached, the root first.
 */
template <typename OnReach>
std::vector<GridPoint>
WalkBreadthFirst(const WireGraph &graph, const GridPoint root, std::set<GridPoint> &reached, const OnReach &on_reach) {
    std::vector<GridPoint> order = {root};
    reached.insert(root);
    for (std::size_t next = 0; next < order.size(); ++next) {
        const GridPoint point = order[next];
        for (const std::size_t index : graph.PiecesAt.at(point)) {
            const GridPoint other = graph.GetOtherEnd(index, point);
            if (reached.insert(other).second) {
                on_reach(other, index);
                order.push_back(other);
            }
        }
    }
    return order;
}

/**
 * @brief   Gathers the points joined to a seed, marking them visited so the caller does not start from them again.
 */
std::vector<GridPoint> FindJoinedPoints(const WireGraph &graph, const GridPoint seed, std::set<GridPoint> &visited) {
    return WalkBreadthFirst(graph, seed, visited, [](const GridPoint, const std::size_t) {});
}

/**
 * @brief   Roots a group at a ground terminal when it has one, since ground takes what the rest leaves over.
 */
GridPoint ChooseRoot(const std::vector<GridPoint> &group, const std::set<GridPoint> &grounded) {
    const auto ground =
        std::ranges::find_if(group, [&grounded](const GridPoint point) { return grounded.contains(point); });
    return ground != group.end() ? *ground : group.front();
}

SpanningTree BuildSpanningTree(const WireGraph &graph, const GridPoint root) {
    SpanningTree tree;
    std::set<GridPoint> reached;
    tree.Order = WalkBreadthFirst(graph, root, reached, [&tree](const GridPoint point, const std::size_t index) {
        tree.ParentPiece[point] = index;
    });
    return tree;
}

/**
 * @brief   Sets the current of the pieces of a tree, from the leaves up: the piece into each point carries what that
 *          point and the points beyond it draw.
 */
void AccumulateTreeCurrents(WireGraph &graph, const SpanningTree &tree, const TerminalLoads &loads) {
    const GridPoint root = tree.Order.front();
    std::map<GridPoint, double> drawn_beyond;
    for (const GridPoint point : std::views::reverse(tree.Order)) {
        const auto drawn = loads.DrawnAt.find(point);
        drawn_beyond[point] += drawn != loads.DrawnAt.end() ? drawn->second : 0.0;
        if (point == root) {
            continue;
        }
        const std::size_t piece_index = tree.ParentPiece.at(point);
        WireCurrent &piece = graph.Pieces[piece_index];
        piece.Current = piece.End == point ? drawn_beyond[point] : -drawn_beyond[point];
        drawn_beyond[graph.GetOtherEnd(piece_index, point)] += drawn_beyond[point];
    }
}

} // namespace

/**
 * @brief   Returns the current flowing into one terminal of a part, from the results of a simulation.
 * @param[in] component  The part.
 * @param[in] terminal   Terminal index, in the order of UIElement::GetTerminals().
 * @param[in] currents   Currents by name, as an operating point reports them.
 * @return  The current into the terminal: the reported current for a probed terminal, and for a relay or a
 *          transformer the opposite of it at the other end of its contact or winding; for a two-terminal part
 *          or the output of a controlled source, its current into the first terminal and the opposite into the
 *          second; for a supply, an op-amp output or a gate output, its current; zero for control pins and the other
 *          op-amp and gate pins.
 *          No value for ground, whose current is whatever its node leaves over, or for a current missing from the
 *          results.
 */
std::optional<double> GetTerminalCurrent(const Core::Component &component,
                                         const std::size_t terminal,
                                         const std::vector<Core::ComponentCurrent> &currents) {
    const std::vector<std::string> names = Core::GetCurrentNames(component);
    if (names.empty()) {
        return std::nullopt;
    }
    if (names.size() > 1) {
        return GetProbedTerminalCurrent(component, names, terminal, currents);
    }
    const std::optional<double> current = FindCurrent(currents, names.front());
    if (!current) {
        return std::nullopt;
    }
    return GetSingleCurrentAt(component, *current, terminal);
}

/**
 * @brief   Returns the largest current in a circuit, in absolute value, to scale heat colors against.
 * @param[in] operating_point  Currents of the circuit.
 * @return  The largest of the reported currents, or zero when there are none.
 */
double LargestCurrent(const Core::OperatingPoint &operating_point) {
    double largest = 0.0;
    for (const Core::ComponentCurrent &current : operating_point.Currents) {
        largest = std::max(largest, std::abs(current.Current));
    }
    return largest;
}

/**
 * @brief   Returns where a current falls on the heat scale the editor colors wires and parts with.
 * @param[in] current          Current to place.
 * @param[in] largest_current  Largest current of the circuit, as LargestCurrent() returns it.
 * @return  The LogarithmicLevel() of the current over CURRENT_DECADES.
 */
float CurrentLevel(const double current, const double largest_current) {
    return LogarithmicLevel(current, largest_current, CURRENT_DECADES);
}

/**
 * @brief   Works out the current along every piece of wire, as the plain wires of a schematic carry it.
 * @param[in] elements  Parts of the schematic.
 * @param[in] wires     Wires of the schematic.
 * @param[in] currents  Currents by name, as an operating point reports them.
 * @return  Every wire split at the connection points on it, with its current.
 * @note    Each group of joined wires is walked as a tree from a ground terminal when it has one: the current of
 *          a piece is what the terminals beyond it draw, and the ground at the root takes the rest, since its own
 *          current is not reported. Pieces that close a loop of wire inside one node carry no current here,
 *          because ideal wires do not tell how a current divides between them.
 */
std::vector<WireCurrent> ComputeWireCurrents(const std::vector<std::unique_ptr<UIElement>> &elements,
                                             const std::vector<UIWire> &wires,
                                             const std::vector<Core::ComponentCurrent> &currents) {
    const TerminalLoads loads = CollectTerminalLoads(elements, currents);
    WireGraph graph = BuildWireGraph(wires, loads.Points);
    std::set<GridPoint> visited;
    for (const GridPoint seed : std::views::keys(graph.PiecesAt)) {
        if (visited.contains(seed)) {
            continue;
        }
        const std::vector<GridPoint> group = FindJoinedPoints(graph, seed, visited);
        const GridPoint root = ChooseRoot(group, loads.Grounded);
        AccumulateTreeCurrents(graph, BuildSpanningTree(graph, root), loads);
    }
    return graph.Pieces;
}

} // namespace GUI
