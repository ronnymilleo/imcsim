/**
 * @file    connectivity.h
 * @brief   Finds which terminals and wires of the schematic are electrically connected.
 */

#ifndef IMCSIM_CONNECTIVITY_H
#define IMCSIM_CONNECTIVITY_H

#include "circuit.h"
#include "helpers.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace GUI {

/**
 * @class   Connectivity
 * @brief   Groups the connection points of a schematic into circuit nodes.
 * @details Connection points are terminals and wire ends. Two points share a node when a wire joins them, when
 *          one lies on the middle of a wire (T-junction) or when they are the same grid point. Wires that only
 *          cross do not connect. The node that contains a ground terminal is node 0; the others are numbered
 *          from 1. Build it again whenever the schematic changes.
 */
class Connectivity {
public:
    Connectivity(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires);

    std::optional<int> GetNode(GridPoint point) const;
    const std::vector<GridPoint> &GetJunctions() const;

private:
    std::map<GridPoint, int> m_NodeOfPoint;
    std::vector<GridPoint> m_Junctions;
};

Core::Circuit BuildCircuit(const std::vector<std::unique_ptr<UIElement>> &elements, const Connectivity &connectivity);

} // namespace GUI

#endif // IMCSIM_CONNECTIVITY_H
