/**
 * @file    ac_sensitivity_tab.cpp
 * @brief   The Sensitivity tab of the Output window for an AC sensitivity result: how the gain and the phase at the
 *          output change with the value of each part, across frequency, and a table that ranks the parts at one
 *          frequency.
 */

#include "windows/output_tabs/ac_sensitivity_tab.h"

#include "base/node_colors.h"
#include "base/theme.h"
#include "implot.h"
#include "plots/plot_figures.h"
#include "schematic/probing.h"
#include "spice/spice_value.h"
#include "windows/output_tabs/plot_widgets.h"
#include "windows/output_tabs/result_tables.h"
#include "windows/output_tabs/statistics_widgets.h"
#include <algorithm>
#include <cfloat>
#include <format>

namespace GUI {

namespace {

constexpr float MARKER_LINE_WEIGHT = 1.5f;

/// The marker that picks the frequency the table and the schematic read.
constexpr ImVec4 MARKER_COLOR = {0.95f, 0.75f, 0.2f, 1.0f};

std::string FormatFrequency(const double frequency) {
    return std::format("{}Hz", Core::FormatValue(frequency));
}

} // namespace

/**
 * @brief   Creates the tab.
 * @param[in] schematic    Schematic that names the nets and the parts a click selects; it must outlive the tab.
 * @param[in] results      Results whose AC sensitivity is shown, and which keep the frequency it is read at; they must
 *                         outlive the tab.
 * @param[in] preferences  Whose wire coloring the checkbox at the top sets; they must outlive the tab.
 */
ACSensitivityTab::ACSensitivityTab(Schematic &schematic, ResultStore &results, Preferences &preferences)
    : m_Schematic(schematic), m_Results(results), m_Preferences(preferences) {
}

/**
 * @brief   Draws the tab: the checkbox that colors the schematic, the warnings, the plots and the table.
 */
void ACSensitivityTab::Draw() {
    const auto &sensitivity = m_Results.GetACSensitivity();
    if (!sensitivity) {
        ImGui::TextDisabled("No sensitivity for the current circuit; pick it beside Run in the editor");
        return;
    }
    if (m_LoadedVersion != m_Results.GetSensitivityVersion()) {
        LoadResult(*sensitivity);
    }
    bool is_colored = m_Preferences.WireColors == WireColoring::SENSITIVITY;
    if (ImGui::Checkbox("Color the schematic", &is_colored)) {
        m_Preferences.WireColors = is_colored ? WireColoring::SENSITIVITY : WireColoring::PLAIN;
    }
    ImGui::SetItemTooltip(
        "Color the parts by their influence on the response at the marked frequency, as View > Wire Colors > "
        "Sensitivity does");
    ImGui::SameLine();
    ImGui::TextDisabled("%s",
                        std::format("{} to {}, driven by {}",
                                    FormatFrequency(sensitivity->Frequencies.front()),
                                    FormatFrequency(sensitivity->Frequencies.back()),
                                    sensitivity->InputSource)
                            .c_str());
    for (const std::string &warning : sensitivity->Warnings) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
        ImGui::PopTextWrapPos();
    }
    DrawWithStatistics([&] { DrawPlots(*sensitivity); }, [&] { DrawTable(*sensitivity); });
}

/**
 * @brief   Tells whether the tab has something to export.
 * @return  True with an AC sensitivity result.
 */
bool ACSensitivityTab::CanExport() const {
    return m_Results.GetACSensitivity().has_value();
}

/**
 * @brief   The figure of both plots for the export, over the visible frequency range, with the checked parts.
 * @return  The figure, or an empty one without an AC sensitivity result.
 */
ExportFigure ACSensitivityTab::BuildFigure() const {
    if (const std::optional<Core::ACSensitivity> &sensitivity = m_Results.GetACSensitivity()) {
        std::vector<ImU32> colors;
        for (std::size_t index = 0; index < sensitivity->Parts.size(); ++index) {
            colors.push_back(GetCurrentColor(index));
        }
        return BuildACSensitivityFigure(*sensitivity,
                                        MakeACSensitivityCurves(*sensitivity),
                                        colors,
                                        m_ShownParts,
                                        GetVoltageLabel(m_Schematic.GetConnectivity(), sensitivity->OutputNode),
                                        m_View);
    }
    return {};
}

