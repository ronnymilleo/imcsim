/**
 * @file    s_parameter_tab.cpp
 * @brief   The S-parameters tab of the Output window: the Smith chart, the magnitude and phase and the stability of the
 *          last S-parameter analysis, in S, Z or Y, with the Touchstone export.
 */

#include "windows/output_tabs/s_parameter_tab.h"

#include "base/theme.h"
#include "imgui.h"
#include "implot.h"
#include "plots/smith_chart.h"
#include "results/stability_circles.h"
#include "results/stability_summary.h"
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
/// Width of the slider of the frequency of the stability circles, in font sizes.
constexpr float CIRCLE_SLIDER_WIDTH = 14.0f;
/// Opacity of the hatching of the unstable side of a circle, light so the grid and the curves show through.
constexpr float HATCH_ALPHA = 0.35f;
constexpr float CIRCLE_WEIGHT = 2.0f;
constexpr float HATCH_WEIGHT = 1.0f;

ImVec4 ToImVec4(const ExportColor color) {
    return {static_cast<float>(color.Red) / 255.0f,
            static_cast<float>(color.Green) / 255.0f,
            static_cast<float>(color.Blue) / 255.0f,
            1.0f};
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
        ImPlot::PlotLine(std::format("##smith_grid_{}", index).c_str(),
                         lines[index].Xs.data(),
                         lines[index].Ys.data(),
                         static_cast<int>(lines[index].Xs.size()),
                         spec);
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
std::string JoinReadings(const Core::SParameters &result,
                         const std::size_t frequency,
                         const std::vector<SParameterEntry> &entries,
                         const SParameterQuantity quantity) {
    std::string text;
    for (const SParameterEntry &entry : entries) {
        for (const std::string &line : DescribeMatrixReading(result, frequency, entry, quantity)) {
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

ImVec4 ToImVec4(const ExportColor color, const float alpha) {
    ImVec4 converted = ToImVec4(color);
    converted.w = alpha;
    return converted;
}

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

std::string DescribeNeedsTwoPorts(const Core::SParameters &result, const char *what) {
    return std::format("{} need exactly two ports; this result has {}", what, result.GetPortCount());
}

void ShowWrapped(const std::string &text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
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
    if (m_Quantity != SParameterQuantity::S
        && CountAbsentFrequencies(*result, m_Quantity) == result->Frequencies.size()) {
        ShowWrapped(DescribeAbsentFrequencies(*result, m_Quantity));
        return;
    }
    if (view == SParameterView::STABILITY) {
        DrawStability();
        return;
    }
    if (GetShownSelection(view).ListShown().empty()) {
        ImGui::TextDisabled("Check an entry of the matrix to plot it");
        return;
    }
    if (view == SParameterView::SMITH) {
        DrawSmith(*result, view);
    } else {
        DrawMagnitudePhase(*result, view);
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
    if (m_Quantity != SParameterQuantity::S
        && CountAbsentFrequencies(*result, m_Quantity) == result->Frequencies.size()) {
        return false;
    }
    const SParameterView view = GetDisplayedView(*result);
    return view == SParameterView::STABILITY || !GetShownSelection(view).ListShown().empty();
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
    const std::vector<SParameterEntry> entries = GetShownSelection(view).ListShown();
    if (m_Quantity != SParameterQuantity::S) {
        return is_table ? BuildMatrixTableFigure(*result, entries, m_Quantity)
                        : BuildMatrixMagnitudePhaseFigure(*result,
                                                          entries,
                                                          ExportRange{.From = m_View.From, .To = m_View.To},
                                                          m_Quantity);
    }
    if (view == SParameterView::SMITH && m_AreCirclesShown && IsTwoPort(*result)) {
        return BuildSmithFigureWithCircles(*result, entries, m_CircleFrequency);
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
    if (m_Selection.GetPortCount() != result.GetPortCount()) {
        m_Selection.Reset(result.GetPortCount());
        m_SmithSelection.ResetToReflections(result.GetPortCount());
        m_ImpedanceSelection.Reset(result.GetPortCount());
        m_AdmittanceSelection.Reset(result.GetPortCount());
    }
    if (!IsQuantityAvailable(result, m_Quantity)) {
        m_Quantity = SParameterQuantity::S;
    }
    m_View = {.From = result.Frequencies.front(), .To = result.Frequencies.back()};
    m_Sweep = ComputeTwoPortSweep(result);
    m_Curves = BuildStabilityCurves(m_Sweep);
    m_StabilitySummary = DescribeStability(m_Sweep);
    m_CircleFrequency = FindLowestMu(m_Sweep).value_or(0);
}

/**
 * @brief   Tells which view is drawn: Z and Y have the magnitude and phase alone, and the stability needs two ports.
 * @details The choice the user made for S is kept in m_Display, so it comes back with S.
 */
SParameterView SParameterTab::GetDisplayedView(const Core::SParameters &result) const {
    if (m_Quantity != SParameterQuantity::S) {
        return SParameterView::MAGNITUDE_PHASE;
    }
    if (m_Display == SParameterView::STABILITY && !IsTwoPort(result)) {
        return SParameterView::SMITH;
    }
    return m_Display;
}

/**
 * @brief   The entries of the view shown: each view of S, and Z, and Y, keeps its own choice.
 */
SParameterSelection &SParameterTab::GetShownSelection(const SParameterView view) {
    switch (m_Quantity) {
    case SParameterQuantity::Z:
        return m_ImpedanceSelection;
    case SParameterQuantity::Y:
        return m_AdmittanceSelection;
    case SParameterQuantity::S:
    default:
        return view == SParameterView::SMITH ? m_SmithSelection : m_Selection;
    }
}

const SParameterSelection &SParameterTab::GetShownSelection(const SParameterView view) const {
    switch (m_Quantity) {
    case SParameterQuantity::Z:
        return m_ImpedanceSelection;
    case SParameterQuantity::Y:
        return m_AdmittanceSelection;
    case SParameterQuantity::S:
    default:
        return view == SParameterView::SMITH ? m_SmithSelection : m_Selection;
    }
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
        DrawCircleControls(result);
    }
    if (view == SParameterView::MAGNITUDE_PHASE && m_Quantity != SParameterQuantity::S) {
        const std::string absent = DescribeAbsentFrequencies(result, m_Quantity);
        if (!absent.empty() && CountAbsentFrequencies(result, m_Quantity) < result.Frequencies.size()) {
            ShowWrapped(absent);
        }
    }
    if (view == SParameterView::STABILITY) {
        ShowWrapped(m_StabilitySummary);
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
    if (DrawChoice("Magnitude and phase", view == SParameterView::MAGNITUDE_PHASE, true, "") && is_s) {
        m_Display = SParameterView::MAGNITUDE_PHASE;
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
    DrawTouchstoneExport(result, exporter);
}

/**
 * @brief   On the Smith chart, the checkbox of the stability circles of a two-port, the frequency they are drawn at,
 * and the note that says when a circle leaves the whole chart stable or unstable.
 */
void SParameterTab::DrawCircleControls(const Core::SParameters &result) {
    const bool is_two_port = IsTwoPort(result);
    ImGui::BeginDisabled(!is_two_port);
    ImGui::Checkbox("Stability circles", &m_AreCirclesShown);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s",
                          is_two_port ? "The source and load stability circles at one frequency; the hatching "
                                        "marks the terminations that make the two-port unstable"
                                      : DescribeNeedsTwoPorts(result, "Stability circles").c_str());
    }
    if (!is_two_port || !m_AreCirclesShown) {
        return;
    }
    m_CircleFrequency = std::min(m_CircleFrequency, result.Frequencies.size() - 1);
    ImGui::SameLine();
    ImGui::TextUnformatted("Circles at");
    ImGui::SameLine();
    ImGui::BeginDisabled(result.Frequencies.size() < 2);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * CIRCLE_SLIDER_WIDTH);
    int index = static_cast<int>(m_CircleFrequency);
    const std::string frequency = Core::FormatValue(result.Frequencies[m_CircleFrequency]) + "Hz";
    if (ImGui::SliderInt("##circles_at",
                         &index,
                         0,
                         static_cast<int>(result.Frequencies.size()) - 1,
                         frequency.c_str(),
                         ImGuiSliderFlags_AlwaysClamp)) {
        m_CircleFrequency = static_cast<std::size_t>(index);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("The frequency of the circles, one point of the sweep at a time");
    ImGui::SameLine();
    if (ImGui::SmallButton("Worst case")) {
        m_CircleFrequency = FindLowestMu(m_Sweep).value_or(0);
    }
    ImGui::SetItemTooltip("Goes to the frequency where mu is lowest, which is where the two-port is the least stable");
    const std::string note = DescribeCircles(ComputeStabilityCircles(ReadTwoPortMatrix(result, m_CircleFrequency)),
                                             result.Frequencies[m_CircleFrequency]);
    if (!note.empty()) {
        ShowWrapped(note);
    }
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
            bool is_shown = GetShownSelection(view).IsShown(row, column);
            if (column > 1) {
                ImGui::SameLine();
            }
            if (ImGui::Checkbox(
                    std::format("{}##entry_{}_{}", GetMatrixEntryName({row, column}, m_Quantity), row, column).c_str(),
                    &is_shown)) {
                GetShownSelection(view).SetShown(row, column, is_shown);
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
 *          frequency, the stability circles when asked, and the readings of the point nearest to the pointer.
 */
void SParameterTab::DrawSmith(const Core::SParameters &result, const SParameterView view) {
    const std::vector<SParameterEntry> entries = GetShownSelection(view).ListShown();
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
    if (m_AreCirclesShown && IsTwoPort(result)) {
        PlotStabilityCircles(result);
    }
    PlotSmithCurves(result, entries);
    if (m_AreCirclesShown && IsTwoPort(result)) {
        ShowCircleFrequency();
    }
    ShowSmithReading(result, entries);
    ImPlot::EndPlot();
}

/**
 * @brief   A curve per checked entry of S, with a dot at its first frequency.
 */
void SParameterTab::PlotSmithCurves(const Core::SParameters &result, const std::vector<SParameterEntry> &entries) {
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
}

/**
 * @brief   The source and load stability circles at the chosen frequency, each with the hatching of its unstable side.
 * @details A boundary that does not cross the chart draws nothing; the note under the controls says why.
 */
void SParameterTab::PlotStabilityCircles(const Core::SParameters &result) {
    const StabilityCircles circles = ComputeStabilityCircles(ReadTwoPortMatrix(result, m_CircleFrequency));
    const std::array<std::pair<const char *, const StabilityBoundary *>, 2> boundaries = {
        {{"Source stability circle", &circles.Source}, {"Load stability circle", &circles.Load}}};
    for (std::size_t index = 0; index < boundaries.size(); ++index) {
        const auto &[name, boundary] = boundaries[index];
        if (boundary->Shape == BoundaryShape::NONE || boundary->Coverage != ChartCoverage::PARTLY) {
            continue;
        }
        const ExportColor color = index == 0 ? SOURCE_CIRCLE_COLOR : LOAD_CIRCLE_COLOR;
        std::vector<double> hatch_xs;
        std::vector<double> hatch_ys;
        for (const HatchSegment &stroke : BuildUnstableHatch(*boundary)) {
            hatch_xs.insert(hatch_xs.end(), {stroke.From.real(), stroke.To.real()});
            hatch_ys.insert(hatch_ys.end(), {stroke.From.imag(), stroke.To.imag()});
        }
        ImPlotSpec hatch_spec;
        hatch_spec.LineColor = ToImVec4(color, HATCH_ALPHA);
        hatch_spec.LineWeight = HATCH_WEIGHT;
        hatch_spec.Flags = static_cast<int>(ImPlotItemFlags_NoLegend) | static_cast<int>(ImPlotItemFlags_NoFit)
                         | static_cast<int>(ImPlotLineFlags_Segments);
        ImPlot::PlotLine(std::format("##hatch_{}", index).c_str(),
                         hatch_xs.data(),
                         hatch_ys.data(),
                         static_cast<int>(hatch_xs.size()),
                         hatch_spec);
        for (const std::vector<std::complex<double>> &piece : SampleBoundary(*boundary)) {
            std::vector<double> xs;
            std::vector<double> ys;
            for (const std::complex<double> point : piece) {
                xs.push_back(point.real());
                ys.push_back(point.imag());
            }
            ImPlotSpec spec;
            spec.LineColor = ToImVec4(color);
            spec.LineWeight = CIRCLE_WEIGHT;
            spec.Flags = ImPlotItemFlags_NoFit;
            ImPlot::PlotLine(name, xs.data(), ys.data(), static_cast<int>(xs.size()), spec);
        }
    }
}

/**
 * @brief   Writes in the corner of the Smith chart the frequency the stability circles are drawn at and mu there.
 */
void SParameterTab::ShowCircleFrequency() {
    const std::size_t frequency = std::min(m_CircleFrequency, m_Sweep.Frequencies.size() - 1);
    const std::string text = DescribeCircleFrequency(m_Sweep.Metrics[frequency], m_Sweep.Frequencies[frequency]);
    const ImVec2 corner = ImPlot::GetPlotPos();
    ImPlot::GetPlotDrawList()->AddText(ImVec2(corner.x + LABEL_OFFSET, corner.y + LABEL_OFFSET),
                                       ImGui::GetColorU32(ImGuiCol_Text),
                                       text.c_str());
}

/**
 * @brief   Rings the point of a curve nearest to the pointer and reads it in a tooltip, when it is near enough.
 */
void SParameterTab::ShowSmithReading(const Core::SParameters &result, const std::vector<SParameterEntry> &entries) {
    if (!ImPlot::IsPlotHovered()) {
        return;
    }
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
    if (!hovered) {
        return;
    }
    const std::complex<double> value =
        result.Get(hovered->second, entries[hovered->first].Row, entries[hovered->first].Column);
    ImPlot::GetPlotDrawList()->AddCircle(ImPlot::PlotToPixels(value.real(), value.imag()),
                                         READING_RING_RADIUS,
                                         ImGui::GetColorU32(ToImVec4(GetEntryColor(hovered->first))),
                                         0,
                                         CURVE_WEIGHT);
    ImGui::SetTooltip("%s",
                      JoinReadings(result, hovered->second, {entries[hovered->first]}, SParameterQuantity::S).c_str());
}

/**
 * @brief   The magnitude plot over the phase plot, sharing the frequency axis.
 */
void SParameterTab::DrawMagnitudePhase(const Core::SParameters &result, const SParameterView view) {
    const std::vector<SParameterEntry> entries = GetShownSelection(view).ListShown();
    // Worked out once, so the plots share the room instead of the second taking what the first left
    const float height = PlotHeight(2);
    DrawBodePlot(result, entries, false, height);
    DrawBodePlot(result, entries, true, height);
    m_IsFitting = false;
}

/**
 * @brief   One of the two Bode plots; hovering it reads every checked entry at the nearest frequency.
 * @details The magnitude of Z and Y is in ohms and siemens on a logarithmic axis; where the matrix does not exist the
 *          curve breaks.
 */
void SParameterTab::DrawBodePlot(const Core::SParameters &result,
                                 const std::vector<SParameterEntry> &entries,
                                 const bool is_phase,
                                 const float height) {
    const bool is_s = m_Quantity == SParameterQuantity::S;
    const bool is_decade_axis = !is_s && !is_phase;
    if (m_IsFitting) {
        // The logarithmic axis is fitted in decades below, since the padding of the fit is linear and crosses zero
        if (is_decade_axis) {
            ImPlot::SetNextAxisToFit(ImAxis_X1);
        } else {
            ImPlot::SetNextAxesToFit();
        }
    }
    if (!BeginTracePlot(is_phase ? "Phase##s" : "Magnitude##s", ImVec2(-1.0f, height), true)) {
        return;
    }
    const std::string unit = GetQuantityUnit(m_Quantity);
    const std::string magnitude_label =
        is_s ? "Magnitude (dB)"
             : std::format("{} magnitude ({})", m_Quantity == SParameterQuantity::Z ? "Impedance" : "Admittance", unit);
    ImPlot::SetupAxes("Frequency", is_phase ? "Phase (deg)" : magnitude_label.c_str());
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
    ImPlot::SetupAxisLinks(ImAxis_X1, &m_View.From, &m_View.To);
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
    if (is_decade_axis) {
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
    if (ImPlot::IsPlotHovered()) {
        const std::size_t frequency = FindNearestFrequency(result.Frequencies, ImPlot::GetPlotMousePos().x);
        ImGui::SetTooltip("%s", JoinReadings(result, frequency, entries, m_Quantity).c_str());
    }
    ImPlot::EndPlot();
}

/**
 * @brief   The factors K and mu over the gain bound, sharing the frequency axis.
 */
void SParameterTab::DrawStability() {
    const float height = PlotHeight(2);
    DrawFactorPlot(height);
    DrawGainPlot(height);
    m_IsFitting = false;
}

/**
 * @brief   K and mu against frequency, with the line at 1 above which the two-port is unconditionally stable.
 * @details Hovering reads the factors and the gain at the nearest frequency.
 */
void SParameterTab::DrawFactorPlot(const float height) {
    if (m_IsFitting) {
        ImPlot::SetNextAxisToFit(ImAxis_X1);
    }
    if (!BeginTracePlot("Stability##s", ImVec2(-1.0f, height), true)) {
        return;
    }
    ImPlot::SetupAxes("Frequency", "K and mu");
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
    ImPlot::SetupAxisLinks(ImAxis_X1, &m_View.From, &m_View.To);
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
    ImPlot::SetupAxisLimits(ImAxis_Y1,
                            m_Curves.FactorRange.From,
                            m_Curves.FactorRange.To,
                            m_IsFitting ? ImPlotCond_Always : ImPlotCond_Once);
    const std::size_t count = m_Curves.Frequencies.size();
    ImPlotSpec k_spec;
    k_spec.LineColor = ToImVec4(STABILITY_K_COLOR);
    k_spec.LineWeight = CURVE_WEIGHT;
    ImPlot::PlotLine("K", m_Curves.Frequencies.data(), m_Curves.K.data(), static_cast<int>(count), k_spec);
    ImPlotSpec mu_spec;
    mu_spec.LineColor = ToImVec4(STABILITY_MU_COLOR);
    mu_spec.LineWeight = CURVE_WEIGHT;
    ImPlot::PlotLine("mu", m_Curves.Frequencies.data(), m_Curves.Mu.data(), static_cast<int>(count), mu_spec);
    constexpr double STABILITY_LIMIT = 1.0;
    ImPlotSpec limit_spec;
    limit_spec.LineColor = ToImVec4(STABILITY_LINE_AT_ONE_COLOR);
    limit_spec.LineWeight = HATCH_WEIGHT * 1.5f;
    limit_spec.Flags = ImPlotInfLinesFlags_Horizontal;
    ImPlot::PlotInfLines("1, stable above", &STABILITY_LIMIT, 1, limit_spec);
    MarkCircleFrequency();
    if (ImPlot::IsPlotHovered()) {
        const std::size_t frequency = FindNearestFrequency(m_Curves.Frequencies, ImPlot::GetPlotMousePos().x);
        std::string text;
        for (const std::string &line : DescribeMetrics(m_Sweep.Metrics[frequency], m_Sweep.Frequencies[frequency])) {
            text += (text.empty() ? "" : "\n") + line;
        }
        ImGui::SetTooltip("%s", text.c_str());
    }
    ImPlot::EndPlot();
}

/**
 * @brief   The gain bound in decibels, as one curve in two kinds: MAG where the two-port is unconditionally stable and
 *          MSG, dashed in the export and in another color here, where it is not.
 */
void SParameterTab::DrawGainPlot(const float height) {
    if (m_IsFitting) {
        ImPlot::SetNextAxesToFit();
    }
    if (!BeginTracePlot("Gain##s", ImVec2(-1.0f, height), true)) {
        return;
    }
    ImPlot::SetupAxes("Frequency", "Gain (dB)");
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
    ImPlot::SetupAxisLinks(ImAxis_X1, &m_View.From, &m_View.To);
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
    const std::size_t count = m_Curves.Frequencies.size();
    const auto has_value = [](const std::vector<double> &values) {
        return std::ranges::any_of(values, [](const double value) { return std::isfinite(value); });
    };
    if (has_value(m_Curves.MaximumAvailableGainDecibels)) {
        ImPlotSpec spec;
        spec.LineColor = ToImVec4(STABILITY_MAG_COLOR);
        spec.LineWeight = CURVE_WEIGHT;
        ImPlot::PlotLine(m_Curves.MaximumAvailableGainLabel.c_str(),
                         m_Curves.Frequencies.data(),
                         m_Curves.MaximumAvailableGainDecibels.data(),
                         static_cast<int>(count),
                         spec);
    }
    if (has_value(m_Curves.MaximumStableGainDecibels)) {
        ImPlotSpec spec;
        spec.LineColor = ToImVec4(STABILITY_MSG_COLOR);
        spec.LineWeight = CURVE_WEIGHT;
        ImPlot::PlotLine(m_Curves.MaximumStableGainLabel.c_str(),
                         m_Curves.Frequencies.data(),
                         m_Curves.MaximumStableGainDecibels.data(),
                         static_cast<int>(count),
                         spec);
    }
    MarkCircleFrequency();
    if (ImPlot::IsPlotHovered()) {
        const std::size_t frequency = FindNearestFrequency(m_Curves.Frequencies, ImPlot::GetPlotMousePos().x);
        std::string text;
        for (const std::string &line : DescribeMetrics(m_Sweep.Metrics[frequency], m_Sweep.Frequencies[frequency])) {
            text += (text.empty() ? "" : "\n") + line;
        }
        ImGui::SetTooltip("%s", text.c_str());
    }
    ImPlot::EndPlot();
}

/**
 * @brief   Draws a vertical line at the frequency of the stability circles on the plot being built, and lets a click on
 *          the plot move it to the swept frequency nearest to the pointer.
 * @details The factor and gain plots share this, so the Smith chart circles follow whichever one is clicked. The click
 *          moves the circles even while they are hidden, so they are already there when the checkbox is turned on.
 * @note    Call it between BeginPlot and EndPlot.
 */
void SParameterTab::MarkCircleFrequency() {
    if (m_Curves.Frequencies.empty()) {
        return;
    }
    m_CircleFrequency = std::min(m_CircleFrequency, m_Curves.Frequencies.size() - 1);
    // A press also starts a pan, so only a release that did not drag counts as a click
    const ImGuiIO &io = ImGui::GetIO();
    const bool is_click =
        ImGui::IsMouseReleased(ImGuiMouseButton_Left)
        && io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] <= io.MouseDragThreshold * io.MouseDragThreshold;
    if (ImPlot::IsPlotHovered() && is_click) {
        m_CircleFrequency = FindNearestFrequency(m_Curves.Frequencies, ImPlot::GetPlotMousePos().x);
    }
    const double circle_frequency = m_Curves.Frequencies[m_CircleFrequency];
    ImPlotSpec spec;
    spec.LineColor = ToImVec4(SOURCE_CIRCLE_COLOR);
    spec.LineWeight = HATCH_WEIGHT * 1.5f;
    ImPlot::PlotInfLines("Circles at", &circle_frequency, 1, spec);
}

} // namespace GUI
