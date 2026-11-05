/**
 * @file    output_window.cpp
 * @brief   Window that plots the transient, AC sweep and DC sweep results of the schematic.
 */

#include "windows/output_window.h"

#include "drawing/node_colors.h"
#include "editing/probing.h"
#include "imgui_internal.h"
#include "implot.h"
#include "plots/plot_figures.h"
#include "windows/output_tabs/plot_widgets.h"
#include "windows/output_tabs/result_tables.h"
#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

namespace {

constexpr float TRACE_LIST_WIDTH = 8.0f;
constexpr ImVec2 INITIAL_SIZE = {65.0f, 35.0f};
/// ImPlot adds half of it on each side, so 0.4 leaves 20% of the data range above and below the curves.
constexpr ImVec2 FIT_PADDING = {0.0f, 0.4f};
constexpr const char *EXPORT_LABEL = "Export...";

/**
 * @brief   Like ImGui::BeginTabBar(), but the bar and the line under it stop short of the right edge by the given
 *          width, leaving room for a button there.
 * @details Mirrors BeginTabBar() in imgui_widgets.cpp, which takes no width.
 */
bool BeginTabBarBefore(const char *id, const float reserved_width) {
    ImGuiContext &context = *ImGui::GetCurrentContext();
    ImGuiWindow *window = context.CurrentWindow;
    if (window->SkipItems) {
        return false;
    }
    const ImGuiID tab_bar_id = window->GetID(id);
    ImGuiTabBar *tab_bar = context.TabBars.GetOrAddByKey(tab_bar_id);
    const ImVec2 start = window->DC.CursorPos;
    const ImRect bounds(start.x, start.y, window->WorkRect.Max.x - reserved_width,
                        start.y + context.FontSize + context.Style.FramePadding.y * 2.0f);
    tab_bar->ID = tab_bar_id;
    tab_bar->SeparatorMinX = bounds.Min.x - IM_TRUNC(window->WindowPadding.x * 0.5f);
    tab_bar->SeparatorMaxX = bounds.Max.x;
    return ImGui::BeginTabBarEx(tab_bar, bounds, ImGuiTabBarFlags_IsFocused);
}

const char *GetTabFileSuffix(const PlotTab tab) {
    switch (tab) {
    case PlotTab::OPERATING_POINT:
        return "operating-point";
    case PlotTab::TRANSIENT:
        return "transient";
    case PlotTab::SPECTRUM:
        return "spectrum";
    case PlotTab::AC_SWEEP:
        return "ac-sweep";
    case PlotTab::DC_SWEEP:
        return "dc-sweep";
    case PlotTab::NOISE:
        return "noise";
    case PlotTab::TRANSFER_FUNCTION:
        return "transfer-function";
    case PlotTab::TRUTH_TABLE:
        return "truth-table";
    }
    return "plot";
}

const char *GetTabTitle(const PlotTab tab) {
    switch (tab) {
    case PlotTab::OPERATING_POINT:
        return "operating point";
    case PlotTab::TRANSIENT:
        return "transient";
    case PlotTab::SPECTRUM:
        return "spectrum";
    case PlotTab::AC_SWEEP:
        return "AC sweep";
    case PlotTab::DC_SWEEP:
        return "DC sweep";
    case PlotTab::NOISE:
        return "noise";
    case PlotTab::TRANSFER_FUNCTION:
        return "transfer function";
    case PlotTab::TRUTH_TABLE:
        return "truth table";
    }
    return "";
}

} // namespace

/**
 * @brief   Creates the window for a schematic.
 * @param[in] schematic  Schematic whose results are plotted; it must outlive the window.
 * @param[in] controls   Runs of the analyses, to stop one from here while its plot grows; they must outlive the window.
 * @param[in] playback   Instant of the transient shown on the schematic, drawn as a line in the transient plot that
 *                       can be dragged; it must outlive the window.
 * @param[in] preferences  Whether a new result brings the window to the front; they must outlive the window.
 */
