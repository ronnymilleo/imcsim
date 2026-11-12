/**
 * @file    result_tables.h
 * @brief   The tabs of the Output window that are tables: the transfer function, the sensitivity and the truth table.
 */

#ifndef IMCSIM_RESULT_TABLES_H
#define IMCSIM_RESULT_TABLES_H

#include "results/result_store.h"
#include "schematic/preferences.h"
#include "schematic/schematic.h"

namespace GUI {

// Tables
void DrawTransferFunctionTab(const ResultStore &results, Schematic &schematic);
void DrawSensitivityTab(const ResultStore &results, Schematic &schematic, Preferences &preferences);
void DrawTruthTableTab(const ResultStore &results, Schematic &schematic);

} // namespace GUI

#endif // IMCSIM_RESULT_TABLES_H
