/**
 * @file    noise_tab.cpp
 * @brief   The Noise tab of the Output window: the noise densities at the output and at the input, and the share of
 *          each part.
 */

#include "windows/output_tabs/noise_tab.h"

#include "base/node_colors.h"
#include "base/theme.h"
#include "implot.h"
#include "plots/plot_figures.h"
#include "schematic/probing.h"
#include "spice/spice_value.h"
#include "windows/output_tabs/plot_widgets.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <span>

namespace GUI {

namespace {

/// Six decades below the largest density, about where the share of a part stops mattering.
constexpr double DECADE_FIT_FLOOR = 1e-6;

/**
 * @brief   The range a logarithmic axis fits to: the positive values of the traces, with a tenth of their decades, and
 *          at least half a decade, of room above and below.
 * @details The first trace is always in full; values of the others far below its peak, such as the share of the ideal
 *          op-amp, are left below the axis, so they do not squash the plot.
 */
PlotSpan GetDecadeFitRange(const std::vector<std::span<const double>> &traces) {
    double highest = 0.0;
    double reference = 0.0;
    for (std::size_t index = 0; index < traces.size(); ++index) {
        for (const double value : traces[index]) {
            highest = std::max(highest, value);
            if (index == 0) {
                reference = std::max(reference, value);
            }
        }
    }
    if (highest <= 0.0) {
        return {.From = 1e-12, .To = 1.0};
    }
    double lowest = highest;
    for (std::size_t index = 0; index < traces.size(); ++index) {
        for (const double value : traces[index]) {
            if (value > 0.0 && (index == 0 || value > reference * DECADE_FIT_FLOOR)) {
                lowest = std::min(lowest, value);
            }
        }
    }
    const double decades = std::log10(highest / lowest);
    const double padding = std::max(decades * 0.1, 0.5);
    return {.From = lowest / std::pow(10.0, padding), .To = highest * std::pow(10.0, padding)};
}

std::string FormatFrequencyRange(const Core::Noise &noise) {
    return std::format("{}Hz to {}Hz", Core::FormatValue(noise.Frequencies.front()),
                       Core::FormatValue(noise.Frequencies.back()));
}

/**
 * @brief   The output in the color of its node, the shares checked in the panel, and the input-referred noise dashed.
 */
void PlotNoiseTraces(const Core::Noise &noise, const Connectivity &connectivity, const std::set<std::string> &shown,
                     const bool input_shown, const std::string &output_unit, const std::string &input_unit,
                     std::vector<PlottedTrace> &plotted) {
    const std::string output_label = GetVoltageLabel(connectivity, noise.OutputNode);
    const ImU32 output_color = GetNodeColor(noise.OutputNode);
    ImPlotSpec output_spec;
    output_spec.LineColor = ImGui::ColorConvertU32ToFloat4(output_color);
    output_spec.LineWeight = MATH_LINE_WEIGHT;
    ImPlot::PlotLine(output_label.c_str(), noise.Frequencies.data(), noise.OutputDensities.data(),
                     static_cast<int>(noise.Frequencies.size()), output_spec);
    plotted.push_back({output_label, &noise.OutputDensities, output_unit.c_str(), output_color});
    for (std::size_t index = 0; index < noise.Contributions.size(); ++index) {
        const Core::NoiseContribution &contribution = noise.Contributions[index];
        if (!shown.contains(contribution.Name)) {
            continue;
        }
        ImPlotSpec spec;
        spec.LineColor = ImGui::ColorConvertU32ToFloat4(GetCurrentColor(index));
        ImPlot::PlotLine(contribution.Name.c_str(), noise.Frequencies.data(), contribution.OutputDensities.data(),
                         static_cast<int>(noise.Frequencies.size()), spec);
        plotted.push_back(
            {contribution.Name, &contribution.OutputDensities, output_unit.c_str(), GetCurrentColor(index)});
    }
    if (!input_shown) {
        return;
    }
    const std::string input_label = std::format("Input-referred ({})", noise.InputSource);
    const ImU32 input_color = AdaptToBackground(INPUT_NOISE_COLOR);
    ImPlot::SetAxes(ImAxis_X1, noise.InputUnit == "V" ? ImAxis_Y1 : ImAxis_Y2);
    PlotDashedLine(input_label, noise.Frequencies, noise.InputDensities, input_color);
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
    plotted.push_back({input_label, &noise.InputDensities, input_unit.c_str(), input_color});
}

} // namespace

/**
 * @brief   Creates the tab.
 * @param[in] schematic  Schematic that keeps the measurements and the names of nets; it must outlive the tab.
 * @param[in] results    Results whose noise analysis is shown; they must outlive the tab.
 */
NoiseTab::NoiseTab(Schematic &schematic, const ResultStore &results) : m_Schematic(schematic), m_Results(results) {
}

/**
 * @brief   Draws the tab: the densities and the shares of the parts.
 * @details An output held by an ideal source, or cut off from every noisy part, has nothing to plot, so the tab says
 *          why.
 */
void NoiseTab::Draw() {
    const auto &noise = m_Results.GetNoise();
    if (!noise) {
        ImGui::TextDisabled("No noise analysis for the current circuit; pick it beside Run in the editor");
        return;
    }
    DrawWithStatistics(
        [&] {
            if (noise->OutputTotal > 0.0) {
                DrawPlot(*noise);
                return;
            }
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(GetWarningTextColor(), "%s",
                               std::format("No noise reaches {}: an ideal source holds it, or no part that adds "
                                           "noise drives it. Pick another output beside Run",
                                           GetVoltageLabel(m_Schematic.GetConnectivity(), noise->OutputNode))
                                   .c_str());
            ImGui::PopTextWrapPos();
        },
        [&] { DrawStatistics(*noise); });
}

/**
 * @brief   Tells whether the tab has something to export.
 * @return  True with a noise analysis.
 */
bool NoiseTab::CanExport() const {
    return m_Results.GetNoise().has_value();
}

/**
 * @brief   The figure of the densities for the export, over the visible frequency range, with the shares checked.
 * @return  The figure, or an empty one without a noise analysis.
 */
ExportFigure NoiseTab::BuildFigure() const {
    if (const std::optional<Core::Noise> &noise = m_Results.GetNoise()) {
        return BuildNoiseFigure(*noise, m_Schematic, m_ShownContributions, m_View);
    }
    return {};
}

/**
 * @brief   Densities on log-log axes, as noise is read: the output in the color of its node, and the input-referred
 *          noise dashed, on the same axis when the input is a voltage source and on the right one in amperes for a
 *          current source.
 * @details The parts checked in the panel add their share at the output.
 */
void NoiseTab::DrawPlot(const Core::Noise &noise) {
    const bool input_shown = HasInputGain(noise);
    if (!input_shown) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", std::format("{} hardly reaches {} (under -120 dB, as through a logic gate), so its "
                                              "input-referred noise is left out",
                                              noise.InputSource,
                                              GetVoltageLabel(m_Schematic.GetConnectivity(), noise.OutputNode))
                                      .c_str());
        ImGui::PopTextWrapPos();
    }
    if (!BeginTracePlot("##noise", ImVec2(-1.0f, PlotHeight(1)), true)) {
        return;
    }
    // ImPlot keeps the format strings until the plot ends
    const std::string output_unit = GetDensityUnit("V");
    const std::string input_unit = GetDensityUnit(noise.InputUnit);
    SetupAxes(noise, input_shown, output_unit, input_unit);
    std::vector<PlottedTrace> plotted;
    PlotNoiseTraces(noise, m_Schematic.GetConnectivity(), m_ShownContributions, input_shown, output_unit, input_unit,
                    plotted);
    DrawCursorReadout(noise.Frequencies, "Hz", true, plotted);
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    m_View = {.From = view.Min, .To = view.Max};
    ImPlot::EndPlot();
}

