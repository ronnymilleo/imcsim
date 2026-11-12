/**
 * @file    monte_carlo_tab.cpp
 * @brief   The Monte Carlo tab of the Output window: the histogram of a measured quantity over the runs, its statistics
 *          and the table of runs.
 */

#include "windows/output_tabs/monte_carlo_tab.h"

#include "base/theme.h"
#include "imgui.h"
#include "implot.h"
#include "schematic/probing.h"
#include "spice/spice_value.h"
#include "windows/output_tabs/plot_widgets.h"
#include "windows/output_tabs/statistics_widgets.h"
#include <algorithm>
#include <format>

namespace GUI {

namespace {

constexpr float HISTOGRAM_HEIGHT = 14.0f;
constexpr float RUN_TABLE_HEIGHT = 16.0f;
constexpr float BAR_FILL = 0.9f;
constexpr const char *NO_RESULT =
    "No Monte Carlo analysis for the current circuit; pick Monte Carlo as the run mode in the analysis settings, give "
    "the parts a tolerance and run an operating point, a transient, an AC sweep or a DC sweep";

std::string FormatMeasure(const std::optional<double> value, const char *unit) {
    return value ? FormatQuantity(*value, unit) : std::string("-");
}

} // namespace

/**
 * @brief   Creates the tab for a schematic.
 * @param[in] schematic  Schematic that keeps the measurements and the names of nets; it must outlive the tab.
 * @param[in] results    Results whose Monte Carlo table is shown; they must outlive the tab.
 */
MonteCarloTab::MonteCarloTab(Schematic &schematic, ResultStore &results) : m_Schematic(schematic), m_Results(results) {
}

/**
 * @brief   Draws the tab: the warnings, the picker of the quantity, its histogram and the table of runs, with the
 *          statistics on the right.
 * @return  True when the user asked to export the table of runs.
 */
bool MonteCarloTab::Draw() {
    const std::optional<MonteCarloTable> &table = m_Results.GetMonteCarlo();
    if (!table || table->Columns.empty()) {
        ImGui::TextDisabled("%s", NO_RESULT);
        return false;
    }
    std::size_t column = 0;
    const std::optional<std::size_t> resolved = ResolveColumn(*table);
    if (!resolved) {
        ImGui::TextDisabled("%s", NO_RESULT);
        return false;
    }
    column = *resolved;
    bool is_export_asked = false;
    DrawWithStatistics(
        [&] {
            for (const std::string &warning : ListMonteCarloWarnings(*table)) {
                ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
            }
            DrawColumnPicker(*table, column);
            ImGui::SameLine();
            is_export_asked = ImGui::Button("Export runs...");
            ImGui::SetItemTooltip("Save every run, the values of the parts and all the measurements as a CSV table");
            DrawHistogram(*table, column);
            DrawRunTable(*table, column);
        },
        [&] { DrawStatistics(*table, column); });
    return is_export_asked;
}

/**
 * @brief   Writes the table of runs as CSV, with each measured column headed by its node and quantity.
 * @return  The CSV text, or nothing without a table.
 */
std::string MonteCarloTab::BuildCsv() const {
    const std::optional<MonteCarloTable> &table = m_Results.GetMonteCarlo();
    if (!table) {
        return {};
    }
    std::vector<std::string> labels;
    for (const MeasureColumn &column : table->Columns) {
        labels.push_back(std::format("{} ({})", LabelColumn(column), GetMeasureUnit(column.Kind)));
    }
    return WriteMonteCarloCsv(*table, labels);
}

/**
 * @brief   Finds the column of the node or current and the quantity picked, falling back to the column that spreads
 * most, the measured nodes first, when the pick is not in the table, as after a run of another analysis.
 */
std::optional<std::size_t> MonteCarloTab::ResolveColumn(const MonteCarloTable &table) {
    if (table.Columns.empty()) {
        return std::nullopt;
    }
    const std::vector<std::size_t> picked = m_Current ? ListCurrentColumns(table, *m_Current)
                                            : m_Node  ? ListNodeColumns(table, *m_Node)
                                                      : std::vector<std::size_t>{};
    for (const std::size_t column : picked) {
        if (table.Columns[column].Kind == m_Kind) {
            return column;
        }
    }
    const std::vector<int> nodes = ListTableNodes(table);
    const std::vector<int> measured =
        nodes.empty() ? std::vector<int>{} : ListMeasuredNodes(static_cast<std::size_t>(nodes.back()) + 1, m_Schematic);
    const std::size_t column = FindDefaultColumn(table, measured);
    PickTrace(table.Columns[column]);
    return column;
}

/**
 * @brief   Remembers the node or current of a column and its quantity as the pick.
 */
void MonteCarloTab::PickTrace(const MeasureColumn &column) {
    m_Node = column.Current.empty() ? std::optional<int>(column.Node) : std::nullopt;
    m_Current = column.Current.empty() ? std::nullopt : std::optional<std::string>(column.Current);
    m_Kind = column.Kind;
}

std::string MonteCarloTab::LabelColumn(const MeasureColumn &column) const {
    return std::format("{} {}", LabelTrace(column), GetMeasureName(column.Kind));
}

/**
 * @brief   Names the node or the current of a column, as the trace list does: V(OUT) or I(R1).
 */
std::string MonteCarloTab::LabelTrace(const MeasureColumn &column) const {
    if (!column.Current.empty()) {
        return std::format("I({})", column.Current);
    }
    return GetVoltageLabel(m_Schematic.GetConnectivity(), column.Node);
}

/**
 * @brief   Two lists: the node or current, and the quantities measured on it.
 */
void MonteCarloTab::DrawColumnPicker(const MonteCarloTable &table, std::size_t &column) {
    const MeasureColumn &shown = table.Columns[column];
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    if (ImGui::BeginCombo("##monte_carlo_node", LabelTrace(shown).c_str())) {
        for (const int node : ListTableNodes(table)) {
            const bool is_shown = shown.Current.empty() && node == shown.Node;
            if (ImGui::Selectable(GetVoltageLabel(m_Schematic.GetConnectivity(), node).c_str(), is_shown)) {
                column = ListNodeColumns(table, node).front();
                PickTrace(table.Columns[column]);
            }
        }
        for (const std::string &current : ListTableCurrents(table)) {
            if (ImGui::Selectable(std::format("I({})", current).c_str(), current == shown.Current)) {
                column = ListCurrentColumns(table, current).front();
                PickTrace(table.Columns[column]);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Node voltage or current measured in every run");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    if (ImGui::BeginCombo("##monte_carlo_quantity", GetMeasureName(table.Columns[column].Kind))) {
        const MeasureColumn &trace = table.Columns[column];
        const std::vector<std::size_t> candidates =
            trace.Current.empty() ? ListNodeColumns(table, trace.Node) : ListCurrentColumns(table, trace.Current);
        for (const std::size_t candidate : candidates) {
            const MeasureKind kind = table.Columns[candidate].Kind;
            if (ImGui::Selectable(GetMeasureName(kind), kind == m_Kind)) {
                m_Kind = kind;
                column = candidate;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("What is measured in every run, as the statistics of the other tabs read it");
}

/**
 * @brief   How many runs fall in each range of the quantity; the nominal run is marked by a line.
 */
void MonteCarloTab::DrawHistogram(const MonteCarloTable &table, const std::size_t column) {
    const std::vector<double> samples = CollectMeasure(table, column);
    if (const std::optional<std::string> unread = DescribeUnreadRuns(table, column)) {
        ImGui::TextColored(GetWarningTextColor(), "%s", unread->c_str());
    }
    if (samples.empty()) {
        ImGui::TextDisabled("No run has a reading of this quantity");
        return;
    }
    // A histogram of readings that do not differ is one bar on an axis of rounding noise
    if (!HasMeasureSpread(table, column)) {
        ImGui::TextDisabled("%s", DescribeUniformReading(table, column).c_str());
        return;
    }
    const char *unit = GetMeasureUnit(table.Columns[column].Kind);
    const Histogram histogram = BuildHistogram(samples, SuggestBinCount(samples.size()));
    const double width = (histogram.To - histogram.From) / static_cast<double>(histogram.Counts.size());
    std::vector<double> centers;
    std::vector<double> counts;
    for (std::size_t bin = 0; bin < histogram.Counts.size(); ++bin) {
        centers.push_back(histogram.From + (static_cast<double>(bin) + 0.5) * width);
        counts.push_back(static_cast<double>(histogram.Counts[bin]));
    }
    if (!ImPlot::BeginPlot("##monte_carlo_histogram", ImVec2(-1.0f, ImGui::GetFontSize() * HISTOGRAM_HEIGHT))) {
        return;
    }
    ImPlot::SetupAxes(GetMeasureName(table.Columns[column].Kind), "Runs", ImPlotAxisFlags_AutoFit,
                      ImPlotAxisFlags_AutoFit);
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>(unit));
    ImPlot::PlotBars("Runs", centers.data(), counts.data(), static_cast<int>(centers.size()), width * BAR_FILL);
    if (const std::optional<double> nominal = table.Runs.front().Measures[column]) {
        ImPlot::PlotInfLines("Nominal", &*nominal, 1);
    }
    ImPlot::EndPlot();
}

/**
 * @brief   The run shown in the other tabs, as a run number, or none without a stepped result to take it from.
 */
std::optional<double> MonteCarloTab::GetShownRun() const {
    if (const auto &steps = m_Results.GetOperatingPointSteps(); steps && steps->Parameter == MONTE_CARLO_PARAMETER) {
        return steps->Values[steps->Selected];
    }
    if (const auto &steps = m_Results.GetTransientSteps(); steps && steps->Parameter == MONTE_CARLO_PARAMETER) {
        return steps->Values[steps->Selected];
    }
    if (const auto &steps = m_Results.GetACSweepSteps(); steps && steps->Parameter == MONTE_CARLO_PARAMETER) {
        return steps->Values[steps->Selected];
    }
    return std::nullopt;
}

/**
 * @brief   Shows a run in the plots and the schematic of the analysis, when its traces were kept.
 */
void MonteCarloTab::ShowRun(const std::size_t run) {
    const auto find_step = [run](const auto &steps) -> std::optional<std::size_t> {
        if (!steps || steps->Parameter != MONTE_CARLO_PARAMETER) {
            return std::nullopt;
        }
        const auto found = std::ranges::find(steps->Values, static_cast<double>(run));
        return found == steps->Values.end() ? std::nullopt : std::optional<std::size_t>(found - steps->Values.begin());
    };
    if (const std::optional<std::size_t> point_step = find_step(m_Results.GetOperatingPointSteps())) {
        m_Results.SelectOperatingPointStep(*point_step);
    } else if (const std::optional<std::size_t> step = find_step(m_Results.GetTransientSteps())) {
        m_Results.SelectTransientStep(*step);
    } else if (const std::optional<std::size_t> ac_step = find_step(m_Results.GetACSweepSteps())) {
        m_Results.SelectACSweepStep(*ac_step);
    }
}

/**
 * @brief   One row per run with the values its parts took and the quantity shown; the header sorts, and a click shows
 *          the run in the operating point, transient or AC sweep tab, and on the schematic.
 * @details Runs past the first ones kept no traces, and the DC sweep draws every run, so those rows only list.
 */
void MonteCarloTab::DrawRunTable(const MonteCarloTable &table, const std::size_t column) {
    const int column_count = static_cast<int>(table.Parts.size()) + 2;
    const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("##monte_carlo_runs", column_count, flags,
                           ImVec2(0.0f, ImGui::GetFontSize() * RUN_TABLE_HEIGHT))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Run", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed);
    for (const std::string &part : table.Parts) {
        ImGui::TableSetupColumn(part.c_str());
    }
    ImGui::TableSetupColumn(GetMeasureName(table.Columns[column].Kind));
    ImGui::TableHeadersRow();
    if (ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
        const auto index = static_cast<std::size_t>(specs->Specs[0].ColumnIndex);
        const std::size_t measure_index = table.Parts.size() + 1;
        m_Sort.By = index == 0               ? RunSort::Key::RUN
                    : index == measure_index ? RunSort::Key::MEASURE
                                             : RunSort::Key::PART;
        m_Sort.Index = index == measure_index ? column : index - 1;
        m_Sort.IsAscending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
    }
    const std::vector<std::size_t> order = OrderRuns(table, m_Sort);
    const std::optional<double> shown = GetShownRun();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(order.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::size_t run = order[static_cast<std::size_t>(row)];
            const MonteCarloRun &entry = table.Runs[run];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string label = run == 0 ? std::string("Nominal") : std::to_string(run);
            if (ImGui::Selectable(label.c_str(), shown && *shown == static_cast<double>(run),
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                ShowRun(run);
            }
            for (std::size_t part = 0; part < entry.Values.size(); ++part) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(FormatQuantity(entry.Values[part], table.Units[part].c_str()).c_str());
            }
            ImGui::TableNextColumn();
            if (entry.IsFailed) {
                ImGui::TextColored(GetErrorTextColor(), "Failed");
                ImGui::SetItemTooltip("%s", entry.Error.c_str());
            } else {
                const std::string text =
                    FormatMeasure(entry.Measures[column], GetMeasureUnit(table.Columns[column].Kind));
                ImGui::TextUnformatted(text.c_str());
            }
        }
    }
    ImGui::EndTable();
}

/**
 * @brief   The nominal reading and what the drawn runs add up to: their count, mean, standard deviation, extremes and
 *          the 1 % and 99 % percentiles.
 */
void MonteCarloTab::DrawStatistics(const MonteCarloTable &table, const std::size_t column) {
    const char *unit = GetMeasureUnit(table.Columns[column].Kind);
    ImGui::TextDisabled("%s", LabelColumn(table.Columns[column]).c_str());
    if (!BeginStatisticsTable("monte_carlo_statistics")) {
        return;
    }
    DrawStatistic("Nominal", FormatMeasure(table.Runs.front().Measures[column], unit));
    const std::vector<double> samples = CollectMeasure(table, column);
    if (const std::optional<SampleStatistics> statistics = ComputeSampleStatistics(samples)) {
        DrawStatistic("Runs", std::to_string(statistics->Count));
        DrawStatistic("Mean", FormatQuantity(statistics->Mean, unit));
        DrawStatistic("Std. deviation", FormatQuantity(statistics->StandardDeviation, unit));
        DrawStatistic("Minimum", FormatQuantity(statistics->Minimum, unit));
        DrawStatistic("Maximum", FormatQuantity(statistics->Maximum, unit));
        DrawStatistic("1 % percentile", FormatQuantity(statistics->Percentile1, unit));
        DrawStatistic("99 % percentile", FormatQuantity(statistics->Percentile99, unit));
    }
    ImGui::EndTable();
}

} // namespace GUI
