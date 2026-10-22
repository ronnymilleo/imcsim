/**
 * @file    truth_table.cpp
 * @brief   Reads the outputs of a truth table as logic states and writes the table as text.
 */

#include "truth_table.h"

#include <algorithm>
#include <format>

namespace GUI {

/**
 * @brief   Finds the row of the table that has the given states of the inputs.
 * @param[in] table        Truth table to look in.
 * @param[in] inputs_high  Whether each input is high, in the order of the table's input names.
 * @return  The index of the row, or no value when the states do not match the inputs of the table.
 */
std::optional<std::size_t> FindTruthTableRow(const Core::TruthTable &table, const std::vector<bool> &inputs_high) {
    if (inputs_high.size() != table.InputNames.size()) {
        return std::nullopt;
    }
    const auto found = std::ranges::find(table.Rows, inputs_high, &Core::TruthTableRow::InputsHigh);
    if (found == table.Rows.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - table.Rows.begin());
}

/**
 * @brief   Finds the logic level an output node swings between.
 * @param[in] table        Truth table whose inputs set the fallback level.
 * @param[in] node         Output node.
 * @param[in] logic_nodes  Nodes the logic parts connect to, with their levels, as FindLogicNodes() lists them.
 * @return  The level of the logic part that drives or reads the node, or else the highest level of the inputs, since
 *          the inputs set what the circuit swings to; 5 V when the table has no inputs.
 */
double GetOutputLogicLevel(const Core::TruthTable &table, const int node, const std::vector<LogicNode> &logic_nodes) {
    const auto found = std::ranges::find(logic_nodes, node, &LogicNode::Node);
    if (found != logic_nodes.end()) {
        return found->LogicLevel;
    }
    if (table.InputLevels.empty()) {
        return LogicNode{}.LogicLevel;
    }
    return std::ranges::max(table.InputLevels);
}

/**
 * @brief   Reads every output of one row as a logic state.
 * @param[in] table        Truth table to read.
 * @param[in] row          Index of the row.
 * @param[in] logic_nodes  Nodes the logic parts connect to, with their levels.
 * @return  One state per output node; all unknown for a row without an operating point.
 */
std::vector<LogicState> ReadTruthTableOutputs(const Core::TruthTable &table, const std::size_t row,
                                              const std::vector<LogicNode> &logic_nodes) {
    const Core::TruthTableRow &entry = table.Rows[row];
    std::vector<LogicState> states;
    states.reserve(table.OutputNodes.size());
    for (std::size_t output = 0; output < table.OutputNodes.size(); ++output) {
        if (!entry.HasOperatingPoint()) {
            states.push_back(LogicState::Unknown);
            continue;
        }
        const double level = GetOutputLogicLevel(table, table.OutputNodes[output], logic_nodes);
        states.push_back(ReadLogicState(entry.OutputVoltages[output], level));
    }
    return states;
}

/**
 * @brief   Writes the table as Markdown, to paste into notes or documents.
 * @param[in] table          Truth table to write.
 * @param[in] output_labels  Name of each output node, as the plots label it, in the order of the table.
 * @param[in] logic_nodes    Nodes the logic parts connect to, with their levels.
 * @return  A header with the input names and the output labels, then one line per row with 0, 1 or X.
 */
std::string FormatTruthTableMarkdown(const Core::TruthTable &table, const std::vector<std::string> &output_labels,
                                     const std::vector<LogicNode> &logic_nodes) {
    std::vector<std::string> columns = table.InputNames;
    columns.insert(columns.end(), output_labels.begin(), output_labels.end());
    std::string text = "|";
    std::string separator = "|";
    for (const std::string &column : columns) {
        text += std::format(" {} |", column);
        separator += " --- |";
    }
    text += "\n" + separator + "\n";
    for (std::size_t row = 0; row < table.Rows.size(); ++row) {
        text += "|";
        for (const bool is_high : table.Rows[row].InputsHigh) {
            text += is_high ? " 1 |" : " 0 |";
        }
        for (const LogicState state : ReadTruthTableOutputs(table, row, logic_nodes)) {
            text += std::format(" {} |", GetLogicStateName(state));
        }
        text += "\n";
    }
    return text;
}

} // namespace GUI
