/**
 * @file    check_truth_table.h
 * @brief   Checks the settings of a truth table before it runs.
 */

#ifndef IMCSIM_CHECK_TRUTH_TABLE_H
#define IMCSIM_CHECK_TRUTH_TABLE_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include <optional>
#include <string>

namespace Core {

std::optional<std::string> CheckTruthTableSettings(const Circuit &circuit, const TruthTableSettings &settings);

} // namespace Core

#endif // IMCSIM_CHECK_TRUTH_TABLE_H
