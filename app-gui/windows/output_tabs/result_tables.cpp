/**
 * @file    result_tables.cpp
 * @brief   The tabs of the Output window that are tables: the operating point, the transfer function, the sensitivity
 *          and the truth table.
 */

#include "windows/output_tabs/result_tables.h"

#include "analysis/sensitivity_view.h"
#include "analysis/truth_table.h"
#include "components/logic_input.h"
#include "drawing/theme.h"
#include "editing/probing.h"
#include "imgui.h"
#include "plots/logic_trace.h"
#include "spice/spice_value.h"
#include "windows/output_tabs/plot_widgets.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

namespace {

/**
 * @brief   Whether each input of the table is high in the schematic as it is now, in the order of the table's inputs;
 *          an input that left the circuit reads low.
 */
std::vector<bool> ReadCurrentInputs(const Core::TruthTable &table, const Core::Circuit &circuit) {
    std::vector<bool> inputs_high;
    for (const std::string &name : table.InputNames) {
        const auto entry = std::ranges::find_if(circuit.GetEntries(), [&name](const Core::CircuitEntry &candidate) {
            return candidate.Part->GetType() == Core::ComponentType::LOGIC_INPUT && candidate.Part->GetName() == name;
        });
        inputs_high.push_back(entry != circuit.GetEntries().end() &&
                              static_cast<const Core::LogicInput &>(*entry->Part).IsHigh());
    }
    return inputs_high;
}

/**
 * @brief   Draws the operating point as tables of DC values, one row per node and per current, under its warnings.
 * @param[in] operating_point  The result.
 * @param[in] schematic        The names of nets, which a node shows before its number.
 */
void DrawOperatingPoint(const Core::OperatingPoint &operating_point, Schematic &schematic) {
    ImGui::PushTextWrapPos(0.0f);
    for (const std::string &warning : operating_point.Warnings) {
        ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
    }
    ImGui::PopTextWrapPos();
    constexpr ImGuiTableFlags TABLE_FLAGS =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    // Fixed widths, so the currents fit beside the voltages
    const ImVec2 table_size = {ImGui::GetFontSize() * 14.0f, 0.0f};
    if (ImGui::BeginTable("node_voltages", 2, TABLE_FLAGS, table_size)) {
        ImGui::TableSetupColumn("Node");
        ImGui::TableSetupColumn("Voltage");
        ImGui::TableHeadersRow();
        for (std::size_t node = 0; node < operating_point.NodeVoltages.size(); ++node) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // A node a net label named shows its name before its number
            const std::optional<std::string> net = schematic.GetConnectivity().GetNetName(static_cast<int>(node));
            std::string name = std::format("{}", node);
            if (node == 0) {
                name = "0 (ground)";
            } else if (net) {
                name = std::format("{} ({})", *net, node);
            }
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(std::format("{}V", Core::FormatValue(operating_point.NodeVoltages[node])).c_str());
        }
        ImGui::EndTable();
    }
    if (operating_point.Currents.empty()) {
        return;
    }
    ImGui::SameLine(0.0f, ImGui::GetFontSize() * 2.0f);
    if (ImGui::BeginTable("currents", 2, TABLE_FLAGS, table_size)) {
        ImGui::TableSetupColumn("Component");
        ImGui::TableSetupColumn("Current");
        ImGui::TableHeadersRow();
        for (const Core::ComponentCurrent &current : operating_point.Currents) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(current.Name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(std::format("{}A", Core::FormatValue(current.Current)).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Currents as in SPICE: positive through a two-terminal part from its first terminal to its "
                        "second, and into each terminal of a transistor");
    ImGui::PopTextWrapPos();
}

/**
 * @brief   Draws the transfer function as a table of three small-signal DC values; an impedance that ngspice reports as
 *          practically infinite, such as an input path held open, reads as an open circuit instead of a giant number.
 * @param[in] transfer   The result.
 * @param[in] schematic  The names of nets, for the output node.
 */
void DrawTransferFunction(const Core::TransferFunction &transfer, Schematic &schematic) {
    constexpr ImGuiTableFlags TABLE_FLAGS =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("transfer_function", 3, TABLE_FLAGS)) {
        ImGui::TableSetupColumn("Quantity");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Unit");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Gain");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Core::FormatValue(transfer.Gain).c_str());
        ImGui::TableNextColumn();
        // The gain is a ratio of like units for a voltage source and a transimpedance for a current source
        ImGui::TextUnformatted(std::format("V/{}", transfer.InputUnit).c_str());
        const auto draw_impedance = [](const char *name, const double ohms) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", name);
            ImGui::TableNextColumn();
            const bool open = !std::isfinite(ohms) || std::abs(ohms) >= OPEN_RESISTANCE;
            ImGui::TextUnformatted(open ? "open" : Core::FormatValue(ohms).c_str());
            ImGui::TableNextColumn();
            if (!open) {
                ImGui::TextUnformatted("Ohm");
            }
        };
        draw_impedance("Input impedance", transfer.InputImpedance);
        draw_impedance("Output impedance", transfer.OutputImpedance);
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Small-signal DC gain from %s to %s", transfer.InputSource.c_str(),
                        GetVoltageLabel(schematic.GetConnectivity(), transfer.OutputNode).c_str());
    ImGui::PopTextWrapPos();
}

