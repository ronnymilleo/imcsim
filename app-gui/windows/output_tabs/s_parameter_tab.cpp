/**
 * @file    s_parameter_tab.cpp
 * @brief   The S-parameters tab of the Output window: the Smith chart, the magnitude and phase and the stability of the
 *          last S-parameter analysis, in S, Z or Y, with the Touchstone export.
 */

#include "windows/output_tabs/s_parameter_tab.h"

#include "base/theme.h"
#include "imgui.h"
#include "implot.h"
#include "plots/stability_view.h"
#include "results/two_port_metrics.h"
#include "spice/spice_value.h"
#include "windows/output_tabs/plot_widgets.h"
#include "windows/output_tabs/s_parameter_widgets.h"
#include <array>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

namespace {

/**
 * @brief   A radio button that may be off for a reason, which its tooltip tells even while it is disabled.
 * @return  True when it was clicked.
 */
bool DrawChoice(const char *label, const bool is_active, const bool is_enabled, const std::string &tooltip) {
    ImGui::BeginDisabled(!is_enabled);
    const bool is_clicked = ImGui::RadioButton(label, is_active);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled) && !tooltip.empty()) {
        ImGui::SetTooltip("%s", tooltip.c_str());
    }
    return is_clicked && is_enabled;
}

} // namespace

/**
 * @brief   Creates the tab.
 * @param[in] schematic  Schematic that keeps the measurements and the names of nets; it must outlive the tab.
 * @param[in] results    Results whose S-parameter result is shown; they must outlive the tab.
 */
SParameterTab::SParameterTab(Schematic &schematic, const ResultStore &results)
    : m_Schematic(schematic), m_Results(results) {
}

/**
 * @brief   Draws the tab: what was analyzed, the controls, and the Smith chart, the magnitude and phase plots or the
 *          stability; or how to get a result.
 * @param[in,out] exporter  Saves the Touchstone file when the user asks for it.
 */
void SParameterTab::Draw(PlotExporter &exporter) {
    const std::optional<Core::SParameters> &result = m_Results.GetSParameters();
    if (!result) {
        ImGui::TextDisabled("No S-parameters for the current circuit; pick S-parameters beside Run in the editor");
        return;
    }
    if (const std::size_t version = m_Results.GetSParametersVersion(); m_FittedVersion != version) {
        m_FittedVersion = version;
        StartResult(*result);
    }
    const SParameterView view = GetDisplayedView(*result);
    DrawControls(*result, exporter, view);
    switch (GetBody(*result, view)) {
    case SParameterBody::NO_FREQUENCY:
        ShowWrapped(DescribeAbsentFrequencies(*result, m_Quantity));
        break;
    case SParameterBody::NOTHING_CHECKED:
        ImGui::TextDisabled("Check an entry of the matrix to plot it");
        break;
    case SParameterBody::STABILITY:
        m_Stability.Draw(m_View, m_IsFitting);
        m_IsFitting = false;
        break;
    case SParameterBody::SMITH:
        m_Smith.Draw(*result, m_Selections.Get(m_Quantity, view).ListShown(), m_Stability, m_IsFitting);
        break;
    case SParameterBody::POLAR:
        DrawPolar(*result, view);
        break;
    case SParameterBody::MAGNITUDE_PHASE:
    default:
        DrawMagnitudePhase(*result, view);
        break;
    }
}

/**
 * @brief   Tells whether the tab has something to export.
 * @return  True with a result and, unless it is the stability, at least one entry checked; false for Z or Y that exist
 *          at no frequency.
 */
bool SParameterTab::CanExport() const {
    const std::optional<Core::SParameters> &result = m_Results.GetSParameters();
    if (!result) {
        return false;
    }
    const SParameterBody body = GetBody(*result, GetDisplayedView(*result));
    return body != SParameterBody::NO_FREQUENCY && body != SParameterBody::NOTHING_CHECKED;
}

/**
 * @brief   The figure of what the tab shows, for the export.
 * @param[in] is_table  True for the table, which lists the real and imaginary parts of S, Z or Y, or the factors and
 * the gain of the stability, whatever the plot shown; false for the image of the plot shown.
 * @return  The Smith chart, with the stability circles when they are shown, the magnitude and phase panels or the
 *          stability panels; or an empty figure without a result.
 */
