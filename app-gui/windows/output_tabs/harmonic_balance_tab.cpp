/**
 * @file    harmonic_balance_tab.cpp
 * @brief   The Harmonic balance tab of the Output window: harmonic spectra, reconstructed periodic waveforms and THD.
 */

#include "windows/output_tabs/harmonic_balance_tab.h"

#include "base/node_colors.h"
#include "base/theme.h"
#include "imgui.h"
#include "implot.h"
#include "plots/plot_figures.h"
#include "plots/plot_helpers.h"
#include "schematic/probing.h"
#include "spice/spice_value.h"
#include "windows/output_tabs/plot_widgets.h"
#include <algorithm>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

namespace {

constexpr const char *NO_RESULT = "No harmonic balance analysis for the current circuit; select Nodalys in Preferences "
                                  "and run Harmonic balance";
constexpr float CYCLES_COMBO_WIDTH = 4.5f;
constexpr float TRACE_PICKER_WIDTH = 10.0f;
constexpr float SUMMARY_TABLE_HEIGHT = 7.5f;
constexpr float HARMONICS_TABLE_HEIGHT = 18.0f;
constexpr float STEM_LINE_WEIGHT = 2.0f;
constexpr float STEM_MARKER_SIZE = 4.0f;

std::vector<std::string> ListMeasuredCurrentNames(const std::vector<Core::ComponentTrace> &currents,
                                                  const Schematic &schematic) {
    return ListMeasuredCurrents(currents, schematic);
}

ImPlotSpec BuildStemSpec(const ImU32 color, const ImPlotMarker marker) {
    ImPlotSpec spec;
    spec.LineColor = ImGui::ColorConvertU32ToFloat4(color);
    spec.LineWeight = STEM_LINE_WEIGHT;
    spec.Marker = marker;
    spec.MarkerSize = STEM_MARKER_SIZE;
    spec.MarkerLineColor = spec.LineColor;
    spec.MarkerFillColor = spec.LineColor;
    return spec;
}

const char *GetVoltageSpectrumAxisLabel(const HarmonicAmplitudeUnit amp_unit, const HarmonicScale scale) {
    switch (scale) {
    case HarmonicScale::LINEAR:
        return amp_unit == HarmonicAmplitudeUnit::RMS ? "Voltage (RMS)" : "Voltage (Peak)";
    case HarmonicScale::DECIBEL:
        return "Voltage (dBV)";
    case HarmonicScale::DBC:
        return "Voltage (dBc)";
    default:
        return "Voltage";
    }
}

const char *GetCurrentSpectrumAxisLabel(const HarmonicAmplitudeUnit amp_unit, const HarmonicScale scale) {
    switch (scale) {
    case HarmonicScale::LINEAR:
        return amp_unit == HarmonicAmplitudeUnit::RMS ? "Current (RMS)" : "Current (Peak)";
    case HarmonicScale::DECIBEL:
        return "Current (dBA)";
    case HarmonicScale::DBC:
        return "Current (dBc)";
    default:
        return "Current";
    }
}

void DrawSpectrumTooltip(const Core::HarmonicBalance &result, Schematic &schematic,
                         const HarmonicAmplitudeUnit amp_unit) {
    if (!ImPlot::IsPlotHovered() || result.Frequencies.empty()) {
        return;
    }
    const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
    const std::size_t bin = FindNearestSample(result.Frequencies, mouse.x, false);
    const std::vector<HarmonicTraceSummary> summaries = BuildHarmonicSummaries(result, schematic, amp_unit);
    if (summaries.empty()) {
        return;
    }
    ImGui::BeginTooltip();
    ImGui::Text("h%zu  %sHz", bin, Core::FormatValue(result.Frequencies[bin]).c_str());
    for (const HarmonicTraceSummary &summary : summaries) {
        if (bin >= summary.Rows.size()) {
            continue;
        }
        const HarmonicTableRow &row = summary.Rows[bin];
        ImGui::Text("%s: %s (%.2f dBc, %.1f deg)", summary.Label.c_str(),
                    FormatQuantity(row.MagnitudeLinear, summary.Unit.c_str()).c_str(), row.MagnitudeDbc,
                    row.PhaseDegrees);
    }
    ImGui::EndTooltip();
}

} // namespace

