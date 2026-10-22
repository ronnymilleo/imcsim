/**
 * @file    truth_table_tests.cpp
 * @brief   Tests for reading the outputs of a truth table as logic states and writing it as text.
 */

#include "truth_table.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace {

// An AND of V1 and V2 on node 3, whose last row has no operating point to show the unknown state
Core::TruthTable MakeTable() {
    Core::TruthTable table;
    table.InputNames = {"V1", "V2"};
    table.InputLevels = {5.0, 5.0};
    table.OutputNodes = {3};
    table.Rows = {{.InputsHigh = {false, false}, .OutputVoltages = {0.1}},
                  {.InputsHigh = {false, true}, .OutputVoltages = {0.2}},
                  {.InputsHigh = {true, false}, .OutputVoltages = {2.5}},
                  {.InputsHigh = {true, true}, .OutputVoltages = {}}};
    return table;
}

} // namespace

TEST_CASE("The row of the inputs as they are now is found by its states", "[truth_table]") {
    const Core::TruthTable table = MakeTable();
    CHECK(GUI::FindTruthTableRow(table, {false, false}) == 0);
    CHECK(GUI::FindTruthTableRow(table, {true, false}) == 2);
    CHECK(GUI::FindTruthTableRow(table, {true, true}) == 3);
    // A different number of inputs matches no row
    CHECK_FALSE(GUI::FindTruthTableRow(table, {true}).has_value());
}

TEST_CASE("The output of a row reads as 0, 1 or X with the thresholds of the logic parts", "[truth_table]") {
    Core::TruthTable table = MakeTable();
    table.Rows[3].OutputVoltages = {4.9};
    const std::vector<GUI::LogicNode> none;
    CHECK(GUI::ReadTruthTableOutputs(table, 0, none) == std::vector{GUI::LogicState::Low});
    // Between 30% and 70% of 5 V is neither a 0 nor a 1
    CHECK(GUI::ReadTruthTableOutputs(table, 2, none) == std::vector{GUI::LogicState::Unknown});
    CHECK(GUI::ReadTruthTableOutputs(table, 3, none) == std::vector{GUI::LogicState::High});
}

TEST_CASE("A row without an operating point reads X at every output", "[truth_table]") {
    Core::TruthTable table = MakeTable();
    table.OutputNodes = {3, 4};
    table.Rows[3].OutputVoltages = {};
    const std::vector<GUI::LogicNode> none;
    CHECK(GUI::ReadTruthTableOutputs(table, 3, none) ==
          std::vector{GUI::LogicState::Unknown, GUI::LogicState::Unknown});
}

TEST_CASE("An output node reads with the level of the logic part on it, else the highest input level",
          "[truth_table]") {
    Core::TruthTable table = MakeTable();
    table.InputLevels = {3.3, 5.0};
    const std::vector<GUI::LogicNode> logic_nodes = {{.Node = 3, .LogicLevel = 3.3}};
    CHECK(GUI::GetOutputLogicLevel(table, 3, logic_nodes) == 3.3);
    CHECK(GUI::GetOutputLogicLevel(table, 4, logic_nodes) == 5.0);
    // 2.5 V is a 1 for a 3.3 V part and unknown for a 5 V one
    CHECK(GUI::ReadTruthTableOutputs(table, 2, logic_nodes) == std::vector{GUI::LogicState::High});
    CHECK(GUI::ReadTruthTableOutputs(table, 2, {}) == std::vector{GUI::LogicState::Unknown});
    table.InputLevels.clear();
    CHECK(GUI::GetOutputLogicLevel(table, 4, {}) == 5.0);
}

TEST_CASE("The table is written as Markdown with its input names and output labels", "[truth_table]") {
    const Core::TruthTable table = MakeTable();
    CHECK(GUI::FormatTruthTableMarkdown(table, {"V(OUT)"}, {}) == "| V1 | V2 | V(OUT) |\n"
                                                                  "| --- | --- | --- |\n"
                                                                  "| 0 | 0 | 0 |\n"
                                                                  "| 0 | 1 | 0 |\n"
                                                                  "| 1 | 0 | X |\n"
                                                                  "| 1 | 1 | X |\n");
}
