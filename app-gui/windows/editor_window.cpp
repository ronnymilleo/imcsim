/**
 * @file    editor_window.cpp
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#include "windows/editor_window.h"

#include "components/net_label.h"
#include "components/subcircuit.h"
#include "document/block_nesting.h"
#include "document/subcircuit_pins.h"
#include "drawing/schematic_drawing.h"
#include "drawing/text_fitting.h"
#include "drawing/theme.h"
#include "editing/element_factory.h"
#include "editing/group_editing.h"
#include "editing/live_input.h"
#include "editing/probing.h"
#include "editing/shortcuts.h"
#include "editing/wire_editing.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace GUI {

namespace {

constexpr float MIN_CANVAS_SIZE = 50.0f;
/// Below this spacing the dots are thinned out, otherwise far zoom-out draws hundreds of thousands of them.
constexpr float MIN_DOT_SPACING = 8.0f;
constexpr float WIRE_CURSOR_HALF_SIZE = 4.0f;
/// How close, in pixels, the cursor must be to a wire to select it.
constexpr float WIRE_PICK_DISTANCE = 5.0f;
/// The middle dot between the run state and its reason, encoded in UTF-8.
constexpr std::string_view MIDDLE_DOT = "\xC2\xB7";
/// Space between the edge of the canvas and the instant it shows, in font sizes.
constexpr float INSTANT_MARGIN = 0.5f;
/// Space between the parts of the status bar, in font sizes.
constexpr float STATUS_GAP_FONT_SIZES = 1.5f;
/// Share of the status bar the run state may take, so a long failure reason leaves room for the hint.
constexpr float MAX_RUN_STATUS_SHARE = 0.6f;
/// The edge choice of a block pin, this many font sizes wide.
constexpr float PIN_SIDE_FIELD_WIDTH = 6.0f;

/**
 * @brief   "Transient running", with the share done when the run reports one.
 */
std::string FormatRunProgress(const char *analysis_name, const bool is_stopping, const double progress) {
    if (is_stopping) {
        return std::format("Stopping {}", analysis_name);
    }
    if (progress > 0.0) {
        return std::format("{} running, {:.0f}%", analysis_name, progress * 100.0);
    }
    return std::format("{} running", analysis_name);
}

/**
 * @brief   "Transient live at 12.5ms", or paused: a live run has no end to count toward, so it tells how far it has
 *          got.
 */
std::string FormatLiveProgress(const AnalysisControls &controls, const std::optional<Core::Transient> &transient) {
    const char *state = controls.IsLivePaused() ? "paused" : "live";
    if (!transient || transient->Times.empty()) {
        return std::format("Transient {}", state);
    }
    return std::format("Transient {} at {}s", state, Core::FormatValue(transient->Times.back()));
}

/**
 * @struct  RunStatusLine
 * @brief   The text of the run status in the status bar, its color, and whether it is a run in progress.
 */
struct RunStatusLine {
    std::string Text;
    /// What follows the state in a muted color, such as the reason a run failed, or empty.
    std::string Detail;
    ImVec4 Color;
    bool IsRunning = false;
    /// Text as wide as the line can get, whose width it keeps so it does not shift as its figures change, or empty.
    std::string_view WidestText;
};

/**
 * @brief   What the last run reported, in the color its outcome asks for; a run in progress takes the place of that
 *          report.
 */
RunStatusLine DescribeRunStatus(const AnalysisControls &controls, const std::optional<Core::Transient> &transient) {
    const ImVec4 plain = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    if (controls.IsLive() && !controls.IsStopping()) {
        return {.Text = FormatLiveProgress(controls, transient),
                .Color = plain,
                .IsRunning = true,
                .WidestText = "Transient paused at 888.8ms"};
    }
    if (const std::optional<Analysis> running = controls.GetRunningAnalysis()) {
        return {.Text = FormatRunProgress(GetAnalysisName(*running), controls.IsStopping(), controls.GetRunProgress()),
                .Color = plain,
                .IsRunning = true};
    }
    const std::optional<Analysis> last_run = controls.GetLastRun();
    if (!last_run) {
        return {.Text = "", .Color = plain, .IsRunning = false};
    }
    const std::string_view name = GetAnalysisName(*last_run);
    switch (controls.GetOutcome(*last_run)) {
    case RunOutcome::SUCCEEDED:
        return {.Text = std::format("{} done", name), .Color = plain, .IsRunning = false};
    case RunOutcome::SUCCEEDED_WITH_WARNINGS:
        return {
            .Text = std::format("{} done, with warnings", name), .Color = GetWarningTextColor(), .IsRunning = false};
    case RunOutcome::STOPPED:
        return {.Text = std::format("{} stopped", name), .Color = plain, .IsRunning = false};
    case RunOutcome::FAILED: {
        const std::optional<std::string> &error = controls.GetError(*last_run);
        return {.Text = std::format("{} failed", name),
                .Detail = error ? std::string(GetFirstLine(*error)) : std::string(),
                .Color = GetErrorTextColor(),
                .IsRunning = false};
    }
    case RunOutcome::NONE:
    default:
        return {.Text = "", .Color = plain, .IsRunning = false};
    }
}

/**
 * @brief   "Transient done", in the warning color when something needs a look and in the error color after a failure,
 *          followed by the reason; a click opens the report of the run, with the ngspice output.
 * @param[in] line       State of the run, from DescribeRunStatus().
 * @param[in] controls   Runs, whose report a click opens.
 * @param[in] max_right  Where the state must end, shortened at a word to fit.
 * @return  Where the state ends, or where the line starts when there is none.
 */
float DrawRunStatus(const RunStatusLine &line, const AnalysisControls &controls, const float max_right) {
    const std::optional<Analysis> last_run = controls.GetLastRun();
    const ImVec2 line_start = ImGui::GetCursorScreenPos();
    if (line.Text.empty()) {
        return line_start.x;
    }
    const auto measure = [](const std::string_view text) {
        return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    };
    const float max_width = max_right - line_start.x;
    const std::string text = ShortenToWidth(line.Text, max_width, measure);
    const float text_width = measure(text);
    // The reason gets what the state leaves
    const std::string detail = line.Detail.empty() ? std::string()
                                                   : ShortenToWidth(std::format(" {} {}", MIDDLE_DOT, line.Detail),
                                                                    max_width - text_width, measure);
    // A run in progress keeps the width of its widest text, so the hint does not shift as its figures change
    const float width = std::min(max_width, std::max(text_width + measure(detail), measure(line.WidestText)));
    if (line.IsRunning) {
        ImGui::TextDisabled("%s", text.c_str());
    } else {
        if (ImGui::Selectable("##run_status", false, ImGuiSelectableFlags_None, {width, 0.0f})) {
            ImGui::OpenPopup("run_report");
        }
        ImGui::SetItemTooltip("Show what the run reported, with the ngspice output");
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        draw_list->AddText(line_start, ImGui::GetColorU32(line.Color), text.c_str());
        draw_list->AddText({line_start.x + text_width, line_start.y}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                           detail.c_str());
    }
    if (ImGui::BeginPopup("run_report")) {
        if (last_run) {
            controls.DrawRunReport(*last_run);
        }
        ImGui::EndPopup();
    }
    return line_start.x + width;
}

/**
 * @brief   The character of a key that starts a value, from the main row or the keypad: a digit, a point or a minus.
 */
std::optional<char> FindTypedValueStart() {
    for (int digit = 0; digit <= 9; ++digit) {
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + digit), false) ||
            ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_Keypad0 + digit), false)) {
            return static_cast<char>('0' + digit);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Period, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false)) {
        return '.';
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) {
        return '-';
    }
    return std::nullopt;
}

constexpr const char *PART_POPOVER = "Part properties";
/// How far below the top of the canvas the properties popover opens, in font sizes.
constexpr float POPOVER_TOP_MARGIN_FONT_SIZES = 1.0f;
constexpr const char *CONTEXT_MENU = "Schematic actions";

void DrawGrid(ImDrawList *draw_list, const ViewTransform &view, const ImVec2 origin, const ImVec2 size,
              const float zoom) {
    int step = 1;
    while (zoom * static_cast<float>(step) < MIN_DOT_SPACING) {
        step *= 2;
    }
    const float step_size = static_cast<float>(step);

    const ImVec2 world_min = view.ToWorld(origin);
    const ImVec2 world_max = view.ToWorld(origin + size);
    const ImVec2 dot_half_size = {1.0f, 1.0f};
    const int first_column = static_cast<int>(std::floor(world_min.x / step_size));
    const int last_column = static_cast<int>(std::floor(world_max.x / step_size));
    const int first_row = static_cast<int>(std::floor(world_min.y / step_size));
    const int last_row = static_cast<int>(std::floor(world_max.y / step_size));
    for (int column = first_column; column <= last_column; ++column) {
        for (int row = first_row; row <= last_row; ++row) {
            const ImVec2 dot =
                view.ToScreen({static_cast<float>(column) * step_size, static_cast<float>(row) * step_size});
            draw_list->AddRectFilled(dot - dot_half_size, dot + dot_half_size, GetPalette().GridDot);
        }
    }
}

constexpr ImU32 SELECTION_BOX_FILL_ALPHA = 40;