/**
 * @brief   Creates the tab for a schematic and user preferences.
 * @param[in]     schematic    Schematic that keeps the measurements and the names of nets; must outlive the tab.
 * @param[in]     results      Results holding the harmonic balance result; must outlive the tab.
 * @param[in,out] preferences  User preferences storing the Peak/RMS amplitude convention; must outlive the tab.
 */
HarmonicBalanceTab::HarmonicBalanceTab(Schematic &schematic, const ResultStore &results, Preferences &preferences)
    : m_Schematic(schematic), m_Results(results), m_Preferences(preferences) {
}

/**
 * @brief   Draws the controls, stacked spectrum and waveform plots, and DC/THD harmonic tables.
 * @param[in,out] exporter  Plot/CSV exporter handling the save dialog for the CSV button.
 */
void HarmonicBalanceTab::Draw(PlotExporter &exporter) {
    const std::optional<Core::HarmonicBalance> &result = m_Results.GetHarmonicBalance();
    if (!result) {
        ImGui::TextDisabled("%s", NO_RESULT);
        return;
    }
    const std::size_t harmonic_count = result->Frequencies.empty() ? 0 : result->Frequencies.size() - 1;
    ImGui::TextDisabled("%s", std::format("Fundamental {}Hz, {} harmonics, {} samples ({}, {} iterations)",
                                          Core::FormatValue(result->FundamentalFrequency), harmonic_count,
                                          result->Times.size(), result->Strategy, result->Iterations)
                                  .c_str());
    for (const std::string &warning : result->Warnings) {
        ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
    }

    const bool has_voltages = HasMeasuredVoltage(result->NodePhasors.size(), m_Schematic);
    const bool has_currents = HasMeasuredCurrent(result->CurrentWaveforms, m_Schematic);
    DrawControls(*result, has_voltages || has_currents, exporter);

    DrawWithStatistics(
        [&] {
            if (!has_voltages && !has_currents) {
                ImGui::TextDisabled("%s", NOTHING_MEASURED);
            }
            const float height = PlotHeight(2);
            DrawSpectrumPlot(*result, has_voltages, has_currents, height);
            DrawWaveformPlot(*result, has_voltages, has_currents, height);
        },
        [&] { DrawStatistics(*result); });
}

/**
 * @brief   Tells whether the toolbar Export button can export the reconstructed waveform figure.
 * @return  True when a harmonic balance result exists and at least one voltage or current is measured.
 */
bool HarmonicBalanceTab::CanExport() const {
    const std::optional<Core::HarmonicBalance> &result = m_Results.GetHarmonicBalance();
    if (!result || result->Times.empty()) {
        return false;
    }
    return HasMeasuredVoltage(result->NodeWaveforms.size(), m_Schematic) ||
           HasMeasuredCurrent(result->CurrentWaveforms, m_Schematic);
}

/**
 * @brief   Builds the exportable figure of the reconstructed periodic waveforms across the chosen cycle count.
 * @return  Figure for SVG or CSV export, or an empty figure when no result is present.
 */
ExportFigure HarmonicBalanceTab::BuildFigure() const {
    const std::optional<Core::HarmonicBalance> &result = m_Results.GetHarmonicBalance();
    if (!result) {
        return {};
    }
    const std::optional<ExportRange> view =
        m_WaveformView.To > m_WaveformView.From
            ? std::optional<ExportRange>({.From = m_WaveformView.From, .To = m_WaveformView.To})
            : std::nullopt;
    return BuildHarmonicBalanceFigure(*result, m_Schematic, m_Cycles, view);
}

