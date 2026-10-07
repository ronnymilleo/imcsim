/**
 * @file    current_flow.cpp
 * @brief   Works out the current along every piece of wire from the currents of the parts at its ends.
 */

#include "current_flow.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>

namespace GUI {

namespace {

std::optional<double> FindCurrent(const std::vector<Core::ComponentCurrent> &currents, const std::string &name) {
    const auto current = std::ranges::find(currents, name, &Core::ComponentCurrent::Name);
    return current != currents.end() ? std::optional(current->Current) : std::nullopt;
}

// Splits every wire at the connection points on it, so each piece joins two points with nothing in between
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

} // namespace

/**
 * @brief   Returns the current flowing into one terminal of a part, from the results of a simulation.
 * @param[in] component  The part.
 * @param[in] terminal   Terminal index, in the order of UIElement::GetTerminals().
 * @param[in] currents   Currents by name, as an operating point reports them.
 * @return  The current into the terminal: the reported current for a transistor terminal; for a two-terminal part,
 *          its current into the first terminal and the opposite into the second; for a supply, its current. No
 *          value for ground, whose current is whatever its node leaves over, or for a current missing from the
 *          results.
 */
std::optional<double> GetTerminalCurrent(const Core::Component &component, const std::size_t terminal,
                                         const std::vector<Core::ComponentCurrent> &currents) {
    const std::vector<std::string> names = Core::GetCurrentNames(component);
    if (names.empty()) {
        return std::nullopt;
    }
    if (names.size() > 1) {
        return terminal < names.size() ? FindCurrent(currents, names[terminal]) : std::nullopt;
    }
    const std::optional<double> current = FindCurrent(currents, names.front());
    if (!current) {
        return std::nullopt;
    }
    return terminal == 0 ? *current : -*current;
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
    // Current each point hands to the parts there, and the points ground holds
    std::map<GridPoint, double> drawn_at;
    std::map<GridPoint, bool> grounded;
    std::vector<GridPoint> points;
    for (const auto &element : elements) {
        const std::vector<GridPoint> terminals = element->GetTerminals();
        for (std::size_t index = 0; index < terminals.size(); ++index) {
            points.push_back(terminals[index]);
            if (const std::optional<double> current = GetTerminalCurrent(element->GetComponent(), index, currents)) {
                drawn_at[terminals[index]] += *current;
            } else if (element->GetComponent().GetType() == Core::ComponentType::Ground) {
                grounded[terminals[index]] = true;
            }
        }
    }
    for (const UIWire &wire : wires) {
        points.push_back(wire.GetStart());
        points.push_back(wire.GetEnd());
    }
    std::vector<WireCurrent> pieces = SplitWires(wires, points);

    std::map<GridPoint, std::vector<std::size_t>> pieces_at;
    for (std::size_t index = 0; index < pieces.size(); ++index) {
        pieces_at[pieces[index].Start].push_back(index);
        pieces_at[pieces[index].End].push_back(index);
    }

    std::map<GridPoint, bool> visited;
    for (const auto &[seed, seed_pieces] : pieces_at) {
        if (visited[seed]) {
            continue;
        }
        // Gathers the group first, to root its tree at a ground terminal when there is one
        std::vector<GridPoint> group = {seed};
        visited[seed] = true;
        for (std::size_t next = 0; next < group.size(); ++next) {
            for (const std::size_t index : pieces_at[group[next]]) {
                const WireCurrent &piece = pieces[index];
                const GridPoint other = piece.Start == group[next] ? piece.End : piece.Start;
                if (!visited[other]) {
                    visited[other] = true;
                    group.push_back(other);
                }
            }
        }
        const auto ground =
            std::ranges::find_if(group, [&grounded](const GridPoint point) { return grounded.contains(point); });
        const GridPoint root = ground != group.end() ? *ground : group.front();

        // Breadth-first tree from the root; each point remembers the piece that reached it
        std::map<GridPoint, std::size_t> parent_piece;
        std::map<GridPoint, bool> reached = {{root, true}};
        std::vector<GridPoint> order = {root};
        for (std::size_t next = 0; next < order.size(); ++next) {
            for (const std::size_t index : pieces_at[order[next]]) {
                const WireCurrent &piece = pieces[index];
                const GridPoint other = piece.Start == order[next] ? piece.End : piece.Start;
                if (!reached[other]) {
                    reached[other] = true;
                    parent_piece[other] = index;
                    order.push_back(other);
                }
            }
        }

        // From the leaves up, the piece into each point carries what that point and the points beyond it draw
        std::map<GridPoint, double> drawn_beyond;
        for (auto point = order.rbegin(); point != order.rend(); ++point) {
            drawn_beyond[*point] += drawn_at[*point];
            if (*point == root) {
                continue;
            }
            WireCurrent &piece = pieces[parent_piece[*point]];
            piece.Current = piece.End == *point ? drawn_beyond[*point] : -drawn_beyond[*point];
            const GridPoint parent = piece.Start == *point ? piece.End : piece.Start;
            drawn_beyond[parent] += drawn_beyond[*point];
        }
    }
    return pieces;
}

} // namespace GUI