ImU32 WithAlpha(const ImU32 color, const ImU32 alpha) {
    return (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

} // namespace

/**
 * @brief   Creates the editor for a schematic.
 * @param[in] schematic       Schematic to edit; it must outlive the window.
 * @param[in] controls        Runs of the analyses, for the Run button; they must outlive the window.
 * @param[in] settings_panel  Analysis picker and settings, for the popover beside Run; it must outlive the window.
 * @param[in] part_editor     Fields of the selected part, for the properties popover; it must outlive the window.
 * @param[in] glow_renderer   Draws the glow of lit LEDs; it must outlive the window.
 * @param[in] playback        Instant of the transient to show, which the time bar moves; it must outlive the window.
 * @param[in] preferences     How the schematic is drawn and exported; they must outlive the window.
 */
EditorWindow::EditorWindow(Schematic &schematic, AnalysisControls &controls, AnalysisSettingsPanel &settings_panel,
                           PartEditor &part_editor, GlowRenderer &glow_renderer, TransientPlayback &playback,
                           const Preferences &preferences)
    : AppWindow("Schematic", false), m_RootSchematic(schematic), m_Schematic(&schematic), m_Controls(controls),
      m_SettingsPanel(settings_panel), m_PartEditor(part_editor), m_Playback(playback), m_Preferences(preferences),
      m_Files(schematic, preferences,
              {.StopEditing = [this] { StopEditing(); },
               .FrameSchematic = [this] { m_View.RequestDefaultFrame(); },
               .PlaceGroup = [this](PastedGroup group) { StartPastingGroup(std::move(group)); },
               .OpenSpiceLibrary =
                   [this](std::string file_name, Core::SpiceLibrary library) {
                       m_SpiceDialog.OpenImport(std::move(file_name), std::move(library));
                   }}),
      m_Toolbar(controls, settings_panel, schematic, preferences), m_PartPicker(preferences),
      m_Overlays(glow_renderer, playback, preferences) {
}

/**
 * @brief   Runs what does not depend on the editor being shown: the transient playback, file dialog results, quit,
 *          example and menu requests, the global shortcuts and the file popups.
 * @note    Call it every frame before Render() and outside any window: a docked editor behind another tab skips
 *          Draw(), and the popups opened here are drawn here too, so they show whichever window is in front.
 */
void EditorWindow::Update() {
    // The playhead of the Output window moves with the time too, so playing goes on while this window is hidden. It
    // plays the window of points that the plot shows; while the window follows a run, the schematic shows its newest
    // point
    if (const std::optional<TimeSpan> window = GetWindowSpan()) {
        if (m_Playback.Window.IsLive()) {
            m_Playback.Time = window->To;
            m_Playback.Playing = false;
        }
        m_Playback.Advance(ImGui::GetIO().DeltaTime, window->From, window->To);
    }
    m_Files.Update();
    HandleCommandRequest();
    HandleGlobalShortcuts();
    m_Files.DrawPopups();
    DrawSubcircuitDialog();
    if (std::optional<SpiceBlockResult> result = m_SpiceDialog.Draw()) {
        ApplySpiceBlockResult(std::move(*result));
    }
}

/**
 * @brief   Asks to quit, as when the user closes the main window. Call it between frames.
 * @note    Quitting is confirmed right away when there is nothing to lose; otherwise the next frame asks the user
 *          to discard the changes. Check IsQuitConfirmed() after drawing the frame. A block being edited is applied
 *          to the schematic first, so its edits count as changes; when it cannot be, the block stays open with the
 *          reason and nothing quits.
 */
void EditorWindow::RequestQuit() {
    if (LeaveSubcircuitForDocument()) {
        m_Files.RequestQuit();
    }
}

/**
 * @brief   Tells whether the application may quit.
 * @return  True once a quit request was confirmed, by the user or because there were no unsaved changes.
 */
bool EditorWindow::IsQuitConfirmed() const {
    return m_Files.IsQuitConfirmed();
}

/**
 * @brief   Asks to open an example, as when the user picks one from the menu.
 * @param[in] example  One of GetExamples().
 * @note    Like quitting, the next frame opens it, after asking the user to discard unsaved changes if there are
 *          any. The example opens untitled, so saving it asks for a file. A block being edited is applied first, as
 *          for quitting.
 */
void EditorWindow::RequestExample(const Example &example) {
    if (LeaveSubcircuitForDocument()) {
        m_Files.RequestExample(example);
    }
}

/**
 * @brief   Asks to insert an example into the schematic, as when the user picks it from the insert menu.
 * @param[in] example  One of GetExamples().
 * @note    The next frame starts placing it like an import. Nothing is replaced, so nothing is asked.
 */
void EditorWindow::RequestInsertExample(const Example &example) {
    m_Files.RequestInsertExample(example);
}

/**
 * @brief   Asks to import a file into the schematic, as when the user drops it on the window.
 * @param[in] path  A schematic file.
 * @note    The next frame starts placing it, as File > Import does after its dialog.
 */
void EditorWindow::RequestImport(std::filesystem::path path) {
    m_Files.RequestImport(std::move(path));
}

/**
 * @brief   Draws the File menu items that act on the schematic, with their shortcuts.
 * @note    Call between BeginMenu() and EndMenu(); the command runs on the next frame of the editor.
 */
void EditorWindow::DrawFileMenuItems() {
    ImGui::BeginDisabled(IsEditingSubcircuit());
    if (ImGui::MenuItem("New", IMCSIM_SHORTCUT "N")) {
        m_RequestedCommand = EditorCommand::NEW;
    }
    if (ImGui::MenuItem("Open...", IMCSIM_SHORTCUT "O")) {
        m_RequestedCommand = EditorCommand::OPEN;
    }
    if (ImGui::MenuItem("Save", IMCSIM_SHORTCUT "S")) {
        m_RequestedCommand = EditorCommand::SAVE;
    }
    if (ImGui::MenuItem("Save As...", IMCSIM_SHORTCUT "Shift+S")) {
        m_RequestedCommand = EditorCommand::SAVE_AS;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Import...")) {
        m_RequestedCommand = EditorCommand::IMPORT;
    }
    ImGui::SetItemTooltip("Add the circuit of a file to this schematic: it follows the cursor until a click places it");
    if (ImGui::MenuItem("Import SPICE Library...")) {
        m_RequestedCommand = EditorCommand::IMPORT_SPICE;
    }
    ImGui::SetItemTooltip("Bring in a vendor's subcircuits, such as op-amp models, as blocks kept in this schematic");
    if (ImGui::MenuItem("Export Schematic...")) {
        m_RequestedCommand = EditorCommand::EXPORT_SCHEMATIC;
    }
    ImGui::SetItemTooltip("Save the schematic as an SVG image, to show next to its plots");
    ImGui::EndDisabled();
}

/**
 * @brief   Draws the Edit menu items, with their shortcuts, enabled only when they apply.
 * @note    Call between BeginMenu() and EndMenu(); the command runs on the next frame of the editor.
 */
void EditorWindow::DrawEditMenuItems() {
    if (ImGui::MenuItem("Undo", IMCSIM_SHORTCUT "Z", false, m_Schematic->CanUndo())) {
        m_RequestedCommand = EditorCommand::UNDO;
    }
    if (ImGui::MenuItem("Redo", IMCSIM_SHORTCUT "Y", false, m_Schematic->CanRedo())) {
        m_RequestedCommand = EditorCommand::REDO;
    }
    ImGui::Separator();
    const bool part_selected = !m_Schematic->GetSelectedElements().empty();
    DrawTurnMenuItems(part_selected);
    ImGui::Separator();
    if (ImGui::MenuItem("Cut", IMCSIM_SHORTCUT "X", false, m_Schematic->HasSelection())) {
        m_RequestedCommand = EditorCommand::CUT;
    }
    if (ImGui::MenuItem("Copy", IMCSIM_SHORTCUT "C", false, m_Schematic->HasSelection())) {
        m_RequestedCommand = EditorCommand::COPY;
    }
    const char *clipboard_text = ImGui::GetClipboardText();
    const bool can_paste = clipboard_text != nullptr && LooksLikeCopiedSchematic(clipboard_text);
    if (ImGui::MenuItem("Paste", IMCSIM_SHORTCUT "V", false, can_paste)) {
        m_RequestedCommand = EditorCommand::PASTE;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Select All", IMCSIM_SHORTCUT "A")) {
        m_RequestedCommand = EditorCommand::SELECT_ALL;
    }
    if (ImGui::MenuItem("Delete", "Del", false, m_Schematic->HasSelection())) {
        m_RequestedCommand = EditorCommand::DELETE_SELECTION;
    }
    ImGui::Separator();
    DrawBlockMenuItems();
}

/**
 * @brief   Draws the View menu items of the editor.
 * @note    Call between BeginMenu() and EndMenu().
 */
void EditorWindow::DrawViewMenuItems() {
    if (ImGui::MenuItem("Fit Schematic", "Home")) {
        m_View.RequestFit();
    }
}

void EditorWindow::Draw() {
    // Only while the editor has the focus, since text fields use Home
    if (ImGui::Shortcut(ImGuiKey_Home)) {
        m_View.RequestFit();
    }
    if (std::optional<EditorMode> picked = m_PartPicker.Draw(ListPlaceableBlocks())) {
        SetMode(std::move(*picked));
    }
    ApplyToolbarRequest(m_Toolbar.Draw(m_Mode, *m_Schematic, IsEditingSubcircuit()));
    m_Overlays.Update(*m_Schematic, m_Controls);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = GetCanvasSize();
    m_CanvasCenter = {origin.x + size.x / 2.0f, origin.y + size.y / 2.0f};
    m_CanvasTop = origin.y;
    m_View.FollowCanvasSize(size);
    m_View.FrameIfRequested(*m_Schematic, size, m_Preferences.Symbols, m_Preferences.ShowTerminalNumbers);

    // The way back from a block sits over the canvas, and takes the clicks that land on it
    if (IsEditingSubcircuit()) {
        ImGui::SetNextItemAllowOverlap();
    }
    ImGui::InvisibleButton("canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    // During a live run the wheel over a potentiometer turns it instead of zooming, and a click on a part the user sets
    // by hand changes it instead of selecting it
    const std::optional<std::size_t> live_part =
        hovered ? FindLivePart(m_View.GetTransform(origin)) : std::optional<std::size_t>();
    const bool turns_part =
        live_part &&
        GetLiveAction(m_Schematic->GetElements()[*live_part]->GetComponent().GetType()) == LiveAction::TURN;
    m_View.HandlePanAndZoom(origin, hovered && !turns_part, active);

    const ViewTransform view = m_View.GetTransform(origin);
    const bool took_mouse = HandleLiveInput(live_part);
    // Selection runs before drawing so a dragged element is drawn where the cursor is this frame
    HandleSelection(view, hovered && !took_mouse);
    HandleContextMenu(view, hovered);
    HandleValueTyping(view);
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(origin, origin + size, true);
    draw_list->AddRectFilled(origin, origin + size, GetPalette().CanvasBackground);
    DrawGrid(draw_list, view, origin, size, m_View.GetZoom());

    DrawListCanvas canvas(draw_list);
    m_Overlays.DrawWires(canvas, view, *m_Schematic, m_View.GetZoom());
    m_Overlays.DrawElements(canvas, view, *m_Schematic, m_Controls.IsLive() && IsSelecting(m_Mode),
                            m_HeldButton ? m_HeldButton : live_part);
    // Over the symbols, so a lit LED looks lit through its own lines
    m_Overlays.DrawGlows(draw_list, view, *m_Schematic, m_View.GetZoom());
    if (m_Preferences.ShowTerminalNumbers) {
        DrawTerminalNumbers(canvas, view, *m_Schematic, SCREEN_TERMINAL_RING_SCALE);
    }
    m_Overlays.DrawNodeVoltages(draw_list, view, *m_Schematic);
    DrawMeasurementMarkers(canvas, view, *m_Schematic);
    DrawSelectionBox(draw_list, view);

    HandleModeKeys();
    // Previews go over the schematic, so the mode handles its clicks after it is drawn
    std::optional<EditorMode> next =
        std::visit([&](auto &mode) { return HandleMode(mode, canvas, view, hovered); }, m_Mode);
    if (next) {
        SetMode(std::move(*next));
    }
    HandleClipboardShortcuts();
    m_Overlays.DrawColorLegend(draw_list, origin, size, *m_Schematic);
    DrawShownInstant(draw_list, origin);

    draw_list->PopClipRect();
    DrawBlockBar(origin);
    DrawStatusBar(view, hovered);
    DrawContextMenu();
    DrawPartPopover();
    m_PopupOpenLastFrame = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

/**
 * @brief   The room left for the canvas, less the line of the status bar below it, never smaller than a usable size.
 */
ImVec2 EditorWindow::GetCanvasSize() {
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.y -= ImGui::GetTextLineHeightWithSpacing();
    return {std::max(size.x, MIN_CANVAS_SIZE), std::max(size.y, MIN_CANVAS_SIZE)};
}

/**
 * @brief   W starts drawing wires, P probing and L placing a net label, while the editor has the focus.
 */
void EditorWindow::HandleModeKeys() {
    if (!ImGui::IsWindowFocused()) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_W)) {
        SetMode(WiringMode{});
    }
    if (ImGui::IsKeyPressed(ImGuiKey_P)) {
        SetMode(ProbingMode{});
    }
    if (ImGui::IsKeyPressed(ImGuiKey_L) && !ImGui::GetIO().KeyCtrl) {
        SetMode(PlacingMode{.Type = Core::ComponentType::NET_LABEL});
    }
}

/**
 * @brief   Starts the mode a toolbar button asked for, or runs its command.
 */
void EditorWindow::ApplyToolbarRequest(ToolbarRequest request) {
    if (request.Mode) {
        SetMode(std::move(*request.Mode));
    }
    if (!request.Command) {
        return;
    }
    switch (*request.Command) {
    case ToolbarCommand::UNDO:
        Undo();
        break;
    case ToolbarCommand::REDO:
        Redo();
        break;
    case ToolbarCommand::FIT:
        m_View.RequestFit();
        break;
    default:
        break;
    }
}

/**
 * @brief   The span of time of the window of the transient that plays, or none without a transient.
 */
std::optional<EditorWindow::TimeSpan> EditorWindow::GetWindowSpan() const {
    const auto &transient = m_Schematic->GetTransient();
    const PointWindow window = m_Playback.Window.GetWindow();
    if (!transient || window.Count == 0 || window.First + window.Count > transient->Times.size()) {
        return std::nullopt;
    }
    return TimeSpan{.From = transient->Times[window.First], .To = transient->Times[window.First + window.Count - 1]};
}

/**
 * @brief   Writes the instant of the transient the schematic shows at the top left of the canvas, so it reads while the
 *          playback, under the plot of the Output window, moves it.
 * @param[in] draw_list  Where to draw.
 * @param[in] origin     Top left corner of the canvas, in screen pixels.
 */
void EditorWindow::DrawShownInstant(ImDrawList *draw_list, const ImVec2 origin) const {
    const std::optional<DisplayedState> &displayed = m_Overlays.GetDisplayed();
    const std::optional<double> time = displayed ? displayed->Time : std::nullopt;
    if (!time || !GetWindowSpan()) {
        return;
    }
    const std::string text = std::format("t = {}s", Core::FormatValue(*time));
    const float margin = ImGui::GetFontSize() * INSTANT_MARGIN;
    draw_list->AddText({origin.x + margin, origin.y + margin}, ImGui::GetColorU32(ImGuiCol_TextDisabled), text.c_str());
}

/**
 * @brief   While a block is open, the path from the schematic down to it, in the top left corner of the canvas so it
 * stays in sight wherever the toolbar wraps; a click on a level above applies the blocks below it and goes back there.
 */
void EditorWindow::DrawBlockBar(const ImVec2 origin) {
    if (!IsEditingSubcircuit()) {
        return;
    }
    const ImVec2 after_canvas = ImGui::GetCursorScreenPos();
    const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
    const ImVec4 text_color = ImGui::ColorConvertU32ToFloat4(GetPalette().CanvasText);
    ImGui::SetCursorScreenPos(origin + spacing);
    std::optional<std::size_t> picked;
    if (ImGui::Button("Schematic")) {
        picked = 0;
    }
    ImGui::SetItemTooltip("Apply the open blocks and return to the schematic");
    for (std::size_t level = 0; level < m_OpenBlocks.size(); ++level) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(text_color, ">");
        ImGui::SameLine();
        const std::string &name = m_OpenBlocks[level].Name;
        if (level + 1 == m_OpenBlocks.size()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(text_color, "%s", name.c_str());
            continue;
        }
        ImGui::PushID(static_cast<int>(level));
        if (ImGui::Button(name.c_str())) {
            picked = level + 1;
        }
        ImGui::PopID();
        ImGui::SetItemTooltip("Apply the blocks inside %s and return to it", name.c_str());
    }
    ImGui::SetCursorScreenPos(after_canvas);
    if (picked) {
        ReturnToLevel(*picked);
    }
}

