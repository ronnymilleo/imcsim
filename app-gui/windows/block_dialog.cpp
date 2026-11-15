/**
 * @file    block_dialog.cpp
 * @brief   The dialog that makes a block or sets the pins and parameters of the one open, and the bar that shows the
 * way back from a block.
 */

#include "windows/block_dialog.h"

#include "base/theme.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "schematic/subcircuit_pins.h"
#include <cstddef>
#include <format>
#include <string>
#include <utility>

namespace GUI {

namespace {

/// The edge choice of a block pin, this many font sizes wide.
constexpr float PIN_SIDE_FIELD_WIDTH = 6.0f;

/**
 * @brief   One row per pin: its name, the edge it goes on and the buttons that move it in the terminal order.
 */
void DrawPinRows(BlockSession &session) {
    std::vector<SavedSubcircuitPin> &pins = session.GetPins();
    for (std::size_t index = 0; index < pins.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        ImGui::InputText("##pin", &pins[index].Name);
        ImGui::SameLine();
        // The edge the pin goes on comes from the way its marker points; picking another turns the marker about its
        // terminal, so its wires stay put
        Schematic &markers = session.GetShown();
        const GridPoint point = pins[index].Point;
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
            std::swap(pins[index], pins[index - 1]);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(index + 1 == pins.size());
        if (ImGui::Button("Down")) {
            std::swap(pins[index], pins[index + 1]);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
}

/**
 * @brief   One row per parameter: its name, its default and a button that removes it.
 */
void DrawParameterRows(BlockSession &session) {
    std::vector<Core::SubcircuitParameter> &parameters = session.GetParameters();
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        ImGui::InputText("##parameter", &parameters[index].Name);
        ImGui::SameLine();
        ImGui::InputDouble("##default", &parameters[index].DefaultValue, 0.0, 0.0, "%g");
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            parameters.erase(parameters.begin() + static_cast<std::ptrdiff_t>(index));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Add parameter")) {
        parameters.push_back({std::format("P{}", parameters.size() + 1), 1.0});
    }
}

/**
 * @brief   Creating the block, or applying the settings of the one open, and the way out without them.
 * @return  Whether the parameters of the settings are the ones the parts of the open block can now follow.
 */
bool DrawDialogButtons(BlockSession &session, const bool editing) {
    bool offers_parameters = false;
    if (ImGui::Button(editing ? "Apply" : "Create block")) {
        if (editing) {
            if (session.ApplyPinNames()) {
                offers_parameters = true;
                session.GetError().clear();
                ImGui::CloseCurrentPopup();
            }
        } else if (session.CreateBlock()) {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        if (editing) {
            session.RestoreSettings();
            offers_parameters = true;
        }
        ImGui::CloseCurrentPopup();
    }
    return offers_parameters;
}

} // namespace

/**
 * @brief   Draws the dialog of a block: its name, its pins in terminal order with their edge, and its parameters, with
 *          the buttons that create the block or apply the settings.
 * @param[in,out] session  The open blocks and the settings the dialog edits.
 * @return  True when the parameters of the open block changed, so the part editor must offer them again.
 * @note    Call it every frame; it opens when the session asked for it.
 */
bool DrawBlockDialog(BlockSession &session) {
    constexpr const char *POPUP = "Subcircuit block";
    if (session.TakeDialogRequest()) {
        ImGui::OpenPopup(POPUP);
    }
    if (!ImGui::BeginPopupModal(POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return false;
    }
    const bool editing = session.IsOpen();
    ImGui::BeginDisabled(editing);
    ImGui::InputText("Name", &session.GetName());
    ImGui::EndDisabled();
    ImGui::SeparatorText("Pins, in terminal order, and their edge");
    DrawPinRows(session);
    if (session.GetPins().empty()) {
        ImGui::TextDisabled("Place pin markers to choose the block connections");
    }
    ImGui::SeparatorText("Parameters");
    DrawParameterRows(session);
    if (!session.GetError().empty()) {
        ImGui::TextColored(GetErrorTextColor(), "%s", session.GetError().c_str());
    }
    const bool offers_parameters = DrawDialogButtons(session, editing);
    ImGui::EndPopup();
    return offers_parameters;
}

/**
 * @brief   While a block is open, the path from the schematic down to it, in the top left corner of the canvas so it
 *          stays in sight wherever the toolbar wraps.
 * @param[in] session  The open blocks.
 * @param[in] origin   Top left corner of the canvas, in screen pixels.
 * @return  The number of blocks to leave open after a click on a level above the one shown, 0 for the schematic, or
 *          none when nothing was clicked.
 */
std::optional<std::size_t> DrawBlockBar(const BlockSession &session, const ImVec2 origin) {
    if (!session.IsOpen()) {
        return std::nullopt;
    }
    const std::vector<OpenBlock> &open_blocks = session.GetOpenBlocks();
    const ImVec2 after_canvas = ImGui::GetCursorScreenPos();
    const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
    const ImVec4 text_color = ImGui::ColorConvertU32ToFloat4(GetPalette().CanvasText);
    ImGui::SetCursorScreenPos(origin + spacing);
    std::optional<std::size_t> picked;
    if (ImGui::Button("Schematic")) {
        picked = 0;
    }
    ImGui::SetItemTooltip("Apply the open blocks and return to the schematic");
    for (std::size_t level = 0; level < open_blocks.size(); ++level) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(text_color, ">");
        ImGui::SameLine();
        const std::string &name = open_blocks[level].Name;
        if (level + 1 == open_blocks.size()) {
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
    return picked;
}

} // namespace GUI
