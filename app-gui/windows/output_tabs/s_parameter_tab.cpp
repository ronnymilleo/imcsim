/**
 * @file    s_parameter_tab.cpp
 * @brief   The S-parameters tab of the Output window: the Smith chart and the magnitude and phase of the last
 *          S-parameter analysis, with the Touchstone export.
 */

#include "windows/output_tabs/s_parameter_tab.h"

#include "drawing/theme.h"
#include "imgui.h"
#include "implot.h"
#include "plots/smith_chart.h"
#include "spice/spice_value.h"
#include "windows/output_tabs/plot_widgets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <expected>
#include <format>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

namespace {

/// How near the pointer must be to a point of a curve of the Smith chart, in pixels, to read it.
constexpr float HOVER_DISTANCE = 16.0f;
/// Radius, in pixels, of the ring around the point being read and of the dot at the first frequency.
constexpr float READING_RING_RADIUS = 7.0f;
constexpr float START_MARKER_SIZE = 5.0f;
constexpr float CURVE_WEIGHT = 2.0f;
constexpr float GRID_ALPHA = 0.35f;
constexpr float OUTER_CIRCLE_ALPHA = 0.7f;
/// Distance, in pixels, of a label of the grid from the point it names.
constexpr float LABEL_OFFSET = 10.0f;
/// Room around the unit circle at first, and when a curve leaves it.
constexpr double SMITH_MARGIN = 1.12;
/// Room taken by the axis labels beside and under the chart, in font sizes, which the fit leaves out.
constexpr float PLOT_SIDE_MARGIN = 3.0f;
constexpr float PLOT_BOTTOM_MARGIN = 5.0f;
/// Width of the combos of the Touchstone export, in font sizes.
constexpr float COMBO_WIDTH = 6.0f;
constexpr const char *TOUCHSTONE_POPUP = "touchstone_options";

ImVec4 ToImVec4(const ExportColor color) {
    return {static_cast<float>(color.Red) / 255.0f, static_cast<float>(color.Green) / 255.0f,
            static_cast<float>(color.Blue) / 255.0f, 1.0f};
}

/**
 * @struct  GridLine
 * @brief   A curve of the Smith grid as the arrays ImPlot reads.
 */
struct GridLine {
    std::vector<double> Xs;
    std::vector<double> Ys;
    SmithCurve Curve;
};

/**
 * @brief   The grid never changes, so its curves are made once.
 */
const std::vector<GridLine> &GetGridLines() {
    static const std::vector<GridLine> lines = [] {
        std::vector<GridLine> built;
        for (SmithCurve &curve : BuildSmithGrid(SMITH_CURVE_SEGMENTS)) {
            GridLine line;
            for (const SmithPoint &point : curve.Points) {
                line.Xs.push_back(point.X);
                line.Ys.push_back(point.Y);
            }
            line.Curve = std::move(curve);
            built.push_back(std::move(line));
        }
        return built;
    }();
    return lines;
}

void PlotGrid() {
    const std::vector<GridLine> &lines = GetGridLines();
    for (std::size_t index = 0; index < lines.size(); ++index) {
        ImPlotSpec spec;
        spec.LineColor = ImVec4(0.55f, 0.55f, 0.55f, index == 0 ? OUTER_CIRCLE_ALPHA : GRID_ALPHA);
        spec.LineWeight = 1.0f;
        ImPlot::PlotLine(std::format("##smith_grid_{}", index).c_str(), lines[index].Xs.data(), lines[index].Ys.data(),
                         static_cast<int>(lines[index].Xs.size()), spec);
    }
    const std::array<double, 2> real_axis_xs = {-1.0, 1.0};
    const std::array<double, 2> real_axis_ys = {0.0, 0.0};
    ImPlotSpec axis_spec;
    axis_spec.LineColor = ImVec4(0.55f, 0.55f, 0.55f, GRID_ALPHA);
    ImPlot::PlotLine("##smith_real_axis", real_axis_xs.data(), real_axis_ys.data(), 2, axis_spec);
    for (const GridLine &line : lines) {
        if (line.Curve.Label.empty()) {
            continue;
        }
        // A resistance label sits just above the axis where its circle crosses it; a reactance label sits outside the
        // outer circle, along the radius
        ImVec2 offset(0.0f, -LABEL_OFFSET);
        if (!line.Curve.IsResistance) {
            const auto radius = static_cast<float>(std::hypot(line.Curve.LabelPosition.X, line.Curve.LabelPosition.Y));
            offset = ImVec2(static_cast<float>(line.Curve.LabelPosition.X) / radius * LABEL_OFFSET,
                            -static_cast<float>(line.Curve.LabelPosition.Y) / radius * LABEL_OFFSET);
        }
        ImPlot::PlotText(line.Curve.Label.c_str(), line.Curve.LabelPosition.X, line.Curve.LabelPosition.Y, offset);
    }
}

/**
 * @brief   Joins the readings of the entries at one frequency into the text of a tooltip.
 */
std::string JoinReadings(const Core::SParameters &result, const std::size_t frequency,
                         const std::vector<SParameterEntry> &entries) {
    std::string text;
    for (const SParameterEntry &entry : entries) {
        for (const std::string &line : DescribeReading(ReadEntry(result, frequency, entry))) {
            text += (text.empty() ? "" : "\n") + line;
        }
        text += "\n";
    }
    if (!text.empty()) {
        text.pop_back();
    }
    return text;
}

/**
 * @brief   The farthest a curve is from the origin, which sets how much of the plane the chart shows.
 */
double FindLargestMagnitude(const Core::SParameters &result, const std::vector<SParameterEntry> &entries) {
    double largest = 1.0;
    for (const SParameterEntry &entry : entries) {
        for (const std::complex<double> value : ListValues(result, entry)) {
            largest = std::max(largest, std::abs(value));
        }
    }
    return largest;
}

} // namespace