/**
 * @brief   Selects the part with this name in the editor, which a click on its row asks for.
 */
void SelectPart(Schematic &schematic, const std::string &name) {
    const auto &elements = schematic.GetElements();
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (elements[index]->GetComponent().GetName() == name) {
            schematic.SelectElement(index);
            return;
        }
    }
}

/**
 * @brief   Draws the sensitivity as a table of parts, the most influential first; a click on a row selects the part.
 * @param[in] sensitivity  The result.
 * @param[in] schematic    The names of nets, for the output node, and the parts a click selects.
 */
void DrawSensitivity(const Core::Sensitivity &sensitivity, Schematic &schematic) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("How the DC voltage of %s changes with the value of each part, around the operating point",
                        GetVoltageLabel(schematic.GetConnectivity(), sensitivity.OutputNode).c_str());
    ImGui::PopTextWrapPos();
    constexpr ImGuiTableFlags TABLE_FLAGS =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
    // Leaves a line below the table for the notes
    const ImVec2 table_size = {0.0f,
                               std::max(ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.0f,
                                        ImGui::GetFontSize() * 6.0f)};
    if (ImGui::BeginTable("sensitivity", 5, TABLE_FLAGS, table_size)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Part");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Absolute");
        ImGui::TableSetupColumn("Per 1 %");
        ImGui::TableSetupColumn("Share of largest");
        ImGui::TableHeadersRow();
        for (const SensitivityRow &row : MakeSensitivityRows(sensitivity)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(row.Part.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                SelectPart(schematic, row.PartName);
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.Value.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.Absolute.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.Normalized.c_str());
            ImGui::TableNextColumn();
            ImGui::ProgressBar(static_cast<float>(row.Share), {-FLT_MIN, 0.0f},
                               std::format("{:.1f} %", row.Share * 100.0).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos(0.0f);
    // The text holds percent signs, so it goes in as an argument and never as the format
    ImGui::TextDisabled("%s", "Per 1 % is the change of the voltage for a 1 % higher value, which compares parts of "
                              "different kinds. Model parameters of diodes and transistors are not shown, and a "
                              "circuit driven only by AC sources reads zero");
    ImGui::PopTextWrapPos();
}

/**
 * @brief   Draws one row per combination of the inputs, in counting order: 0 dimmed, 1 bright and X, which no operating
 *          point or a voltage between the logic levels gives, in the warning color; the row the inputs are in now is
 *          highlighted.
 * @param[in] table      The result.
 * @param[in] schematic  The circuit as it is now, for the logic levels and the current inputs.
 */
void DrawTruthTable(const Core::TruthTable &table, Schematic &schematic) {
    const Core::Circuit circuit = schematic.BuildCircuit();
    const std::vector<LogicNode> logic_nodes = FindLogicNodes(circuit);
    std::vector<std::string> output_labels;
    for (const int node : table.OutputNodes) {
        output_labels.push_back(GetVoltageLabel(schematic.GetConnectivity(), node));
    }
    if (ImGui::Button("Copy as Markdown")) {
        ImGui::SetClipboardText(FormatTruthTableMarkdown(table, output_labels, logic_nodes).c_str());
    }
    ImGui::SetItemTooltip("Put the table on the clipboard, to paste into notes or documents");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu combinations; X is a voltage between the logic levels, or no operating point",
                        table.Rows.size());
    for (const std::string &warning : table.Warnings) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
        ImGui::PopTextWrapPos();
    }

    const std::optional<std::size_t> current = FindTruthTableRow(table, ReadCurrentInputs(table, circuit));
    constexpr ImGuiTableFlags TABLE_FLAGS = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
    const int column_count = static_cast<int>(table.InputNames.size() + output_labels.size());
    if (!ImGui::BeginTable("truth_table", column_count, TABLE_FLAGS)) {
        return;
    }
    for (const std::string &name : table.InputNames) {
        ImGui::TableSetupColumn(name.c_str());
    }
    for (const std::string &label : output_labels) {
        ImGui::TableSetupColumn(label.c_str());
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    for (std::size_t row = 0; row < table.Rows.size(); ++row) {
        ImGui::TableNextRow();
        if (current == row) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
        }
        for (const bool is_high : table.Rows[row].InputsHigh) {
            ImGui::TableNextColumn();
            if (is_high) {
                ImGui::TextUnformatted("1");
            } else {
                ImGui::TextDisabled("0");
            }
        }
        for (const LogicState state : ReadTruthTableOutputs(table, row, logic_nodes)) {
            ImGui::TableNextColumn();
            if (state == LogicState::UNKNOWN) {
                ImGui::TextColored(GetWarningTextColor(), "X");
            } else {
                ImGui::TextUnformatted(GetLogicStateName(state));
            }
        }
    }
    ImGui::EndTable();
}

} // namespace

