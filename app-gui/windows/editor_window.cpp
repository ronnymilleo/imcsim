/**
 * @file    editor_window.cpp
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#include "windows/editor_window.h"

#include "schematic/live_input.h"
#include "schematic/placement_editing.h"
#include "schematic/schematic_drawing.h"
#include "schematic/selection_editing.h"
#include "schematic/spice_import.h"
#include "schematic/wire_editing.h"
#include "windows/clipboard_actions.h"
#include "windows/editor_canvas_drawing.h"
#include "windows/editor_status_bar.h"
#include "windows/mode_input.h"
#include "windows/selection_input.h"
#include <algorithm>
#include <utility>

namespace GUI {

namespace {

constexpr float MIN_CANVAS_SIZE = 50.0f;

} // namespace

/**
 * @brief   Creates the editor for a schematic.
 * @param[in] schematic       Schematic to edit; it must outlive the window.
 * @param[in] results         Results of the analyses of that schematic, which the editor shows; it must outlive the
 *                            window.
 * @param[in] controls        Runs of the analyses, for the Run button; they must outlive the window.
 * @param[in] settings_panel  Analysis picker and settings, for the popover beside Run; it must outlive the window.
 * @param[in] part_editor     Fields of the selected part, for the properties popover; it must outlive the window.
 * @param[in] glow_renderer   Draws the glow of lit LEDs; it must outlive the window.
 * @param[in] playback        Instant of the transient to show, which the time bar moves; it must outlive the window.
 * @param[in] preferences     How the schematic is drawn and exported; they must outlive the window.
 */
EditorWindow::EditorWindow(Schematic &schematic,
                           const ResultStore &results,
                           AnalysisControls &controls,
                           AnalysisSettingsPanel &settings_panel,
                           PartEditor &part_editor,
                           GlowRenderer &glow_renderer,
                           TransientPlayback &playback,
                           const Preferences &preferences)
    : AppWindow("Schematic", false), m_RootSchematic(schematic), m_RootResults(results), m_Controls(controls),
      m_SettingsPanel(settings_panel), m_PartEditor(part_editor), m_Playback(playback), m_Preferences(preferences),
      m_Files(schematic,
              preferences,
              {.StopEditing = [this] { GUI::StopEditing(m_Navigator.GetShown(), m_Mode); },
               .FrameSchematic = [this] { m_View.RequestDefaultFrame(); },
               .PlaceGroup = [this](PastedGroup group) { SetMode(PastingMode{.Group = std::move(group)}); },
               .OpenSpiceLibrary =
                   [this](std::string file_name, Core::SpiceLibrary library) {
                       m_SpiceImport.Open(std::move(file_name), std::move(library));
                   }}),
      m_Toolbar(controls, settings_panel, schematic), m_PartPicker(preferences), m_PartRail(preferences),
      m_Overlays(glow_renderer, playback, preferences),
      m_Navigator(schematic, {.Mode = m_Mode, .Part = part_editor, .Overlays = m_Overlays, .View = m_View}) {
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
    RunPressedShortcuts(ShortcutScope::DOCUMENT);
    RunPressedShortcuts(ShortcutScope::EDITING);
    m_Files.DrawPopups();
    m_Navigator.DrawDialogs(m_Files);
    if (std::optional<SpiceImportResult> imported = m_SpiceImport.Draw()) {
        ImportSpiceBlocks(std::move(*imported));
    }
}

/**
 * @brief   Draws the command bar in the current window, which the application keeps under the menu bar, and carries out
 *          what its buttons and its part search ask.
 * @note    It works on the schematic being edited, a block included, whichever window is in front.
 */