/**
 * @brief   The state of the last run first, then what the current mode does and which keys it takes, so the shortcuts
 *          can be learned while working, and the grid point under the cursor on the right.
 * @details The run state leads, so a finished run and a failed one tell apart at a glance; the hint takes the space
 *          left and is shortened at a word, never cut mid-word.
 */
void EditorWindow::DrawStatusBar(const ViewTransform &view, const bool hovered) {
    std::string hint;
    if (std::optional<std::string> mode_hint = DescribeMode(m_Mode)) {
        hint = std::move(*mode_hint);
    } else {
        std::optional<std::string> selected_part;
        if (const UIElement *element = m_Schematic->GetSelectedElement()) {
            const Core::Component &component = element->GetComponent();
            selected_part =
                component.GetName().empty() ? std::string(GetPartName(component.GetType())) : component.GetName();
        }
        const std::size_t count = m_Schematic->GetSelectedElements().size() + m_Schematic->GetSelectedWires().size();
        hint = DescribeSelecting(m_Controls.IsLive(), selected_part, count);
    }
    const ImVec2 line_start = ImGui::GetCursorScreenPos();
    const float gap = ImGui::GetFontSize() * STATUS_GAP_FONT_SIZES;
    float right_edge = line_start.x + ImGui::GetContentRegionAvail().x;
    if (hovered) {
        const GridPoint point = Snap(view.ToWorld(ImGui::GetIO().MousePos));
        const std::string position = std::format("x {}, y {}", point.X, point.Y);
        right_edge -= ImGui::CalcTextSize(position.c_str()).x;
        ImGui::SetCursorScreenPos({right_edge, line_start.y});
        ImGui::TextDisabled("%s", position.c_str());
        right_edge -= gap;
    }
    ImGui::SetCursorScreenPos(line_start);
    // The reason of a failure is worth more than the hint, so it takes the whole line
    const RunStatusLine line = DescribeRunStatus(m_Controls, m_Schematic->GetTransient());
    if (!line.Detail.empty()) {
        DrawRunStatus(line, m_Controls, right_edge);
        return;
    }
    float hint_start =
        DrawRunStatus(line, m_Controls, line_start.x + (right_edge - line_start.x) * MAX_RUN_STATUS_SHARE);
    if (hint_start > line_start.x) {
        hint_start += gap;
    }
    const std::string fitted = ShortenToWidth(hint, right_edge - hint_start, [](const std::string_view text) {
        return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    });
    ImGui::SetCursorScreenPos({hint_start, line_start.y});
    ImGui::TextDisabled("%s", fitted.c_str());
}

/**
 * @brief   What the Probe tool would measure under the cursor: the current of a part, or the voltage of a wire.
 */
std::optional<MeasurementTarget> EditorWindow::FindTargetAt(const ViewTransform &view) const {
    return FindMeasurementTarget(m_Schematic->GetElements(), m_Schematic->GetWires(), m_Schematic->GetConnectivity(),
                                 view.ToWorld(ImGui::GetIO().MousePos), WIRE_PICK_DISTANCE / m_View.GetZoom());
}

/**
 * @brief   Leaves the mode the editor is in for another, as the toolbar, the part picker, a key or the end of a mode
 *          asks.
 * @details Selecting keeps the selection, which a paste or a placed label has just made; every other mode starts with
 *          an empty selection, ending a drag in progress first. Wire mode asked again keeps the wire in progress, and
 *          placing starts over with the new part. A block cannot be placed where it would contain itself.
 */
void EditorWindow::SetMode(EditorMode mode) {
    if (std::holds_alternative<SelectingMode>(mode)) {
        if (!IsSelecting(m_Mode)) {
            m_Mode = SelectingMode{};
        }
        return;
    }
    if (const auto *placing = std::get_if<PlacingMode>(&mode);
        placing != nullptr && !placing->Block.empty() && !CanPlaceBlock(placing->Block)) {
        return;
    }
    ClearSelection();
    if (std::holds_alternative<WiringMode>(mode) && std::holds_alternative<WiringMode>(m_Mode)) {
        return;
    }
    m_Mode = std::move(mode);
}

/**
 * @brief   The selecting mode, whose drag and selection box live as long as it does.
 * @return  The mode, or null while another one is on.
 */
SelectingMode *EditorWindow::GetSelecting() {
    return std::get_if<SelectingMode>(&m_Mode);
}

/**
 * @brief   A drag keeps the positions of the items it moves, so deleting, cutting or undoing waits until it ends.
 */
bool EditorWindow::IsDragging() const {
    const auto *selecting = std::get_if<SelectingMode>(&m_Mode);
    return selecting != nullptr && selecting->Drag.has_value();
}

/**
 * @brief   Before the schematic is cleared or replaced: a drag or paste in progress would point at parts that are gone.
 * @details Probing measures whatever schematic is shown, so it goes on.
 */
void EditorWindow::StopEditing() {
    EndDrag();
    if (!IsSelecting(m_Mode) && !std::holds_alternative<ProbingMode>(m_Mode)) {
        m_Mode = SelectingMode{};
    }
}

bool EditorWindow::IsEditingSubcircuit() const {
    return !m_OpenBlocks.empty();
}

/**
 * @brief   Quitting or opening another document while blocks are open applies them first, innermost first, so their
 *          edits count as unsaved changes of the document instead of vanishing with it.
 * @details Returns false when a block cannot be applied, which leaves it open with the reason shown.
 */
bool EditorWindow::LeaveSubcircuitForDocument() {
    return ReturnToLevel(0);
}

/**
 * @brief   Applies the open blocks, innermost first, until as many are left open as asked: 0 goes back to the
 * schematic.
 * @details Returns false when a block cannot be applied, which stops there with the reason shown.
 */
bool EditorWindow::ReturnToLevel(const std::size_t depth) {
    while (m_OpenBlocks.size() > depth) {
        const std::size_t open = m_OpenBlocks.size();
        FinishSubcircuitEditing(true);
        if (m_OpenBlocks.size() == open) {
            return false;
        }
    }
    return true;
}

/**
 * @brief   Shows a schematic, the document or the contents of a block, starting it unselected and framed.
 * @details A drag belongs to the schematic it started on, and a part or a group on its way would land in the other one,
 *          so both end first.
 */
void EditorWindow::ShowSchematic(Schematic &schematic) {
    EndDrag();
    if (std::holds_alternative<PlacingMode>(m_Mode) || std::holds_alternative<PastingMode>(m_Mode)) {
        m_Mode = SelectingMode{};
    }
    m_Schematic = &schematic;
    m_PartEditor.SetSchematic(schematic);
    if (IsEditingSubcircuit()) {
        OfferBlockParameters();
    } else {
        m_PartEditor.SetBlockParameters({});
    }
    m_Schematic->ClearSelection();
    m_Overlays.Reset();
    m_View.RequestDefaultFrame();
}

