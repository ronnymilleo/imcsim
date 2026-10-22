/**
 * @file    truth_table.h
 * @brief   Reads the outputs of a truth table as logic states and writes the table as text.
 * @details Pure functions, so the Output window only draws what they give.
 */

#ifndef IMCSIM_TRUTH_TABLE_H
#define IMCSIM_TRUTH_TABLE_H

#include "logic_trace.h"
#include "simulator.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

// Reading
std::optional<std::size_t> FindTruthTableRow(const Core::TruthTable &table, const std::vector<bool> &inputs_high);
double GetOutputLogicLevel(const Core::TruthTable &table, int node, const std::vector<LogicNode> &logic_nodes);
std::vector<LogicState> ReadTruthTableOutputs(const Core::TruthTable &table, std::size_t row,
                                              const std::vector<LogicNode> &logic_nodes);

// Writing
std::string FormatTruthTableMarkdown(const Core::TruthTable &table, const std::vector<std::string> &output_labels,
                                     const std::vector<LogicNode> &logic_nodes);

} // namespace GUI

#endif // IMCSIM_TRUTH_TABLE_H
