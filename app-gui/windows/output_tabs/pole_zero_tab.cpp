/**
 * @file    pole_zero_tab.cpp
 * @brief   The Poles and zeros tab of the Output window: the s-plane of the last pole-zero analysis and its table.
 */

#include "windows/output_tabs/pole_zero_tab.h"

#include "base/theme.h"
#include "imgui.h"
#include "implot.h"
#include "results/pole_zero_view.h"
#include "schematic/probing.h"
#include "spice/spice_value.h"
#include "windows/output_tabs/plot_widgets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

namespace {

/// Share of the tab height the s-plane takes; the table under it has the rest, over the whole width, so no column is
/// cut.
constexpr float PLANE_HEIGHT_SHARE = 0.6f;
/// A symmetric logarithmic axis labels 0 and the decades from 10 up; the labels of -1 and 1 would run into the 0.
constexpr double SMALLEST_LABELLED_ROOT = 5.0;
/// Radius, in pixels, of the marks of the roots.
constexpr float ROOT_MARKER_SIZE = 7.0f;
/// How near the pointer must be to a mark, in pixels, to name its root.
constexpr float HOVER_DISTANCE = 12.0f;
/// Weight, in pixels, of the edge of the marks.
constexpr float ROOT_MARKER_WEIGHT = 2.0f;

/**
 * @brief   Labels the ticks of a symmetric logarithmic axis in SI units, leaving out the ones next to 0, which would
 *          overlap it.
 */
int FormatSymLogTick(const double value, char *buffer, const int size, void * /*unused*/) {
    const bool is_crowded = value != 0.0 && std::abs(value) < SMALLEST_LABELLED_ROOT;
    return std::snprintf(buffer, static_cast<std::size_t>(size), "%s",
                         is_crowded ? "" : Core::FormatValue(value).c_str());
}

/**
 * @brief   Draws the marks of one kind of root and half-plane, if there are any.
 */
void PlotRoots(const Core::PoleZero &result, const bool is_pole, const bool is_right_half_plane) {
    const PoleZeroPoints points = CollectPoleZeroPoints(result, is_pole, is_right_half_plane);
    if (points.Real.empty()) {
        return;
    }
    ImPlotSpec spec;
    spec.Marker = is_pole ? ImPlotMarker_Cross : ImPlotMarker_Circle;
    spec.MarkerSize = ROOT_MARKER_SIZE;
    spec.LineWeight = ROOT_MARKER_WEIGHT;
    spec.LineColor = GetPoleZeroColor(is_pole, is_right_half_plane);
    spec.MarkerLineColor = spec.LineColor;
    // A zero is a hollow circle; a pole is a cross, which has no face
    spec.FillAlpha = 0.0f;
    const char *name = is_pole ? (is_right_half_plane ? "Poles (right half-plane)" : "Poles")
                               : (is_right_half_plane ? "Zeros (right half-plane)" : "Zeros");
    ImPlot::PlotScatter(name, points.Real.data(), points.Imaginary.data(), static_cast<int>(points.Real.size()), spec);
}

/**
 * @brief   Names the root under the pointer, if one is near enough.
 */
void DrawHoveredRoot(const Core::PoleZero &result) {
    if (!ImPlot::IsPlotHovered()) {
        return;
    }
    const ImVec2 pointer = ImGui::GetMousePos();
    float nearest = HOVER_DISTANCE;
    const Core::PoleZeroRoot *hovered = nullptr;
    for (const Core::PoleZeroRoot &root : result.Roots) {
        for (const double sign : {1.0, -1.0}) {
            const ImVec2 mark = ImPlot::PlotToPixels(root.Real, sign * root.Imaginary);
            const float distance = std::hypot(mark.x - pointer.x, mark.y - pointer.y);
            if (distance < nearest) {
                nearest = distance;
                hovered = &root;
            }
        }
    }
    if (hovered != nullptr) {
        ImGui::SetTooltip("%s", DescribePoleZeroRoot(*hovered).c_str());
    }
}

} // namespace

/**
 * @brief   Creates the tab.
 * @param[in] schematic  Schematic that keeps the measurements and the names of nets; it must outlive the tab.
 * @param[in] results    Results whose pole-zero result is shown; they must outlive the tab.
 */
PoleZeroTab::PoleZeroTab(Schematic &schematic, const ResultStore &results)
    : m_Schematic(schematic), m_Results(results) {
}

/**
 * @brief   Draws the tab: what was analyzed, the s-plane on the left and the table on the right, or how to get a
 * result.
 */
void PoleZeroTab::Draw() {
    const std::optional<Core::PoleZero> &result = m_Results.GetPoleZero();
    if (!result) {
        ImGui::TextDisabled(
            "No poles and zeros for the current circuit; pick Poles and zeros beside Run in the editor");
        return;
    }
    DrawSummary(*result);
    const float height = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginChild("s_plane", ImVec2(0.0f, height * PLANE_HEIGHT_SHARE))) {
        DrawPlane(*result);
    }
    ImGui::EndChild();
    if (ImGui::BeginChild("pole_zero_table", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        DrawTable(*result);
    }
    ImGui::EndChild();
}

/**
 * @brief   Says between which nodes the roots were found, and warns about what makes them doubtful.
 */
void PoleZeroTab::DrawSummary(const Core::PoleZero &result) {
    const std::string output = GetVoltageLabel(m_Schematic.GetConnectivity(), result.OutputNode);
    ImGui::TextUnformatted(std::format("Poles and zeros of {} per {}{}", output,
                                       result.InputUnit == "A" ? "A of " : "V of ", result.InputSource)
                               .c_str());
    ImGui::PushTextWrapPos(0.0f);
    for (const std::string &warning : result.Warnings) {
        ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
    }
    ImGui::PopTextWrapPos();
}