/**
 * @brief   Whether a block can be placed where the editor is: never inside itself, nor inside a block it contains, at
 *          any depth.
 */
bool EditorWindow::CanPlaceBlock(const std::string_view name) const {
    return std::ranges::none_of(m_OpenBlocks, [&](const OpenBlock &block) {
        return ContainsBlock(m_RootSchematic.GetSubcircuits(), name, block.Name);
    });
}

/**
 * @brief   The blocks of the document the part picker offers where the editor is, which CanPlaceBlock() allows.
 */
std::vector<const SavedSubcircuit *> EditorWindow::ListPlaceableBlocks() const {
    std::vector<const SavedSubcircuit *> blocks;
    for (const SavedSubcircuit &definition : m_RootSchematic.GetSubcircuits()) {
        if (CanPlaceBlock(definition.Name)) {
            blocks.push_back(&definition);
        }
    }
    return blocks;
}

/**
 * @brief   The contents of every open block take the blocks and models of the document again, after it gained some.
 */
void EditorWindow::SyncOpenBlockDefinitions() {
    for (OpenBlock &block : m_OpenBlocks) {
        block.Content->CopyDefinitionsFrom(m_RootSchematic, block.Name);
    }
}

/**
 * @brief   The parts of the block can follow the parameters in the settings, as they stand.
 */
void EditorWindow::OfferBlockParameters() {
    std::vector<std::string> names;
    for (const Core::SubcircuitParameter &parameter : m_SubcircuitParameters) {
        names.push_back(parameter.Name);
    }
    m_PartEditor.SetBlockParameters(std::move(names));
}

void EditorWindow::RequestCreateSubcircuit() {
    if (IsEditingSubcircuit()) {
        return;
    }
    auto candidates = m_RootSchematic.GetSubcircuitPinCandidates();
    m_SubcircuitName = "block";
    while (std::ranges::find(m_RootSchematic.GetSubcircuits(), m_SubcircuitName, &SavedSubcircuit::Name) !=
           m_RootSchematic.GetSubcircuits().end()) {
        m_SubcircuitName += "_2";
    }
    m_SubcircuitPins.clear();
    m_SubcircuitParameters.clear();
    m_SubcircuitError.clear();
    if (candidates) {
        for (const auto &candidate : *candidates) {
            m_SubcircuitPins.push_back({candidate.SuggestedName, candidate.Point});
        }
    } else {
        m_SubcircuitError = candidates.error();
    }
    m_SubcircuitDialogRequested = true;
}

/**
 * @brief   Opens the block selected where the editor is, which may be inside another block: the way back up keeps the
 *          path, and the block it leaves keeps its pins and parameters until the editor comes back to it.
 * @details A block kept as SPICE text has no drawing to open, only its pins and parameters to set.
 */
void EditorWindow::RequestEditSelectedSubcircuit() {
    const UIElement *element = m_Schematic->GetSelectedElement();
    if (element == nullptr || element->GetComponent().GetType() != Core::ComponentType::SUBCIRCUIT) {
        return;
    }
    const std::string name = static_cast<const Core::SubcircuitInstance &>(element->GetComponent()).GetDefinitionName();
    const std::vector<SavedSubcircuit> &definitions = m_RootSchematic.GetSubcircuits();
    const auto definition = std::ranges::find(definitions, name, &SavedSubcircuit::Name);
    if (definition != definitions.end() && definition->Spice) {
        m_SpiceDialog.OpenSettings(*definition);
        return;
    }
    auto content = m_RootSchematic.OpenSubcircuit(name);
    if (!content) {
        m_SubcircuitError = content.error();
        m_SubcircuitDialogRequested = true;
        return;
    }
    if (IsEditingSubcircuit()) {
        m_OpenBlocks.back().Pins = std::move(m_SubcircuitPins);
        m_OpenBlocks.back().Parameters = std::move(m_SubcircuitParameters);
    }
    m_OpenBlocks.push_back({.Name = name, .Content = std::move(*content), .Pins = {}, .Parameters = {}});
    m_SubcircuitName = name;
    m_SubcircuitPins = definition->Pins;
    m_SubcircuitParameters = definition->Parameters;
    ShowSchematic(*m_OpenBlocks.back().Content);
}
bool EditorWindow::SyncSubcircuitPins() {
    auto markers = m_OpenBlocks.back().Content->GetSubcircuitPinMarkers();
    if (!markers) {
        m_SubcircuitError = markers.error();
        return false;
    }
    std::vector<SavedSubcircuitPin> synchronized;
    std::set<std::string> added;
    for (const SavedSubcircuitPin &pin : m_SubcircuitPins) {
        const auto marker = std::ranges::find(*markers, pin.Name, &Schematic::SubcircuitPinCandidate::SuggestedName);
        if (marker != markers->end()) {
            synchronized.push_back({pin.Name, marker->Point});
            added.insert(pin.Name);
        }
    }
    for (const auto &marker : *markers) {
        if (added.insert(marker.SuggestedName).second) {
            synchronized.push_back({marker.SuggestedName, marker.Point});
        }
    }
    m_SubcircuitPins = std::move(synchronized);
    return true;
}

bool EditorWindow::ApplySubcircuitPinNames() {
    Schematic &content = *m_OpenBlocks.back().Content;
    for (auto &element : content.GetElements()) {
        if (!Core::IsSubcircuitPin(element->GetComponent().GetType())) {
            continue;
        }
        const GridPoint point = element->GetTerminals().front();
        const auto pin = std::ranges::find(m_SubcircuitPins, point, &SavedSubcircuitPin::Point);
        if (pin == m_SubcircuitPins.end()) {
            m_SubcircuitError = "Every pin marker needs a block pin name";
            return false;
        }
        auto &marker = static_cast<Core::NetLabel &>(element->GetComponent());
        if (marker.GetNet() != pin->Name) {
            marker.SetNet(pin->Name);
            content.MarkModified();
        }
    }
    return true;
}

/**
 * @brief   Closes the innermost open block, applying it to the document or dropping its changes, and goes back to the
 *          level above it.
 * @details Applied, it is one undo step of the document, and the blocks still open take it too, since they may hold
 *          instances of it. When it cannot be applied it stays open with the reason.
 */
