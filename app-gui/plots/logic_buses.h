/**
 * @file    logic_buses.h
 * @brief   The panel of the logic buses beside the plots, where they are added, edited and shown.
 */

#ifndef IMCSIM_LOGIC_BUSES_H
#define IMCSIM_LOGIC_BUSES_H

#include "document/schematic.h"
#include "plots/logic_bus.h"
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   LogicBuses
 * @brief   Lists the buses of a schematic with a check box each, and edits one in a popup: its name, its bits and the
 *          format of its value.
 * @details The buses belong to the schematic, which saves them with the file; the panel only changes them.
 */
class LogicBuses {
public:
    explicit LogicBuses(Schematic &schematic);

    void Draw(const std::vector<std::string> &signals);

private:
    Schematic &m_Schematic;

    bool DrawEditor(LogicBus &bus, const std::vector<std::string> &signals) const;
    static void DrawBitRows(LogicBus &bus);
    static void DrawAddBit(LogicBus &bus, const std::vector<std::string> &signals);
};

// Describing
std::string DescribeBus(const LogicBus &bus);

} // namespace GUI

#endif // IMCSIM_LOGIC_BUSES_H
