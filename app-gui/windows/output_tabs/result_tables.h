/**
 * @file    result_tables.h
 * @brief   The tabs of the Output window that are tables: the operating point, the transfer function and the truth
 *          table.
 */

#ifndef IMCSIM_RESULT_TABLES_H
#define IMCSIM_RESULT_TABLES_H

#include "document/schematic.h"

namespace GUI {

// Tables
void DrawOperatingPointTab(Schematic &schematic);
void DrawTransferFunctionTab(Schematic &schematic);
void DrawTruthTableTab(Schematic &schematic);

} // namespace GUI

#endif // IMCSIM_RESULT_TABLES_H