void EditorWindow::FinishSubcircuitEditing(const bool apply) {
    if (!IsEditingSubcircuit()) {
        return;
    }
    const OpenBlock &block = m_OpenBlocks.back();
    if (apply) {
        if (!SyncSubcircuitPins()) {
            m_SubcircuitDialogRequested = true;
            return;
        }
        // A block opened and closed without a change leaves the document unmodified
        const auto definition = std::ranges::find(m_RootSchematic.GetSubcircuits(), block.Name, &SavedSubcircuit::Name);
        const bool is_changed = block.Content->IsModified() || definition == m_RootSchematic.GetSubcircuits().end() ||
                                definition->Pins != m_SubcircuitPins ||
                                definition->Parameters != m_SubcircuitParameters;
        if (is_changed) {
            auto updated =
                m_RootSchematic.UpdateSubcircuit(block.Name, *block.Content, m_SubcircuitPins, m_SubcircuitParameters);
            if (!updated) {
                m_SubcircuitError = updated.error();
                m_SubcircuitDialogRequested = true;
                return;
            }
            m_RootSchematic.CommitUndoStep();
            // Every copy holds the same blocks as the document, so they take it as the document did
            for (std::size_t level = 0; level + 1 < m_OpenBlocks.size(); ++level) {
                static_cast<void>(m_OpenBlocks[level].Content->UpdateSubcircuit(
                    block.Name, *block.Content, m_SubcircuitPins, m_SubcircuitParameters));
            }
        }
    }
    m_OpenBlocks.pop_back();
    if (IsEditingSubcircuit()) {
        OpenBlock &outer = m_OpenBlocks.back();
        m_SubcircuitName = outer.Name;
        m_SubcircuitPins = std::move(outer.Pins);
        m_SubcircuitParameters = std::move(outer.Parameters);
        ShowSchematic(*outer.Content);
    } else {
        m_SubcircuitPins.clear();
        m_SubcircuitParameters.clear();
        ShowSchematic(m_RootSchematic);
    }
}
void EditorWindow::DrawSubcircuitDialog() {
    constexpr const char *POPUP = "Subcircuit block";
    if (std::exchange(m_SubcircuitDialogRequested, false)) {
        ImGui::OpenPopup(POPUP);
    }
    if (!ImGui::BeginPopupModal(POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const bool editing = IsEditingSubcircuit();
    ImGui::BeginDisabled(editing);
    ImGui::InputText("Name", &m_SubcircuitName);
    ImGui::EndDisabled();
    ImGui::SeparatorText("Pins, in terminal order, and their edge");
    DrawSubcircuitPinRows(editing);
    if (m_SubcircuitPins.empty()) {
        ImGui::TextDisabled("Place pin markers to choose the block connections");
    }
    ImGui::SeparatorText("Parameters");
    DrawSubcircuitParameterRows();
    if (!m_SubcircuitError.empty()) {
        ImGui::TextColored(GetErrorTextColor(), "%s", m_SubcircuitError.c_str());
    }
    DrawSubcircuitDialogButtons(editing);
    ImGui::EndPopup();
}

/**
 * @brief   One row per pin: its name, the edge it goes on and the buttons that move it in the terminal order.
 */
void EditorWindow::DrawSubcircuitPinRows(const bool editing) {
    for (std::size_t index = 0; index < m_SubcircuitPins.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        ImGui::InputText("##pin", &m_SubcircuitPins[index].Name);
        ImGui::SameLine();
        // The edge the pin goes on comes from the way its marker points; picking another turns the marker about its
        // terminal, so its wires stay put
        Schematic &markers = editing ? *m_OpenBlocks.back().Content : m_RootSchematic;
        const GridPoint point = m_SubcircuitPins[index].Point;
        const std::optional<Core::PinSide> side = FindMarkerSide(markers.GetElements(), point);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * PIN_SIDE_FIELD_WIDTH);
        ImGui::BeginDisabled(!side);
        if (ImGui::BeginCombo("##side", side ? GetPinSideName(*side) : "?")) {
            for (const Core::PinSide choice : PIN_SIDES) {
                if (ImGui::Selectable(GetPinSideName(choice), side == choice)) {
                    markers.SetPinMarkerSide(point, choice);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(index == 0);
        if (ImGui::Button("Up")) {
            std::swap(m_SubcircuitPins[index], m_SubcircuitPins[index - 1]);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(index + 1 == m_SubcircuitPins.size());
        if (ImGui::Button("Down")) {
            std::swap(m_SubcircuitPins[index], m_SubcircuitPins[index + 1]);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
}

/**
 * @brief   One row per parameter: its name, its default and a button that removes it.
 */
void EditorWindow::DrawSubcircuitParameterRows() {
    for (std::size_t index = 0; index < m_SubcircuitParameters.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        ImGui::InputText("##parameter", &m_SubcircuitParameters[index].Name);
        ImGui::SameLine();
        ImGui::InputDouble("##default", &m_SubcircuitParameters[index].DefaultValue, 0.0, 0.0, "%g");
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            m_SubcircuitParameters.erase(m_SubcircuitParameters.begin() + static_cast<std::ptrdiff_t>(index));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Add parameter")) {
        m_SubcircuitParameters.push_back({std::format("P{}", m_SubcircuitParameters.size() + 1), 1.0});
    }
}

/**
 * @brief   Creating the block, or applying the settings of the one open, and the way out without them.
 */
void EditorWindow::DrawSubcircuitDialogButtons(const bool editing) {
    if (ImGui::Button(editing ? "Apply" : "Create block")) {
        if (editing) {
            if (ApplySubcircuitPinNames()) {
                OfferBlockParameters();
                m_SubcircuitError.clear();
                ImGui::CloseCurrentPopup();
            }
        } else {
            auto created = m_RootSchematic.CreateSubcircuitFromSelection(m_SubcircuitName, m_SubcircuitPins,
                                                                         m_SubcircuitParameters);
            if (created) {
                m_RootSchematic.CommitUndoStep();
                m_SubcircuitError.clear();
                ImGui::CloseCurrentPopup();
            } else {
                m_SubcircuitError = created.error();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        if (editing) {
            const auto definition =
                std::ranges::find(m_RootSchematic.GetSubcircuits(), m_OpenBlocks.back().Name, &SavedSubcircuit::Name);
            m_SubcircuitPins = definition->Pins;
            m_SubcircuitParameters = definition->Parameters;
            OfferBlockParameters();
        }
        ImGui::CloseCurrentPopup();
    }
}

/**
 * @brief   Imported blocks join the schematic as one undo step and the first one starts placing, so it lands where the
 *          user clicks; new settings of a block reach every instance of it.
 */
void EditorWindow::ApplySpiceBlockResult(SpiceBlockResult result) {
    if (!result.Imported.empty() || !result.Models.empty()) {
        std::vector<std::unique_ptr<UIElement>> no_parts;
        std::vector<SavedSubcircuit> no_blocks;
        const std::vector<std::string> models =
            m_RootSchematic.MergeLibraryModels(std::move(result.Models), no_parts, no_blocks);
        const std::vector<std::string> names = m_RootSchematic.MergeSubcircuits(std::move(result.Imported), no_parts);
        m_RootSchematic.CommitUndoStep();
        if (!names.empty()) {
            SetMode(PlacingMode{.Type = Core::ComponentType::SUBCIRCUIT, .Block = names.front()});
        } else {
            m_Files.QueueMessages(
                std::format("{} device model{} imported", models.size(), models.size() == 1 ? "" : "s"),
                {"Select a diode or a transistor and pick the model in its Model choice"});
        }
        return;
    }
    if (!result.UpdatedName) {
        return;
    }
    const auto updated = m_RootSchematic.UpdateSpiceSubcircuit(*result.UpdatedName, result.Pins, result.Parameters);
    if (!updated) {
        m_Files.QueueMessages("Could not change the SPICE block", {updated.error()});
        return;
    }
    m_RootSchematic.CommitUndoStep();
    // The open blocks hold copies of it, whose instances follow it as the document's do
    for (OpenBlock &block : m_OpenBlocks) {
        static_cast<void>(block.Content->UpdateSpiceSubcircuit(*result.UpdatedName, result.Pins, result.Parameters));
    }
}

/**
 * @brief   Selecting handles its clicks before the schematic is drawn (HandleSelection()), so a dragged part is drawn
 *          where the cursor is; here, hovering a wire or a part shows its value on display.
 */
std::optional<EditorMode> EditorWindow::HandleMode(SelectingMode &mode, SchematicCanvas & /*canvas*/,
                                                   const ViewTransform &view, const bool hovered) {
    if (!hovered || mode.Drag || !m_Overlays.GetDisplayed()) {
        return std::nullopt;
    }
    if (const std::optional<MeasurementTarget> target = FindTargetAt(view)) {
        m_Overlays.DrawHoveredValue(*target, *m_Schematic);
    }
    return std::nullopt;
}

/**
 * @brief   Placement mode: R rotates, M mirrors left to right and Shift+M top to bottom, Esc or right click cancels,
 *          left click places and keeps the mode active so several components can be placed in a row.
 * @details A label or a pin marker needs a name next, so placing one ends the mode and opens its popover on the name.
 */
std::optional<EditorMode> EditorWindow::HandleMode(PlacingMode &mode, SchematicCanvas &canvas,
                                                   const ViewTransform &view, const bool hovered) {
    // Keyboard shortcuts only apply while the editor has focus, so typing elsewhere does not trigger them
    if (ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            TurnPlacement(mode);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_M)) {
            MirrorPlacement(mode, ImGui::GetIO().KeyShift);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            return SelectingMode{};
        }
    }
    if (!hovered) {
        return std::nullopt;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return SelectingMode{};
    }

    const GridPoint position = Snap(view.ToWorld(ImGui::GetIO().MousePos));
    auto preview = CreateElement(mode.Type, position, mode.Orientation);
    preview->SetMirrored(mode.IsMirrored);
    const bool is_label = mode.Type == Core::ComponentType::NET_LABEL;
    const bool is_pin = Core::IsSubcircuitPin(mode.Type);
    if (is_label) {
        // A new label starts with the name of the last one, so the same net is a click away
        static_cast<Core::NetLabel &>(preview->GetComponent()).SetNet(FindLastNet());
    }
    if (!mode.Block.empty()) {
        const auto definition = std::ranges::find(m_RootSchematic.GetSubcircuits(), mode.Block, &SavedSubcircuit::Name);
        if (definition == m_RootSchematic.GetSubcircuits().end()) {
            return SelectingMode{};
        }
        ApplyDefinitionPins(static_cast<Core::SubcircuitInstance &>(preview->GetComponent()), *definition);
    }
    preview->Draw(canvas, view, GetPreviewColor(), m_Preferences.Symbols);
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return std::nullopt;
    }
    m_Schematic->AddElement(std::move(preview));
    if (!is_label && !is_pin) {
        return std::nullopt;
    }
    m_Schematic->SelectElement(m_Schematic->GetElements().size() - 1);
    m_PartEditor.FocusNetName();
    PlacePartPopover();
    m_PartPopoverRequested = true;
    return SelectingMode{};
}

/**
 * @brief   Wire mode: the first click starts a wire, each following click adds an L-shaped bend up to the cursor and
 *          continues from there. F flips the bend, and the wire ends on a terminal, on a second click at the same
 *          point, or with Esc or right click. Esc or right click with no wire in progress leaves the mode.
 */
std::optional<EditorMode> EditorWindow::HandleMode(WiringMode &mode, SchematicCanvas &canvas, const ViewTransform &view,
                                                   const bool hovered) {
    if (ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_F)) {
            m_WireVerticalFirst = !m_WireVerticalFirst;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            return StopWire(mode) ? std::optional<EditorMode>(SelectingMode{}) : std::nullopt;
        }
    }
    if (!hovered) {
        return std::nullopt;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return StopWire(mode) ? std::optional<EditorMode>(SelectingMode{}) : std::nullopt;
    }

    const GridPoint cursor = Snap(view.ToWorld(ImGui::GetIO().MousePos));
    if (!mode.Start) {
        const ImVec2 cursor_on_screen = view.ToScreen(ToVec2(cursor));
        const ImVec2 half_size = {WIRE_CURSOR_HALF_SIZE, WIRE_CURSOR_HALF_SIZE};
        ImGui::GetWindowDrawList()->AddRect(cursor_on_screen - half_size, cursor_on_screen + half_size,
                                            GetPreviewColor());
    } else {
        const GridPoint corner = FindWireCorner(*mode.Start, cursor, m_WireVerticalFirst);
        UIWire(*mode.Start, corner).Draw(canvas, view, GetPreviewColor());
        UIWire(corner, cursor).Draw(canvas, view, GetPreviewColor());
    }
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return std::nullopt;
    }
    for (const UIWire &wire : ClickWire(mode, cursor, m_WireVerticalFirst, m_Schematic->IsTerminal(cursor))) {
        m_Schematic->AddWire(wire.GetStart(), wire.GetEnd());
    }
    return std::nullopt;
}

/**
 * @brief   Probe mode: the item under the cursor is outlined with what it measures, a node voltage on a wire or a
 *          current on a part, and a left click adds it to the plots or removes it. Esc or right click leaves the mode.
 */
std::optional<EditorMode> EditorWindow::HandleMode(ProbingMode & /*mode*/, SchematicCanvas & /*canvas*/,
                                                   const ViewTransform &view, const bool hovered) {
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        return SelectingMode{};
    }
    if (!hovered) {
        return std::nullopt;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return SelectingMode{};
    }

    const std::optional<MeasurementTarget> target = FindTargetAt(view);
    if (!target) {
        ImGui::SetTooltip("Click a wire for its voltage, or a part for its current");
        return std::nullopt;
    }
    const bool measured =
        target->Node ? m_Schematic->IsVoltageMeasured(*target->Node) : m_Schematic->IsCurrentMeasured(target->Current);
    ImGui::GetWindowDrawList()->AddCircle(ImGui::GetIO().MousePos, MEASUREMENT_MARKER_RADIUS * 2.0f,
                                          GetHighlightColor(), 0, LINE_THICKNESS);
    const std::optional<DisplayedState> &displayed = m_Overlays.GetDisplayed();
    ImGui::SetTooltip("%s", std::format("{}\nClick to {} the plots",
                                        DescribeMeasurement(*target, m_Schematic->GetConnectivity(),
                                                            displayed ? &displayed->Point : nullptr),
                                        measured ? "remove it from" : "add it to")
                                .c_str());
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return std::nullopt;
    }
    if (target->Node) {
        m_Schematic->SetVoltageMeasured(*target->Node, !measured);
    } else {
        m_Schematic->SetCurrentMeasured(target->Current, !measured);
    }
    return std::nullopt;
}

/**
 * @brief   Paste mode: the pasted group follows the cursor, half see-through, R rotates it, M mirrors it left to right
 *          and Shift+M top to bottom, Esc or right click cancels, and a left click places it and selects what was
 *          placed. Parts get the next free names of the schematic.
 */
std::optional<EditorMode> EditorWindow::HandleMode(PastingMode &mode, SchematicCanvas &canvas,
                                                   const ViewTransform &view, const bool hovered) {
    PastedGroup &group = mode.Group;
    if (ImGui::IsWindowFocused() && HandlePastingKeys(group)) {
        return SelectingMode{};
    }
    if (!hovered) {
        return std::nullopt;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return SelectingMode{};
    }

    MovePivotTo(group, Snap(view.ToWorld(ImGui::GetIO().MousePos)));
    for (const UIWire &wire : group.Wires) {
        wire.Draw(canvas, view, GetPreviewColor());
    }
    for (const auto &element : group.Elements) {
        element->Draw(canvas, view, GetPreviewColor(), m_Preferences.Symbols);
    }
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return std::nullopt;
    }
    PlacePastedGroup(group);
    return SelectingMode{};
}

/**
 * @brief   Turns or mirrors the pasted group by the keys pressed this frame.
 * @return  True when Esc cancels the paste.
 */
