/**
 * @file    editor_window.cpp
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#include "windows/editor_window.h"

#include "circuit/components/subcircuit.h"
#include "schematic/live_input.h"
#include "schematic/placement_editing.h"
#include "schematic/schematic_drawing.h"
#include "schematic/selection_editing.h"
#include "schematic/wire_editing.h"
#include "windows/block_dialog.h"
#include "windows/editor_canvas_drawing.h"
#include "windows/editor_status_bar.h"
#include "windows/mode_input.h"
#include <algorithm>
#include <format>
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
    : AppWindow("Schematic", false), m_RootSchematic(schematic), m_Schematic(&schematic), m_RootResults(results),
      m_Blocks(schematic), m_Controls(controls), m_SettingsPanel(settings_panel), m_PartEditor(part_editor),
      m_Playback(playback), m_Preferences(preferences),
      m_Files(schematic,
              preferences,
              {.StopEditing = [this] { GUI::StopEditing(*m_Schematic, m_Mode); },
               .FrameSchematic = [this] { m_View.RequestDefaultFrame(); },
               .PlaceGroup = [this](PastedGroup group) { StartPastingGroup(std::move(group)); },
               .OpenSpiceLibrary =
                   [this](std::string file_name, Core::SpiceLibrary library) {
                       m_SpiceDialog.OpenImport(std::move(file_name), std::move(library));
                   }}),
      m_Toolbar(controls, settings_panel, schematic), m_PartPicker(preferences), m_PartRail(preferences),
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
    RunPressedShortcuts(ShortcutScope::DOCUMENT);
    RunPressedShortcuts(ShortcutScope::EDITING);
    m_Files.DrawPopups();
    if (DrawBlockDialog(m_Blocks)) {
        OfferBlockParameters();
    }
    if (std::optional<SpiceBlockResult> result = m_SpiceDialog.Draw()) {
        ApplySpiceBlockResult(std::move(*result));
    }
}

/**
 * @brief   Draws the command bar in the current window, which the application keeps under the menu bar, and carries out
 *          what its buttons and its part search ask.
 * @note    It works on the schematic being edited, a block included, whichever window is in front.
 */
void EditorWindow::DrawCommandBar() {
    ToolbarRequest request =
        m_Toolbar.Draw(m_Mode, *m_Schematic, IsEditingSubcircuit(), m_PartPicker, m_Blocks.ListPlaceableBlocks());
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
    if (const std::optional<EditorCommand> command = DrawFileItems(IsEditingSubcircuit())) {
        m_RequestedCommand = command;
    }
}

/**
 * @brief   Draws the Edit menu items, with their shortcuts, enabled only when they apply.
 * @note    Call between BeginMenu() and EndMenu(); the command runs on the next frame of the editor.
 */