/**
 * @brief   The s-plane: crosses for poles, circles for zeros, in rad/s; the roots of a pair are drawn as both members.
 */
void PoleZeroTab::DrawPlane(const Core::PoleZero &result) {
    ImGui::Checkbox("Linear axes", &m_IsLinear);
    ImGui::SetItemTooltip("Symmetric logarithmic axes show roots decades apart; linear ones show their true spacing");
    const std::string cancelled = DescribeCancelledPairs(result.CancelledPairs);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", cancelled.c_str());
    const std::size_t version = m_Results.GetPoleZeroVersion();
    // The limits follow a new result and a change of scale; the user's zoom and pan stay in between
    const bool is_fitting = m_FittedVersion != version || m_FittedLinear != m_IsLinear;
    m_FittedVersion = version;
    m_FittedLinear = m_IsLinear;
    if (!ImPlot::BeginPlot("##s_plane_plot", ImVec2(-1.0f, -1.0f))) {
        return;
    }
    SetupPlaneAxes(result, is_fitting);
    const double origin = 0.0;
    ImPlotSpec axis_spec;
    axis_spec.LineColor = ImVec4(0.5f, 0.5f, 0.5f, 0.6f);
    ImPlot::PlotInfLines("##real_axis", &origin, 1, axis_spec);
    axis_spec.Flags = ImPlotInfLinesFlags_Horizontal;
    ImPlot::PlotInfLines("##imaginary_axis", &origin, 1, axis_spec);
    for (const bool is_pole : {true, false}) {
        for (const bool is_right_half_plane : {false, true}) {
            PlotRoots(result, is_pole, is_right_half_plane);
        }
    }
    DrawHoveredRoot(result);
    ImPlot::EndPlot();
}

/**
 * @brief   Sets the scale, tick labels, limits and legend of the s-plane, inside its plot.
 * @param[in] result      The result, whose roots set the limits.
 * @param[in] is_fitting  Whether the limits are set anew, for a new result or a change of scale.
 */
void PoleZeroTab::SetupPlaneAxes(const Core::PoleZero &result, const bool is_fitting) {
    ImPlot::SetupAxes("Real (rad/s)", "Imaginary (rad/s)");
    const ImPlotScale scale = m_IsLinear ? ImPlotScale_Linear : ImPlotScale_SymLog;
    ImPlot::SetupAxisScale(ImAxis_X1, scale);
    ImPlot::SetupAxisScale(ImAxis_Y1, scale);
    if (m_IsLinear) {
        ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>(""));
        ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>(""));
    } else {
        ImPlot::SetupAxisFormat(ImAxis_X1, FormatSymLogTick);
        ImPlot::SetupAxisFormat(ImAxis_Y1, FormatSymLogTick);
    }
    const PlaneLimits limits = ComputePlaneLimits(result, !m_IsLinear);
    const ImPlotCond condition = is_fitting ? ImPlotCond_Always : ImPlotCond_Once;
    ImPlot::SetupAxisLimits(ImAxis_X1, limits.RealMin, limits.RealMax, condition);
    ImPlot::SetupAxisLimits(ImAxis_Y1, limits.ImaginaryMin, limits.ImaginaryMax, condition);
    // Outside and above the plot, where no root can be, since a legend over the plane would hide marks
    ImPlot::SetupLegend(ImPlotLocation_North,
                        ImPlotLegendFlags_NoButtons | ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);
}

/**
 * @brief   One row per pole or zero, with the natural frequency, quality factor and damping ratio of each.
 */
void PoleZeroTab::DrawTable(const Core::PoleZero &result) {
    const std::vector<PoleZeroRow> rows = MakePoleZeroRows(result);
    if (rows.empty()) {
        ImGui::TextDisabled("No poles or zeros left after the cancelling pairs");
        return;
    }
    constexpr ImGuiTableFlags TABLE_FLAGS =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("pole_zero_rows", 6, TABLE_FLAGS)) {
        // Each column is as wide as its widest text, so nothing is cut at any window size
        ImGui::TableSetupColumn("Root");
        ImGui::TableSetupColumn("Real (rad/s)");
        ImGui::TableSetupColumn("Imaginary (rad/s)");
        ImGui::TableSetupColumn("Frequency");
        ImGui::TableSetupColumn("Q");
        ImGui::TableSetupColumn("Damping");
        ImGui::TableHeadersRow();
        for (const PoleZeroRow &row : rows) {
            ImGui::TableNextRow();
            for (const std::string *cell :
                 {&row.Kind, &row.Real, &row.Imaginary, &row.Frequency, &row.Quality, &row.Damping}) {
                ImGui::TableNextColumn();
                if (row.IsRightHalfPlane) {
                    ImGui::TextColored(GetWarningTextColor(), "%s", cell->c_str());
                } else {
                    ImGui::TextUnformatted(cell->c_str());
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", "A complex pair is one row, named by its natural frequency; Q and the damping ratio "
                              "belong to complex roots, and a real root shows -.");
    if (std::ranges::any_of(rows, &PoleZeroRow::IsRightHalfPlane)) {
        ImGui::TextColored(GetWarningTextColor(), "%s",
                           "A root in the right half-plane means the circuit is unstable at its operating point, or "
                           "that ngspice's solver got it wrong; compare it with an AC sweep");
    }
    ImGui::PopTextWrapPos();
}

} // namespace GUI