void HarmonicBalanceTab::DrawControls(const Core::HarmonicBalance &result, const bool has_measured,
                                      PlotExporter &exporter) {
    ImGui::TextUnformatted("Amplitudes:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Peak##hb_peak", m_Preferences.HarmonicAmplitudes == HarmonicAmplitudeUnit::PEAK)) {
        m_Preferences.HarmonicAmplitudes = HarmonicAmplitudeUnit::PEAK;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("RMS##hb_rms", m_Preferences.HarmonicAmplitudes == HarmonicAmplitudeUnit::RMS)) {
        m_Preferences.HarmonicAmplitudes = HarmonicAmplitudeUnit::RMS;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextUnformatted("Scale:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Linear##hb_lin", m_Scale == HarmonicScale::LINEAR)) {
        m_Scale = HarmonicScale::LINEAR;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("dB##hb_db", m_Scale == HarmonicScale::DECIBEL)) {
        m_Scale = HarmonicScale::DECIBEL;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("dBc##hb_dbc", m_Scale == HarmonicScale::DBC)) {
        m_Scale = HarmonicScale::DBC;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * CYCLES_COMBO_WIDTH);
    if (ImGui::BeginCombo("Periods##hb_cycles", std::format("{}", m_Cycles).c_str())) {
        for (int candidate = 1; candidate <= MAX_HARMONIC_WAVEFORM_CYCLES; ++candidate) {
            if (ImGui::Selectable(std::format("{}", candidate).c_str(), m_Cycles == candidate)) {
                m_Cycles = candidate;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!has_measured);
    if (ImGui::Button("Export CSV...")) {
        exporter.StartText(BuildHarmonicBalanceCsv(result, m_Schematic, m_Preferences.HarmonicAmplitudes),
                           m_Schematic.GetFilePath(), "harmonic-balance");
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Save DC, THD, harmonic spectra and one-cycle waveforms as a CSV table");
}

void HarmonicBalanceTab::DrawSpectrumPlot(const Core::HarmonicBalance &result, const bool has_voltages,
                                          const bool has_currents, const float height) {
    if (result.Frequencies.empty()) {
        return;
    }
    const std::size_t version = m_Results.GetHarmonicBalanceVersion();
    const bool scale_changed = std::exchange(m_DrawnScale, m_Scale) != m_Scale;
    const bool unit_changed =
        std::exchange(m_DrawnAmplitudeUnit, m_Preferences.HarmonicAmplitudes) != m_Preferences.HarmonicAmplitudes;
    const bool new_result = m_SpectrumFit.TakeNewResult(version) || scale_changed || unit_changed;
    const bool nodes_changed =
        m_SpectrumFit.TakeNodesChanged(ListMeasuredNodes(result.NodePhasors.size(), m_Schematic));
    const bool currents_changed =
        m_SpectrumFit.TakeCurrentsChanged(ListMeasuredCurrentNames(result.CurrentWaveforms, m_Schematic));
    SetNextFits(new_result, nodes_changed, currents_changed);

    if (!BeginTracePlot("##hb_spectrum", ImVec2(-1.0f, height))) {
        return;
    }
    ImPlot::SetupAxes("Frequency", GetVoltageSpectrumAxisLabel(m_Preferences.HarmonicAmplitudes, m_Scale),
                      ImPlotAxisFlags_None, VoltageAxisFlags(has_voltages, has_currents));
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
    if (m_Scale == HarmonicScale::LINEAR) {
        ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>("V"));
    }
    if (has_currents) {
        ImPlot::SetupAxis(ImAxis_Y2, GetCurrentSpectrumAxisLabel(m_Preferences.HarmonicAmplitudes, m_Scale),
                          ImPlotAxisFlags_AuxDefault);
        if (m_Scale == HarmonicScale::LINEAR) {
            ImPlot::SetupAxisFormat(ImAxis_Y2, FormatAxisValue, const_cast<char *>("A"));
        }
    }

    const double stem_ref = m_Scale == HarmonicScale::LINEAR ? 0.0 : HARMONIC_DECIBEL_FLOOR;
    const int count = static_cast<int>(result.Frequencies.size());
    for (std::size_t node = 1; node < result.NodePhasors.size(); ++node) {
        if (!m_Schematic.IsVoltageMeasured(static_cast<int>(node))) {
            continue;
        }
        const std::vector<double> values =
            ListSpectrumValues(result.NodePhasors[node], m_Preferences.HarmonicAmplitudes, m_Scale);
        const int stem_count = std::min(count, static_cast<int>(values.size()));
        if (stem_count <= 0) {
            continue;
        }
        const std::string label = GetVoltageLabel(m_Schematic.GetConnectivity(), static_cast<int>(node));
        ImPlot::PlotStems(label.c_str(), result.Frequencies.data(), values.data(), stem_count, stem_ref,
                          BuildStemSpec(GetNodeColor(static_cast<int>(node)), ImPlotMarker_Circle));
    }
    if (has_currents) {
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
        for (std::size_t index = 0; index < result.CurrentPhasors.size(); ++index) {
            if (!m_Schematic.IsCurrentMeasured(result.CurrentPhasors[index].Name)) {
                continue;
            }
            const std::vector<double> values =
                ListSpectrumValues(result.CurrentPhasors[index].Phasors, m_Preferences.HarmonicAmplitudes, m_Scale);
            const int stem_count = std::min(count, static_cast<int>(values.size()));
            if (stem_count <= 0) {
                continue;
            }
            const std::string label = std::format("I({})", result.CurrentPhasors[index].Name);
            ImPlot::PlotStems(label.c_str(), result.Frequencies.data(), values.data(), stem_count, stem_ref,
                              BuildStemSpec(GetCurrentColor(index), ImPlotMarker_Square));
        }
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
    }
    DrawSpectrumTooltip(result, m_Schematic, m_Preferences.HarmonicAmplitudes);
    ImPlot::EndPlot();
}

void HarmonicBalanceTab::DrawWaveformPlot(const Core::HarmonicBalance &result, const bool has_voltages,
                                          const bool has_currents, const float height) {
    const HarmonicWaveformSet waveforms = BuildHarmonicWaveforms(result, m_Schematic, m_Cycles);
    if (waveforms.Times.empty()) {
        return;
    }
    const bool cycles_changed = std::exchange(m_DrawnCycles, m_Cycles) != m_Cycles;
    const bool new_result = m_WaveformFit.TakeNewResult(m_Results.GetHarmonicBalanceVersion()) || cycles_changed;
    const bool nodes_changed =
        m_WaveformFit.TakeNodesChanged(ListMeasuredNodes(result.NodeWaveforms.size(), m_Schematic));
    const bool currents_changed =
        m_WaveformFit.TakeCurrentsChanged(ListMeasuredCurrentNames(result.CurrentWaveforms, m_Schematic));
    SetNextFits(new_result, nodes_changed, currents_changed);

    if (!BeginTracePlot("##hb_waveform", ImVec2(-1.0f, height))) {
        return;
    }
    ImPlot::SetupAxes("Time", "Voltage", ImPlotAxisFlags_None, VoltageAxisFlags(has_voltages, has_currents));
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("s"));
    ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>("V"));
    if (has_currents) {
        ImPlot::SetupAxis(ImAxis_Y2, "Current", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisFormat(ImAxis_Y2, FormatAxisValue, const_cast<char *>("A"));
    }

    // Keep waveforms immutable after plotting so DrawCursorReadout() pointers stay valid without reallocation.
    std::vector<PlottedTrace> plotted;
    plotted.reserve(waveforms.Traces.size());
    const int count = static_cast<int>(waveforms.Times.size());
    bool on_current_axis = false;
    for (const HarmonicWaveformTrace &trace : waveforms.Traces) {
        if (trace.IsCurrent) {
            if (!has_currents) {
                continue;
            }
            if (!on_current_axis) {
                ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
                on_current_axis = true;
            }
            const ImU32 color = GetCurrentColor(trace.SourceIndex);
            PlotDashedLine(trace.Label, waveforms.Times, trace.Values, color);
            plotted.push_back({trace.Label, &trace.Values, "A", color});
        } else {
            const ImU32 color = GetNodeColor(static_cast<int>(trace.SourceIndex));
            ImPlotSpec spec;
            spec.LineColor = ImGui::ColorConvertU32ToFloat4(color);
            ImPlot::PlotLine(trace.Label.c_str(), waveforms.Times.data(), trace.Values.data(), count, spec);
            plotted.push_back({trace.Label, &trace.Values, "V", color});
        }
    }
    if (on_current_axis) {
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
    }
    const ImPlotRect limits = ImPlot::GetPlotLimits();
    m_WaveformView = {.From = limits.X.Min, .To = limits.X.Max};
    DrawCursorReadout(waveforms.Times, "s", false, plotted);
    ImPlot::EndPlot();
}