OutputWindow::OutputWindow(Schematic &schematic, AnalysisControls &controls, TransientPlayback &playback,
                           const Preferences &preferences)
    : AppWindow("Output", true), m_Schematic(schematic), m_Controls(controls), m_Playback(playback),
      m_Preferences(preferences), m_MathChannels(schematic), m_Buses(schematic),
      m_TransientTab(schematic, controls, playback), m_SpectrumTab(schematic), m_ACSweepTab(schematic),
      m_DCSweepTab(schematic), m_NoiseTab(schematic) {
    SetInitialSize(INITIAL_SIZE);
    // Nothing to plot yet; the first result opens it
    SetOpen(false);
}

/**
 * @brief   Opens the window, if closed, on one of its tabs, and brings it to the front on the next frame.
 * @param[in] tab                Tab of the analysis whose result just arrived.
 * @param[in] may_come_to_front  False to leave the window behind whatever the preference, as for a live run, which
 *                               the user follows on the schematic.
 * @note    When the user turned off bringing it to the front, or the caller did, the window marks its title or tab
 *          with a dot instead, until it is shown.
 */
void OutputWindow::ShowTab(const PlotTab tab, const bool may_come_to_front) {
    m_TabToShow = tab;
    if (m_Preferences.BringOutputToFront && may_come_to_front) {
        SetOpen(true);
        Focus();
    } else {
        OpenBehind();
        SetNewContentMarker(true);
    }
}

void OutputWindow::Draw() {
    // Drawn means shown, so the new result is seen; a window that just appeared behind another tab may be drawn
    // once before it is hidden
    if (!ImGui::IsWindowAppearing()) {
        SetNewContentMarker(false);
    }
    m_Exporter.Update();
    m_Exporter.DrawError();
    const std::size_t node_count = GetListedNodeCount();
    const std::vector<Core::ComponentTrace> &currents = GetListedCurrents();
    if (node_count > 1 || !currents.empty()) {
        DrawTraceList(node_count, currents);
        ImGui::SameLine();
    }

    if (!ImGui::BeginChild("plots")) {
        ImGui::EndChild();
        return;
    }
    // The Export button goes on the right of the tab row, drawn once the tabs are done
    const ImVec2 tab_row = ImGui::GetCursorScreenPos();
    const float tab_row_width = ImGui::GetContentRegionAvail().x;
    const float export_width = ImGui::CalcTextSize(EXPORT_LABEL).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    if (!BeginTabBarBefore("plots", export_width + ImGui::GetStyle().ItemSpacing.x)) {
        ImGui::EndChild();
        return;
    }
    UpdateFullMath();
    const FrameResults frame = EvaluateFrame(node_count, currents);
    // Applies to the automatic fit of every new result and to the user's double-click fit
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, FIT_PADDING);
    DrawTabs(frame);
    ImPlot::PopStyleVar();
    ImGui::EndTabBar();
    DrawExportButton(tab_row, tab_row_width, export_width, frame);
    ImGui::EndChild();
}

/**
 * @brief   Every result comes from the same circuit, so they have the same components; the first result there is lists
 *          them.
 */
const std::vector<Core::ComponentTrace> &OutputWindow::GetListedCurrents() const {
    static const std::vector<Core::ComponentTrace> no_currents;
    if (const auto &transient = m_Schematic.GetTransient()) {
        return transient->Currents;
    }
    if (const auto &sweep = m_Schematic.GetACSweep()) {
        return sweep->CurrentMagnitudesDecibels;
    }
    if (const auto &dc_sweep = m_Schematic.GetDCSweep()) {
        return dc_sweep->Curves.front().Currents;
    }
    return no_currents;
}

/**
 * @brief   The nodes of the results, which share them as they share the components.
 */
std::size_t OutputWindow::GetListedNodeCount() const {
    const auto &transient = m_Schematic.GetTransient();
    const auto &sweep = m_Schematic.GetACSweep();
    const auto &dc_sweep = m_Schematic.GetDCSweep();
    return std::max({transient ? transient->NodeVoltages.size() : 0, sweep ? sweep->NodeMagnitudesDecibels.size() : 0,
                     dc_sweep ? dc_sweep->Curves.front().NodeVoltages.size() : 0});
}

