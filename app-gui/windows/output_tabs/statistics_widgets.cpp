/**
 * @file    statistics_widgets.cpp
 * @brief   The statistics panels under the plots of the Output window, with the formatting of their values.
 */

#include "windows/output_tabs/statistics_widgets.h"

#include "base/theme.h"
#include "base/value_field.h"
#include "results/trace_statistics.h"
#include "schematic/probing.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cfloat>
#include <format>

namespace GUI {

namespace {

/// Gap on the left of each trace column of a statistics table, in font sizes.
constexpr float TRACE_COLUMN_GAP = 1.0f;
/// Space between the table of the traces and that of the cursors beside it, in font sizes.
constexpr float CURSOR_TABLE_GAP = 1.5f;
/// Most of the height of a tab the statistics under the plots take before they scroll.
constexpr float MAX_STATISTICS_SHARE = 0.4f;

/**
 * @brief   The text of a table cell, muted when it stands for no value.
 */
void DrawCellText(const std::string &text) {
    if (text == NO_VALUE) {
        ImGui::TextDisabled("%s", text.c_str());
    } else {
        ImGui::TextUnformatted(text.c_str());
    }
}

/**
 * @brief   The readings of the cursors themselves, a name and a value per row, beside the table just drawn when the
 *          panel has room for them and under it otherwise, so the line over the table never runs past the panel.
 */
void DrawCursorTable(const CursorReadings &readings) {
    if (readings.empty()) {
        return;
    }
    const auto measure = [](const std::string &text) { return ImGui::CalcTextSize(text.c_str()).x; };
    float name_width = 0.0f;
    float value_width = 0.0f;
    for (const auto &[name, value] : readings) {
        name_width = std::max(name_width, measure(name));
        value_width = std::max(value_width, measure(value));
    }
    const float column_gap = ImGui::GetFontSize() * TRACE_COLUMN_GAP;
    value_width += column_gap;
    const float table_width = name_width + value_width + ImGui::GetStyle().CellPadding.x * 4.0f;
    const float gap = ImGui::GetFontSize() * CURSOR_TABLE_GAP;
    ImGui::SameLine(0.0f, gap);
    if (ImGui::GetContentRegionAvail().x < table_width) {
        ImGui::NewLine();
    }
    if (!ImGui::BeginTable("cursor_readings", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg,
                           {table_width, 0.0f})) {
        return;
    }
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, name_width);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, value_width);
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableNextColumn();
    ImGui::TextDisabled("Cursors");
    for (const auto &[name, value] : readings) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", name.c_str());
        ImGui::TableNextColumn();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + value_width - measure(value));
        DrawCellText(value);
    }
    ImGui::EndTable();
}

} // namespace

/**
 * @brief   Writes a measured value with four significant digits, a SPICE suffix and its unit, such as "4.700kOhm".
 */
std::string FormatQuantity(const double value, const char *unit) {
    return std::format("{}{}", Core::FormatMeasurement(value), unit);
}

/**
 * @brief   Writes a value with two decimals and its unit: decibels and degrees read better with fixed decimals than
 *          with SPICE suffixes, which would show 500m dB.
 */
std::string FormatFixed(const double value, const char *unit) {
    return Core::FormatFixed(value, 2) + unit;
}

/**
 * @brief   Writes a value with its suffix and unit, or "-" for no value.
 */
std::string FormatOptionalQuantity(const std::optional<double> value, const char *unit) {
    return value ? FormatQuantity(*value, unit) : "-";
}

/**
 * @brief   Draws a two-column row of a statistics table: what is measured and its value.
 */
void DrawStatistic(const char *name, const std::string &value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", name);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value.c_str());
}

/**
 * @brief   Draws a row with a value; a negligible value reads as ~0, so it is not mistaken for an exact zero, and
 *          hovering it shows the exact value.
 * @param[in] name   What is measured.
 * @param[in] value  The value.
 * @param[in] scale  The size of the trace, which tells residue from real values.
 * @param[in] unit   Unit of the value.
 */
void DrawMeasuredValue(const char *name, const double value, const double scale, const char *unit) {
    if (!IsNegligible(value, scale)) {
        DrawStatistic(name, FormatQuantity(value, unit));
        return;
    }
    DrawStatistic(name, std::format("~0{}", unit));
    ImGui::SetItemTooltip(
        "%s", std::format("{}{}, negligible next to the trace", Core::FormatMeasurement(value), unit).c_str());
}

/**
 * @brief   Begins a two-column table of statistics.
 * @param[in] id  ImGui ID of the table, such as the label of its trace.
 * @return  True when the table is drawn; then call ImGui::EndTable().
 */
bool BeginStatisticsTable(const std::string &id) {
    return ImGui::BeginTable(id.c_str(), 2, ImGuiTableFlags_SizingStretchProp);
}

/**
 * @brief   Draws the line over a statistics table: what span the readings cover.
 * @param[in] span_text  What the readings cover, such as "Over the visible time".
 */