bool EditorWindow::HandlePastingKeys(PastedGroup &group) {
    if (ImGui::IsKeyPressed(ImGuiKey_R)) {
        TransformGroup(group, {.Type = PointTransform::Kind::QUARTER_TURN, .Delta = {}, .Pivot = {}});
    }
    if (ImGui::IsKeyPressed(ImGuiKey_M)) {
        const bool top_to_bottom = ImGui::GetIO().KeyShift;
        TransformGroup(group, {.Type = top_to_bottom ? PointTransform::Kind::MIRROR_TOP_BOTTOM
                                                     : PointTransform::Kind::MIRROR_LEFT_RIGHT,
                               .Delta = {},
                               .Pivot = {}});
    }
    return ImGui::IsKeyPressed(ImGuiKey_Escape);
}

/**
 * @brief   Adds the pasted group to the schematic shown and selects it, after its blocks and models join the document.
 * @details Inside an open block, a group that places a block containing the one being edited is refused with a
 *          message, as it would make a block that contains itself.
 */
void EditorWindow::PlacePastedGroup(PastedGroup &group) {
    // The blocks and models a group brings belong to the document, wherever it is pasted. Models go first, so a
    // renamed one is renamed in the blocks too before they join
    m_RootSchematic.MergeLibraryModels(std::move(group.Models), group.Elements, group.Subcircuits);
    m_RootSchematic.MergeSubcircuits(std::move(group.Subcircuits), group.Elements);
    if (IsEditingSubcircuit()) {
        SyncOpenBlockDefinitions();
        for (const std::string &used : ListUsedBlocks(group.Elements)) {
            if (!CanPlaceBlock(used)) {
                m_Files.QueueMessages(
                    "Could not paste inside the block",
                    {std::format("{} would contain itself, directly or through another block", used)});
                return;
            }
        }
    }
    const std::size_t first_element = m_Schematic->GetElements().size();
    const std::size_t first_wire = m_Schematic->GetWires().size();
    for (auto &element : group.Elements) {
        m_Schematic->AddElement(std::move(element));
    }
    for (const UIWire &wire : group.Wires) {
        m_Schematic->AddWire(wire.GetStart(), wire.GetEnd());
    }
    std::set<std::size_t> elements;
    for (std::size_t index = first_element; index < m_Schematic->GetElements().size(); ++index) {
        elements.insert(index);
    }
    std::set<std::size_t> wires;
    for (std::size_t index = first_wire; index < m_Schematic->GetWires().size(); ++index) {
        wires.insert(index);
    }
    m_Schematic->SetSelection(std::move(elements), std::move(wires));
}

/**
 * @brief   The name of the newest label that has one, or none.
 */
std::string EditorWindow::FindLastNet() const {
    for (const auto &element : std::views::reverse(m_Schematic->GetElements())) {
        if (element->GetComponent().GetType() != Core::ComponentType::NET_LABEL) {
            continue;
        }
        if (const std::string &net = static_cast<const Core::NetLabel &>(element->GetComponent()).GetNet();
            !net.empty()) {
            return net;
        }
    }
    return {};
}

/**
 * @brief   Ctrl+C, Ctrl+X and Ctrl+V while the editor has the focus and no text field wants the keys.
 */
void EditorWindow::HandleClipboardShortcuts() {
    if (!ImGui::IsWindowFocused() || ImGui::GetIO().WantTextInput) {
        return;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
        CopySelected();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_X)) {
        CutSelected();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V)) {
        StartPasting();
    }
}

/**
 * @brief   The selection goes to the clipboard as text in the format of the files, so another copy of the program can
 *          paste it.
 */
void EditorWindow::CopySelected() {
    if (!IsSelecting(m_Mode) || !m_Schematic->HasSelection()) {
        return;
    }
    const Selection selection = {m_Schematic->GetSelectedElements(), m_Schematic->GetSelectedWires()};
    ImGui::SetClipboardText(CopySelection(m_Schematic->GetElements(), m_Schematic->GetWires(), selection,
                                          m_Schematic->GetSubcircuits(), m_Schematic->GetLibraryModels())
                                .c_str());
}

/**
 * @brief   Deleting in the middle of a drag would leave the drag pointing at removed items.
 */
void EditorWindow::CutSelected() {
    if (IsDragging() || !IsSelecting(m_Mode) || !m_Schematic->HasSelection()) {
        return;
    }
    CopySelected();
    m_Schematic->DeleteSelection();
}

/**
 * @brief   Reads the clipboard and, when it holds a schematic, places it.
 * @details Text that is not a schematic is left alone.
 */
void EditorWindow::StartPasting() {
    const char *text = ImGui::GetClipboardText();
    if (text == nullptr) {
        return;
    }
    auto group = ParseCopiedSchematic(text);
    if (!group) {
        return;
    }
    StartPastingGroup(std::move(*group));
}

/**
 * @brief   Leaves every other mode, so the group is the only thing to place.
 */
void EditorWindow::StartPastingGroup(PastedGroup group) {
    SetMode(PastingMode{.Group = std::move(group)});
}
/**
 * @brief   Handles selection mode, active while not placing, wiring or probing: a click selects an element or a wire,
 *          Shift+click adds or removes one, a drag on empty canvas selects what the box touches, and a double click on
 *          a wire selects its whole node. Dragging the selection moves it, a single wire sliding across itself, with
 *          the wires around it following. R rotates the selection, M mirrors it left to right and Shift+M top to
 *          bottom, both about its center, and holding Alt leaves the wires where they are; Delete removes the
 *          selection and Esc clears it.
 */
void EditorWindow::HandleSelection(const ViewTransform &view, const bool hovered) {
    SelectingMode *selecting = GetSelecting();
    if (selecting == nullptr) {
        return;
    }

    const ImVec2 cursor_world = view.ToWorld(ImGui::GetIO().MousePos);
    const bool shift = ImGui::GetIO().KeyShift;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const Selection picked = PickAt(m_Schematic->GetElements(), m_Schematic->GetWires(), cursor_world,
                                        WIRE_PICK_DISTANCE / m_View.GetZoom());
        const Selection current = {m_Schematic->GetSelectedElements(), m_Schematic->GetSelectedWires()};
        if (picked.Empty()) {
            // Empty canvas starts a box; with Shift it adds to what is selected, otherwise it starts over
            if (!shift) {
                ClearSelection();
            }
            selecting->Box = SelectionBox{.Start = cursor_world, .Base = shift ? current : Selection{}};
        } else if (shift) {
            ApplySelection(Toggled(current, picked));
        } else if (Joined(current, picked) == current) {
            // Part of a group: keep the group so it can be dragged, and collapse to the item if it is only a click
            selecting->Drag = MakeDrag(Snap(cursor_world), picked);
        } else {
            ApplySelection(picked);
            selecting->Drag = MakeDrag(Snap(cursor_world), std::nullopt);
        }
    }

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (m_Schematic->GetSelectedElement() != nullptr) {
            EndDrag();
            if (m_Schematic->GetSelectedElement()->GetComponent().GetType() == Core::ComponentType::SUBCIRCUIT) {
                RequestEditSelectedSubcircuit();
            } else {
                PlacePartPopover();
                m_PartPopoverRequested = true;
            }
        } else if (m_Schematic->GetSelectedElements().empty() && m_Schematic->GetSelectedWires().size() == 1) {
            const Selection node = PickNode(m_Schematic->GetWires(), m_Schematic->GetConnectivity(),
                                            *m_Schematic->GetSelectedWires().begin());
            EndDrag();
            ApplySelection(node);
        }
    }

    if (selecting->Box) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const Selection inside = PickInRectangle(m_Schematic->GetElements(), m_Schematic->GetWires(),
                                                     selecting->Box->Start, cursor_world);
            ApplySelection(Joined(selecting->Box->Base, inside));
        } else {
            selecting->Box.reset();
        }
    }

    if (selecting->Drag) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            UpdateDrag(Snap(cursor_world));
        } else {
            EndDrag();
        }
    }

    if (ImGui::IsWindowFocused()) {
        const bool keep_wires = ImGui::GetIO().KeyAlt;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            TransformSelection(PointTransform::Kind::QUARTER_TURN, keep_wires);
        }
        const bool enter =
            ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        if (enter && m_Schematic->GetSelectedElement() != nullptr && !ImGui::IsAnyItemActive()) {
            PlacePartPopover();
            m_PartPopoverRequested = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_M)) {
            TransformSelection(
                shift ? PointTransform::Kind::MIRROR_TOP_BOTTOM : PointTransform::Kind::MIRROR_LEFT_RIGHT, keep_wires);
        }
        // Deleting mid-drag would leave the drag pointing at removed items
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !selecting->Drag) {
            m_Schematic->DeleteSelection();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !m_PopupOpenLastFrame) {
            ClearSelection();
        }
    }
}

/**
 * @brief   Replaces the selection; a drag belongs to the items it was started with, so it ends.
 */
void EditorWindow::ApplySelection(const Selection &selection) {
    EndDrag();
    m_Schematic->SetSelection(selection.Elements, selection.Wires);
}

/**
 * @brief   A drag belongs to the selection, so it ends with it, and so does a selection box.
 */
void EditorWindow::ClearSelection() {
    EndDrag();
    if (SelectingMode *selecting = GetSelecting()) {
        selecting->Box.reset();
    }
    m_Schematic->ClearSelection();
}

void EditorWindow::SelectAll() {
    if (!IsSelecting(m_Mode)) {
        return;
    }
    ApplySelection(PickAll(m_Schematic->GetElements(), m_Schematic->GetWires()));
}

/**
 * @brief   Captures what a drag of the current selection needs, so every frame can be computed from the start.
 * @details A click on one item of a group only keeps the group while the mouse moves: without a move it leaves just
 *          that item selected.
 */
GroupDrag EditorWindow::MakeDrag(const GridPoint cursor, std::optional<Selection> collapse_to) const {
    GroupDrag drag;
    drag.StartCursor = cursor;
    drag.Elements = m_Schematic->GetSelectedElements();
    drag.Wires = m_Schematic->GetSelectedWires();
    drag.StartWires = m_Schematic->GetWires();
    drag.CollapseTo = std::move(collapse_to);
    const auto &elements = m_Schematic->GetElements();
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const std::vector<GridPoint> terminals = elements[index]->GetTerminals();
        if (drag.Elements.contains(index)) {
            drag.StartPositions.push_back(elements[index]->GetPosition());
            drag.StartTerminals.insert(drag.StartTerminals.end(), terminals.begin(), terminals.end());
        } else {
            drag.FixedTerminals.insert(drag.FixedTerminals.end(), terminals.begin(), terminals.end());
        }
    }
    if (drag.Elements.empty() && drag.Wires.size() == 1) {
        const UIWire &wire = drag.StartWires[*drag.Wires.begin()];
        drag.SlidesVertically = wire.GetStart().Y == wire.GetEnd().Y;
    }
    return drag;
}

/**
 * @brief   Moves the selection by how far the cursor went from where the drag started, rebuilding the wires from the
 *          wires the drag started with so moving back and forth does not pile up bends.
 */