/**
 * @brief   The math channels go over the whole run, so a derivative or an integral starts at the start of the
 *          simulation and not at the edge of the window.
 * @details It is done again only when the run or the channels change, and not at all while a run grows, which waits for
 *          the end of the run.
 */
void OutputWindow::UpdateFullMath() {
    const auto &transient = m_Schematic.GetTransient();
    if (!transient || (m_Controls.IsRunning() && !transient->IsComplete)) {
        m_FullMath.reset();
        return;
    }
    const std::string signature = m_MathChannels.GetSignature();
    if (m_FullMath && m_FullMath->DataVersion == m_Schematic.GetTransientDataVersion() &&
        m_FullMath->Signature == signature) {
        return;
    }
    FullMath math{
        .DataVersion = m_Schematic.GetTransientDataVersion(), .Signature = signature, .Traces = {}, .Errors = {}};
    math.Traces = m_MathChannels.Evaluate(transient->Times, {.Seconds = 1}, transient->NodeVoltages,
                                          transient->Currents, math.Errors);
    m_FullMath = std::move(math);
}

/**
 * @brief   The math channels of the plot are cut from those over the whole run, so a run of any length costs the same,
 *          and there are none while a run grows.
 * @details Every curve of a DC sweep has the same nodes and currents, so the first one reports the errors of all.
 */
FrameResults OutputWindow::EvaluateFrame(const std::size_t node_count,
                                         const std::vector<Core::ComponentTrace> &currents) const {
    FrameResults frame;
    if (const auto &transient = m_Schematic.GetTransient()) {
        const PointWindow window = m_Playback.Window.GetWindow();
        frame.TransientWindow = SliceTransient(*transient, window);
        const Core::Transient &shown = *frame.TransientWindow;
        if (const auto &steps = m_Schematic.GetTransientSteps(); steps && !shown.Times.empty()) {
            StepFamily<Core::Transient> windows{.Parameter = steps->Parameter,
                                                .Unit = steps->Unit,
                                                .Values = steps->Values,
                                                .Results = {},
                                                .Selected = steps->Selected};
            for (const Core::Transient &step : steps->Results) {
                windows.Results.push_back(SliceTransientByTime(step, shown.Times.front(), shown.Times.back()));
            }
            frame.StepWindows = std::move(windows);
        }
        // None while a run grows: the channels wait for its end, and the panel says so
        if (m_FullMath) {
            frame.TransientMath = SliceMathTraces(m_FullMath->Traces, window);
            frame.TransientErrors = m_FullMath->Errors;
            frame.WholeRunMath = &*m_FullMath;
        }
    }
    if (const auto &dc_sweep = m_Schematic.GetDCSweep()) {
        const Dimension swept_unit = dc_sweep->SweptUnit == "A" ? Dimension{.Amperes = 1} : Dimension{.Volts = 1};
        for (const Core::DCSweepCurve &curve : dc_sweep->Curves) {
            std::vector<std::string> curve_errors;
            frame.DCSweepMath.push_back(m_MathChannels.Evaluate(dc_sweep->SweptValues, swept_unit, curve.NodeVoltages,
                                                                curve.Currents, curve_errors));
            if (frame.DCSweepMath.size() == 1) {
                frame.DCSweepErrors = std::move(curve_errors);
            }
        }
    }
    frame.AnythingMeasured = HasMeasuredVoltage(node_count, m_Schematic) || HasMeasuredCurrent(currents, m_Schematic);
    frame.HasMathChannels = !m_MathChannels.IsEmpty();
    return frame;
}

/**
 * @brief   The check marks take the colors of the curves, which match the probes in the editor.
 */
