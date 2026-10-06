/**
 * @file    wire_editing.h
 * @brief   Keeps wires attached to terminals that move and cleans up the segments left behind.
 */

#ifndef IMCSIM_WIRE_EDITING_H
#define IMCSIM_WIRE_EDITING_H

#include "helpers.h"
#include "ui_elements/ui_wire.h"
#include <vector>

namespace GUI {

std::vector<UIWire> FollowTerminals(const std::vector<UIWire> &wires, const std::vector<GridPoint> &old_terminals,
                                    const std::vector<GridPoint> &new_terminals);
std::vector<UIWire> SimplifyWires(const std::vector<UIWire> &wires, const std::vector<GridPoint> &terminals);

} // namespace GUI

#endif // IMCSIM_WIRE_EDITING_H