void EditorWindow::UpdateDrag(const GridPoint cursor) {
    SelectingMode *selecting = GetSelecting();
    if (selecting == nullptr || !selecting->Drag) {
        return;
    }
    GroupDrag &drag = *selecting->Drag;
    drag.LastCursor = cursor;
    GridPoint delta = cursor - drag.StartCursor;
    if (drag.SlidesVertically) {
        // A wire only slides across itself: along its own line, moving it would change nothing
        (*drag.SlidesVertically ? delta.X : delta.Y) = 0;
    }
    if (delta == drag.LastDelta) {
        return;
    }
    drag.LastDelta = delta;
    drag.Moved = delta != GridPoint{};
    if (drag.Moved) {
        drag.CollapseTo.reset();
    }

    std::vector<GridPoint> new_terminals;
    std::size_t position_index = 0;
    for (const std::size_t index : drag.Elements) {
        UIElement &element = m_Schematic->GetElement(index);
        element.SetPosition(drag.StartPositions[position_index++] + delta);
        const std::vector<GridPoint> terminals = element.GetTerminals();
        new_terminals.insert(new_terminals.end(), terminals.begin(), terminals.end());
    }
    const PointTransform translation = {.Type = PointTransform::Kind::TRANSLATE, .Delta = delta, .Pivot = {}};
    MovedWires moved =
        MoveGroup(drag.StartWires, drag.Wires, drag.StartTerminals, new_terminals, drag.FixedTerminals, translation);
    m_Schematic->SetWires(std::move(moved.Wires));
    m_Schematic->SetSelection(drag.Elements, std::move(moved.Selected));
    m_Schematic->MarkModified();
}

/**
 * @brief   What the drag left selected: just the clicked item when the mouse never moved over a group.
 * @details The cleanup of the wires is done by every frame of the drag already, so it only matters when the drag
 *          changed nothing.
 */
void EditorWindow::EndDrag() {
    SelectingMode *selecting = GetSelecting();
    if (selecting == nullptr || !selecting->Drag) {
        return;
    }
    const std::optional<Selection> collapse_to = std::move(selecting->Drag->CollapseTo);
    selecting->Drag.reset();
    if (collapse_to) {
        m_Schematic->SetSelection(collapse_to->Elements, collapse_to->Wires);
    }
    m_Schematic->SimplifyAllWires();
}

/**
 * @brief   The box and the items it takes in, drawn over the schematic.
 */
void EditorWindow::DrawSelectionBox(ImDrawList *draw_list, const ViewTransform &view) const {
    const auto *selecting = std::get_if<SelectingMode>(&m_Mode);
    if (selecting == nullptr || !selecting->Box) {
        return;
    }
    const ImVec2 start = view.ToScreen(selecting->Box->Start);
    const ImVec2 end = ImGui::GetIO().MousePos;
    const ImU32 color = GetPalette().Selected;
    draw_list->AddRectFilled(start, end, WithAlpha(color, SELECTION_BOX_FILL_ALPHA));
    draw_list->AddRect(start, end, color, 0.0f, 0, 1.0f);
}

/**
 * @brief   Turns or mirrors every selected part about the center of the selection, and the selected wires with them.
 * @details The wires that meet them follow, unless Alt is held: then the parts turn alone and every wire stays where it
 *          was. A turn during a drag ends that move and goes on dragging from the new position.
 */
void EditorWindow::TransformSelection(const PointTransform::Kind kind, const bool keep_wires) {
    if (m_Schematic->GetSelectedElements().empty()) {
        return;
    }
    const SelectingMode *selecting = GetSelecting();
    const std::optional<GridPoint> drag_cursor =
        selecting != nullptr && selecting->Drag ? std::optional(selecting->Drag->LastCursor) : std::nullopt;
    EndDrag();

    const auto &elements = m_Schematic->GetElements();
    const std::set<std::size_t> selected_elements = m_Schematic->GetSelectedElements();
    const std::set<std::size_t> selected_wires = m_Schematic->GetSelectedWires();
    const PointTransform transform = {
        .Type = kind,
        .Delta = {},
        .Pivot = FindPivot(elements, selected_elements, m_Schematic->GetWires(), selected_wires)};

    std::vector<GridPoint> old_terminals;
    std::vector<GridPoint> new_terminals;
    std::vector<GridPoint> fixed_terminals;
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const std::vector<GridPoint> before = elements[index]->GetTerminals();
        if (!selected_elements.contains(index)) {
            fixed_terminals.insert(fixed_terminals.end(), before.begin(), before.end());
            continue;
        }
        old_terminals.insert(old_terminals.end(), before.begin(), before.end());
        TransformElement(m_Schematic->GetElement(index), transform);
        const std::vector<GridPoint> after = elements[index]->GetTerminals();
        new_terminals.insert(new_terminals.end(), after.begin(), after.end());
    }
    if (!keep_wires) {
        MovedWires moved = MoveGroup(m_Schematic->GetWires(), selected_wires, old_terminals, new_terminals,
                                     fixed_terminals, transform);
        m_Schematic->SetWires(std::move(moved.Wires));
        m_Schematic->SetSelection(selected_elements, std::move(moved.Selected));
    }
    m_Schematic->MarkModified();
    if (drag_cursor) {
        GetSelecting()->Drag = MakeDrag(*drag_cursor, std::nullopt);
    }
}

/**
 * @brief   Beside the part, on its right, or on its left when the right side lacks room The part under the cursor that
 *          the user sets by hand during a live run, or none outside one or outside selecting.
 */
std::optional<std::size_t> EditorWindow::FindLivePart(const ViewTransform &view) const {
    if (IsEditingSubcircuit() || !m_Controls.IsLive() || !IsSelecting(m_Mode)) {
        return std::nullopt;
    }
    return PickLivePart(m_Schematic->GetElements(), view.ToWorld(ImGui::GetIO().MousePos));
}

/**
 * @brief   A click flips a switch or a logic input, a push button stays pressed while the mouse button holds it, and
 *          the wheel turns a potentiometer; each change goes to the run through the schematic.
 * @details Returns whether the mouse went to a part, so the click does not also select.
 */
bool EditorWindow::HandleLiveInput(const std::optional<std::size_t> live_part) {
    if (m_HeldButton) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ReleaseHeldButton();
        }
        return true;
    }
    if (!live_part) {
        return false;
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    Core::Component &component = m_Schematic->GetElement(*live_part).GetComponent();
    switch (GetLiveAction(component.GetType())) {
    case LiveAction::TURN:
        if (Turn(component, ImGui::GetIO().MouseWheel)) {
            m_Schematic->MarkLiveChanged(component);
        }
        return false;
    case LiveAction::FLIP:
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            return false;
        }
        Flip(component);
        m_Schematic->MarkLiveChanged(component);
        return true;
    case LiveAction::HOLD:
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            return false;
        }
        SetPressed(component, true);
        m_Schematic->MarkLiveChanged(component);
        m_HeldButton = live_part;
        return true;
    case LiveAction::NONE:
    default:
        return false;
    }
}

/**
 * @brief   The push button lets go when the mouse button does, wherever the cursor is then.
 */
void EditorWindow::ReleaseHeldButton() {
    const std::optional<std::size_t> index = std::exchange(m_HeldButton, std::nullopt);
    if (!index || *index >= m_Schematic->GetElements().size()) {
        return;
    }
    Core::Component &component = m_Schematic->GetElement(*index).GetComponent();
    SetPressed(component, false);
    m_Schematic->MarkLiveChanged(component);
}

/**
 * @brief   Opens the popover at the top of the canvas, centered across it, so it fits whatever part asked for it and
 *          has the height of the canvas to grow into when a part turns custom.
 */
void EditorWindow::PlacePartPopover() {
    m_PartPopoverAnchor = {m_CanvasCenter.x, m_CanvasTop + ImGui::GetFontSize() * POPOVER_TOP_MARGIN_FONT_SIZES};
}

/**
 * @brief   A digit, a point or a minus typed on a selected part starts editing its value, as in LTspice: "4k7" and
 *          Enter.
 * @details Keys are read rather than characters, since SDL sends characters only while a text field is active.
 */
void EditorWindow::HandleValueTyping(const ViewTransform &view) {
    const ImGuiIO &io = ImGui::GetIO();
    if (!ImGui::IsWindowFocused() || ImGui::IsAnyItemActive() || io.KeyCtrl || io.KeyAlt || io.KeyShift ||
        !m_PartEditor.CanTypeValue()) {
        return;
    }
    const std::optional<char> first = FindTypedValueStart();
    if (!first) {
        return;
    }
    m_PartEditor.StartTypingValue(*first);
    PlacePartPopover();
    m_PartPopoverRequested = true;
}

/**
 * @brief   Draws the properties of the selected part in a popover at the top of the canvas. Edits apply as they
 * are typed; Enter or Esc closes it, and so does a click outside.
 */