void EditorWindow::DrawCommandBar() {
    ToolbarRequest request = m_Toolbar.Draw(m_Mode,
                                            m_Navigator.GetShown(),
                                            m_Navigator.IsEditingBlock(),
                                            m_PartPicker,
                                            m_Navigator.GetSession().ListPlaceableBlocks());
    if (request.Mode) {
        SetMode(std::move(*request.Mode));
    }
    if (request.Command) {
        RunCommand(*request.Command);
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
    if (m_Navigator.ReturnToDocument()) {
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
    if (m_Navigator.ReturnToDocument()) {
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
    if (const std::optional<EditorCommand> command = DrawFileItems(m_Navigator.IsEditingBlock())) {
        m_RequestedCommand = command;
    }
}

/**
 * @brief   Draws the Edit menu items, with their shortcuts, enabled only when they apply.
 * @note    Call between BeginMenu() and EndMenu(); the command runs on the next frame of the editor.
 */
void EditorWindow::DrawEditMenuItems() {
    if (const std::optional<EditorCommand> command = DrawEditItems(m_Navigator.GetShown())) {
        m_RequestedCommand = command;
    }
    if (const std::optional<EditorCommand> command =
            DrawBlockItems(m_Navigator.GetShown(), m_Navigator.IsEditingBlock())) {
        m_RequestedCommand = command;
    }
}

/**
 * @brief   Draws the View menu items of the editor.
 * @note    Call between BeginMenu() and EndMenu(); the command runs on the next frame of the editor.
 */
void EditorWindow::DrawViewMenuItems() {
    CommandItems items;
    items.Draw("Fit Schematic", EditorCommand::FIT);
    if (const std::optional<EditorCommand> command = items.GetPicked()) {
        m_RequestedCommand = command;
    }
}

/**
 * @brief   Draws the frame of the editor: the rail, then the canvas, whose input runs before the schematic is drawn and
 *          whose mode draws its preview over it, then the bars and menus over the canvas.
 */
void EditorWindow::Draw() {
    RunPressedShortcuts(ShortcutScope::VIEW);
    RunPressedShortcuts(ShortcutScope::CANVAS);
    HandlePartPicker();
    m_Overlays.Update(m_Navigator.GetShown(), GetShownResults(), m_Controls);
    DrawRail();
    const CanvasFrame frame = BeginCanvas();
    HandleCanvasInput(frame);
    DrawCanvas(frame);
    DrawCanvasOverlays(frame);
}

/**
 * @brief   A part picked in the search of the command bar is placed here, so the editor takes the keys of placing;
 *          Space opens the search.
 */
void EditorWindow::HandlePartPicker() {
    if (m_PartPicker.TakeEditorFocus()) {
        ImGui::SetWindowFocus();
    }
    m_PartPicker.WatchShortcut();
}

/**
 * @brief   The rail runs down the left of the canvas, as tall as it, and starts the mode or the search it asks for.
 */
void EditorWindow::DrawRail() {
    RailRequest rail = m_PartRail.Draw(m_Mode, GetCanvasSize().y);
    if (rail.Mode) {
        SetMode(std::move(*rail.Mode));
    }
    if (rail.IsSearchAsked) {
        m_PartPicker.RequestOpen();
    }
}

/**
 * @brief   Lays the canvas out beside the rail as one button that takes every mouse button, and lets the wheel and the
 *          middle button move the view.
 * @return  Where the canvas is and what the cursor is over, for the rest of the frame.
 */
EditorWindow::CanvasFrame EditorWindow::BeginCanvas() {
    ImGui::SameLine(0.0f, 0.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = GetCanvasSize();
    m_Popover.SetCanvas({origin.x + size.x / 2.0f, origin.y + size.y / 2.0f}, origin.y);
    m_View.FollowCanvasSize(size);
    m_View.FrameIfRequested(m_Navigator.GetShown(), size, m_Preferences.Symbols, m_Preferences.ShowTerminalNumbers);

    // The way back from a block sits over the canvas, and takes the clicks that land on it
    if (m_Navigator.IsEditingBlock()) {
        ImGui::SetNextItemAllowOverlap();
    }
    ImGui::InvisibleButton("canvas",
                           size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight
                               | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const std::optional<LivePart> live_part = hovered ? FindHoveredLivePart(origin) : std::nullopt;
    // During a live run the wheel over a potentiometer turns it instead of zooming
    const bool is_turning_part = live_part && live_part->Action == LiveAction::TURN;
    m_View.HandlePanAndZoom(origin, hovered && !is_turning_part, ImGui::IsItemActive());
    return {.Origin = origin,
            .Size = size,
            .View = m_View.GetTransform(origin),
            .IsHovered = hovered,
            .HoveredLivePart = live_part};
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
 * @brief   The part under the cursor that a click changes instead of selecting it, during a live run of the document
 *          while the editor is selecting.
 */
std::optional<LivePart> EditorWindow::FindHoveredLivePart(const ImVec2 origin) const {
    const bool is_live_part_pickable = !m_Navigator.IsEditingBlock() && m_Controls.IsLive() && IsSelecting(m_Mode);
    return m_Live.FindLivePart(m_Navigator.GetShown(), m_View.GetTransform(origin), is_live_part_pickable);
}

/**
 * @brief   The input that runs before the schematic is drawn: the parts of a live run, selecting, the right click and
 *          typing a value into the properties.
 * @details Selection runs before drawing so a dragged element is drawn where the cursor is this frame.
 */
void EditorWindow::HandleCanvasInput(const CanvasFrame &frame) {
    const bool took_mouse = m_Live.HandleInput(m_Navigator.GetShown(), frame.HoveredLivePart);
    const SelectionInput selection = {.Shown = m_Navigator.GetShown(),
                                      .Mode = m_Mode,
                                      .Popover = m_Popover,
                                      .View = frame.View,
                                      .Zoom = m_View.GetZoom(),
                                      .IsHovered = frame.IsHovered && !took_mouse,
                                      .WasPopupOpenLastFrame = m_PopupOpenLastFrame};
    for (const EditorCommand command : HandleSelectionInput(selection)) {
        RunCommand(command);
    }
    m_ContextMenu
        .HandleRightClick(m_Navigator.GetShown(), m_Mode, frame.View, m_View.GetZoom(), frame.IsHovered, m_Popover);
    m_Popover.HandleValueTyping(m_PartEditor);
}

/**
 * @brief   Draws the schematic, lets the mode draw its preview over it and handle its clicks, and draws the color
 *          legend and the instant on show on top, all clipped to the canvas.
 */
void EditorWindow::DrawCanvas(const CanvasFrame &frame) {
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(frame.Origin, frame.Origin + frame.Size, true);
    DrawListCanvas canvas(draw_list);
    DrawSchematicLayers(frame, canvas);
    HandleModeInput(frame, canvas);
    m_Overlays.DrawColorLegend(draw_list, frame.Origin, frame.Size, m_Navigator.GetShown());
    DrawShownInstant(draw_list, frame.Origin, GetShownInstant());
    draw_list->PopClipRect();
}

/**
 * @brief   Draws the schematic from the bottom up: background, grid, wires, parts, glows, terminal numbers, node
 *          voltages, measurement markers and the selection box.
 */
void EditorWindow::DrawSchematicLayers(const CanvasFrame &frame, SchematicCanvas &canvas) {
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const ViewTransform &view = frame.View;
    const float zoom = m_View.GetZoom();
    Schematic &shown = m_Navigator.GetShown();
    draw_list->AddRectFilled(frame.Origin, frame.Origin + frame.Size, GetPalette().CanvasBackground);
    DrawGrid(draw_list, view, frame.Origin, frame.Size, zoom);
    m_Overlays.DrawWires(canvas, view, shown, zoom);
    const std::optional<std::size_t> held_button = m_Live.GetHeldButton();
    const std::optional<std::size_t> hovered_part =
        frame.HoveredLivePart ? std::optional<std::size_t>(frame.HoveredLivePart->Index) : std::nullopt;
    m_Overlays.DrawElements(canvas,
                            view,
                            shown,
                            m_Controls.IsLive() && IsSelecting(m_Mode),
                            held_button ? held_button : hovered_part);
    // Over the symbols, so a lit LED looks lit through its own lines
    m_Overlays.DrawGlows(draw_list, view, shown, zoom);
    if (m_Preferences.ShowTerminalNumbers) {
        DrawTerminalNumbers(canvas, view, shown, SCREEN_TERMINAL_RING_SCALE);
    }
    m_Overlays.DrawNodeVoltages(draw_list, view, shown, zoom);
    DrawMeasurementMarkers(canvas, view, shown);
    DrawSelectionBox(draw_list, view, m_Mode);
}

/**
 * @brief   The keys that start a mode, then the input of the mode the editor is in, which may switch to another.
 * @details Previews go over the schematic, so the mode handles its clicks after it is drawn.
 */
void EditorWindow::HandleModeInput(const CanvasFrame &frame, SchematicCanvas &canvas) {
    HandleModeKeys();
    const ModeInput input = {.Root = m_RootSchematic,
                             .Blocks = m_Navigator.GetSession(),
                             .Overlays = m_Overlays,
                             .Part = m_PartEditor,
                             .Popover = m_Popover,
                             .Files = m_Files,
                             .UserPreferences = m_Preferences,
                             .Canvas = canvas,
                             .View = frame.View,
                             .Zoom = m_View.GetZoom(),
                             .IsHovered = frame.IsHovered,
                             .IsWireVerticalFirst = m_WireVerticalFirst};
    std::optional<EditorMode> next = std::visit([&](auto &mode) { return HandleMode(mode, input); }, m_Mode);
    if (next) {
        SetMode(std::move(*next));
    }
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
 * @brief   What sits over the canvas once it is drawn: the way back from a block, the status bar, the context menu and
 *          the properties popover.
 * @details ImGui closes a popup on Esc before the next frame starts, so whether one is open is kept for that frame.
 */
void EditorWindow::DrawCanvasOverlays(const CanvasFrame &frame) {
    m_Navigator.DrawWayBack(frame.Origin);
    DrawStatusBar(m_Mode,
                  m_Navigator.GetShown(),
                  m_Controls,
                  GetShownResults().GetTransient(),
                  frame.View,
                  frame.IsHovered);
    const ContextMenuServices services = {.Shown = m_Navigator.GetShown(),
                                          .Controls = m_Controls,
                                          .Picker = m_PartPicker,
                                          .Popover = m_Popover};
    if (const std::optional<EditorCommand> command =
            m_ContextMenu.Draw(services,
                               m_RootSchematic.GetSimulationSettings().Selected,
                               m_Navigator.IsEditingBlock())) {
        RunCommand(*command);
    }
    m_Popover.Draw(m_PartEditor, m_Navigator.GetShown());
    m_PopupOpenLastFrame = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

/**
 * @brief   The span of time of the window of the transient that plays, or none without a transient.
 */
std::optional<EditorWindow::TimeSpan> EditorWindow::GetWindowSpan() const {
    const auto &transient = GetShownResults().GetTransient();
    const PointWindow window = m_Playback.Window.GetWindow();
    if (!transient || window.Count == 0 || window.First + window.Count > transient->Times.size()) {
        return std::nullopt;
    }
    return TimeSpan{.From = transient->Times[window.First], .To = transient->Times[window.First + window.Count - 1]};
}

/**
 * @brief   The instant of the transient the schematic shows, when a window of it plays, or none.
 */
std::optional<double> EditorWindow::GetShownInstant() const {
    const std::optional<DisplayedState> &displayed = m_Overlays.GetDisplayed();
    const std::optional<double> time = displayed ? displayed->Time : std::nullopt;
    if (!time || !GetWindowSpan()) {
        return std::nullopt;
    }
    return time;
}

/**
 * @brief   Leaves the mode the editor is in for another, as the toolbar, the part picker, a key or the end of a mode
 *          asks.
 * @details See SwitchMode().
 */
void EditorWindow::SetMode(EditorMode mode) {
    SwitchMode(m_Mode, std::move(mode), m_Navigator.GetShown(), m_Navigator.GetSession());
}

/**
 * @brief   The results of the schematic on show: those of the document, and none inside a block, whose parts and nodes
 *          they do not belong to.
 */
const ResultStore &EditorWindow::GetShownResults() const {
    return m_Navigator.IsEditingBlock() ? m_NoResults : m_RootResults;
}

/**
 * @brief   Imported blocks and models join the document as one undo step, and the first block starts placing, so it
 *          lands where the user clicks; models alone are only told about, since they show in the Model choice of the
 *          parts.
 */
void EditorWindow::ImportSpiceBlocks(SpiceImportResult imported) {
    // A library brings no parts of its own, which the merges would point at a renamed model or block
    std::vector<std::unique_ptr<UIElement>> no_parts;
    std::vector<SavedSubcircuit> no_blocks;
    const std::vector<std::string> models =
        m_RootSchematic.MergeLibraryModels(std::move(imported.Models), no_parts, no_blocks);
    const std::vector<std::string> names = m_RootSchematic.MergeSubcircuits(std::move(imported.Blocks), no_parts);
    m_RootSchematic.CommitUndoStep();
    if (!names.empty()) {
        SetMode(PlacingMode{.Type = Core::ComponentType::SUBCIRCUIT, .Block = names.front()});
        return;
    }
    m_Files.QueueMessages(DescribeImportedModels(models.size()),
                          {"Select a diode or a transistor and pick the model in its Model choice"});
}

/**
 * @brief   Menu commands run here, from Update(), where their popups are opened and drawn whichever window is in front.
 */
void EditorWindow::HandleCommandRequest() {
    if (const std::optional<EditorCommand> command = std::exchange(m_RequestedCommand, std::nullopt)) {
        RunCommand(*command);
    }
}

/**
 * @brief   Runs the commands whose keys of one scope were pressed this frame, as the table of shortcuts lists them.
 * @note    The global scopes are read from Update(), outside any window, so they work whichever window is in front;
 *          the others from Draw(), inside the editor window, whose focus they need.
 */
void EditorWindow::RunPressedShortcuts(const ShortcutScope scope) {
    const ShortcutState state = {.IsEditingBlock = m_Navigator.IsEditingBlock(),
                                 .IsTyping = ImGui::GetIO().WantTextInput,
                                 .IsEditorFocused = ImGui::IsWindowFocused()};
    if (!IsShortcutScopeOpen(scope, state)) {
        return;
    }
    for (const EditorShortcut &shortcut : GetEditorShortcuts()) {
        if (shortcut.Scope == scope && ImGui::Shortcut(shortcut.Chord, GetShortcutRoute(scope))) {
            RunCommand(shortcut.Command);
        }
    }
}

/**
 * @brief   Carries out a command, whichever menu, button or key asked for it.
 * @details A drag keeps pointers into the elements, so the commands that remove or replace them wait until it ends.
 */
void EditorWindow::RunCommand(const EditorCommand command) {
    if (IsDragging(m_Mode) && !CanRunDuringDrag(command)) {
        return;
    }
    switch (command) {
    case EditorCommand::NEW:
        m_Files.RequestNew();
        break;
    case EditorCommand::OPEN:
        m_Files.RequestOpen();
        break;
    case EditorCommand::SAVE:
        m_Files.Save();
        break;
    case EditorCommand::SAVE_AS:
        m_Files.SaveAs();
        break;
    case EditorCommand::IMPORT:
        m_Files.Import();
        break;
    case EditorCommand::IMPORT_SPICE:
        m_Files.ImportSpiceLibrary();
        break;
    case EditorCommand::EXPORT_SCHEMATIC:
        m_Files.OpenExportPopup();
        break;
    case EditorCommand::QUIT:
        RequestQuit();
        break;
    case EditorCommand::RUN:
        // Like the Run button of the analysis settings, the key never stops a run
        if (m_Controls.GetRunAction() != RunAction::STOP) {
            m_Controls.PressRun();
        }
        break;
    case EditorCommand::UNDO:
        m_Navigator.GetShown().Undo();
        break;
    case EditorCommand::REDO:
        m_Navigator.GetShown().Redo();
        break;
    case EditorCommand::ROTATE:
    case EditorCommand::MIRROR:
    case EditorCommand::FLIP:
    case EditorCommand::ROTATE_KEEP_WIRES:
    case EditorCommand::MIRROR_KEEP_WIRES:
    case EditorCommand::FLIP_KEEP_WIRES:
        RunTurnCommand(command);
        break;
    case EditorCommand::CUT:
        if (IsSelecting(m_Mode)) {
            CutSelectionToClipboard(m_Navigator.GetShown());
        }
        break;
    case EditorCommand::COPY:
        if (IsSelecting(m_Mode)) {
            CopySelectionToClipboard(m_Navigator.GetShown());
        }
        break;
    case EditorCommand::PASTE:
        if (std::optional<PastedGroup> group = ReadClipboardGroup()) {
            SetMode(PastingMode{.Group = std::move(*group)});
        }
        break;
    case EditorCommand::SELECT_ALL:
        SelectAll(m_Navigator.GetShown(), m_Mode);
        break;
    case EditorCommand::DELETE_SELECTION:
        m_Navigator.GetShown().DeleteSelection();
        break;
    case EditorCommand::FIT:
        m_View.RequestFit();
        break;
    case EditorCommand::CREATE_BLOCK:
        m_Navigator.GetSession().RequestCreate();
        break;
    case EditorCommand::EDIT_BLOCK:
        m_Navigator.EditSelectedBlock();
        break;
    case EditorCommand::OPEN_BLOCK_SETTINGS:
        m_Navigator.OpenBlockSettings();
        break;
    case EditorCommand::APPLY_BLOCK:
        m_Navigator.ApplyAndLeaveBlock();
        break;
    case EditorCommand::DISCARD_BLOCK:
        m_Navigator.DiscardAndLeaveBlock();
        break;
    default:
        break;
    }
}

/**
 * @brief   Rotate, Mirror and Flip, and their variants that keep the wires where they are.
 */
void EditorWindow::RunTurnCommand(const EditorCommand command) {
    if (const std::optional<TurnRequest> turn = GetTurnRequest(command)) {
        TransformSelection(m_Navigator.GetShown(), m_Mode, turn->Kind, turn->IsKeepingWires);
    }
}

} // namespace GUI
