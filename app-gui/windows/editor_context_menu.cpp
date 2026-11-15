/**
 * @file    editor_context_menu.cpp
 * @brief   The menu a right click opens on the schematic: what the part, the wire or the empty canvas under it can do.
 */

#include "windows/editor_context_menu.h"

#include "imgui.h"
#include "schematic/selection.h"
#include "schematic/selection_editing.h"
#include "schematic/wire_editing.h"
#include <format>
#include <string>

namespace GUI {

namespace {

constexpr const char *CONTEXT_MENU = "Schematic actions";

/**
 * @brief   Properties and, for a block, its contents, for a part selected alone.
 * @return  Whether the contents of the block were asked for.
 */
bool DrawPartItems(const ContextMenuServices &services) {
    bool is_block_edit_asked = false;
    if (ImGui::MenuItem("Properties...", "Enter")) {
        services.Popover.Request();
    }
    if (services.Shown.GetSelectedElement()->GetComponent().GetType() == Core::ComponentType::SUBCIRCUIT &&
        ImGui::MenuItem("Edit Block Contents")) {
        is_block_edit_asked = true;
    }
    ImGui::Separator();
    return is_block_edit_asked;
}

/**
 * @brief   Rotate, mirror and flip the selected parts, with or without their wires.
 */
void DrawTurnItems(const ContextMenuServices &services) {
    if (ImGui::MenuItem("Rotate", "R")) {
        TransformSelection(services.Shown, services.Mode, PointTransform::Kind::QUARTER_TURN, false);
    }
    if (ImGui::MenuItem("Mirror", "M")) {
        TransformSelection(services.Shown, services.Mode, PointTransform::Kind::MIRROR_LEFT_RIGHT, false);
    }
    if (ImGui::MenuItem("Flip", "Shift+M")) {
        TransformSelection(services.Shown, services.Mode, PointTransform::Kind::MIRROR_TOP_BOTTOM, false);
    }
    if (ImGui::BeginMenu("Keep Wires in Place")) {
        if (ImGui::MenuItem("Rotate", "Alt+R")) {
            TransformSelection(services.Shown, services.Mode, PointTransform::Kind::QUARTER_TURN, true);
        }
        if (ImGui::MenuItem("Mirror", "Alt+M")) {
            TransformSelection(services.Shown, services.Mode, PointTransform::Kind::MIRROR_LEFT_RIGHT, true);
        }
        if (ImGui::MenuItem("Flip", "Alt+Shift+M")) {
            TransformSelection(services.Shown, services.Mode, PointTransform::Kind::MIRROR_TOP_BOTTOM, true);
        }
        ImGui::EndMenu();
    }
}

/**
 * @brief   Adds the item under the cursor to the plots, or removes it.
 */
void DrawMeasureItem(Schematic &schematic, const MeasurementTarget &target) {
    const bool measured =
        target.Node ? schematic.IsVoltageMeasured(*target.Node) : schematic.IsCurrentMeasured(target.Current);
    const std::string label = std::format("{} {}", measured ? "Stop measuring" : "Measure",
                                          GetMeasurementLabel(target, schematic.GetConnectivity()));
    if (ImGui::MenuItem(label.c_str())) {
        if (target.Node) {
            schematic.SetVoltageMeasured(*target.Node, !measured);
        } else {
            schematic.SetCurrentMeasured(target.Current, !measured);
        }
    }
}

/**
 * @brief   Running, stopping or continuing the live run, for the document and not a block.
 */
void DrawRunItems(AnalysisControls &controls, const Analysis selected_analysis) {
    if (controls.IsLive() && ImGui::MenuItem("Discard the Live Run")) {
        controls.DiscardLive();
    }
    if (controls.GetRunAction() == RunAction::CONTINUE) {
        if (ImGui::MenuItem("Continue the Live Run", "F5")) {
            controls.ContinueLive();
        }
    } else if (controls.IsRunning()) {
        if (ImGui::MenuItem("Stop the Run")) {
            controls.StopRun();
        }
    } else if (ImGui::MenuItem(std::format("Run {}", GetAnalysisName(selected_analysis)).c_str(), "F5")) {
        controls.RunSelected();
    }
}

/**
 * @brief   What the empty canvas offers: finding a part, running and fitting the view.
 */
void DrawCanvasItems(const ContextMenuServices &services, const Analysis selected_analysis,
                     const bool is_editing_block) {
    if (ImGui::MenuItem("Find a Part...", "Space")) {
        services.Picker.RequestOpen();
    }
    if (!is_editing_block) {
        DrawRunItems(services.Controls, selected_analysis);
    }
    if (ImGui::MenuItem("Fit Schematic", "Home")) {
        services.View.RequestFit();
    }
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
void EditorContextMenu::HandleRightClick(Schematic &schematic, EditorMode &mode, const ViewTransform &view,
                                         const float zoom, const bool hovered, PartPopover &popover) {
    if (!IsSelecting(mode) || !hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return;
    }
    const ImVec2 cursor_world = view.ToWorld(ImGui::GetIO().MousePos);
    // Right click keeps a group when it lands on one of its items; anywhere else it selects what is under the cursor
    const Selection picked =
        PickAt(schematic.GetElements(), schematic.GetWires(), cursor_world, WIRE_PICK_DISTANCE / zoom);
    const Selection current = {schematic.GetSelectedElements(), schematic.GetSelectedWires()};
    if (picked.Empty() || Joined(current, picked) != current) {
        ApplySelection(schematic, mode, picked);
    }
    m_Target = FindMeasurementTarget(schematic.GetElements(), schematic.GetWires(), schematic.GetConnectivity(),
                                     cursor_world, WIRE_PICK_DISTANCE / zoom);
    popover.Place();
    ImGui::OpenPopup(CONTEXT_MENU);
}

/**
 * @brief   Draws the context menu: properties, orientation, measuring and deletion for a part, measuring and
 *          deletion for a wire, and finding a part, running and fitting the view on empty canvas.
 * @param[in] services           What the items act on.
 * @param[in] selected_analysis  The analysis the Run item runs.
 * @param[in] is_editing_block   Whether a block is open, which has no run of its own.
 * @return  Whether the item that opens the contents of the selected block was picked, which the editor carries out.
 */
bool EditorContextMenu::Draw(const ContextMenuServices &services, const Analysis selected_analysis,
                             const bool is_editing_block) {
    if (!ImGui::BeginPopup(CONTEXT_MENU)) {
        return false;
    }
    Schematic &schematic = services.Shown;
    const bool on_element = !schematic.GetSelectedElements().empty();
    const bool on_wire = !schematic.GetSelectedWires().empty();
    const bool single = schematic.GetSelectedElements().size() + schematic.GetSelectedWires().size() == 1;
    bool is_block_edit_asked = false;
    if (schematic.GetSelectedElement() != nullptr) {
        is_block_edit_asked = DrawPartItems(services);
    }
    if (on_element) {
        DrawTurnItems(services);
    }
    if (single && m_Target) {
        if (on_element) {
            ImGui::Separator();
        }
        DrawMeasureItem(schematic, *m_Target);
    }
    if (on_element || on_wire) {
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del")) {
            schematic.DeleteSelection();
        }
    } else {
        DrawCanvasItems(services, selected_analysis, is_editing_block);
    }
    ImGui::EndPopup();
    return is_block_edit_asked;
}

} // namespace GUI