ExportFigure SParameterTab::BuildFigure(const bool is_table) const {
    const std::optional<Core::SParameters> &result = m_Results.GetSParameters();
    if (!result) {
        return {};
    }
    const SParameterView view = GetDisplayedView(*result);
    if (view == SParameterView::STABILITY) {
        const TwoPortSweep sweep = ComputeTwoPortSweep(*result);
        return is_table ? BuildStabilityTableFigure(sweep)
                        : BuildStabilityFigure(sweep, ExportRange{.From = m_View.From, .To = m_View.To});
    }
    const std::vector<SParameterEntry> entries = m_Selections.Get(m_Quantity, view).ListShown();
    if (view == SParameterView::POLAR && !is_table) {
        return BuildMatrixPolarFigure(*result, entries, m_Quantity);
    }
    if (m_Quantity != SParameterQuantity::S) {
        return is_table ? BuildMatrixTableFigure(*result, entries, m_Quantity)
                        : BuildMatrixMagnitudePhaseFigure(*result,
                                                          entries,
                                                          ExportRange{.From = m_View.From, .To = m_View.To},
                                                          m_Quantity);
    }
    if (view == SParameterView::SMITH && m_Smith.AreCirclesShown() && IsTwoPort(*result)) {
        return BuildSmithFigureWithCircles(*result, entries, m_Stability.GetCircleFrequency());
    }
    if (is_table || view == SParameterView::SMITH) {
        return BuildSmithFigure(*result, entries);
    }
    return BuildMagnitudePhaseFigure(*result, entries, ExportRange{.From = m_View.From, .To = m_View.To});
}

/**
 * @brief   Sets the selections, the axes and what is worked out of the result for a result that has just arrived.
 * @details The selections are kept when the number of ports is the same, so a rerun does not undo the user's choices; a
 *          quantity the new result does not carry gives way to S, and the circles go to the frequency where mu is
 *          lowest, the worst case.
 */
void SParameterTab::StartResult(const Core::SParameters &result) {
    m_IsFitting = true;
    if (m_Selections.GetPortCount() != result.GetPortCount()) {
        m_Selections.Reset(result.GetPortCount());
    }
    if (!IsQuantityAvailable(result, m_Quantity)) {
        m_Quantity = SParameterQuantity::S;
    }
    m_View = {.From = result.Frequencies.front(), .To = result.Frequencies.back()};
    m_Stability.StartResult(result);
}

/**
 * @brief   Tells which view is drawn: Z and Y have the magnitude and phase alone, and the stability needs two ports.
 * @details The choice the user made for S is kept in m_Display, so it comes back with S.
 */
SParameterView SParameterTab::GetDisplayedView(const Core::SParameters &result) const {
    if (m_Quantity != SParameterQuantity::S) {
        return m_MatrixDisplay;
    }
    if (m_Display == SParameterView::STABILITY && !IsTwoPort(result)) {
        return SParameterView::SMITH;
    }
    return m_Display;
}

/**
 * @brief   What the body draws for the view shown, as ChooseBody() decides it from the entries checked.
 */
SParameterBody SParameterTab::GetBody(const Core::SParameters &result, const SParameterView view) const {
    return ChooseBody(result, m_Quantity, view, !m_Selections.Get(m_Quantity, view).ListShown().empty());
}

/**
 * @brief   Says what was analyzed, warns about what makes it doubtful, and offers the quantity, the view, the entries
 *          and the Touchstone file, with the sentences that belong to the view.
 */
void SParameterTab::DrawControls(const Core::SParameters &result, PlotExporter &exporter, const SParameterView view) {
    DrawHeader(result);
    DrawQuantityChoice(result);
    DrawViewChoice(result, view, exporter);
    if (view == SParameterView::SMITH) {
        m_Smith.DrawCircleControls(result, m_Stability);
    }
    if ((view == SParameterView::MAGNITUDE_PHASE || view == SParameterView::POLAR)
        && m_Quantity != SParameterQuantity::S) {
        const std::string absent = DescribeAbsentFrequencies(result, m_Quantity);
        if (!absent.empty() && !AreAllFrequenciesAbsent(result, m_Quantity)) {
            ShowWrapped(absent);
        }
    }
    if (view == SParameterView::STABILITY) {
        ShowWrapped(m_Stability.GetSummary());
    } else {
        DrawSelection(result, view);
    }
}

/**
 * @brief   The line that says what was analyzed, and the warnings of the run.
 */
void SParameterTab::DrawHeader(const Core::SParameters &result) {
    ImGui::TextUnformatted(std::format("S-parameters of {} {}, {}Hz to {}Hz, {} points",
                                       result.GetPortCount(),
                                       result.GetPortCount() == 1 ? "port" : "ports",
                                       Core::FormatValue(result.Frequencies.front()),
                                       Core::FormatValue(result.Frequencies.back()),
                                       result.Frequencies.size())
                               .c_str());
    ImGui::PushTextWrapPos(0.0f);
    for (const std::string &warning : result.Warnings) {
        ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
    }
    ImGui::PopTextWrapPos();
}

/**
 * @brief   The choice between S, Z and Y; Z and Y stay in view but off when the engine gives no such matrix, and the
 *          tooltip says why.
 */
