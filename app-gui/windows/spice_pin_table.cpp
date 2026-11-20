/**
 * @file    spice_pin_table.cpp
 * @brief   The table of the pins of a block kept as SPICE text, which the import and the settings dialogs both show.
 */

#include "windows/spice_pin_table.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "schematic/subcircuit_pins.h"

namespace GUI {

namespace {

// Widths in font sizes, so the dialogs follow the font
constexpr float PORT_COLUMN_WIDTH = 6.0f;
constexpr float SIDE_FIELD_WIDTH = 6.0f;

} // namespace

/**
 * @brief   Draws one row per port: the port as the text names it, the pin name and the edge of the box it goes on.
 * @param[in,out] pins   One pin per port, whose name and edge the user edits.
 * @param[in]     ports  The ports as the SPICE text names them.
 */
void DrawSpicePinTable(std::vector<SavedSubcircuitPin> &pins, const std::vector<std::string> &ports) {
    const float font_size = ImGui::GetFontSize();
    for (std::size_t index = 0; index < pins.size() && index < ports.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", ports[index].c_str());
        ImGui::SameLine(font_size * PORT_COLUMN_WIDTH);
        ImGui::SetNextItemWidth(font_size * SPICE_NAME_FIELD_WIDTH);
        ImGui::InputText("##name", &pins[index].Name);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(font_size * SIDE_FIELD_WIDTH);
        if (ImGui::BeginCombo("##side", GetPinSideName(pins[index].Side))) {
            for (const Core::PinSide side : PIN_SIDES) {
                if (ImGui::Selectable(GetPinSideName(side), side == pins[index].Side)) {
                    pins[index].Side = side;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
}

} // namespace GUI
