/**
 * @file    result_tables.h
 * @brief   The tabs of the Output window that are tables: the operating point, the transfer function and the truth
 *          table.
 */

#ifndef IMCSIM_RESULT_TABLES_H
#define IMCSIM_RESULT_TABLES_H

#include "document/schematic.h"
#include "simulation/simulation_results.h"

namespace GUI {

// Tables
void DrawOperatingPoint(const Core::OperatingPoint &operating_point, Schematic &schematic);
void DrawTransferFunction(const Core::TransferFunction &transfer, Schematic &schematic);
void DrawTruthTable(const Core::TruthTable &table, Schematic &schematic);

} // namespace GUI

#endif // IMCSIM_RESULT_TABLES_H