void SParameterTab::DrawQuantityChoice(const Core::SParameters &result) {
    ImGui::TextUnformatted("Quantity");
    constexpr std::array<std::pair<SParameterQuantity, const char *>, 3> CHOICES = {
        {{SParameterQuantity::S, "Scattering parameters: how much of a wave each port reflects and passes on"},
         {SParameterQuantity::Z, "Impedance matrix: the voltage at a port per current into another, the others open"},
         {SParameterQuantity::Y,
          "Admittance matrix: the current at a port per voltage on another, the others shorted"}}};
    for (const auto &[quantity, description] : CHOICES) {
        ImGui::SameLine();
        const bool is_available = IsQuantityAvailable(result, quantity);
        const std::string label(1, GetQuantityLetter(quantity));
        if (DrawChoice(label.c_str(),
                       m_Quantity == quantity,
                       is_available,
                       is_available ? std::string(description) : DescribeUnavailableQuantity(quantity))) {
            m_Quantity = quantity;
            m_IsFitting = true;
        }
    }
}

/**
 * @brief   The choice between the Smith chart, the magnitude and phase and the stability, with the Touchstone button.
 * @details The Smith chart plots reflections, so it is for S; the stability is worked out from S and needs two ports.
 *          Both stay in view but off otherwise, with the reason as tooltip.
 */
void SParameterTab::DrawViewChoice(const Core::SParameters &result, const SParameterView view, PlotExporter &exporter) {
    const bool is_s = m_Quantity == SParameterQuantity::S;
    if (DrawChoice("Smith chart",
                   view == SParameterView::SMITH,
                   is_s,
                   is_s ? "" : "A Smith chart plots reflections, so it shows S only")) {
        m_Display = SParameterView::SMITH;
        m_IsFitting = true;
    }
    ImGui::SameLine();
    if (DrawChoice("Polar",
                   view == SParameterView::POLAR,
                   true,
                   "Magnitude and phase on concentric circles and 30-degree angle spokes")) {
        if (is_s) {
            m_Display = SParameterView::POLAR;
        } else {
            m_MatrixDisplay = SParameterView::POLAR;
        }
        m_IsFitting = true;
    }
    ImGui::SameLine();
    if (DrawChoice("Magnitude and phase", view == SParameterView::MAGNITUDE_PHASE, true, "")) {
        if (is_s) {
            m_Display = SParameterView::MAGNITUDE_PHASE;
        } else {
            m_MatrixDisplay = SParameterView::MAGNITUDE_PHASE;
        }
        m_IsFitting = true;
    }
    ImGui::SameLine();
    const bool is_two_port = IsTwoPort(result);
    std::string stability_tooltip = "K, mu and the gain bound of the two-port against frequency, and where it can "
                                    "oscillate";
    if (!is_s) {
        stability_tooltip = "The stability is worked out from S, so it is shown with S only";
    } else if (!is_two_port) {
        stability_tooltip = DescribeNeedsTwoPorts(result, "The stability factors");
    }
    if (DrawChoice("Stability", view == SParameterView::STABILITY, is_s && is_two_port, stability_tooltip)) {
        m_Display = SParameterView::STABILITY;
        m_IsFitting = true;
    }
    ImGui::SameLine();
    m_Touchstone.Draw(result, exporter, m_Schematic.GetFilePath());
}

/**
 * @brief   One checkbox per entry of the matrix, a row of them for each row of the matrix.
 */