/**
 * @brief   Draws the Operating point tab: the table of the last operating point, or how to get one.
 * @param[in] schematic  The schematic, whose result is shown.
 */
void DrawOperatingPointTab(Schematic &schematic) {
    if (const auto &operating_point = schematic.GetOperatingPoint()) {
        DrawOperatingPoint(*operating_point, schematic);
    } else {
        ImGui::TextDisabled("No operating point for the current circuit; pick it beside Run in the editor");
    }
}

/**
 * @brief   Draws the Transfer function tab: the table of the last transfer function, or how to get one.
 * @param[in] schematic  The schematic, whose result is shown.
 */
void DrawTransferFunctionTab(Schematic &schematic) {
    if (const auto &transfer = schematic.GetTransferFunction()) {
        DrawTransferFunction(*transfer, schematic);
    } else {
        ImGui::TextDisabled("No transfer function for the current circuit; pick it beside Run in the editor");
    }
}

/**
 * @brief   Draws the Sensitivity tab: the table of the last sensitivity, or how to get one.
 * @param[in] schematic    The schematic, whose result is shown.
 * @param[in,out] preferences  Whose wire coloring the checkbox at the top sets to Sensitivity, or back to Plain.
 */
void DrawSensitivityTab(Schematic &schematic, Preferences &preferences) {
    if (const auto &sensitivity = schematic.GetSensitivity()) {
        bool is_colored = preferences.WireColors == WireColoring::SENSITIVITY;
        if (ImGui::Checkbox("Color the schematic", &is_colored)) {
            preferences.WireColors = is_colored ? WireColoring::SENSITIVITY : WireColoring::PLAIN;
        }
        ImGui::SetItemTooltip(
            "Color the parts by their influence on the output, as View > Wire Colors > Sensitivity does");
        DrawSensitivity(*sensitivity, schematic);
    } else {
        ImGui::TextDisabled("No sensitivity for the current circuit; pick it beside Run in the editor");
    }
}

/**
 * @brief   Draws the Truth table tab: the last truth table, or how to get one.
 * @param[in] schematic  The schematic, whose result is shown.
 */
void DrawTruthTableTab(Schematic &schematic) {
    if (const auto &truth_table = schematic.GetTruthTable()) {
        DrawTruthTable(*truth_table, schematic);
    } else {
        ImGui::TextDisabled("No truth table for the current circuit; pick it beside Run in the editor");
    }
}

} // namespace GUI