/**
 * @brief   Creates the tab.
 * @param[in] schematic  Schematic whose S-parameter result is shown; it must outlive the tab.
 */
SParameterTab::SParameterTab(Schematic &schematic) : m_Schematic(schematic) {
}

/**
 * @brief   Draws the tab: what was analyzed, the controls, and the Smith chart or the magnitude and phase plots; or how
 *          to get a result.
 * @param[in,out] exporter  Saves the Touchstone file when the user asks for it.
 */
void SParameterTab::Draw(PlotExporter &exporter) {
    const std::optional<Core::SParameters> &result = m_Schematic.GetSParameters();
    if (!result) {
        ImGui::TextDisabled("No S-parameters for the current circuit; pick S-parameters beside Run in the editor");
        return;
    }
    if (const std::size_t version = m_Schematic.GetSParametersVersion(); m_FittedVersion != version) {
        m_FittedVersion = version;
        m_IsFitting = true;
        if (m_Selection.GetPortCount() != result->GetPortCount()) {
            m_Selection.Reset(result->GetPortCount());
            m_SmithSelection.ResetToReflections(result->GetPortCount());
        }
        m_View = {.From = result->Frequencies.front(), .To = result->Frequencies.back()};
    }
    DrawControls(*result, exporter);
    if (GetShownSelection().ListShown().empty()) {
        ImGui::TextDisabled("Check an entry of the matrix to plot it");
        return;
    }
    if (m_IsSmith) {
        DrawSmith(*result);
    } else {
        DrawMagnitudePhase(*result);
    }
}

/**
 * @brief   Tells whether the tab has something to export.
 * @return  True with a result and at least one entry checked.
 */
bool SParameterTab::CanExport() const {
    return m_Schematic.GetSParameters().has_value() && !GetShownSelection().ListShown().empty();
}

/**
 * @brief   The figure of the checked entries for the export.
 * @param[in] is_table  True for the table, which lists the real and imaginary parts whatever the view; false for the
 *                      image of the plot shown.
 * @return  The Smith chart or the magnitude and phase panels, or an empty figure without a result.
 */