void HarmonicBalanceTab::DrawStatistics(const Core::HarmonicBalance &result) {
    const std::vector<HarmonicTraceSummary> summaries =
        BuildHarmonicSummaries(result, m_Schematic, m_Preferences.HarmonicAmplitudes);
    if (summaries.empty()) {
        ImGui::TextDisabled("Measure a voltage or current with the Probe tool to inspect harmonics and THD");
        return;
    }

    ImGui::TextUnformatted("Summary");
    const ImGuiTableFlags summary_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
    const float font_size = ImGui::GetFontSize();
    if (ImGui::BeginTable("##hb_summary", 3, summary_flags, ImVec2(0.0f, font_size * SUMMARY_TABLE_HEIGHT))) {
        ImGui::TableSetupColumn("Signal");
        ImGui::TableSetupColumn("DC");
        ImGui::TableSetupColumn("THD");
        ImGui::TableHeadersRow();
        for (const HarmonicTraceSummary &summary : summaries) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(summary.Label.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FormatQuantity(summary.DcValue, summary.Unit.c_str()).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.2f%%", summary.ThdPercent);
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    m_SelectedTableTrace = std::min(m_SelectedTableTrace, summaries.size() - 1);
    ImGui::SetNextItemWidth(font_size * TRACE_PICKER_WIDTH);
    if (ImGui::BeginCombo("Harmonics##hb_trace", summaries[m_SelectedTableTrace].Label.c_str())) {
        for (std::size_t index = 0; index < summaries.size(); ++index) {
            if (ImGui::Selectable(summaries[index].Label.c_str(), index == m_SelectedTableTrace)) {
                m_SelectedTableTrace = index;
            }
        }
        ImGui::EndCombo();
    }

    const HarmonicTraceSummary &shown = summaries[m_SelectedTableTrace];
    const std::string mag_header =
        std::format("Mag ({})", GetHarmonicAmplitudeUnitName(m_Preferences.HarmonicAmplitudes));
    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##hb_harmonics", 5, table_flags, ImVec2(0.0f, font_size * HARMONICS_TABLE_HEIGHT))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("h");
        ImGui::TableSetupColumn("Freq");
        ImGui::TableSetupColumn(mag_header.c_str());
        ImGui::TableSetupColumn("dBc");
        ImGui::TableSetupColumn("Phase");
        ImGui::TableHeadersRow();
        for (const HarmonicTableRow &row : shown.Rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%zu", row.Order);
            ImGui::TableNextColumn();
            ImGui::Text("%sHz", Core::FormatValue(row.FrequencyHertz).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FormatQuantity(row.MagnitudeLinear, shown.Unit.c_str()).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", row.MagnitudeDbc);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f deg", row.PhaseDegrees);
        }
        ImGui::EndTable();
    }
}

} // namespace GUI