void EditorWindow::DrawPartPopover() {
    if (std::exchange(m_PartPopoverRequested, false)) {
        ImGui::OpenPopup(PART_POPOVER);
    }
    ImGui::SetNextWindowPos(m_PartPopoverAnchor, ImGuiCond_Appearing, {0.5f, 0.0f});
    if (!ImGui::BeginPopup(PART_POPOVER)) {
        return;
    }
    // Undo or a deletion may take the part away while the popover is open
    if (m_Schematic->GetSelectedElement() == nullptr) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    m_PartEditor.Draw();
    // The Enter that opened it is ignored; Esc closes only the popover, not the selection below
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    if (enter && !ImGui::IsWindowAppearing()) {
        ImGui::CloseCurrentPopup();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetCurrentWindow()->ID, ImGuiInputFlags_LockThisFrame);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

/**
 * @brief   A right click while selecting selects what is under the cursor and opens the menu of what it can do.
 */
void EditorWindow::HandleContextMenu(const ViewTransform &view, const bool hovered) {
    if (!IsSelecting(m_Mode) || !hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return;
    }
    const ImVec2 cursor_world = view.ToWorld(ImGui::GetIO().MousePos);
    // Right click keeps a group when it lands on one of its items; anywhere else it selects what is under the cursor
    const Selection picked = PickAt(m_Schematic->GetElements(), m_Schematic->GetWires(), cursor_world,
                                    WIRE_PICK_DISTANCE / m_View.GetZoom());
    const Selection current = {m_Schematic->GetSelectedElements(), m_Schematic->GetSelectedWires()};
    if (picked.Empty() || Joined(current, picked) != current) {
        ApplySelection(picked);
    }
    m_ContextTarget =
        FindMeasurementTarget(m_Schematic->GetElements(), m_Schematic->GetWires(), m_Schematic->GetConnectivity(),
                              cursor_world, WIRE_PICK_DISTANCE / m_View.GetZoom());
    PlacePartPopover();
    ImGui::OpenPopup(CONTEXT_MENU);
}

/**
 * @brief   Draws the context menu: properties, orientation, measuring and deletion for a part, measuring and
 *          deletion for a wire, and finding a part, running and fitting the view on empty canvas.
 */
void EditorWindow::DrawContextMenu() {
    if (!ImGui::BeginPopup(CONTEXT_MENU)) {
        return;
    }
    const bool on_element = !m_Schematic->GetSelectedElements().empty();
    const bool on_wire = !m_Schematic->GetSelectedWires().empty();
    const bool single = m_Schematic->GetSelectedElements().size() + m_Schematic->GetSelectedWires().size() == 1;
    if (m_Schematic->GetSelectedElement() != nullptr) {
        if (ImGui::MenuItem("Properties...", "Enter")) {
            m_PartPopoverRequested = true;
        }
        if (m_Schematic->GetSelectedElement()->GetComponent().GetType() == Core::ComponentType::SUBCIRCUIT &&
            ImGui::MenuItem("Edit Block Contents")) {
            RequestEditSelectedSubcircuit();
        }
        ImGui::Separator();
    }
    if (on_element) {
        if (ImGui::MenuItem("Rotate", "R")) {
            TransformSelection(PointTransform::Kind::QUARTER_TURN, false);
        }
        if (ImGui::MenuItem("Mirror", "M")) {
            TransformSelection(PointTransform::Kind::MIRROR_LEFT_RIGHT, false);
        }
        if (ImGui::MenuItem("Flip", "Shift+M")) {
            TransformSelection(PointTransform::Kind::MIRROR_TOP_BOTTOM, false);
        }
        if (ImGui::BeginMenu("Keep Wires in Place")) {
            if (ImGui::MenuItem("Rotate", "Alt+R")) {
                TransformSelection(PointTransform::Kind::QUARTER_TURN, true);
            }
            if (ImGui::MenuItem("Mirror", "Alt+M")) {
                TransformSelection(PointTransform::Kind::MIRROR_LEFT_RIGHT, true);
            }
            if (ImGui::MenuItem("Flip", "Alt+Shift+M")) {
                TransformSelection(PointTransform::Kind::MIRROR_TOP_BOTTOM, true);
            }
            ImGui::EndMenu();
        }
    }
    if (single && m_ContextTarget) {
        if (on_element) {
            ImGui::Separator();
        }
        const MeasurementTarget &target = *m_ContextTarget;
        const bool measured =
            target.Node ? m_Schematic->IsVoltageMeasured(*target.Node) : m_Schematic->IsCurrentMeasured(target.Current);
        const std::string label = std::format("{} {}", measured ? "Stop measuring" : "Measure",
                                              GetMeasurementLabel(target, m_Schematic->GetConnectivity()));
        if (ImGui::MenuItem(label.c_str())) {
            if (target.Node) {
                m_Schematic->SetVoltageMeasured(*target.Node, !measured);
            } else {
                m_Schematic->SetCurrentMeasured(target.Current, !measured);
            }
        }
    }
    if (on_element || on_wire) {
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del")) {
            m_Schematic->DeleteSelection();
        }
    } else {
        if (ImGui::MenuItem("Find a Part...", "Space")) {
            m_PartPicker.RequestOpen();
        }
        if (!IsEditingSubcircuit()) {
            if (m_Controls.IsLive() && ImGui::MenuItem("Discard the Live Run")) {
                m_Controls.DiscardLive();
            }
            if (m_Controls.GetRunAction() == RunAction::CONTINUE) {
                if (ImGui::MenuItem("Continue the Live Run", "F5")) {
                    m_Controls.ContinueLive();
                }
            } else if (m_Controls.IsRunning()) {
                if (ImGui::MenuItem("Stop the Run")) {
                    m_Controls.StopRun();
                }
            } else if (ImGui::MenuItem(
                           std::format("Run {}", GetAnalysisName(m_RootSchematic.GetSimulationSettings().Selected))
                               .c_str(),
                           "F5")) {
                m_Controls.RunSelected();
            }
        }
        if (ImGui::MenuItem("Fit Schematic", "Home")) {
            m_View.RequestFit();
        }
    }
    ImGui::EndPopup();
}

/**
 * @brief   Global routing makes the shortcuts work while another editor window, such as Properties, has focus.
 */
void EditorWindow::HandleGlobalShortcuts() {
    if (!IsEditingSubcircuit()) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, ImGuiInputFlags_RouteGlobal)) {
            m_Files.RequestNew();
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) {
            m_Files.RequestOpen();
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
            m_Files.Save();
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
            m_Files.SaveAs();
        }
        if (ImGui::Shortcut(ImGuiKey_F5, ImGuiInputFlags_RouteGlobal) && m_Controls.GetRunAction() != RunAction::STOP) {
            m_Controls.PressRun();
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, ImGuiInputFlags_RouteGlobal)) {
            RequestQuit();
        }
    }
    // A focused text field keeps Ctrl+Z for undoing its own typing
    if (ImGui::GetIO().WantTextInput) {
        return;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A, ImGuiInputFlags_RouteGlobal)) {
        SelectAll();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) {
        Undo();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) {
        Redo();
    }
}

/**
 * @brief   A drag keeps pointers into the elements, so undoing in the middle of one waits until it ends.
 */
void EditorWindow::Undo() {
    if (IsDragging()) {
        return;
    }
    m_Schematic->Undo();
}

void EditorWindow::Redo() {
    if (IsDragging()) {
        return;
    }
    m_Schematic->Redo();
}

/**
 * @brief   Menu commands run here, from Update(), where their popups are opened and drawn whichever window is in front.
 */
void EditorWindow::HandleCommandRequest() {
    const std::optional<EditorCommand> command = std::exchange(m_RequestedCommand, std::nullopt);
    if (command && !RunFileCommand(*command) && !RunClipboardCommand(*command)) {
        RunEditCommand(*command);
    }
}

/**
 * @brief   The commands of the File menu; false for any other.
 */
bool EditorWindow::RunFileCommand(const EditorCommand command) {
    switch (command) {
    case EditorCommand::NEW:
        m_Files.RequestNew();
        return true;
    case EditorCommand::OPEN:
        m_Files.RequestOpen();
        return true;
    case EditorCommand::SAVE:
        m_Files.Save();
        return true;
    case EditorCommand::SAVE_AS:
        m_Files.SaveAs();
        return true;
    case EditorCommand::IMPORT:
        m_Files.Import();
        return true;
    case EditorCommand::IMPORT_SPICE:
        m_Files.ImportSpiceLibrary();
        return true;
    case EditorCommand::EXPORT_SCHEMATIC:
        m_Files.OpenExportPopup();
        return true;
    default:
        return false;
    }
}

/**
 * @brief   Cut, Copy and Paste; false for any other.
 */
bool EditorWindow::RunClipboardCommand(const EditorCommand command) {
    switch (command) {
    case EditorCommand::CUT:
        CutSelected();
        return true;
    case EditorCommand::COPY:
        CopySelected();
        return true;
    case EditorCommand::PASTE:
        StartPasting();
        return true;
    default:
        return false;
    }
}

/**
 * @brief   The commands of the Edit menu: history, selecting all, deleting and turning the selection.
 */
void EditorWindow::RunEditCommand(const EditorCommand command) {
    switch (command) {
    case EditorCommand::UNDO:
        Undo();
        break;
    case EditorCommand::REDO:
        Redo();
        break;
    case EditorCommand::SELECT_ALL:
        SelectAll();
        break;
    case EditorCommand::DELETE_SELECTION:
        // Deleting mid-drag would leave the drag pointing at a removed element
        if (!IsDragging()) {
            m_Schematic->DeleteSelection();
        }
        break;
    default:
        RunTurnCommand(command);
        break;
    }
}

/**
 * @brief   Rotate, Mirror and Flip, and their variants that keep the wires where they are.
 */
void EditorWindow::RunTurnCommand(const EditorCommand command) {
    switch (command) {
    case EditorCommand::ROTATE:
        TransformSelection(PointTransform::Kind::QUARTER_TURN, false);
        break;
    case EditorCommand::ROTATE_KEEP_WIRES:
        TransformSelection(PointTransform::Kind::QUARTER_TURN, true);
        break;
    case EditorCommand::MIRROR:
        TransformSelection(PointTransform::Kind::MIRROR_LEFT_RIGHT, false);
        break;
    case EditorCommand::MIRROR_KEEP_WIRES:
        TransformSelection(PointTransform::Kind::MIRROR_LEFT_RIGHT, true);
        break;
    case EditorCommand::FLIP:
        TransformSelection(PointTransform::Kind::MIRROR_TOP_BOTTOM, false);
        break;
    case EditorCommand::FLIP_KEEP_WIRES:
        TransformSelection(PointTransform::Kind::MIRROR_TOP_BOTTOM, true);
        break;
    default:
        break;
    }
}

/**
 * @brief   Rotate, mirror and flip the selected parts, with or without their wires.
 */
void EditorWindow::DrawTurnMenuItems(const bool part_selected) {
    if (ImGui::MenuItem("Rotate", "R", false, part_selected)) {
        m_RequestedCommand = EditorCommand::ROTATE;
    }
    if (ImGui::MenuItem("Mirror", "M", false, part_selected)) {
        m_RequestedCommand = EditorCommand::MIRROR;
    }
    if (ImGui::MenuItem("Flip", "Shift+M", false, part_selected)) {
        m_RequestedCommand = EditorCommand::FLIP;
    }
    if (ImGui::BeginMenu("Keep Wires in Place", part_selected)) {
        if (ImGui::MenuItem("Rotate", "Alt+R")) {
            m_RequestedCommand = EditorCommand::ROTATE_KEEP_WIRES;
        }
        if (ImGui::MenuItem("Mirror", "Alt+M")) {
            m_RequestedCommand = EditorCommand::MIRROR_KEEP_WIRES;
        }
        if (ImGui::MenuItem("Flip", "Alt+Shift+M")) {
            m_RequestedCommand = EditorCommand::FLIP_KEEP_WIRES;
        }
        ImGui::EndMenu();
    }
}

/**
 * @brief   Making a block from the selection, opening the selected block, and, inside a block, its settings and the
 *          ways out.
 */
void EditorWindow::DrawBlockMenuItems() {
    if (!IsEditingSubcircuit() &&
        ImGui::MenuItem("Create Block from Selection...", nullptr, false, m_Schematic->HasSelection())) {
        RequestCreateSubcircuit();
    }
    const UIElement *selected = m_Schematic->GetSelectedElement();
    const bool block_selected =
        selected != nullptr && selected->GetComponent().GetType() == Core::ComponentType::SUBCIRCUIT;
    if (ImGui::MenuItem("Edit Block Contents", nullptr, false, block_selected)) {
        RequestEditSelectedSubcircuit();
    }
    if (!IsEditingSubcircuit()) {
        return;
    }
    if (ImGui::MenuItem("Block Pin and Parameter Settings...")) {
        SyncSubcircuitPins();
        m_SubcircuitDialogRequested = true;
    }
    if (ImGui::MenuItem("Apply Block and Return")) {
        FinishSubcircuitEditing(true);
    }
    if (ImGui::MenuItem("Discard Block Changes")) {
        FinishSubcircuitEditing(false);
    }
}
} // namespace GUI