void EditorWindow::DrawEditMenuItems() {
    if (const std::optional<EditorCommand> command = DrawEditItems(*m_Schematic)) {
        m_RequestedCommand = command;
    }
    if (const std::optional<EditorCommand> command = DrawBlockItems(*m_Schematic, IsEditingSubcircuit())) {
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
    m_Overlays.Update(*m_Schematic, GetShownResults(), m_Controls);
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
    m_View.FrameIfRequested(*m_Schematic, size, m_Preferences.Symbols, m_Preferences.ShowTerminalNumbers);

    // The way back from a block sits over the canvas, and takes the clicks that land on it
    if (IsEditingSubcircuit()) {
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
    const bool is_live_part_pickable = !IsEditingSubcircuit() && m_Controls.IsLive() && IsSelecting(m_Mode);
    return m_Live.FindLivePart(*m_Schematic, m_View.GetTransform(origin), is_live_part_pickable);
}

/**
 * @brief   The input that runs before the schematic is drawn: the parts of a live run, selecting, the right click and
 *          typing a value into the properties.
 * @details Selection runs before drawing so a dragged element is drawn where the cursor is this frame.
 */
void EditorWindow::HandleCanvasInput(const CanvasFrame &frame) {
    const bool took_mouse = m_Live.HandleInput(*m_Schematic, frame.HoveredLivePart);
    HandleSelection(frame.View, frame.IsHovered && !took_mouse);
    m_ContextMenu.HandleRightClick(*m_Schematic, m_Mode, frame.View, m_View.GetZoom(), frame.IsHovered, m_Popover);
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
    m_Overlays.DrawColorLegend(draw_list, frame.Origin, frame.Size, *m_Schematic);
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
    draw_list->AddRectFilled(frame.Origin, frame.Origin + frame.Size, GetPalette().CanvasBackground);
    DrawGrid(draw_list, view, frame.Origin, frame.Size, zoom);
    m_Overlays.DrawWires(canvas, view, *m_Schematic, zoom);
    const std::optional<std::size_t> held_button = m_Live.GetHeldButton();
    const std::optional<std::size_t> hovered_part =
        frame.HoveredLivePart ? std::optional<std::size_t>(frame.HoveredLivePart->Index) : std::nullopt;
    m_Overlays.DrawElements(canvas,
                            view,
                            *m_Schematic,
                            m_Controls.IsLive() && IsSelecting(m_Mode),
                            held_button ? held_button : hovered_part);
    // Over the symbols, so a lit LED looks lit through its own lines
    m_Overlays.DrawGlows(draw_list, view, *m_Schematic, zoom);
    if (m_Preferences.ShowTerminalNumbers) {
        DrawTerminalNumbers(canvas, view, *m_Schematic, SCREEN_TERMINAL_RING_SCALE);
    }
    m_Overlays.DrawNodeVoltages(draw_list, view, *m_Schematic, zoom);
    DrawMeasurementMarkers(canvas, view, *m_Schematic);
    DrawSelectionBox(draw_list, view, m_Mode);
}

/**
 * @brief   The keys that start a mode, then the input of the mode the editor is in, which may switch to another.
 * @details Previews go over the schematic, so the mode handles its clicks after it is drawn.
 */
void EditorWindow::HandleModeInput(const CanvasFrame &frame, SchematicCanvas &canvas) {
    HandleModeKeys();
    const ModeInput input = {.Root = m_RootSchematic,
                             .Blocks = m_Blocks,
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
    if (const std::optional<std::size_t> picked = DrawBlockBar(m_Blocks, frame.Origin)) {
        ReturnToLevel(*picked);
    }
    DrawStatusBar(m_Mode, *m_Schematic, m_Controls, GetShownResults().GetTransient(), frame.View, frame.IsHovered);
    const ContextMenuServices services = {.Shown = *m_Schematic,
                                          .Controls = m_Controls,
                                          .Picker = m_PartPicker,
                                          .Popover = m_Popover};
    if (const std::optional<EditorCommand> command =
            m_ContextMenu.Draw(services, m_RootSchematic.GetSimulationSettings().Selected, IsEditingSubcircuit())) {
        RunCommand(*command);
    }
    m_Popover.Draw(m_PartEditor, *m_Schematic);
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
    SwitchMode(m_Mode, std::move(mode), *m_Schematic, m_Blocks);
}

bool EditorWindow::IsEditingSubcircuit() const {
    return m_Blocks.IsOpen();
}

/**
 * @brief   The results of the schematic on show: those of the document, and none inside a block, whose parts and nodes
 *          they do not belong to.
 */
const ResultStore &EditorWindow::GetShownResults() const {
    return IsEditingSubcircuit() ? m_NoResults : m_RootResults;
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
    while (m_Blocks.GetOpenBlocks().size() > depth) {
        const std::size_t open = m_Blocks.GetOpenBlocks().size();
        FinishSubcircuitEditing(true);
        if (m_Blocks.GetOpenBlocks().size() == open) {
            return false;
        }
    }
    return true;
}

/**
 * @brief   Shows the schematic of the innermost open block, or the document, starting it unselected and framed.
 * @details A drag belongs to the schematic it started on, and a part or a group on its way would land in the other one,
 *          so both end first.
 */
void EditorWindow::ShowSchematic() {
    EndDrag(*m_Schematic, m_Mode);
    if (std::holds_alternative<PlacingMode>(m_Mode) || std::holds_alternative<PastingMode>(m_Mode)) {
        m_Mode = SelectingMode{};
    }
    m_Schematic = &m_Blocks.GetShown();
    m_PartEditor.SetSchematic(*m_Schematic);
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
 * @brief   The parts of the block can follow the parameters in the settings, as they stand.
 */
void EditorWindow::OfferBlockParameters() {
    std::vector<std::string> names;
    for (const Core::SubcircuitParameter &parameter : m_Blocks.GetParameters()) {
        names.push_back(parameter.Name);
    }
    m_PartEditor.SetBlockParameters(std::move(names));
}

/**
 * @brief   Opens the block selected where the editor is, which may be inside another block, or its settings when it is
 *          kept as SPICE text.
 * @details A block kept as SPICE text has no drawing to open, only its pins and parameters to set.
 */
void EditorWindow::RequestEditSelectedSubcircuit() {
    const std::optional<std::string> name = BlockSession::FindBlockName(m_Schematic->GetSelectedElement());
    if (!name) {
        return;
    }
    const SavedSubcircuit *definition = m_Blocks.FindDefinition(*name);
    if (definition != nullptr && definition->Spice) {
        m_SpiceDialog.OpenSettings(*definition);
        return;
    }
    if (const auto opened = m_Blocks.Open(*name); !opened) {
        m_Blocks.ReportError(opened.error());
        return;
    }
    ShowSchematic();
}

/**
 * @brief   Closes the innermost open block, applying it to the document or dropping its changes, and shows the level
 *          above it; a block that cannot be applied stays open, with the reason in the block dialog.
 */
void EditorWindow::FinishSubcircuitEditing(const bool apply) {
    if (const auto closed = m_Blocks.Close(apply); !closed) {
        m_Blocks.ReportError(closed.error());
        return;
    }
    ShowSchematic();
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
    const auto updated = m_Blocks.ApplySpiceUpdate(*result.UpdatedName, result.Pins, result.Parameters);
    if (!updated) {
        m_Files.QueueMessages("Could not change the SPICE block", {updated.error()});
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
    const Selection selection = {m_Schematic->GetSelection().Elements, m_Schematic->GetSelection().Wires};
    ImGui::SetClipboardText(CopySelection(m_Schematic->GetElements(),
                                          m_Schematic->GetWires(),
                                          selection,
                                          m_Schematic->GetSubcircuits(),
                                          m_Schematic->GetLibraryModels())
                                .c_str());
}

/**
 * @brief   The selection goes to the clipboard, as CopySelected() puts it, and leaves the schematic.
 */
void EditorWindow::CutSelected() {
    if (!IsSelecting(m_Mode) || !m_Schematic->HasSelection()) {
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
    auto *selecting = std::get_if<SelectingMode>(&m_Mode);
    if (selecting == nullptr) {
        return;
    }
    const ImVec2 cursor_world = view.ToWorld(ImGui::GetIO().MousePos);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        HandleSelectionClick(*selecting, cursor_world);
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        HandleSelectionDoubleClick();
    }
    UpdateSelectionBox(*selecting, cursor_world);
    UpdateSelectionDrag(*selecting, cursor_world);
    if (ImGui::IsWindowFocused()) {
        HandleSelectionKeys();
    }
}

/**
 * @brief   A click picks what is under the cursor: empty canvas starts a box, an item selects it or, with Shift,
 * toggles it, and an item of a selected group keeps the group so it can be dragged.
 */
void EditorWindow::HandleSelectionClick(SelectingMode &selecting, const ImVec2 cursor_world) {
    const bool shift = ImGui::GetIO().KeyShift;
    const Selection picked = PickAt(m_Schematic->GetElements(),
                                    m_Schematic->GetWires(),
                                    cursor_world,
                                    WIRE_PICK_DISTANCE / m_View.GetZoom());
    const Selection current = {m_Schematic->GetSelection().Elements, m_Schematic->GetSelection().Wires};
    if (picked.Empty()) {
        // Empty canvas starts a box; with Shift it adds to what is selected, otherwise it starts over
        if (!shift) {
            ClearSelection(*m_Schematic, m_Mode);
        }
        selecting.Box = SelectionBox{.Start = cursor_world, .Base = shift ? current : Selection{}};
    } else if (shift) {
        ApplySelection(*m_Schematic, m_Mode, Toggled(current, picked));
    } else if (Joined(current, picked) == current) {
        // Part of a group: keep the group so it can be dragged, and collapse to the item if it is only a click
        selecting.Drag = MakeDrag(*m_Schematic, Snap(cursor_world), picked);
    } else {
        ApplySelection(*m_Schematic, m_Mode, picked);
        selecting.Drag = MakeDrag(*m_Schematic, Snap(cursor_world), std::nullopt);
    }
}

/**
 * @brief   A double click opens the properties of the selected part, or the contents of a block, or selects the whole
 *          node of a wire.
 */
void EditorWindow::HandleSelectionDoubleClick() {
    if (m_Schematic->GetSelectedElement() != nullptr) {
        EndDrag(*m_Schematic, m_Mode);
        if (m_Schematic->GetSelectedElement()->GetComponent().GetType() == Core::ComponentType::SUBCIRCUIT) {
            RequestEditSelectedSubcircuit();
        } else {
            m_Popover.Open();
        }
    } else if (m_Schematic->GetSelection().Elements.empty() && m_Schematic->GetSelection().Wires.size() == 1) {
        const Selection node = PickNode(m_Schematic->GetWires(),
                                        m_Schematic->GetConnectivity(),
                                        *m_Schematic->GetSelection().Wires.begin());
        EndDrag(*m_Schematic, m_Mode);
        ApplySelection(*m_Schematic, m_Mode, node);
    }
}

/**
 * @brief   While the mouse button is down the selection box takes in what it touches; it ends when the button comes up.
 */
void EditorWindow::UpdateSelectionBox(SelectingMode &selecting, const ImVec2 cursor_world) {
    if (!selecting.Box) {
        return;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const Selection inside =
            PickInRectangle(m_Schematic->GetElements(), m_Schematic->GetWires(), selecting.Box->Start, cursor_world);
        ApplySelection(*m_Schematic, m_Mode, Joined(selecting.Box->Base, inside));
    } else {
        selecting.Box.reset();
    }
}

/**
 * @brief   While the mouse button is down the drag follows the cursor; it ends when the button comes up.
 */
void EditorWindow::UpdateSelectionDrag(SelectingMode &selecting, const ImVec2 cursor_world) {
    if (!selecting.Drag) {
        return;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        UpdateDrag(*m_Schematic, m_Mode, Snap(cursor_world));
    } else {
        EndDrag(*m_Schematic, m_Mode);
    }
}

/**
 * @brief   R rotates the selection, M mirrors it, Enter opens its properties, Delete removes it and Esc clears it.
 * @details R, M and Delete ask for the commands of the Edit menu; they work whatever other modifier is held, which a
 *          shortcut of the table would not.
 */
void EditorWindow::HandleSelectionKeys() {
    if (ImGui::IsKeyPressed(ImGuiKey_R)) {
        RunTurnKey(PointTransform::Kind::QUARTER_TURN);
    }
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    if (enter && m_Schematic->GetSelectedElement() != nullptr && !ImGui::IsAnyItemActive()) {
        m_Popover.Open();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_M)) {
        RunTurnKey(ImGui::GetIO().KeyShift ? PointTransform::Kind::MIRROR_TOP_BOTTOM
                                           : PointTransform::Kind::MIRROR_LEFT_RIGHT);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        RunCommand(EditorCommand::DELETE_SELECTION);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !m_PopupOpenLastFrame) {
        ClearSelection(*m_Schematic, m_Mode);
    }
}

/**
 * @brief   Runs the turning command of a key of the selecting mode; holding Alt keeps the wires where they are.
 */
void EditorWindow::RunTurnKey(const PointTransform::Kind kind) {
    const TurnRequest turn = {.Kind = kind, .IsKeepingWires = ImGui::GetIO().KeyAlt};
    if (const std::optional<EditorCommand> command = GetTurnCommand(turn)) {
        RunCommand(*command);
    }
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
    const ShortcutState state = {.IsEditingBlock = IsEditingSubcircuit(),
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
        m_Schematic->Undo();
        break;
    case EditorCommand::REDO:
        m_Schematic->Redo();
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
        CutSelected();
        break;
    case EditorCommand::COPY:
        CopySelected();
        break;
    case EditorCommand::PASTE:
        StartPasting();
        break;
    case EditorCommand::SELECT_ALL:
        SelectAll(*m_Schematic, m_Mode);
        break;
    case EditorCommand::DELETE_SELECTION:
        m_Schematic->DeleteSelection();
        break;
    case EditorCommand::FIT:
        m_View.RequestFit();
        break;
    case EditorCommand::CREATE_BLOCK:
        m_Blocks.RequestCreate();
        break;
    case EditorCommand::EDIT_BLOCK:
        RequestEditSelectedSubcircuit();
        break;
    case EditorCommand::OPEN_BLOCK_SETTINGS:
        if (const auto synced = m_Blocks.SyncPins(); !synced) {
            m_Blocks.ReportError(synced.error());
        }
        m_Blocks.RequestDialog();
        break;
    case EditorCommand::APPLY_BLOCK:
        FinishSubcircuitEditing(true);
        break;
    case EditorCommand::DISCARD_BLOCK:
        FinishSubcircuitEditing(false);
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
        TransformSelection(*m_Schematic, m_Mode, turn->Kind, turn->IsKeepingWires);
    }
}

} // namespace GUI
