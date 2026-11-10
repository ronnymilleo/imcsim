/**
 * @file    result_tables.h
 * @brief   The tabs of the Output window that are tables: the transfer function, the sensitivity and the truth table.
 */

#ifndef IMCSIM_RESULT_TABLES_H
#define IMCSIM_RESULT_TABLES_H

#include "document/schematic.h"
#include "preferences.h"

namespace GUI {

// Tables
void DrawTransferFunctionTab(Schematic &schematic);
void DrawSensitivityTab(Schematic &schematic, Preferences &preferences);
void DrawTruthTableTab(Schematic &schematic);

} // namespace GUI

#endif // IMCSIM_RESULT_TABLES_H