/**
 * @brief   Logarithmic axes, fitted in decades, since the padding of the other plots is linear and would reach below
 *          zero: every axis for a new result, and the output axis when the plotted shares change.
 * @details Referred to a voltage source, the input noise is in volts too and shares the axis of the output; referred to
 *          a current source, it has its own. The limits are set during the setup, since ImPlot fits an axis on its own
 *          the first frame it is shown.
 */
void NoiseTab::SetupAxes(const Core::Noise &noise, const bool input_shown, const std::string &output_unit,
                         const std::string &input_unit) {
    const bool new_result = m_Fit.TakeNewResult(m_Results.GetNoiseVersion());
    const bool shares_changed = TakeTracesChanged(
        m_PlottedContributions, std::vector<std::string>(m_ShownContributions.begin(), m_ShownContributions.end()));
    const bool input_in_volts = noise.InputUnit == "V";
    ImPlot::SetupAxes("Frequency", "Noise density", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
    ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
    ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>(output_unit.c_str()));
    if (input_shown && !input_in_volts) {
        ImPlot::SetupAxis(ImAxis_Y2, "Input-referred noise", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisScale(ImAxis_Y2, ImPlotScale_Log10);
        ImPlot::SetupAxisFormat(ImAxis_Y2, FormatAxisValue, const_cast<char *>(input_unit.c_str()));
    }
    if (new_result) {
        ImPlot::SetupAxisLimits(ImAxis_X1, noise.Frequencies.front(), noise.Frequencies.back(), ImPlotCond_Always);
        if (input_shown && !input_in_volts) {
            const PlotSpan input_range = GetDecadeFitRange({noise.InputDensities});
            ImPlot::SetupAxisLimits(ImAxis_Y2, input_range.From, input_range.To, ImPlotCond_Always);
        }
    }
    if (!new_result && !shares_changed) {
        return;
    }
    std::vector<std::span<const double>> voltage_traces{noise.OutputDensities};
    if (input_shown && input_in_volts) {
        voltage_traces.emplace_back(noise.InputDensities);
    }
    for (const Core::NoiseContribution &contribution : noise.Contributions) {
        if (m_ShownContributions.contains(contribution.Name)) {
            voltage_traces.emplace_back(contribution.OutputDensities);
        }
    }
    const PlotSpan voltage_range = GetDecadeFitRange(voltage_traces);
    ImPlot::SetupAxisLimits(ImAxis_Y1, voltage_range.From, voltage_range.To, ImPlotCond_Always);
}

/**
 * @brief   The RMS noise over the whole range, at the output and at the input, then the share of every part in the
 *          output noise, largest first; checking a part plots its share.
 */
void NoiseTab::DrawStatistics(const Core::Noise &noise) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", std::format("RMS from {}", FormatFrequencyRange(noise)).c_str());
    ImGui::PopTextWrapPos();
    if (BeginStatisticsTable("noise_totals")) {
        const std::string output_name =
            std::format("Output {}", GetVoltageLabel(m_Schematic.GetConnectivity(), noise.OutputNode));
        DrawStatistic(output_name.c_str(), FormatQuantity(noise.OutputTotal, "V"));
        const std::string input_name = std::format("Input {}", noise.InputSource);
        DrawStatistic(input_name.c_str(),
                      HasInputGain(noise) ? FormatQuantity(noise.InputTotal, noise.InputUnit.c_str()) : "-");
        ImGui::SetItemTooltip("The noise a source at the input would need to cause the same noise at the output");
        ImGui::EndTable();
    }
    ImGui::Separator();
    ImGui::TextDisabled("Share of each part at the output");
    if (noise.Contributions.empty()) {
        ImGui::TextDisabled("No part adds noise");
        return;
    }
    const std::optional<bool> show_all = DrawAllNoneButtons("contributions");
    if (!ImGui::BeginTable("noise_contributions", 3, ImGuiTableFlags_SizingStretchProp)) {
        return;
    }
    const double output_power = noise.OutputTotal * noise.OutputTotal;
    for (std::size_t index = 0; index < noise.Contributions.size(); ++index) {
        const Core::NoiseContribution &contribution = noise.Contributions[index];
        if (show_all) {
            if (*show_all) {
                m_ShownContributions.insert(contribution.Name);
            } else {
                m_ShownContributions.erase(contribution.Name);
            }
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        bool shown = m_ShownContributions.contains(contribution.Name);
        ImGui::PushStyleColor(ImGuiCol_CheckMark, GetCurrentColor(index));
        if (ImGui::Checkbox(contribution.Name.c_str(), &shown)) {
            if (shown) {
                m_ShownContributions.insert(contribution.Name);
            } else {
                m_ShownContributions.erase(contribution.Name);
            }
        }
        ImGui::PopStyleColor();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(FormatQuantity(contribution.OutputTotal, "V").c_str());
        ImGui::TableNextColumn();
        // Powers add up, so the shares of all parts make the whole
        const double share =
            output_power > 0.0 ? contribution.OutputTotal * contribution.OutputTotal / output_power : 0.0;
        ImGui::TextUnformatted(FormatFixed(share * 100.0, "%").c_str());
    }
    ImGui::EndTable();
}

} // namespace GUI