ExportFigure SParameterTab::BuildFigure(const bool is_table) const {
    const std::optional<Core::SParameters> &result = m_Schematic.GetSParameters();
    if (!result) {
        return {};
    }
    const std::vector<SParameterEntry> entries = GetShownSelection().ListShown();
    if (is_table || m_IsSmith) {
        return BuildSmithFigure(*result, entries);
    }
    return BuildMagnitudePhaseFigure(*result, entries, ExportRange{.From = m_View.From, .To = m_View.To});
}

/**
 * @brief   Says what was analyzed, warns about what makes it doubtful, and offers the view, the entries and the
 *          Touchstone file.
 */
void SParameterTab::DrawControls(const Core::SParameters &result, PlotExporter &exporter) {
    ImGui::TextUnformatted(std::format("S-parameters of {} ports, {}Hz to {}Hz, {} points", result.GetPortCount(),
                                       Core::FormatValue(result.Frequencies.front()),
                                       Core::FormatValue(result.Frequencies.back()), result.Frequencies.size())
                               .c_str());
    ImGui::PushTextWrapPos(0.0f);
    for (const std::string &warning : result.Warnings) {
        ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
    }
    ImGui::PopTextWrapPos();
    if (ImGui::RadioButton("Smith chart", m_IsSmith)) {
        m_IsSmith = true;
        m_IsFitting = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Magnitude and phase", !m_IsSmith)) {
        m_IsSmith = false;
        m_IsFitting = true;
    }
    ImGui::SameLine();
    DrawTouchstoneExport(result, exporter);
    DrawSelection(result);
}

/**
 * @brief   One checkbox per entry of the matrix, a row of them for each row of the matrix.
 */
