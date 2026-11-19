/**
 * @file    editor_context_menu.cpp
 * @brief   The menu a right click opens on the schematic: what the part, the wire or the empty canvas under it can do.
 */

#include "windows/editor_context_menu.h"

#include "imgui.h"
#include "schematic/selection.h"
#include "schematic/selection_editing.h"
#include "schematic/shortcuts.h"
#include "windows/editor_menus.h"
#include <format>
#include <string>

namespace GUI {

namespace {

constexpr const char *CONTEXT_MENU = "Schematic actions";

/**
 * @brief   Properties and, for a block, its contents, for a part selected alone.
 */
void DrawPartItems(const ContextMenuServices &services, CommandItems &items) {
    if (ImGui::MenuItem("Properties...", "Enter")) {
        services.Popover.Request();
    }
    if (services.Shown.GetSelectedElement()->GetComponent().GetType() == Core::ComponentType::SUBCIRCUIT) {
        items.Draw("Edit Block Contents", EditorCommand::EDIT_BLOCK);
    }
    ImGui::Separator();
}

/**
 * @brief   Adds the item under the cursor to the plots, or removes it.
 */
void DrawMeasureItem(Schematic &schematic, const MeasurementTarget &target) {
    const bool measured = target.Node ? schematic.GetMeasurements().IsVoltageMeasured(*target.Node)
                                      : schematic.GetMeasurements().IsCurrentMeasured(target.Current);
    const std::string label = std::format("{} {}",
                                          measured ? "Stop measuring" : "Measure",
                                          GetMeasurementLabel(target, schematic.GetConnectivity()));
    if (ImGui::MenuItem(label.c_str())) {
        if (target.Node) {
            schematic.SetVoltageMeasured(*target.Node, !measured);
        } else {
            schematic.GetMeasurements().SetCurrentMeasured(target.Current, !measured);
        }
    }
}

/**
 * @brief   Running, stopping or continuing the live run, for the document and not a block.
 * @details These drive the run directly, as the Run button does, since each says which of the three it does; F5 only
 *          ever runs or continues.
 */
void DrawRunItems(AnalysisControls &controls, const Analysis selected_analysis) {
    if (controls.IsLive() && ImGui::MenuItem("Discard the Live Run")) {
        controls.DiscardLive();
    }
    if (controls.GetRunAction() == RunAction::CONTINUE) {
        if (ImGui::MenuItem("Continue the Live Run", GetShortcutLabel(EditorCommand::RUN))) {
            controls.ContinueLive();
        }
    } else if (controls.IsRunning()) {
        if (ImGui::MenuItem("Stop the Run")) {
            controls.StopRun();
        }
    } else if (ImGui::MenuItem(std::format("Run {}", GetAnalysisName(selected_analysis)).c_str(),
                               GetShortcutLabel(EditorCommand::RUN))) {
        controls.RunSelected();
    }
}

/**
 * @brief   What the empty canvas offers: finding a part, running and fitting the view.
 */
void DrawCanvasItems(const ContextMenuServices &services,
                     CommandItems &items,
                     const Analysis selected_analysis,
                     const bool is_editing_block) {
    if (ImGui::MenuItem("Find a Part...", "Space")) {
        services.Picker.RequestOpen();
    }
    if (!is_editing_block) {
        DrawRunItems(services.Controls, selected_analysis);
    }
    items.Draw("Fit Schematic", EditorCommand::FIT);
}

} // namespace

/**
 * @brief   A right click while selecting selects what is under the cursor and opens the menu of what it can do.
 * @param[in,out] schematic  Schematic on show, whose selection takes what the click landed on.
 * @param[in,out] mode       The mode of the editor; nothing happens outside selecting.
 * @param[in]     view       Pan and zoom of the canvas.
 * @param[in]     zoom       Pixels per grid unit.
 * @param[in]     hovered    Whether the cursor is over the canvas.
 * @param[in,out] popover    Properties popover, placed so the Properties item opens it where a double click would.
 */
void EditorContextMenu::HandleRightClick(Schematic &schematic,
                                         EditorMode &mode,
                                         const ViewTransform &view,
                                         const float zoom,
                                         const bool hovered,
                                         PartPopover &popover) {
    if (!IsSelecting(mode) || !hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return;
    }
    const ImVec2 cursor_world = view.ToWorld(ImGui::GetIO().MousePos);
    // Right click keeps a group when it lands on one of its items; anywhere else it selects what is under the cursor
    const Selection picked =
        PickAt(schematic.GetElements(), schematic.GetWires(), cursor_world, WIRE_PICK_DISTANCE / zoom);
    const Selection current = {schematic.GetSelection().Elements, schematic.GetSelection().Wires};
    if (picked.Empty() || Joined(current, picked) != current) {
        ApplySelection(schematic, mode, picked);
    }
    m_Target = FindMeasurementTarget(schematic.GetElements(),
                                     schematic.GetWires(),
                                     schematic.GetConnectivity(),
                                     cursor_world,
                                     WIRE_PICK_DISTANCE / zoom);
    popover.Place();
    ImGui::OpenPopup(CONTEXT_MENU);
}

/**
 * @brief   Draws the context menu: properties, orientation, measuring and deletion for a part, measuring and
 *          deletion for a wire, and finding a part, running and fitting the view on empty canvas.
 * @param[in] services           What the items act on.
 * @param[in] selected_analysis  The analysis the Run item runs.
 * @param[in] is_editing_block   Whether a block is open, which has no run of its own.
 * @return  The command of the item picked, such as turning the selection or opening the selected block, which the
 *          editor carries out; none when the menu is closed or the item acted by itself.
 */
std::optional<EditorCommand> EditorContextMenu::Draw(const ContextMenuServices &services,
                                                     const Analysis selected_analysis,
                                                     const bool is_editing_block) {
    if (!ImGui::BeginPopup(CONTEXT_MENU)) {
        return std::nullopt;
    }
    Schematic &schematic = services.Shown;
    const bool on_element = !schematic.GetSelection().Elements.empty();
    const bool on_wire = !schematic.GetSelection().Wires.empty();
    const bool single = schematic.GetSelection().Elements.size() + schematic.GetSelection().Wires.size() == 1;
    CommandItems items;
    if (schematic.GetSelectedElement() != nullptr) {
        DrawPartItems(services, items);
    }
    if (on_element) {
        DrawTurnItems(items, true);
    }
    if (single && m_Target) {
        if (on_element) {
            ImGui::Separator();
        }
        DrawMeasureItem(schematic, *m_Target);
    }
    if (on_element || on_wire) {
        ImGui::Separator();
        items.Draw("Delete", EditorCommand::DELETE_SELECTION);
    } else {
        DrawCanvasItems(services, items, selected_analysis, is_editing_block);
    }
    ImGui::EndPopup();
    return items.GetPicked();
}

} // namespace GUI