void OutputWindow::DrawTraceList(const std::size_t node_count, const std::vector<Core::ComponentTrace> &currents) {
    ImGui::BeginChild("traces", ImVec2(ImGui::GetFontSize() * TRACE_LIST_WIDTH, 0.0f), ImGuiChildFlags_Borders);
    if (node_count > 1) {
        ImGui::TextDisabled("Voltages");
        const std::optional<bool> measure_all = DrawAllNoneButtons("voltages");
        for (int node = 1; node < static_cast<int>(node_count); ++node) {
            if (measure_all) {
                m_Schematic.SetVoltageMeasured(node, *measure_all);
            }
            bool measured = m_Schematic.IsVoltageMeasured(node);
            ImGui::PushStyleColor(ImGuiCol_CheckMark, GetNodeColor(node));
            if (ImGui::Checkbox(GetVoltageLabel(m_Schematic.GetConnectivity(), node).c_str(), &measured)) {
                m_Schematic.SetVoltageMeasured(node, measured);
            }
            ImGui::PopStyleColor();
        }
    }

    if (!currents.empty()) {
        ImGui::TextDisabled("Currents");
        const std::optional<bool> measure_all = DrawAllNoneButtons("currents");
        for (std::size_t index = 0; index < currents.size(); ++index) {
            const std::string &name = currents[index].Name;
            if (measure_all) {
                m_Schematic.SetCurrentMeasured(name, *measure_all);
            }
            bool measured = m_Schematic.IsCurrentMeasured(name);
            ImGui::PushStyleColor(ImGuiCol_CheckMark, GetCurrentColor(index));
            if (ImGui::Checkbox(std::format("I({})", name).c_str(), &measured)) {
                m_Schematic.SetCurrentMeasured(name, measured);
            }
            ImGui::PopStyleColor();
        }
    }

    // A node a label names is offered by its name, so a channel saved with the file still finds it after edits that
    // number the nodes again
    std::vector<std::string> operands;
    for (std::size_t node = 1; node < node_count; ++node) {
        operands.push_back(GetVoltageLabel(m_Schematic.GetConnectivity(), static_cast<int>(node)));
    }
    for (const Core::ComponentTrace &current : currents) {
        operands.push_back(std::format("I({})", current.Name));
    }
    // The channels go over the whole run, so they wait for a run that is still going on
    const auto &growing = m_Schematic.GetTransient();
    const bool is_growing = growing && !growing->IsComplete && m_Controls.IsRunning();
    ImGui::BeginDisabled(is_growing);
    m_MathChannels.Draw(operands);
    ImGui::EndDisabled();
    if (is_growing) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Math waits for the end of the run");
        ImGui::PopTextWrapPos();
    }
    // Buses read the signals of logic parts, so a circuit without any has none to offer
    std::vector<std::string> signals;
    for (const LogicNode &logic_node : FindLogicNodes(m_Schematic.BuildCircuit())) {
        if (static_cast<std::size_t>(logic_node.Node) < node_count) {
            signals.push_back(GetVoltageLabel(m_Schematic.GetConnectivity(), logic_node.Node));
        }
    }
    if (!signals.empty() || !m_Schematic.GetBuses().empty()) {
        m_Buses.Draw(signals);
    }
    ImGui::EndChild();
}

/**
 * @brief   One tab per analysis, and the spectrum of the transient; a new result brings its tab to the front once.
 */
