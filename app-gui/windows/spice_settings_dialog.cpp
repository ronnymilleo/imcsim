/**
 * @file    spice_settings_dialog.cpp
 * @brief   The dialog that sets the pins and parameter defaults of a block kept as SPICE text.
 */

#include "windows/spice_settings_dialog.h"

#include "base/theme.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "schematic/spice_import.h"
#include "windows/spice_pin_table.h"
#include <format>
#include <utility>

namespace GUI {

namespace {

constexpr const char *POPUP = "SPICE block";
// Size of the text of the block, in font sizes
constexpr float TEXT_WIDTH = 36.0f;
constexpr float TEXT_HEIGHT = 12.0f;

} // namespace

/**
 * @brief   Opens the dialog on the pins and parameters of a block kept as SPICE text.
 * @param[in] definition  The block; one that is not SPICE text opens nothing.
 */
void SpiceSettingsDialog::Open(const SavedSubcircuit &definition) {
    if (!definition.Spice) {
        return;
    }
    m_BlockName = definition.Name;
    m_Ports = definition.Spice->Ports;
    m_Pins = definition.Pins;
    m_Parameters = definition.Parameters;
    m_Text = definition.Spice->Text;
    m_Error.clear();
    m_IsOpenRequested = true;
}

/**
 * @brief   Draws the dialog while it is open: the pins and parameter defaults of the block, and its text, which stays
 *          as it came.
 * @return  The new settings, in the frame the user applies them; no value otherwise.
 * @note    Call it every frame from the same place, outside any window, so the popup opens where it is drawn.
 */
std::optional<SpiceSettingsResult> SpiceSettingsDialog::Draw() {
    if (std::exchange(m_IsOpenRequested, false)) {
        ImGui::OpenPopup(POPUP);
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return std::nullopt;
    }
    ImGui::TextUnformatted(std::format("{}, SPICE text from a library", m_BlockName).c_str());
    ImGui::SeparatorText("Pins: port, name and edge");
    DrawSpicePinTable(m_Pins, m_Ports);
    DrawParameterDefaults();
    DrawText();
    if (!m_Error.empty()) {
        ImGui::TextColored(GetErrorTextColor(), "%s", m_Error.c_str());
    }

    std::optional<SpiceSettingsResult> result;
    if (ImGui::Button("Apply")) {
        if (const std::optional<std::string> problem = CheckSpicePins(m_Pins)) {
            m_Error = *problem;
        } else {
            result = SpiceSettingsResult{.Name = m_BlockName, .Pins = m_Pins, .Parameters = m_Parameters};
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return result;
}

/**
 * @brief   One field per parameter of the block, for the value it takes when an instance does not set it.
 */
void SpiceSettingsDialog::DrawParameterDefaults() {
    if (m_Parameters.empty()) {
        return;
    }
    ImGui::SeparatorText("Parameter defaults");
    const float font_size = ImGui::GetFontSize();
    for (std::size_t index = 0; index < m_Parameters.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::SetNextItemWidth(font_size * SPICE_NAME_FIELD_WIDTH);
        ImGui::InputDouble(m_Parameters[index].Name.c_str(), &m_Parameters[index].DefaultValue, 0.0, 0.0, "%g");
        ImGui::PopID();
    }
}

/**
 * @brief   The SPICE text of the block, read only, in a fixed-width font.
 */
void SpiceSettingsDialog::DrawText() {
    const float font_size = ImGui::GetFontSize();
    ImGui::SeparatorText("Text");
    ImGui::PushFont(GetMonospaceFont(), 0.0f);
    ImGui::InputTextMultiline("##text",
                              &m_Text,
                              {font_size * TEXT_WIDTH, font_size * TEXT_HEIGHT},
                              ImGuiInputTextFlags_ReadOnly);
    ImGui::PopFont();
}

} // namespace GUI