void SParameterTab::DrawSelection(const Core::SParameters &result) {
    if (!ImGui::CollapsingHeader("Entries shown", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const int port_count = result.GetPortCount();
    for (int row = 1; row <= port_count; ++row) {
        for (int column = 1; column <= port_count; ++column) {
            bool is_shown = GetShownSelection().IsShown(row, column);
            if (column > 1) {
                ImGui::SameLine();
            }
            if (ImGui::Checkbox(std::format("{}##entry_{}_{}", GetEntryName({row, column}), row, column).c_str(),
                                &is_shown)) {
                GetShownSelection().SetShown(row, column, is_shown);
                m_IsFitting = true;
            }
        }
    }
}

/**
 * @brief   The frequency unit and number format of the Touchstone file, and the button that saves it.
 * @details The file holds the whole matrix, not only the checked entries.
 */
void SParameterTab::DrawTouchstoneExport(const Core::SParameters &result, PlotExporter &exporter) {
    if (ImGui::Button("Save Touchstone...")) {
        ImGui::OpenPopup(TOUCHSTONE_POPUP);
    }
    ImGui::SetItemTooltip("Saves the whole matrix as a Touchstone .sNp file; a two-port lists S11 S21 S12 S22");
    if (ImGui::BeginPopup(TOUCHSTONE_POPUP)) {
        ImGui::TextUnformatted("Touchstone file of the whole matrix");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * COMBO_WIDTH);
        if (ImGui::BeginCombo("Frequency unit", GetTouchstoneUnitName(m_Touchstone.Unit))) {
            for (const TouchstoneFrequencyUnit unit : TOUCHSTONE_UNITS) {
                if (ImGui::Selectable(GetTouchstoneUnitName(unit), unit == m_Touchstone.Unit)) {
                    m_Touchstone.Unit = unit;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * COMBO_WIDTH);
        if (ImGui::BeginCombo("Format", GetTouchstoneFormatName(m_Touchstone.Format))) {
            for (const TouchstoneFormat format : TOUCHSTONE_FORMATS) {
                if (ImGui::Selectable(GetTouchstoneFormatName(format), format == m_Touchstone.Format)) {
                    m_Touchstone.Format = format;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("RI: real and imaginary parts. MA: magnitude and angle in degrees. DB: decibels and "
                              "angle in degrees");
        if (PrimaryButton("Save...")) {
            SaveTouchstone(result, exporter);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!m_ExportError.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(GetErrorTextColor(), "%s", m_ExportError.c_str());
        ImGui::PopTextWrapPos();
    }
}

/**
 * @brief   Writes the file in the unit and format picked and asks where to save it; a matrix that cannot be written
 *          leaves the reason under the button.
 */
void SParameterTab::SaveTouchstone(const Core::SParameters &result, PlotExporter &exporter) {
    const std::expected<std::string, std::string> text = FormatTouchstone(result, m_Touchstone);
    if (text) {
        m_ExportError.clear();
        exporter.StartTouchstone(*text, result.GetPortCount(), m_Schematic.GetFilePath(), "s-parameters");
    } else {
        m_ExportError = text.error();
    }
}

/**
 * @brief   The Smith chart: the grid of resistance and reactance, a curve per checked entry with a dot at its first
 *          frequency, and the readings of the point nearest to the pointer.
 */
void SParameterTab::DrawSmith(const Core::SParameters &result) {
    const std::vector<SParameterEntry> entries = GetShownSelection().ListShown();
    // The room the plot gets, less the axis labels; the axes are equal, so the shorter side shows the extent and the
    // longer one shows more, and the whole chart fits whatever the shape of the window
    const ImVec2 room = ImGui::GetContentRegionAvail();
    const bool is_resized = std::abs(room.x - m_FittedWidth) > 1.0f || std::abs(room.y - m_FittedHeight) > 1.0f;
    if (is_resized) {
        m_FittedWidth = room.x;
        m_FittedHeight = room.y;
        m_IsFitting = true;
    }
    const float font_size = ImGui::GetFontSize();
    const double aspect =
        std::max(room.x - font_size * PLOT_SIDE_MARGIN, 1.0f) / std::max(room.y - font_size * PLOT_BOTTOM_MARGIN, 1.0f);
    if (!ImPlot::BeginPlot("##smith_chart", ImVec2(-1.0f, -1.0f), ImPlotFlags_Equal)) {
        return;
    }
    constexpr ImPlotAxisFlags AXIS_FLAGS = ImPlotAxisFlags_NoGridLines;
    ImPlot::SetupAxes("Real", "Imaginary", AXIS_FLAGS, AXIS_FLAGS);
    ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_NoButtons);
    const double extent = std::max(SMITH_MARGIN, FindLargestMagnitude(result, entries) * 1.05);
    const ImPlotCond condition = m_IsFitting ? ImPlotCond_Always : ImPlotCond_Once;
    const double half_width = extent * std::max(1.0, aspect);
    ImPlot::SetupAxisLimits(ImAxis_X1, -half_width, half_width, condition);
    ImPlot::SetupAxisLimits(ImAxis_Y1, -extent, extent, condition);
    m_IsFitting = false;
    PlotGrid();

    for (std::size_t index = 0; index < entries.size(); ++index) {
        const std::vector<std::complex<double>> values = ListValues(result, entries[index]);
        std::vector<double> xs;
        std::vector<double> ys;
        for (const std::complex<double> value : values) {
            xs.push_back(value.real());
            ys.push_back(value.imag());
        }
        ImPlotSpec spec;
        spec.LineColor = ToImVec4(GetEntryColor(index));
        spec.LineWeight = CURVE_WEIGHT;
        ImPlot::PlotLine(GetEntryName(entries[index]).c_str(), xs.data(), ys.data(), static_cast<int>(xs.size()), spec);
        ImPlotSpec start_spec = spec;
        start_spec.Marker = ImPlotMarker_Circle;
        start_spec.MarkerSize = START_MARKER_SIZE;
        start_spec.MarkerLineColor = spec.LineColor;
        start_spec.MarkerFillColor = spec.LineColor;
        ImPlot::PlotScatter(std::format("##start_{}", index).c_str(), xs.data(), ys.data(), 1, start_spec);
    }

    if (ImPlot::IsPlotHovered()) {
        const ImVec2 pointer = ImGui::GetMousePos();
        float nearest = HOVER_DISTANCE;
        std::optional<std::pair<std::size_t, std::size_t>> hovered;
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const std::vector<std::complex<double>> values = ListValues(result, entries[index]);
            const std::size_t point =
                FindNearestReflection(values, {ImPlot::GetPlotMousePos().x, ImPlot::GetPlotMousePos().y});
            const ImVec2 mark = ImPlot::PlotToPixels(values[point].real(), values[point].imag());
            const float distance = std::hypot(mark.x - pointer.x, mark.y - pointer.y);
            if (distance < nearest) {
                nearest = distance;
                hovered = std::make_pair(index, point);
            }
        }
        if (hovered) {
            const std::complex<double> value =
                result.Get(hovered->second, entries[hovered->first].Row, entries[hovered->first].Column);
            ImPlot::GetPlotDrawList()->AddCircle(ImPlot::PlotToPixels(value.real(), value.imag()), READING_RING_RADIUS,
                                                 ImGui::GetColorU32(ToImVec4(GetEntryColor(hovered->first))), 0,
                                                 CURVE_WEIGHT);
            ImGui::SetTooltip("%s", JoinReadings(result, hovered->second, {entries[hovered->first]}).c_str());
        }
    }
    ImPlot::EndPlot();
}

/**
 * @brief   The magnitude plot over the phase plot, sharing the frequency axis.
 */
void SParameterTab::DrawMagnitudePhase(const Core::SParameters &result) {
    const std::vector<SParameterEntry> entries = GetShownSelection().ListShown();
    // Worked out once, so the plots share the room instead of the second taking what the first left
    const float height = PlotHeight(2);
    DrawBodePlot(result, entries, false, height);
    DrawBodePlot(result, entries, true, height);
    m_IsFitting = false;
}

/**
 * @brief   One of the two Bode plots; hovering it reads every checked entry at the nearest frequency.
 */
void SParameterTab::DrawBodePlot(const Core::SParameters &result, const std::vector<SParameterEntry> &entries,
                                 const bool is_phase, const float height) {
    if (m_IsFitting) {
        ImPlot::SetNextAxesToFit();
    }
    if (!BeginTracePlot(is_phase ? "Phase##s" : "Magnitude##s", ImVec2(-1.0f, height))) {
        return;
    }
    ImPlot::SetupAxes("Frequency", is_phase ? "Phase (deg)" : "Magnitude (dB)");
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
    ImPlot::SetupAxisLinks(ImAxis_X1, &m_View.From, &m_View.To);
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const std::vector<double> values =
            is_phase ? ListAnglesDegrees(result, entries[index]) : ListMagnitudesDecibels(result, entries[index]);
        ImPlotSpec spec;
        spec.LineColor = ToImVec4(GetEntryColor(index));
        spec.LineWeight = CURVE_WEIGHT;
        ImPlot::PlotLine(GetEntryName(entries[index]).c_str(), result.Frequencies.data(), values.data(),
                         static_cast<int>(values.size()), spec);
    }
    if (ImPlot::IsPlotHovered()) {
        const std::size_t frequency = FindNearestFrequency(result.Frequencies, ImPlot::GetPlotMousePos().x);
        ImGui::SetTooltip("%s", JoinReadings(result, frequency, entries).c_str());
    }
    ImPlot::EndPlot();
}

/**
 * @brief   The entries of the view shown: each view keeps its own choice.
 */
SParameterSelection &SParameterTab::GetShownSelection() {
    return m_IsSmith ? m_SmithSelection : m_Selection;
}

const SParameterSelection &SParameterTab::GetShownSelection() const {
    return m_IsSmith ? m_SmithSelection : m_Selection;
}

} // namespace GUI