void OutputWindow::DrawTabs(const FrameResults &frame) {
    const std::optional<PlotTab> tab_to_show = std::exchange(m_TabToShow, std::nullopt);
    const auto begin_tab = [this, &tab_to_show](const char *label, const PlotTab tab) {
        if (!ImGui::BeginTabItem(label, nullptr,
                                 tab_to_show == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
            return false;
        }
        m_ShownTab = tab;
        return true;
    };
    if (begin_tab("Operating point", PlotTab::OPERATING_POINT)) {
        DrawOperatingPointTab(m_Schematic);
        ImGui::EndTabItem();
    }
    if (begin_tab("Transient", PlotTab::TRANSIENT)) {
        m_TransientTab.Draw(frame);
        ImGui::EndTabItem();
    }
    if (begin_tab("Spectrum", PlotTab::SPECTRUM)) {
        // The spectrum goes over the stretch the transient statistics measure
        m_SpectrumTab.Draw(frame, m_TransientTab.GetMeasuredSpan());
        ImGui::EndTabItem();
    }
    if (begin_tab("AC sweep", PlotTab::AC_SWEEP)) {
        m_ACSweepTab.Draw(frame);
        ImGui::EndTabItem();
    }
    if (begin_tab("DC sweep", PlotTab::DC_SWEEP)) {
        m_DCSweepTab.Draw(frame);
        ImGui::EndTabItem();
    }
    if (begin_tab("Noise", PlotTab::NOISE)) {
        m_NoiseTab.Draw();
        ImGui::EndTabItem();
    }
    if (begin_tab("Transfer function", PlotTab::TRANSFER_FUNCTION)) {
        DrawTransferFunctionTab(m_Schematic);
        ImGui::EndTabItem();
    }
    if (begin_tab("Truth table", PlotTab::TRUTH_TABLE)) {
        DrawTruthTableTab(m_Schematic);
        ImGui::EndTabItem();
    }
}

/**
 * @brief   A plain button apart from the tabs, so it does not read as another page; its popup hangs below it, aligned
 *          to its right edge, so it stays inside the window.
 */
void OutputWindow::DrawExportButton(const ImVec2 tab_row, const float tab_row_width, const float export_width,
                                    const FrameResults &frame) {
    ImGui::SetCursorScreenPos({tab_row.x + tab_row_width - export_width, tab_row.y});
    if (ImGui::Button(EXPORT_LABEL)) {
        m_Exporter.OpenPopup();
    }
    ImGui::SetItemTooltip("Save the plot of this tab as an SVG image or a CSV table");
    ImGui::SetNextWindowPos({ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y}, ImGuiCond_Appearing,
                            ImVec2(1.0f, 0.0f));
    if (const std::optional<ExportChoice> choice =
            m_Exporter.DrawPopup(GetTabTitle(m_ShownTab), CanExport(frame), HasLongerRun())) {
        const std::string suffix =
            std::format("{}{}", GetTabFileSuffix(m_ShownTab), choice->IsWholeRun ? "-whole-run" : "");
        m_Exporter.Start(BuildShownFigure(frame, choice->IsWholeRun), choice->Format, m_Schematic.GetFilePath(),
                         suffix);
    }
}

/**
 * @brief   Whether the shown tab has something to export: a result with a measured trace or a math channel.
 */
bool OutputWindow::CanExport(const FrameResults &frame) const {
    switch (m_ShownTab) {
    case PlotTab::TRANSIENT:
        return m_TransientTab.CanExport(frame);
    case PlotTab::SPECTRUM:
        return m_SpectrumTab.CanExport();
    case PlotTab::AC_SWEEP:
        return m_ACSweepTab.CanExport(frame);
    case PlotTab::DC_SWEEP:
        return m_DCSweepTab.CanExport(frame);
    case PlotTab::NOISE:
        return m_NoiseTab.CanExport();
    case PlotTab::OPERATING_POINT:
    case PlotTab::TRANSFER_FUNCTION:
    case PlotTab::TRUTH_TABLE:
    default:
        return false;
    }
}

/**
 * @brief   Whether the transient plot shows only a window of a longer run that is over, whose every point a CSV table
 *          can hold.
 */
bool OutputWindow::HasLongerRun() const {
    const auto &transient = m_Schematic.GetTransient();
    return m_ShownTab == PlotTab::TRANSIENT && transient &&
           transient->Times.size() > m_Playback.Window.GetWindow().Count && !m_Controls.IsRunning();
}

/**
 * @brief   The figure of the shown tab, which CanExport() has found something to export in.
 */
ExportFigure OutputWindow::BuildShownFigure(const FrameResults &frame, const bool is_whole_run) const {
    switch (m_ShownTab) {
    case PlotTab::TRANSIENT:
        return m_TransientTab.BuildFigure(frame, is_whole_run);
    case PlotTab::SPECTRUM:
        return m_SpectrumTab.BuildFigure();
    case PlotTab::AC_SWEEP:
        return m_ACSweepTab.BuildFigure();
    case PlotTab::DC_SWEEP:
        return m_DCSweepTab.BuildFigure(frame);
    case PlotTab::NOISE:
        return m_NoiseTab.BuildFigure();
    case PlotTab::OPERATING_POINT:
    case PlotTab::TRANSFER_FUNCTION:
    case PlotTab::TRUTH_TABLE:
    default:
        return {};
    }
}

} // namespace GUI