void DrawStatisticsHeader(const char *span_text) {
    ImGui::TextDisabled("%s", span_text);
}

/**
 * @brief   Draws readings as one table with a row per quantity and a column per trace, headed by the trace labels in
 *          their colors, with the figures right-aligned so their digits line up.
 * @param[in] id               ImGui ID of the table.
 * @param[in] traces           Traces of the columns, for their labels and colors.
 * @param[in] rows             Readings, each row holding one cell per trace in the same order.
 * @param[in] cursor_readings  Name and value of each reading of the cursors themselves, such as A and its time, drawn
 *                             as a table of their own beside the traces, or under them when the panel is too narrow;
 *                             empty while the cursors are hidden.
 * @note    The table is as wide as its widest cells, and the statistics panel scrolls sideways when the traces do not
 *          fit.
 */
void DrawTraceTable(const char *id, const std::vector<PlottedTrace> &traces, const std::vector<StatisticsRow> &rows,
                    const CursorReadings &cursor_readings) {
    const auto measure = [](const std::string &text) { return ImGui::CalcTextSize(text.c_str()).x; };
    // Each column is as wide as its widest text, so the table width is known before it begins
    std::vector<float> widths(traces.size() + 1, 0.0f);
    for (std::size_t column = 0; column < traces.size(); ++column) {
        widths[column + 1] = measure(traces[column].Label);
    }
    // The figures of neighboring columns would run together, so each trace column keeps a gap on its left
    const float column_gap = ImGui::GetFontSize() * TRACE_COLUMN_GAP;
    for (const StatisticsRow &row : rows) {
        widths[0] = std::max(widths[0], measure(row.Name));
        for (std::size_t column = 0; column < row.Cells.size() && column < traces.size(); ++column) {
            widths[column + 1] = std::max(widths[column + 1], measure(row.Cells[column].Text));
        }
    }
    for (std::size_t column = 1; column < widths.size(); ++column) {
        widths[column] += column_gap;
    }
    const float padding = ImGui::GetStyle().CellPadding.x * 2.0f;
    float table_width = 0.0f;
    for (const float width : widths) {
        table_width += width + padding;
    }
    if (!ImGui::BeginTable(id, static_cast<int>(widths.size()), ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg,
                           {table_width, 0.0f})) {
        return;
    }
    for (const float width : widths) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, width);
    }
    const auto right_aligned = [&measure](const std::string &text, const float column_width) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + column_width - measure(text));
    };
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableNextColumn();
    for (std::size_t column = 0; column < traces.size(); ++column) {
        ImGui::TableNextColumn();
        right_aligned(traces[column].Label, widths[column + 1]);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(traces[column].Color), "%s", traces[column].Label.c_str());
    }
    for (const StatisticsRow &row : rows) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", row.Name.c_str());
        for (std::size_t column = 0; column < row.Cells.size() && column < traces.size(); ++column) {
            const StatisticsCell &cell = row.Cells[column];
            ImGui::TableNextColumn();
            right_aligned(cell.Text, widths[column + 1]);
            DrawCellText(cell.Text);
            if (!cell.Tooltip.empty()) {
                ImGui::SetItemTooltip("%s", cell.Tooltip.c_str());
            }
        }
    }
    ImGui::EndTable();
    DrawCursorTable(cursor_readings);
}

/**
 * @brief   Splits a tab into the plots and the statistics panel under them, which reads across the traces and is as
 *          tall as its content, up to a share of the tab.
 * @param[in] draw_plots       Draws the plots, above, in the height the statistics leave.
 * @param[in] draw_statistics  Draws the statistics, in the bordered panel below, which scrolls when it is taller than
 *                             its share or wider than the tab.
 * @details The panel fits its content, which is only known once drawn, so the plots take the height the panel had
 *          the frame before, kept in the ImGui storage of the tab.
 */
void DrawWithStatistics(const std::function<void()> &draw_plots, const std::function<void()> &draw_statistics) {
    const ImGuiID height_id = ImGui::GetID("statistics_height");
    ImGuiStorage *storage = ImGui::GetStateStorage();
    const float available = ImGui::GetContentRegionAvail().y;
    const float max_height = available * MAX_STATISTICS_SHARE;
    const float statistics_height = std::min(storage->GetFloat(height_id, 0.0f), max_height);
    const float plots_height = std::max(available - statistics_height - ImGui::GetStyle().ItemSpacing.y, 0.0f);
    if (ImGui::BeginChild("plots_area", ImVec2(0.0f, plots_height))) {
        draw_plots();
    }
    ImGui::EndChild();
    ImGui::SetNextWindowSizeConstraints({0.0f, 0.0f}, {FLT_MAX, max_height});
    if (ImGui::BeginChild("statistics", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        draw_statistics();
    }
    ImGui::EndChild();
    storage->SetFloat(height_id, ImGui::GetItemRectSize().y);
}

} // namespace GUI