/**
 * @brief   Starts a new result with its most influential parts checked and the axes fitted to the whole sweep.
 */
void ACSensitivityTab::LoadResult(const Core::ACSensitivity &sensitivity) {
    m_LoadedVersion = m_Results.GetSensitivityVersion();
    m_Curves = MakeACSensitivityCurves(sensitivity);
    m_ShownParts.clear();
    for (const std::size_t index : FindMostInfluential(sensitivity, PRESELECTED_PARTS)) {
        m_ShownParts.insert(sensitivity.Parts[index].Name);
    }
    // The plots link their frequency axes to this range, so it fits them to the whole sweep
    m_View = {.From = sensitivity.Frequencies.front(), .To = sensitivity.Frequencies.back()};
}

/**
 * @brief   The gain and the phase, one above the other, which share their frequency range.
 */
void ACSensitivityTab::DrawPlots(const Core::ACSensitivity &sensitivity) {
    const float height = PlotHeight(2);
    DrawPlot(sensitivity, false, height);
    DrawPlot(sensitivity, true, height);
}

/**
 * @brief   One plot: the curve of every checked part on a logarithmic frequency axis, fitted in decades to the sweep,
 *          and the marker that picks the frequency the table reads.
 * @param[in] sensitivity  The result.
 * @param[in] is_phase     Whether this is the plot of the phase, or of the gain.
 * @param[in] height       Height of the plot, in pixels.
 */
void ACSensitivityTab::DrawPlot(const Core::ACSensitivity &sensitivity, const bool is_phase, const float height) {
    if (!BeginTracePlot(is_phase ? "Phase##ac_sensitivity" : "Gain##ac_sensitivity",
                        ImVec2(-1.0f, height),
                        !is_phase)) {
        return;
    }
    ImPlot::SetupAxes("Frequency",
                      is_phase ? "Phase (deg per 1 %)" : "Gain (dB per 1 %)",
                      ImPlotAxisFlags_None,
                      ImPlotAxisFlags_AutoFit);
    SetupLogFrequencyAxis(&m_View);
    for (std::size_t index = 0; index < sensitivity.Parts.size(); ++index) {
        const Core::ACPartSensitivity &part = sensitivity.Parts[index];
        if (!m_ShownParts.contains(part.Name)) {
            continue;
        }
        const std::vector<double> &values = is_phase ? m_Curves[index].Phase : m_Curves[index].Magnitude;
        ImPlotSpec spec;
        spec.LineColor = ImGui::ColorConvertU32ToFloat4(GetCurrentColor(index));
        ImPlot::PlotLine(part.Name.c_str(),
                         sensitivity.Frequencies.data(),
                         values.data(),
                         static_cast<int>(values.size()),
                         spec);
    }
    double frequency =
        sensitivity.Frequencies[std::min(m_Results.GetACSensitivityFrequency(), sensitivity.Frequencies.size() - 1)];
    if (ImPlot::DragLineX(0, &frequency, MARKER_COLOR, MARKER_LINE_WEIGHT, ImPlotDragToolFlags_NoFit)) {
        m_Results.SetACSensitivityFrequency(FindNearestFrequencyIndex(sensitivity, frequency));
    }
    ImPlot::EndPlot();
}

/**
 * @brief   The table of every part at the marked frequency, the part that moves the response most first; a click on a
 *          name selects the part, and the checkbox in front of it shows its curve in the plots.
 */