void SParameterTab::DrawSelection(const Core::SParameters &result, const SParameterView view) {
    if (!ImGui::CollapsingHeader("Entries shown", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const int port_count = result.GetPortCount();
    for (int row = 1; row <= port_count; ++row) {
        for (int column = 1; column <= port_count; ++column) {
            bool is_shown = m_Selections.Get(m_Quantity, view).IsShown(row, column);
            if (column > 1) {
                ImGui::SameLine();
            }
            if (ImGui::Checkbox(
                    std::format("{}##entry_{}_{}", GetMatrixEntryName({row, column}, m_Quantity), row, column).c_str(),
                    &is_shown)) {
                m_Selections.Get(m_Quantity, view).SetShown(row, column, is_shown);
                m_IsFitting = true;
            }
        }
    }
}

void SParameterTab::DrawPolar(const Core::SParameters &result, const SParameterView view) {
    const std::vector<SParameterEntry> entries = m_Selections.Get(m_Quantity, view).ListShown();
    const std::string unit = GetQuantityUnit(m_Quantity);
    std::vector<ComplexLocus> loci;
    loci.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const std::string name = GetMatrixEntryName(entries[index], m_Quantity);
        loci.push_back({.Name = name,
                        .LegendLabel = name,
                        .Unit = unit,
                        .Color = ImGui::GetColorU32(ToImVec4(GetEntryColor(index))),
                        .IsDashed = false,
                        .IsFaint = false,
                        .Values = ListMatrixValues(result, entries[index], m_Quantity)});
    }
    m_Polar.Draw("##sp_polar",
                 ComplexPlaneKind::POLAR,
                 loci,
                 false,
                 true,
                 m_IsFitting,
                 [&](const std::size_t locus_index, const std::size_t sample_index) {
                     return DescribeMatrixReadings(result, sample_index, {entries[locus_index]}, m_Quantity);
                 });
}

/**
 * @brief   The magnitude plot over the phase plot, sharing the frequency axis.
 */
void SParameterTab::DrawMagnitudePhase(const Core::SParameters &result, const SParameterView view) {
    const std::vector<SParameterEntry> entries = m_Selections.Get(m_Quantity, view).ListShown();
    // Worked out once, so the plots share the room instead of the second taking what the first left
    const float height = PlotHeight(2);
    DrawBodePlot(result, entries, false, height);
    DrawBodePlot(result, entries, true, height);
    m_IsFitting = false;
}

/**
 * @brief   One of the two Bode plots; hovering it reads every checked entry at the nearest frequency.
 */
void SParameterTab::DrawBodePlot(const Core::SParameters &result,
                                 const std::vector<SParameterEntry> &entries,
                                 const bool is_phase,
                                 const float height) {
    const bool is_decade_axis = m_Quantity != SParameterQuantity::S && !is_phase;
    if (m_IsFitting) {
        // The logarithmic axis is fitted in decades by SetupBodeAxes(), since the padding of the fit is linear and
        // crosses zero
        if (is_decade_axis) {
            ImPlot::SetNextAxisToFit(ImAxis_X1);
        } else {
            ImPlot::SetNextAxesToFit();
        }
    }
    if (!BeginTracePlot(is_phase ? "Phase##s" : "Magnitude##s", ImVec2(-1.0f, height), true)) {
        return;
    }
    SetupBodeAxes(result, entries, is_phase);
    PlotBodeCurves(result, entries, is_phase);
    if (ImPlot::IsPlotHovered()) {
        const std::size_t frequency = FindNearestFrequency(result.Frequencies, ImPlot::GetPlotMousePos().x);
        ImGui::SetTooltip("%s", DescribeMatrixReadings(result, frequency, entries, m_Quantity).c_str());
    }
    ImPlot::EndPlot();
}

/**
 * @brief   The frequency axis the plots share, and the magnitude or phase axis.
 * @details The magnitude of Z and Y is in ohms and siemens on a logarithmic axis, fitted in decades to the curves.
 */
void SParameterTab::SetupBodeAxes(const Core::SParameters &result,
                                  const std::vector<SParameterEntry> &entries,
                                  const bool is_phase) {
    const bool is_s = m_Quantity == SParameterQuantity::S;
    const std::string unit = GetQuantityUnit(m_Quantity);
    const std::string magnitude_label =
        is_s ? "Magnitude (dB)"
             : std::format("{} magnitude ({})", m_Quantity == SParameterQuantity::Z ? "Impedance" : "Admittance", unit);
    ImPlot::SetupAxes("Frequency", is_phase ? "Phase (deg)" : magnitude_label.c_str());
    SetupLogFrequencyAxis(&m_View);
    if (is_s || is_phase) {
        return;
    }
    ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
    std::vector<std::vector<double>> magnitudes;
    for (const SParameterEntry &entry : entries) {
        magnitudes.push_back(ListMatrixMagnitudes(result, entry, m_Quantity));
    }
    const ExportRange range = FindDecadeRange(magnitudes);
    ImPlot::SetupAxisLimits(ImAxis_Y1, range.From, range.To, m_IsFitting ? ImPlotCond_Always : ImPlotCond_Once);
    ImPlot::SetupAxisFormat(ImAxis_Y1,
                            FormatAxisValue,
                            const_cast<char *>(m_Quantity == SParameterQuantity::Z ? "Ohm" : "S"));
}

/**
 * @brief   A curve per checked entry; where the matrix does not exist the curve breaks.
 */
void SParameterTab::PlotBodeCurves(const Core::SParameters &result,
                                   const std::vector<SParameterEntry> &entries,
                                   const bool is_phase) const {
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const std::vector<double> values = is_phase ? ListMatrixAngles(result, entries[index], m_Quantity)
                                                    : ListMatrixMagnitudes(result, entries[index], m_Quantity);
        ImPlotSpec spec;
        spec.LineColor = ToImVec4(GetEntryColor(index));
        spec.LineWeight = CURVE_WEIGHT;
        ImPlot::PlotLine(GetMatrixEntryName(entries[index], m_Quantity).c_str(),
                         result.Frequencies.data(),
                         values.data(),
                         static_cast<int>(values.size()),
                         spec);
    }
}

} // namespace GUI