void ACSensitivityTab::DrawTable(const Core::ACSensitivity &sensitivity) {
    DrawFrequencyChoice(sensitivity);
    const std::size_t frequency_index =
        std::min(m_Results.GetACSensitivityFrequency(), sensitivity.Frequencies.size() - 1);
    ImGui::PushTextWrapPos(0.0f);
    if (const std::optional<std::string> summary = FormatACWorstCaseSummary(sensitivity, frequency_index)) {
        ImGui::TextUnformatted(summary->c_str());
        ImGui::SetItemTooltip("Extreme adds every toleranced part's worst corner together; RSS takes the root sum of "
                              "squares for independent tolerances");
    } else {
        ImGui::TextDisabled(
            "Set tolerances on resistors, capacitors, inductors or DC voltage sources to bound the worst-case "
            "gain and phase change here without Monte Carlo sampling");
    }
    ImGui::PopTextWrapPos();
    const std::vector<ACSensitivityRow> rows = MakeACSensitivityRows(sensitivity, frequency_index);
    const std::optional<bool> show_all = DrawAllNoneButtons("ac_sensitivity_parts");
    constexpr ImGuiTableFlags TABLE_FLAGS =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("ac_sensitivity", 7, TABLE_FLAGS)) {
        ImGui::TableSetupColumn("Plot", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Part");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Gain per 1 %");
        ImGui::TableSetupColumn("Phase per 1 %");
        ImGui::TableSetupColumn("Worst case");
        ImGui::TableSetupColumn("Share of largest");
        ImGui::TableHeadersRow();
        for (std::size_t position = 0; position < rows.size(); ++position) {
            const ACSensitivityRow &row = rows[position];
            const auto part = std::ranges::find(sensitivity.Parts, row.PartName, &Core::ACPartSensitivity::Name);
            const auto part_index = static_cast<std::size_t>(part - sensitivity.Parts.begin());
            if (show_all && *show_all) {
                m_ShownParts.insert(row.PartName);
            } else if (show_all) {
                m_ShownParts.erase(row.PartName);
            }
            ImGui::PushID(static_cast<int>(position));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool is_shown = m_ShownParts.contains(row.PartName);
            ImGui::PushStyleColor(ImGuiCol_CheckMark, GetCurrentColor(part_index));
            if (ImGui::Checkbox("##shown", &is_shown)) {
                if (is_shown) {
                    m_ShownParts.insert(row.PartName);
                } else {
                    m_ShownParts.erase(row.PartName);
                }
            }
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(row.Part.c_str(), false)) {
                SelectPart(m_Schematic, row.PartName);
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.Value.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.Magnitude.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.Phase.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.Tolerance.c_str());
            ImGui::TableNextColumn();
            ImGui::ProgressBar(static_cast<float>(row.Share),
                               {-FLT_MIN, 0.0f},
                               std::format("{:.1f} %", row.Share * 100.0).c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos(0.0f);
    // The text holds percent signs, so it goes in as an argument and never as the format
    ImGui::TextDisabled("%s",
                        "Gain and phase per 1 % are the change of the response for a 1 % higher value of the "
                        "part, which compares parts of different kinds. The share is the size of the change of "
                        "the whole response, gain and phase together. A part that sets the bias of a transistor "
                        "counts through the bias as well. Drag the marker in a plot to read another "
                        "frequency");
    ImGui::PopTextWrapPos();
}

/**
 * @brief   Says which frequency the table reads, and goes back to the -3 dB point, where the corner is.
 */
void ACSensitivityTab::DrawFrequencyChoice(const Core::ACSensitivity &sensitivity) {
    const std::size_t corner = FindCornerFrequencyIndex(sensitivity);
    const std::size_t chosen = std::min(m_Results.GetACSensitivityFrequency(), sensitivity.Frequencies.size() - 1);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(std::format("Parts ranked at {}{}",
                                       FormatFrequency(sensitivity.Frequencies[chosen]),
                                       chosen == corner ? ", the -3 dB point" : "")
                               .c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(chosen == corner);
    if (ImGui::SmallButton("Go to the -3 dB point")) {
        m_Results.SetACSensitivityFrequency(corner);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Read the table at the frequency where the response has fallen 3 dB from its peak, which "
                          "is where the corner is");
}

} // namespace GUI
